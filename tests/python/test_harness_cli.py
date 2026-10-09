from __future__ import annotations

import json
from pathlib import Path

import pytest
import diskann_harness.runner as runner_module

from diskann_harness.cli import main
from diskann_harness.cli import build_parser
from diskann_harness.runner import EXIT_MEANINGS, RunnerError, build_runner_command, run_runner
from diskann_harness.workspace import WorkspaceError, normalize_workspace, resolve_confined_path


def test_runner_command_is_an_argument_array(tmp_path: Path) -> None:
    command = build_runner_command(
        tmp_path / "runner",
        "execute",
        tmp_path / "request.json",
        tmp_path / "result.json",
    )
    assert command == [
        str(tmp_path / "runner"),
        "execute",
        "--request",
        str(tmp_path / "request.json"),
        "--result",
        str(tmp_path / "result.json"),
    ]


def test_workspace_normalization_and_confinement(tmp_path: Path) -> None:
    roots = normalize_workspace(tmp_path / "workspace")
    assert roots["normal_root"] != roots["quarantine_root"]
    assert roots["report_root"] != roots["normal_root"]
    assert resolve_confined_path(roots["normal_root"], "a/b.json").is_absolute()
    with pytest.raises(WorkspaceError):
        resolve_confined_path(roots["normal_root"], "../escape")


def test_all_harness_lifecycles_have_stable_argument_shapes() -> None:
    parser = build_parser()
    for operation in ("build", "execute", "validate", "evaluate"):
        assert parser.parse_args([operation]).operation == operation
    assert EXIT_MEANINGS == {
        0: "success",
        2: "usage_or_request_invalid",
        3: "preflight_rejected",
        4: "stage_failed_or_cancelled",
        5: "invalid_output",
        6: "validation_rejected",
        7: "evaluation_failed",
        10: "internal_error",
    }


def test_invalid_runner_json_has_stable_error(tmp_path: Path) -> None:
    runner = tmp_path / "runner"
    runner.write_text("#!/bin/sh\nprintf 'not-json' > \"$5\"\nexit 0\n", encoding="utf-8")
    runner.chmod(0o755)
    request = tmp_path / "request.json"
    request.write_text('{"operation":"execute"}', encoding="utf-8")
    with pytest.raises(RunnerError, match="runner result"):
        run_runner(runner, request, tmp_path / "result.json")


def test_cli_propagates_configuration_error(tmp_path: Path) -> None:
    config = tmp_path / "bad.yaml"
    config.write_text("workspace: &w\n  root: .\nworkspace: *w\n", encoding="utf-8")
    assert main(["execute", "--config", str(config), "--workspace", str(tmp_path),
                 "--runner", str(tmp_path / "missing")]) == 2


@pytest.mark.parametrize("requires_force", [False, True])
def test_harness_interrupt_records_cancelled_without_artifact(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, requires_force: bool
) -> None:
    class InterruptedProcess:
        returncode: int | None = None
        terminated = False
        killed = False
        observations = 0

        def communicate(self, timeout: int | None = None) -> tuple[str, str]:
            self.observations += 1
            if self.observations == 1:
                raise KeyboardInterrupt
            if requires_force and self.observations == 2:
                raise runner_module.subprocess.TimeoutExpired("runner", timeout)
            self.returncode = -9 if requires_force else -15
            return "", "terminated by Harness"

        def terminate(self) -> None:
            self.terminated = True

        def kill(self) -> None:
            self.killed = True

    process = InterruptedProcess()
    monkeypatch.setattr(runner_module.subprocess, "Popen", lambda *a, **k: process)
    request = tmp_path / "request.json"
    request.write_text(
        json.dumps({"request_id": "interrupt-test", "operation": "execute"}),
        encoding="utf-8",
    )
    result_path = tmp_path / "result.json"
    result = run_runner(
        tmp_path / "runner", request, result_path,
        repository_root=Path(__file__).resolve().parents[2],
    )
    assert result["outcome"] == "CANCELLED"
    assert result["exit_code"] == 4
    assert result["result_version"] == "2.0.0"
    assert result["stage_invocations"] is None
    assert "artifact" not in result
    assert "quarantine_id" not in result
    assert process.terminated is True
    assert process.killed is requires_force
    assert f"forced={str(requires_force).lower()}" in result["failure"]["reason"]


def test_real_interrupt_after_local_method_started(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    import hashlib
    import os
    import select
    from diskann_harness.config import load_yaml_file, normalize_run_request

    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("DISKANN_TEST_BUILD_DIR", root / "build/feature-001"))
    runner = build / "tests/cpp/fakes/diskann_pipeline_fake_runner"
    assert runner.is_file(), f"Missing rebuilt test runner: {runner}"
    config = load_yaml_file(root / "tests/fixtures/fake_pipeline/experiment.yaml")
    workspace = tmp_path / "workspace"
    config["workspace"]["root"] = str(workspace)
    request = tmp_path / "request.json"
    request.write_text(json.dumps(normalize_run_request(config, repository_root=root)))
    monkeypatch.setenv("DISKANN_TEST_INTERRUPT_LOCAL", "1")
    real_popen = runner_module.subprocess.Popen
    children = []
    evidence = {}

    class InterruptAtHandshake:
        def __init__(self, *args, **kwargs):
            self.child = real_popen(*args, **kwargs, stdin=runner_module.subprocess.PIPE)
            children.append(self.child)
            self.first = True

        @property
        def returncode(self):
            return self.child.returncode

        def communicate(self, timeout=None):
            if self.first:
                self.first = False
                ready, _, _ = select.select([self.child.stdout], [], [], 15)
                assert ready, "child did not reach Local method within timeout"
                marker = json.loads(self.child.stdout.readline())
                assert marker == {"checkpoint": "local-method-entered", "factories": [1, 1, 0], "methods": [1, 1, 0]}
                evidence["marker"] = marker
                evidence["before"] = {
                    path: hashlib.sha256(path.read_bytes()).hexdigest()
                    for base in (workspace / "normal", workspace / "manifests")
                    for path in base.rglob("*") if path.is_file()
                }
                manifests = [json.loads(path.read_text()) for path in (workspace / "manifests").rglob("stages/*.json")]
                assert len(manifests) == 1 and manifests[0]["status"] == "SUCCEEDED"
                assert manifests[0]["stage"] == "partition"
                raise KeyboardInterrupt
            return self.child.communicate(timeout=timeout)

        def terminate(self):
            self.child.terminate()

        def kill(self):
            self.child.kill()

    monkeypatch.setattr(runner_module.subprocess, "Popen", InterruptAtHandshake)
    try:
        result = run_runner(runner, request, tmp_path / "result.json", repository_root=root)
        assert result["outcome"] == "CANCELLED" and result["exit_code"] == 4
        assert "artifact" not in result and "quarantine_id" not in result
        assert "build_manifest_path" not in result
        assert evidence["marker"]["methods"] == [1, 1, 0]
        for path, checksum in evidence["before"].items():
            assert hashlib.sha256(path.read_bytes()).hexdigest() == checksum
        assert not list((workspace / "reports").rglob("*.json"))
        assert result["result_version"] == "2.0.0"
        assert result["stage_invocations"] is None
        assert json.loads((tmp_path / "result.json").read_text()) == result
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)


@pytest.mark.parametrize("operation", ["preflight", "validate", "evaluate", "registry"])
def test_interrupt_non_execute_has_provable_zero_counts(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, operation: str) -> None:
    class InterruptedProcess:
        returncode = -15
        interrupted = False

        def communicate(self, timeout=None):
            if not self.interrupted:
                self.interrupted = True
                raise KeyboardInterrupt
            return "", "terminated"

        def terminate(self):
            pass

    monkeypatch.setattr(runner_module.subprocess, "Popen", lambda *a, **k: InterruptedProcess())
    request = tmp_path / "request.json"
    request.write_text(json.dumps({"request_id": "no-stages", "operation": operation}))
    result = run_runner(tmp_path / "runner", request, tmp_path / "result.json")
    assert result["result_version"] == "1.0.0"
    assert result["stage_invocations"] == {"partition": 0, "local_build": 0, "merge": 0}
    assert result["outcome"] == "CANCELLED" and result["exit_code"] == 4


def test_interrupt_does_not_overwrite_child_terminal_result(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    result_path = tmp_path / "result.json"
    original = json.dumps({
        "result_version": "1.0.0", "request_id": "race", "operation": "execute",
        "outcome": "SUCCEEDED", "exit_code": 0,
        "stage_invocations": {"partition": 1, "local_build": 2, "merge": 1},
    }).encode()

    class RacingProcess:
        returncode = 0
        interrupted = False

        def communicate(self, timeout=None):
            if not self.interrupted:
                self.interrupted = True
                raise KeyboardInterrupt
            result_path.write_bytes(original)
            return "", ""

        def terminate(self):
            pass

    monkeypatch.setattr(runner_module.subprocess, "Popen", lambda *a, **k: RacingProcess())
    request = tmp_path / "request.json"
    request.write_text(json.dumps({"request_id": "race", "operation": "execute"}))
    with pytest.raises(RunnerError, match="conflicting terminal result"):
        run_runner(tmp_path / "runner", request, result_path)
    assert result_path.read_bytes() == original


@pytest.mark.parametrize("version", ["1.0.0", "2.0.0"])
def test_result_reader_keeps_unknown_counts_distinct(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, version: str) -> None:
    result = {"result_version": version, "request_id": "reader", "operation": "execute",
              "outcome": "CANCELLED", "exit_code": 4,
              "stage_invocations": None if version == "2.0.0" else {"partition": 1, "local_build": 1, "merge": 0},
              "failure": {"category": "cancelled", "reason": "retained reason"}}
    result_path = tmp_path / "result.json"
    class CompletedProcess:
        returncode = 4
        def communicate(self):
            result_path.write_text(json.dumps(result))
            return "", ""
    monkeypatch.setattr(runner_module.subprocess, "Popen", lambda *a, **k: CompletedProcess())
    request = tmp_path / "request.json"
    request.write_text(json.dumps({"request_id": "reader", "operation": "execute"}))
    assert run_runner(tmp_path / "runner", request, result_path) == result
