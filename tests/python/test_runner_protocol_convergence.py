from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

from diskann_harness.config import load_yaml_file, normalize_run_request


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = Path(os.environ.get("DISKANN_TEST_BUILD_DIR", ROOT / "build/feature-001"))
FAKE_RUNNER = BUILD_DIR / "tests/cpp/fakes/diskann_pipeline_fake_runner"
PRODUCTION_RUNNER = BUILD_DIR / "src/cli/diskann_pipeline_runner"


def _normalized_request(workspace: Path) -> dict[str, object]:
    config = load_yaml_file(ROOT / "tests/fixtures/fake_pipeline/experiment.yaml")
    config["workspace"]["root"] = str(workspace)
    return normalize_run_request(config, repository_root=ROOT)


def test_registry_command_is_request_free_and_enumerates_test_registry(
    tmp_path: Path,
) -> None:
    result_path = tmp_path / "registry.json"
    completed = subprocess.run(
        [str(FAKE_RUNNER), "registry", "--result", str(result_path)],
        capture_output=True,
        text=True,
        shell=False,
    )
    assert completed.returncode == 0, completed.stderr
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["operation"] == "registry"
    assert [
        item["implementation_id"]
        for item in result["registered_implementations"]
    ] == ["fake_local_graph_v1", "fake_merge_v1", "fake_partition_v1"]


def test_operation_mismatch_and_unknown_shape_fail_before_workspace_mutation(
    tmp_path: Path,
) -> None:
    for identity, command, request_operation, mutate in (
        ("mismatch", "preflight", "execute", None),
        ("unknown", "unknown", "unknown", None),
        ("extra", "execute", "execute", "unexpected"),
    ):
        workspace = tmp_path / f"workspace-{identity}"
        request = _normalized_request(workspace)
        request["request_id"] = f"protocol-{identity}"
        request["operation"] = request_operation
        if mutate:
            request[mutate] = True
        request_path = tmp_path / f"{identity}-request.json"
        result_path = tmp_path / f"{identity}-result.json"
        request_path.write_text(json.dumps(request), encoding="utf-8")
        completed = subprocess.run(
            [
                str(FAKE_RUNNER),
                command,
                "--request",
                str(request_path),
                "--result",
                str(result_path),
            ],
            capture_output=True,
            text=True,
            shell=False,
        )
        assert completed.returncode == 2, completed.stderr
        result = json.loads(result_path.read_text(encoding="utf-8"))
        assert result["exit_code"] == 2
        assert result["stage_invocations"] == {
            "partition": 0,
            "local_build": 0,
            "merge": 0,
        }
        assert not workspace.exists()


def test_direct_runners_reject_result_target_before_workspace_mutation(
    tmp_path: Path,
) -> None:
    for identity, runner in (
        ("fake", FAKE_RUNNER),
        ("production", PRODUCTION_RUNNER),
    ):
        workspace = tmp_path / f"forbidden-result-{identity}-workspace"
        request = _normalized_request(workspace)
        request["request_id"] = f"forbidden-result-{identity}"
        request_path = tmp_path / f"forbidden-result-{identity}.json"
        request_path.write_text(json.dumps(request), encoding="utf-8")
        forbidden = (
            ROOT
            / "upstream"
            / "diskann-cpp-main"
            / f".feature-001-forbidden-{identity}.json"
        )
        assert not forbidden.exists()
        completed = subprocess.run(
            [
                str(runner),
                "execute",
                "--request",
                str(request_path),
                "--result",
                str(forbidden),
            ],
            capture_output=True,
            text=True,
            shell=False,
        )
        assert completed.returncode == 10
        assert not forbidden.exists()
        assert not workspace.exists()
