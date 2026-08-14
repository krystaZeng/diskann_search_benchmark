from __future__ import annotations

import copy
import json
import shutil
import subprocess
from pathlib import Path
from typing import Callable

import pytest

from diskann_harness import hnsw_merger_merge_adaptation as feature


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "hnsw-merger-merge-adaptation-linux"
IMAGE = "diskann-cpp-main-baseline-builder:jammy"
HELPER = BUILD / "tests/cpp/papers/hnsw_merger/hnsw_merger_merge_only_execution"
RUNNER = BUILD / "src/papers/hnsw_merger/vamana/hnsw_merger_experiment_runner"
VALIDATION_RUNNER = BUILD / "src/cli/diskann_pipeline_runner"
DRAFT_PROFILE = (
    ROOT
    / "experiments/configs/hnsw-merger-diskann-merge-adaptation/"
    "sift1m-structural.yaml"
)


def _container_path(path: Path) -> str:
    return "/workspace/" + path.resolve().relative_to(ROOT).as_posix()


def _small_request(tmp_name: str) -> Path:
    workspace = BUILD / "python-tests" / tmp_name
    if workspace.exists():
        shutil.rmtree(workspace)
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "--network",
            "none",
            "--pull=never",
            "--user",
            "0:0",
            "-v",
            f"{ROOT}:/workspace",
            "-w",
            "/workspace",
            IMAGE,
            _container_path(HELPER),
            "--emit-request",
            _container_path(workspace),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return workspace / "comparison-request.json"


def test_normalized_request_is_single_shared_tuple_and_closed() -> None:
    request = json.loads(_small_request("request-contract").read_text(encoding="utf-8"))
    assert feature.validate_normalized_request(request) is request
    assert set(request["shared_inputs"]) == {"dataset", "partition", "local_graphs"}
    assert request["merges"]["m0"]["implementation_id"] == feature.M0_ID
    assert request["merges"]["m1"]["implementation_id"] == feature.M1_ID

    unknown = copy.deepcopy(request)
    unknown["second_shared_inputs"] = unknown["shared_inputs"]
    with pytest.raises(feature.HnswMergerHarnessError, match="unknown"):
        feature.validate_normalized_request(unknown)

    drifted = copy.deepcopy(request)
    drifted["merges"]["m1"]["parameters"]["R"] = 15
    with pytest.raises(feature.HnswMergerHarnessError, match="M1 parameters"):
        feature.validate_normalized_request(drifted)


def test_real_feature_runner_executes_m0_and_m1_once_from_same_inputs() -> None:
    request_path = _small_request("real-runner")
    outcome = feature.execute(
        request_path,
        RUNNER,
        repository_root=ROOT,
        container_image=IMAGE,
    )
    result = outcome["result"]
    comparison = outcome["comparison"]
    assert result["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 2,
    }
    assert comparison["shared_input_identity"] == "PASS"
    assert comparison["structural_validation"] == "PASS"
    assert comparison["m0"]["status"] == "SUCCEEDED"
    assert comparison["m1"]["status"] == "SUCCEEDED"
    assert comparison["m0"]["global_graph"] != comparison["m1"]["global_graph"]
    assert comparison["formal_graph_parity"] is False
    assert comparison["query_evaluation_performed"] is False


def test_formal_source_admission_fails_without_canonical_remote() -> None:
    with pytest.raises(feature.HnswMergerHarnessError, match="canonical project Git remote"):
        feature._project_source_admission(ROOT)


def _git_reader(values: dict[tuple[str, ...], str]) -> Callable[..., str]:
    def read(*arguments: str) -> str:
        return values[arguments]

    return read


def test_canonical_project_source_projection_is_unique_and_not_a_local_path() -> None:
    commit = "1" * 40
    exact = feature._project_source_admission(
        ROOT,
        git_reader=_git_reader(
            {
                ("remote",): "origin\nmirror",
                ("remote", "get-url", "origin"): "https://example.test/project.git",
                ("remote", "get-url", "mirror"): "https://example.test/project.git",
                ("status", "--porcelain=v1", "--untracked-files=all"): "",
                ("rev-parse", "HEAD"): commit,
                ("branch", "--show-current"): "feature/merge-adaptation",
            }
        ),
    )
    assert exact == {
        "repository": "https://example.test/project.git",
        "branch": "feature/merge-adaptation",
        "commit": commit,
        "dirty_worktree": False,
    }

    base = {
        ("status", "--porcelain=v1", "--untracked-files=all"): "",
        ("rev-parse", "HEAD"): commit,
        ("branch", "--show-current"): "feature/merge-adaptation",
    }
    for remotes, urls in (
        ("", {}),
        (
            "origin\nmirror",
            {
                ("remote", "get-url", "origin"): "https://example.test/a.git",
                ("remote", "get-url", "mirror"): "https://example.test/b.git",
            },
        ),
        (
            "origin",
            {("remote", "get-url", "origin"): str(ROOT)},
        ),
    ):
        with pytest.raises(
            feature.HnswMergerHarnessError,
            match="canonical project Git remote",
        ):
            feature._project_source_admission(
                ROOT,
                git_reader=_git_reader({("remote",): remotes, **urls, **base}),
            )


def test_formal_profile_rejects_noncanonical_compiled_project_source() -> None:
    request_path = _small_request("formal-source-mismatch")
    request = json.loads(request_path.read_text(encoding="utf-8"))
    request["profile_ref"] = (
        "experiments/configs/hnsw-merger-diskann-merge-adaptation/"
        "sift1m-structural.yaml"
    )
    request_path.write_text(json.dumps(request), encoding="utf-8")
    result_path = request_path.parent / "reports/formal-source-result.json"
    process = subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "--network",
            "none",
            "--pull=never",
            "--user",
            "0:0",
            "-v",
            f"{ROOT}:/workspace",
            "-w",
            "/workspace",
            IMAGE,
            _container_path(RUNNER),
            "execute",
            "--request",
            _container_path(request_path),
            "--result",
            _container_path(result_path),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert process.returncode != 0
    assert not result_path.exists()
    assert "canonical clean project source identity" in process.stderr


def _fixture_profile(request: dict[str, object], path: Path) -> Path:
    profile = {
        "status": "frozen",
        "request_id": "hnsw-merger-small-preflight",
        "dataset": {"global_node_count": 12},
        "shared_inputs": request["shared_inputs"],
        "merges": request["merges"],
        "source_admission": {
            "toolchain": request["source_admission"]["toolchain"]
        },
        "execution": {
            "memory_budget_bytes": 67_108_864,
            "thread_budget": 1,
            "parameter_sweep": False,
            "automatic_tuning": False,
            "best_result_selection": False,
        },
    }
    path.write_text(json.dumps(profile), encoding="utf-8")
    return path


def test_fixture_preflight_fresh_validates_complete_chain_before_request(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    emitted = _small_request("fresh-preflight")
    request = json.loads(emitted.read_text(encoding="utf-8"))
    workspace = emitted.parent
    profile = _fixture_profile(request, workspace / "fixture-profile.json")
    project = {
        "repository": "https://example.test/project.git",
        "branch": "feature/merge-adaptation",
        "commit": "1" * 40,
        "dirty_worktree": False,
    }
    monkeypatch.setattr(feature, "_project_source_admission", lambda _: project)

    result = feature.preflight(
        profile,
        workspace,
        repository_root=ROOT,
        validation_runner=VALIDATION_RUNNER,
        container_image=IMAGE,
    )
    assert result["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 0,
    }
    assert [item["role"] for item in result["fresh_validation_evidence"]] == [
        "dataset",
        "partition",
        "local_graph:0",
        "local_graph:1",
        "local_graph:2",
    ]
    for item in result["fresh_validation_evidence"]:
        assert (workspace / item["path"]).is_file()
    assert (workspace / feature.FORMAL_REQUEST_RELATIVE).is_file()


def test_fixture_preflight_validation_failure_writes_no_comparison_request(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    emitted = _small_request("fresh-preflight-invalid")
    request = json.loads(emitted.read_text(encoding="utf-8"))
    request["shared_inputs"]["partition"]["aggregate_checksum"] = "0" * 64
    workspace = emitted.parent
    profile = _fixture_profile(request, workspace / "fixture-profile.json")
    monkeypatch.setattr(
        feature,
        "_project_source_admission",
        lambda _: {
            "repository": "https://example.test/project.git",
            "branch": "feature/merge-adaptation",
            "commit": "1" * 40,
            "dirty_worktree": False,
        },
    )
    with pytest.raises(feature.HnswMergerHarnessError, match="fresh validation"):
        feature.preflight(
            profile,
            workspace,
            repository_root=ROOT,
            validation_runner=VALIDATION_RUNNER,
            container_image=IMAGE,
        )
    assert not (workspace / feature.FORMAL_REQUEST_RELATIVE).exists()


def test_sift1m_profile_is_predeclared_draft_and_not_formally_admitted() -> None:
    profile = feature.load_yaml_file(DRAFT_PROFILE)
    assert profile["status"] == "draft"
    assert profile["dataset"] == {
        "path": "sift1M_dataset/base_vector.bin",
        "global_node_count": 1_000_000,
        "vector_dimension": 128,
        "byte_length": 512_000_008,
        "checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
    }
    assert len(profile["shared_inputs"]["local_graphs"]) == 3
    assert profile["execution"]["parameter_sweep"] is False
    with pytest.raises(feature.HnswMergerHarnessError, match="not frozen"):
        feature._validate_preflight_profile(profile)
