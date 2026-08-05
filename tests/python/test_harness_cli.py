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
    assert result["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 0,
    }
    assert "artifact" not in result
    assert "quarantine_id" not in result
    assert process.terminated is True
    assert process.killed is requires_force
    assert f"forced={str(requires_force).lower()}" in result["failure"]["reason"]
