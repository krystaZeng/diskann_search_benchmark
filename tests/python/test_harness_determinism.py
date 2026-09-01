from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

import diskann_harness.runner as runner_module
from diskann_harness.runner import (
    RunnerError,
    _require_terminal_build_manifest,
    execute_configuration,
)


ROOT = Path(__file__).resolve().parents[2]


def _valid_terminal_build_manifest(build_root: Path) -> dict[str, object]:
    cache = build_root / "CMakeCache.txt"
    return {
        "manifest_version": "1.0.0",
        "run_id": "harness-build",
        "stage": "build",
        "implementation": {
            "implementation_id": "diskann_harness_build",
            "implementation_version": "1.0.0",
        },
        "source": {
            "repository": ROOT.as_posix(),
            "branch": "test-branch",
            "commit": "1" * 40,
            "dirty_worktree": False,
        },
        "toolchain": {
            "os": "test-os",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": "c++ test version",
            "cmake_version": "cmake version 3.22.1",
            "generator": "Ninja",
            "build_type": "Debug",
            "cmake_definitions": [
                "CMAKE_BUILD_TYPE=Debug",
                "PROJECT_COMPILE_OPTIONS=-Wall;-Wextra;-Wpedantic",
            ],
            "compile_commands": [
                "cmake -S /source -B /build -DCMAKE_BUILD_TYPE=Debug",
                "cmake --build /build --parallel",
            ],
            "link_commands": [],
        },
        "dataset": {
            "dataset_id": "fake-six-node-v1",
            "dataset_checksum": "2" * 64,
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "parameters": {
            "immutable_build_configuration": {
                "source_dir": ROOT.as_posix(),
                "build_dir": build_root.resolve().as_posix(),
                "build_type": "Debug",
                "cmake_cache_sha256": hashlib.sha256(cache.read_bytes()).hexdigest(),
            },
            "source_config": {
                "path": "tests/fixtures/fake_pipeline/experiment.yaml",
                "yaml_sha256": "3" * 64,
                "normalized_json_sha256": "4" * 64,
            },
        },
        "resource_budget": {"memory_budget_bytes": 1, "thread_budget": 1},
        "input_artifacts": [],
        "output_artifacts": [],
        "status": "SUCCEEDED",
        "started_at": "2026-08-05T00:00:00Z",
        "ended_at": "2026-08-05T00:00:01Z",
    }


def _terminal_build_layout(tmp_path: Path) -> tuple[Path, Path, Path]:
    build_root = tmp_path / "feature-build"
    runner = build_root / "tests/cpp/fakes/diskann_pipeline_fake_runner"
    runner.parent.mkdir(parents=True)
    runner.write_text("not executed", encoding="utf-8")
    cache = build_root / "CMakeCache.txt"
    cache.write_text("CMAKE_GENERATOR:INTERNAL=Ninja\n", encoding="utf-8")
    manifest = build_root / "build-manifest.json"
    manifest.write_text(
        json.dumps(_valid_terminal_build_manifest(build_root)), encoding="utf-8"
    )
    return runner, cache, manifest


@pytest.mark.parametrize(
    "case",
    [
        "missing_cache",
        "missing_manifest",
        "schema_invalid",
        "wrong_stage",
        "wrong_status",
        "wrong_implementation_id",
        "wrong_implementation_version",
        "not_git_branch",
        "zero_commit",
        "unavailable_compiler_path",
        "unavailable_compiler_version",
        "unknown_generator",
        "insufficient_compile_commands",
        "missing_project_compile_options",
        "empty_project_compile_options",
        "missing_immutable_configuration",
        "wrong_build_dir",
        "wrong_build_type",
        "wrong_cache_checksum",
        "missing_source_config",
        "empty_source_config_path",
        "invalid_source_config_digest",
    ],
)
def test_terminal_build_manifest_gate_rejects_invalid_evidence(
    tmp_path: Path, case: str
) -> None:
    runner, cache, manifest_path = _terminal_build_layout(tmp_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    if case == "missing_cache":
        cache.unlink()
    elif case == "missing_manifest":
        manifest_path.unlink()
        (tmp_path / "build-manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )
    elif case == "schema_invalid":
        del manifest["source"]["repository"]
    elif case == "wrong_stage":
        manifest["stage"] = "partition"
    elif case == "wrong_status":
        manifest["status"] = "REUSED"
    elif case == "wrong_implementation_id":
        manifest["implementation"]["implementation_id"] = "other_build"
    elif case == "wrong_implementation_version":
        manifest["implementation"]["implementation_version"] = "2.0.0"
    elif case == "not_git_branch":
        manifest["source"]["branch"] = "not-git"
    elif case == "zero_commit":
        manifest["source"]["commit"] = "0" * 40
    elif case == "unavailable_compiler_path":
        manifest["toolchain"]["compiler_path"] = "unavailable"
    elif case == "unavailable_compiler_version":
        manifest["toolchain"]["compiler_version"] = "unavailable"
    elif case == "unknown_generator":
        manifest["toolchain"]["generator"] = "unknown"
    elif case == "insufficient_compile_commands":
        manifest["toolchain"]["compile_commands"] = ["cmake -S /source -B /build"]
    elif case == "missing_project_compile_options":
        manifest["toolchain"]["cmake_definitions"] = ["CMAKE_BUILD_TYPE=Debug"]
    elif case == "empty_project_compile_options":
        manifest["toolchain"]["cmake_definitions"] = ["PROJECT_COMPILE_OPTIONS="]
    elif case == "missing_immutable_configuration":
        del manifest["parameters"]["immutable_build_configuration"]
    elif case == "wrong_build_dir":
        manifest["parameters"]["immutable_build_configuration"]["build_dir"] = (
            (tmp_path / "other-build").as_posix()
        )
    elif case == "wrong_build_type":
        manifest["parameters"]["immutable_build_configuration"]["build_type"] = (
            "Release"
        )
    elif case == "wrong_cache_checksum":
        manifest["parameters"]["immutable_build_configuration"][
            "cmake_cache_sha256"
        ] = "5" * 64
    elif case == "missing_source_config":
        del manifest["parameters"]["source_config"]
    elif case == "empty_source_config_path":
        manifest["parameters"]["source_config"]["path"] = ""
    elif case == "invalid_source_config_digest":
        manifest["parameters"]["source_config"]["yaml_sha256"] = "A" * 64

    if manifest_path.exists():
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    with pytest.raises(RunnerError, match="terminal BuildManifest"):
        _require_terminal_build_manifest(runner, ROOT)


def test_terminal_build_manifest_gate_returns_bound_evidence(tmp_path: Path) -> None:
    runner, _, manifest_path = _terminal_build_layout(tmp_path)

    path, document, digest = _require_terminal_build_manifest(runner, ROOT)

    assert path == manifest_path.resolve()
    assert document == json.loads(manifest_path.read_text(encoding="utf-8"))
    assert digest == hashlib.sha256(manifest_path.read_bytes()).hexdigest()


@pytest.mark.parametrize(
    "mismatch", ["aggregate", "payload", "projection", "provenance"]
)
def test_two_successful_runs_fail_closed_when_identity_or_provenance_differs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mismatch: str
) -> None:
    def fake_run_runner(*args: object, **kwargs: object) -> dict[str, object]:
        request_path = Path(args[1])
        request = json.loads(request_path.read_text(encoding="utf-8"))
        return {"request_id": request["request_id"], "outcome": "SUCCEEDED"}

    def mismatched_identity(
        result: dict[str, object], roots: dict[str, str]
    ) -> dict[str, object]:
        run_id = str(result["request_id"])
        repetition = run_id[-1]
        return {
            "aggregate_checksum": (
                repetition if mismatch == "aggregate" else "same"
            ),
            "payload_checksums": [
                repetition if mismatch == "payload" else "same"
            ],
            "evaluation_projection": {
                "graph_integrity": "VALID",
                "logical_node_count": 6,
                "invalid_neighbor_count": 0,
                "declared_max_degree": 3,
                "observed_max_degree": (
                    int(repetition) if mismatch == "projection" else 3
                ),
                "degree_bound": True,
                "passed": True,
            },
            "report_id": (
                "same-report" if mismatch == "provenance" else f"report-{run_id}"
            ),
            "generated_at": (
                "2026-08-05T00:00:00Z"
                if mismatch == "provenance"
                else f"2026-08-05T00:00:0{repetition}Z"
            ),
        }

    monkeypatch.setattr(runner_module, "run_runner", fake_run_runner)
    monkeypatch.setattr(runner_module, "_result_identity", mismatched_identity)
    monkeypatch.setattr(
        runner_module,
        "_require_terminal_build_manifest",
        lambda *args, **kwargs: (Path("/build/build-manifest.json"), {}, "0" * 64),
    )
    workspace = tmp_path / "workspace"
    with pytest.raises(RunnerError, match="both runs were retained"):
        execute_configuration(
            ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
            workspace,
            ROOT / "unused-runner",
            repository_root=ROOT,
        )
    summaries = list((workspace / "reports" / "summaries").glob("*.json"))
    assert len(summaries) == 1
    retained = json.loads(summaries[0].read_text(encoding="utf-8"))
    assert len(retained["retained_runs"]) == 2
    assert (
        retained["distinct_run_provenance"] is False
        if mismatch == "provenance"
        else retained["deterministic_equal"] is False
    )


def test_execute_rejects_missing_terminal_build_manifest_before_workspace_mutation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = tmp_path / "incomplete-build"
    runner = build / "tests/cpp/fakes/diskann_pipeline_fake_runner"
    runner.parent.mkdir(parents=True)
    runner.write_text("not executed", encoding="utf-8")
    (build / "CMakeCache.txt").write_text(
        "CMAKE_GENERATOR:INTERNAL=Ninja\n", encoding="utf-8"
    )
    monkeypatch.setattr(
        runner_module,
        "run_runner",
        lambda *args, **kwargs: pytest.fail("runner must not be started"),
    )
    monkeypatch.setattr(
        runner_module,
        "create_workspace",
        lambda *args, **kwargs: pytest.fail("workspace must not be created"),
    )
    workspace = tmp_path / "workspace"
    with pytest.raises(RunnerError, match="terminal BuildManifest"):
        execute_configuration(
            ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
            workspace,
            runner,
            repository_root=ROOT,
        )
    assert not workspace.exists()
