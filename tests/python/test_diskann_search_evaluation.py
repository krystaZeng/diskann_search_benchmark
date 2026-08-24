from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import struct
import sys
from pathlib import Path

import pytest

from diskann_harness import diskann_search_evaluation as search


def test_materialization_request_is_strict_and_fixed(tmp_path: Path) -> None:
    request = {
        "request_id": "small-pq",
        "run_id": "small-pq",
        "workspace": {"root": str(tmp_path)},
        "operation": "pq",
        "dataset": {
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "a" * 64,
            "metadata_path": "dataset/artifact.json",
        },
        "implementation": {
            "implementation_id": "cpp_main_post_graph_pq_v1",
            "implementation_version": "1.0.0",
            "reproduction_mode": "upstream_faithful",
        },
        "parameters": {
            "B": 0.03,
            "QD": 0,
            "pq_chunks": 32,
            "sample_rate": 0.256,
            "centroid_count": 256,
            "opq": False,
            "distance_metric": "l2",
        },
        "resource_budget": {
            "memory_budget_bytes": 67108864,
            "thread_budget": 1,
        },
        "candidate_path": "temporary/small-pq/candidate/artifact.json",
    }
    normalized = search.normalize_materialization_request(request)
    assert normalized["parameters"] == request["parameters"]
    assert "global_graph" not in normalized

    changed = json.loads(json.dumps(request))
    changed["parameters"]["opq"] = True
    with pytest.raises(search.SearchEvaluationError):
        search.normalize_materialization_request(changed)


def test_materialize_cli_uses_one_cpp_runner_invocation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    request = tmp_path / "request.json"
    request.write_text("{}", encoding="utf-8")
    observed: list[list[str]] = []

    def fake_run(argv: list[str]) -> int:
        observed.append(argv)
        return 0

    monkeypatch.setattr(search, "_run_argv", fake_run)
    assert (
        search.main(
            [
                "materialize",
                "--operation",
                "pq",
                "--request",
                str(request),
                "--runner",
                "/tmp/diskann_search_evaluation_runner",
            ]
        )
        == 0
    )
    assert observed == [
        [
            "/tmp/diskann_search_evaluation_runner",
            "materialize",
            "--request",
            str(request),
        ]
    ]


def test_selected_query_normalization_preserves_opaque_source_id(tmp_path: Path) -> None:
    source = Path("tests/fixtures/search/selected_query_small.csv")
    output = search.normalize_query_set_fixture(
        source,
        workspace=tmp_path,
        query_set_id="selected-query-small",
        expected_dimension=4,
    )
    assert output["reference"]["query_identity"] == "query_row_index"
    assert output["reference"]["source_query_id_semantics"] == "opaque"
    assert output["mapping"] == [
        {"query_row_index": 0, "source_query_id": "0042"},
        {"query_row_index": 1, "source_query_id": "not-a-dataset-id"},
    ]
    assert output["native_header"] == (2, 4)


def test_input_materialization_manifests_capture_runtime_and_provenance(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    project_source = {
        "repository": "https://example.invalid/project.git",
        "branch": "feature/search-evaluation",
        "commit": "b" * 40,
        "dirty_worktree": True,
        "dirty_source_checksum": "c" * 64,
    }
    monkeypatch.setattr(search, "_git_source_identity", lambda root: project_source)
    dataset_identity = {
        "dataset_id": "small-search-dataset",
        "dataset_checksum": "d" * 64,
        "global_node_count": 256,
        "vector_dimension": 4,
        "scalar_type": "float32",
        "distance_metric": "l2",
    }
    native_toolchain = {
        "os": "linux",
        "compiler_path": "/usr/bin/c++",
        "compiler_version": "11.4.0",
        "cmake_version": "3.22.1",
        "generator": "Unix Makefiles",
        "build_type": "Release",
        "cmake_definitions": [
            "CMAKE_BUILD_TYPE=Release",
            "DISKANN_CPP_MAIN_BASELINE=ON",
            "DISKANN_SEARCH_EVALUATION=ON",
        ],
        "compile_commands": [
            "/usr/bin/c++ -std=c++17 -O3 -DNDEBUG -mavx2 -fopenmp"
        ],
        "link_commands": ["compute_groundtruth <- diskann_s,OpenMP"],
    }
    common = {
        "workspace_root": str(tmp_path),
        "source": project_source,
        "toolchain": native_toolchain,
        "dataset_identity": dataset_identity,
        "resource_budget": {
            "memory_budget_bytes": 16 * 1024 * 1024,
            "thread_budget": 1,
        },
        "started_at": "2000-01-01T00:00:00Z",
        "ended_at": "2000-01-01T00:00:00Z",
        "input_artifacts": [],
    }
    csv_path = tmp_path / "selected-query.csv"
    csv_path.write_text(
        "id,v0,v1,v2,v3\nopaque-42,0,1,2,3\n", encoding="utf-8"
    )
    query_request = {
        **common,
        "run_id": "query-provenance",
        "source_csv": str(csv_path),
        "source_byte_length": csv_path.stat().st_size,
        "source_sha256": search._sha256(csv_path),
        "query_set_id": "query-provenance",
        "dimension": 4,
        "query_count": 1,
        "reference_path": "inputs/query-reference.json",
        "manifest_path": "manifests/query-provenance.json",
        "validation_evidence_path": "manifests/evidence/query-provenance.json",
    }
    query_request_path = tmp_path / "query-request.json"
    query_request_path.write_text(json.dumps(query_request), encoding="utf-8")
    query_times = iter(["2026-08-23T01:00:00Z", "2026-08-23T01:00:01Z"])
    monkeypatch.setattr(search, "_now_utc", lambda: next(query_times))
    query_result = search.materialize_query_set_request(query_request_path)
    query_manifest = json.loads(
        Path(query_result["manifest_path"]).read_text(encoding="utf-8")
    )
    search._schema_validator(
        "build-manifest.schema.json", search._repository_root()
    ).validate(query_manifest)
    assert query_manifest["started_at"] == "2026-08-23T01:00:00Z"
    assert query_manifest["ended_at"] == "2026-08-23T01:00:01Z"
    assert query_manifest["source"] == project_source
    assert query_manifest["toolchain"]["compiler_path"] == sys.executable
    assert query_manifest["toolchain"]["generator"] == "python-harness"
    assert query_manifest["parameters"]["source_csv"]["sha256"] == search._sha256(csv_path)

    query_reference = json.loads(
        Path(query_result["reference_path"]).read_text(encoding="utf-8")
    )
    groundtruth_binary = tmp_path / "bin/compute_groundtruth"
    groundtruth_binary.parent.mkdir(parents=True)
    groundtruth_binary.write_bytes(b"fixture-compute-groundtruth")
    truth_request = {
        **common,
        "run_id": "truth-provenance",
        "compute_groundtruth_binary": str(groundtruth_binary),
        "compute_groundtruth_sha256": search._sha256(groundtruth_binary),
        "dataset_payload": str(tmp_path / "dataset.bin"),
        "query_payload": str(tmp_path / query_reference["native_payload"]["path"]),
        "payload_path": "inputs/truth.bin",
        "query_count": 1,
        "width": 101,
        "ground_truth_id": "truth-provenance",
        "dataset_artifact": {
            "metadata_path": "dataset/artifact.json",
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "a" * 64,
        },
        "query_set_checksum": query_reference["canonical_checksum"],
        "reference_path": "inputs/truth-reference.json",
        "manifest_path": "manifests/truth-provenance.json",
        "validation_evidence_path": "manifests/evidence/truth-provenance.json",
        "stdout_path": "reports/truth.stdout",
        "stderr_path": "reports/truth.stderr",
    }
    truth_request_path = tmp_path / "truth-request.json"
    truth_request_path.write_text(json.dumps(truth_request), encoding="utf-8")

    def groundtruth_run(
        argv: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        output = Path(argv[argv.index("--gt_file") + 1])
        ids = list(range(101))
        distances = [float(index) for index in ids]
        output.write_bytes(
            struct.pack("<II", 1, 101)
            + struct.pack("<101I", *ids)
            + struct.pack("<101f", *distances)
        )
        return subprocess.CompletedProcess(argv, 0, "fixture stdout", "")

    monkeypatch.setattr(search.subprocess, "run", groundtruth_run)
    truth_times = iter(["2026-08-23T02:00:00Z", "2026-08-23T02:00:02Z"])
    monkeypatch.setattr(search, "_now_utc", lambda: next(truth_times))
    truth_result = search.materialize_ground_truth_request(truth_request_path)
    truth_manifest = json.loads(
        Path(truth_result["manifest_path"]).read_text(encoding="utf-8")
    )
    search._schema_validator(
        "build-manifest.schema.json", search._repository_root()
    ).validate(truth_manifest)
    assert truth_manifest["started_at"] == "2026-08-23T02:00:00Z"
    assert truth_manifest["ended_at"] == "2026-08-23T02:00:02Z"
    assert truth_manifest["source"] == project_source
    assert truth_manifest["toolchain"] == native_toolchain
    assert truth_manifest["parameters"]["diskann_source"] == {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": "78256bbab4685e1774e78d331e081a153be26823",
        "dirty_worktree": False,
    }
    assert truth_manifest["parameters"]["compute_groundtruth_binary"] == {
        "path": str(groundtruth_binary),
        "expected_sha256": search._sha256(groundtruth_binary),
        "actual_sha256": search._sha256(groundtruth_binary),
        "sha256": search._sha256(groundtruth_binary),
    }

    failed_truth_request = {
        **truth_request,
        "run_id": "truth-failure-provenance",
        "payload_path": "inputs/truth-failure.bin",
        "reference_path": "inputs/truth-failure-reference.json",
        "manifest_path": "manifests/truth-failure-provenance.json",
        "validation_evidence_path": "manifests/evidence/truth-failure.json",
        "stdout_path": "reports/truth-failure.stdout",
        "stderr_path": "reports/truth-failure.stderr",
    }
    failed_truth_request_path = tmp_path / "truth-failure-request.json"
    failed_truth_request_path.write_text(
        json.dumps(failed_truth_request), encoding="utf-8"
    )
    monkeypatch.setattr(
        search.subprocess,
        "run",
        lambda argv, **kwargs: subprocess.CompletedProcess(
            argv, 7, "fixture failure stdout", "fixture failure stderr"
        ),
    )
    failed_truth_times = iter(
        ["2026-08-23T02:30:00Z", "2026-08-23T02:30:00Z"]
    )
    monkeypatch.setattr(search, "_now_utc", lambda: next(failed_truth_times))
    with pytest.raises(search.SearchEvaluationError):
        search.materialize_ground_truth_request(failed_truth_request_path)
    failed_truth_manifest = json.loads(
        (tmp_path / failed_truth_request["manifest_path"]).read_text(
            encoding="utf-8"
        )
    )
    search._schema_validator(
        "build-manifest.schema.json", search._repository_root()
    ).validate(failed_truth_manifest)
    assert failed_truth_manifest["status"] == "FAILED"
    assert failed_truth_manifest["started_at"] == "2026-08-23T02:30:00Z"
    assert failed_truth_manifest["ended_at"] == "2026-08-23T02:30:00Z"
    assert failed_truth_manifest["failure"]["exit_code"] == 7


def test_input_materialization_failure_manifests_capture_terminal_time(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    project_source = {
        "repository": "https://example.invalid/project.git",
        "branch": "feature/search-evaluation",
        "commit": "b" * 40,
        "dirty_worktree": False,
    }
    monkeypatch.setattr(search, "_git_source_identity", lambda root: project_source)
    request = {
        "workspace_root": str(tmp_path),
        "run_id": "query-failure-provenance",
        "source": project_source,
        "toolchain": {
            "os": "linux", "compiler_path": "/usr/bin/c++",
            "compiler_version": "11", "cmake_version": "3.22",
            "generator": "Unix Makefiles", "build_type": "Release",
            "cmake_definitions": ["CMAKE_BUILD_TYPE=Release"],
            "compile_commands": ["c++ -O3 -DNDEBUG"],
            "link_commands": ["compute_groundtruth"],
        },
        "dataset_identity": {
            "dataset_id": "small", "dataset_checksum": "d" * 64,
            "global_node_count": 256, "vector_dimension": 4,
            "scalar_type": "float32", "distance_metric": "l2",
        },
        "resource_budget": {"memory_budget_bytes": 1, "thread_budget": 1},
        "started_at": "2000-01-01T00:00:00Z",
        "ended_at": "2000-01-01T00:00:00Z",
        "input_artifacts": [],
        "source_csv": str(tmp_path / "missing.csv"),
        "source_byte_length": 1,
        "source_sha256": "a" * 64,
        "query_set_id": "missing",
        "dimension": 4,
        "query_count": 1,
        "reference_path": "inputs/missing-reference.json",
        "manifest_path": "manifests/query-failure-provenance.json",
        "validation_evidence_path": "manifests/evidence/query-failure.json",
    }
    path = tmp_path / "query-failure-request.json"
    path.write_text(json.dumps(request), encoding="utf-8")
    times = iter(["2026-08-23T03:00:00Z", "2026-08-23T03:00:00Z"])
    monkeypatch.setattr(search, "_now_utc", lambda: next(times))
    with pytest.raises(Exception):
        search.materialize_query_set_request(path)
    manifest = json.loads(
        (tmp_path / request["manifest_path"]).read_text(encoding="utf-8")
    )
    search._schema_validator(
        "build-manifest.schema.json", search._repository_root()
    ).validate(manifest)
    assert manifest["status"] == "FAILED"
    assert manifest["started_at"] == "2026-08-23T03:00:00Z"
    assert manifest["ended_at"] == "2026-08-23T03:00:00Z"


@pytest.mark.parametrize(
    ("binary_case", "expected_reason"),
    [
        ("missing", "compute_groundtruth binary is missing"),
        ("checksum-mismatch", "compute_groundtruth binary checksum mismatch"),
    ],
)
def test_ground_truth_binary_admission_retains_failed_manifest(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    binary_case: str,
    expected_reason: str,
) -> None:
    project_source = {
        "repository": "https://example.invalid/project.git",
        "branch": "feature/search-evaluation",
        "commit": "b" * 40,
        "dirty_worktree": False,
    }
    monkeypatch.setattr(search, "_git_source_identity", lambda root: project_source)
    binary = tmp_path / "bin/compute_groundtruth"
    if binary_case == "checksum-mismatch":
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"unexpected binary")
    request = {
        "workspace_root": str(tmp_path),
        "run_id": f"truth-binary-{binary_case}",
        "source": project_source,
        "toolchain": {
            "os": "linux",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": "11",
            "cmake_version": "3.22",
            "generator": "Unix Makefiles",
            "build_type": "Release",
            "cmake_definitions": ["CMAKE_BUILD_TYPE=Release"],
            "compile_commands": ["c++ -O3 -DNDEBUG"],
            "link_commands": ["compute_groundtruth"],
        },
        "dataset_identity": {
            "dataset_id": "small",
            "dataset_checksum": "d" * 64,
            "global_node_count": 256,
            "vector_dimension": 4,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "resource_budget": {"memory_budget_bytes": 1024, "thread_budget": 1},
        "started_at": "2000-01-01T00:00:00Z",
        "ended_at": "2000-01-01T00:00:00Z",
        "input_artifacts": [],
        "compute_groundtruth_binary": str(binary),
        "compute_groundtruth_sha256": "e" * 64,
        "dataset_payload": str(tmp_path / "dataset.bin"),
        "query_payload": str(tmp_path / "query.bin"),
        "payload_path": "inputs/truth.bin",
        "query_count": 1,
        "width": 101,
        "ground_truth_id": "truth-binary-admission",
        "dataset_artifact": {
            "metadata_path": "dataset/artifact.json",
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "a" * 64,
        },
        "query_set_checksum": "c" * 64,
        "reference_path": "inputs/truth-reference.json",
        "manifest_path": f"manifests/truth-binary-{binary_case}.json",
        "validation_evidence_path": "manifests/evidence/truth.json",
        "stdout_path": "reports/truth.stdout",
        "stderr_path": "reports/truth.stderr",
    }
    request_path = tmp_path / f"truth-binary-{binary_case}-request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    subprocess_called = False

    def unexpected_subprocess(*args: object, **kwargs: object) -> None:
        nonlocal subprocess_called
        subprocess_called = True
        pytest.fail("binary admission failure must not execute subprocess")

    monkeypatch.setattr(search.subprocess, "run", unexpected_subprocess)
    times = iter(["2026-08-23T04:00:00Z", "2026-08-23T04:00:01Z"])
    monkeypatch.setattr(search, "_now_utc", lambda: next(times))

    with pytest.raises(search.SearchEvaluationError, match=expected_reason):
        search.materialize_ground_truth_request(request_path)

    assert not subprocess_called
    manifest_path = tmp_path / request["manifest_path"]
    assert manifest_path.is_file()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    search._schema_validator(
        "build-manifest.schema.json", search._repository_root()
    ).validate(manifest)
    assert manifest["status"] == "FAILED"
    assert manifest["started_at"] == "2026-08-23T04:00:00Z"
    assert manifest["ended_at"] == "2026-08-23T04:00:01Z"
    assert manifest["started_at"] != request["started_at"]
    assert manifest["ended_at"] != request["ended_at"]
    assert expected_reason in manifest["failure"]["reason"]
    binary_identity = manifest["parameters"]["compute_groundtruth_binary"]
    assert binary_identity["path"] == str(binary)
    assert binary_identity["expected_sha256"] == request["compute_groundtruth_sha256"]
    if binary_case == "missing":
        assert "actual_sha256" not in binary_identity
    else:
        assert binary_identity["actual_sha256"] == search._sha256(binary)
    assert not (tmp_path / request["reference_path"]).exists()
    assert not (tmp_path / request["payload_path"]).exists()


def _protocol(cache_mode: str) -> dict:
    checksum = "a" * 64
    ref = lambda kind, version, path: {
        "metadata_path": path,
        "artifact_type": kind,
        "format_version": version,
        "aggregate_checksum": checksum,
    }
    query_set = {
        "reference_version": "1.0.0",
        "query_set_id": "query-small",
        "logical_source": "tests/fixtures/search/selected_query_small.csv",
        "source": {"path": "inputs/query.csv", "format": "csv", "byte_length": 10, "sha256": checksum},
        "query_count": 2,
        "dimension": 4,
        "scalar_type": "float32",
        "query_identity": "query_row_index",
        "source_query_id_semantics": "opaque",
        "self_match_policy": "no_id_based_exclusion",
        "native_payload": {"path": "inputs/query.bin", "format": "diskann-float32-bin", "byte_length": 40, "sha256": checksum},
        "row_mapping": {"path": "inputs/query-map.json", "format": "json", "byte_length": 10, "sha256": checksum},
        "conversion_manifest_path": "manifests/query.json",
        "validation_evidence_path": "manifests/evidence/query.json",
        "canonical_checksum": checksum,
    }
    ground_truth = {
        "reference_version": "1.0.0",
        "ground_truth_id": "truth-small",
        "payload": {"path": "inputs/truth.bin", "format": "diskann-truthset", "byte_length": 100, "sha256": checksum},
        "query_count": 2,
        "width": 100,
        "id_space": "global",
        "distance_metric": "l2_squared_float32",
        "dataset_artifact": ref("dataset", "diskann-float32-dataset/v1", "dataset/artifact.json"),
        "query_set_checksum": checksum,
        "self_match_policy": "no_id_based_exclusion",
        "rank_100_tie_group_closed": True,
        "generation_manifest_path": "manifests/truth.json",
        "validation_evidence_path": "manifests/evidence/truth.json",
        "canonical_checksum": checksum,
    }
    cache = {"mode": "zero", "requested_nodes": 0}
    if cache_mode == "fixed_bfs":
        cache = {"mode": "fixed_bfs", "requested_nodes": 10000, "construction": "medoid_bfs", "shuffle": False}
    execution = {
        "execution_id": f"small-{cache_mode}",
        "dataset_artifact": ref("dataset", "diskann-float32-dataset/v1", "dataset/artifact.json"),
        "pq_artifact": ref("pq", "pq-artifact/v1", "pq/artifact.json"),
        "disk_layout_artifact": ref("disk_layout", "disk-layout-artifact/v1", "layout/artifact.json"),
        "global_graph_artifact": ref("global_graph", "global-graph-artifact/v1", "graph/artifact.json"),
        "query_set": query_set,
        "distance_metric": "l2", "K": 100, "L_search": [100, 200, 400],
        "beamwidth": 2, "thread_count": 6, "cache": cache,
        "io_limit": 4294967295, "warmup_policy": "none",
        "query_order": "query_row_index_ascending",
        "timing_boundary": "cpp_main_search_disk_index_query_batch_v1",
        "storage_placement": "fixture", "repetition_count": 3,
        "schedule": "repetition_major_then_L_order", "entry_policy": "end_to_end",
        "environment": {"os": "linux", "storage": "local_ssd", "direct_io": "required_and_probed", "linux_aio": "required_and_probed", "remote_storage_allowed": False},
        "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
        "runner_build": {"project_repository": "https://example.invalid/project.git", "project_branch": "feature/search-evaluation", "project_commit": "b" * 40, "project_dirty": False, "binary_path": "bin/runner", "binary_sha256": checksum, "build_manifest_path": "manifests/build.json", "build_manifest_sha256": checksum},
        "diskann_source_commit": "78256bbab4685e1774e78d331e081a153be26823",
    }
    evaluation = {
        "ground_truth": ground_truth,
        "evaluator": {"implementation_id": "cpp_main_search_metrics_evaluator_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
        "metrics": ["recall_at_10", "recall_at_100", "qps_k100", "mean_latency_us_k100", "p99_9_latency_us_k100", "mean_ios_k100"],
        "quality_eligibility_policy": "complete_valid_ground_truth_identity_search_semantics_v1",
        "performance_eligibility_policy": "complete_valid_normative_environment_timing_parity_v1",
        "aggregation_policy": "median_eligible", "replacement_repetitions": False,
    }
    return {"protocol_version": "1.0.0", "protocol_id": f"protocol-{cache_mode}", "status": "frozen", "search_execution": execution, "search_execution_checksum": checksum, "evaluation": evaluation, "canonicalization": "diskann-search-protocol-c14n-v1", "protocol_checksum": checksum}


def _fresh_validation_evidence(artifact: dict, *, valid: bool = True) -> dict:
    return {
        "evidence_version": "1.0.0",
        "validator_version": "artifact-contract-validator/1.1.0",
        "artifact_candidate": {
            "artifact_type": artifact["artifact_type"],
            "format_version": artifact["format_version"],
            "metadata_path": artifact["metadata_path"],
            "claimed_aggregate_checksum": artifact["aggregate_checksum"],
        },
        "checks": [{
            "check_id": "artifact.contract/v1",
            "passed": valid,
            "message": "fresh fixture validation",
        }],
        "result": "VALID" if valid else "INVALID",
        **({"failure_category": "schema_invalid"} if not valid else {}),
    }


def _formal_preflight_config(
    tmp_path: Path, *, fixture_mode: bool, include_evidence: bool
) -> tuple[Path, list[dict], dict[str, Path]]:
    zero = _protocol("zero")
    fixed = _protocol("fixed_bfs")
    runner = tmp_path / "bin/runner"
    runner.parent.mkdir(parents=True, exist_ok=True)
    runner.write_bytes(b"small-runner")
    files: dict[str, Path] = {"runner": runner}
    for declaration in (zero, fixed):
        execution = declaration["search_execution"]
        execution["runner_build"]["binary_path"] = "bin/runner"
        execution["runner_build"]["binary_sha256"] = search._sha256(runner)
        for name in (
            "dataset_artifact", "pq_artifact", "disk_layout_artifact",
            "global_graph_artifact",
        ):
            metadata = tmp_path / "normal" / execution[name]["metadata_path"]
            metadata.parent.mkdir(parents=True, exist_ok=True)
            metadata.write_text("{}", encoding="utf-8")
        query = tmp_path / execution["query_set"]["native_payload"]["path"]
        query.parent.mkdir(parents=True, exist_ok=True)
        query.write_bytes(b"query")
        truth = tmp_path / declaration["evaluation"]["ground_truth"]["payload"]["path"]
        truth.parent.mkdir(parents=True, exist_ok=True)
        truth.write_bytes(b"truth")
    if include_evidence:
        evidence_root = tmp_path / "manifests/evidence"
        evidence_root.mkdir(parents=True, exist_ok=True)
        evidence_refs = {}
        for key, artifact_key in (
            ("pq_artifact", "pq_artifact"),
            ("disk_layout_artifact", "disk_layout_artifact"),
        ):
            artifact = zero["search_execution"][artifact_key]
            path = evidence_root / f"fresh-{key}.json"
            path.write_text(
                json.dumps(_fresh_validation_evidence(artifact)), encoding="utf-8"
            )
            files[key] = path
            evidence_refs[key] = {
                "path": path.relative_to(tmp_path).as_posix(),
                "sha256": search._sha256(path),
                "expected_artifact": artifact,
            }
        for declaration in (zero, fixed):
            declaration["search_execution"]["admission_evidence"] = json.loads(
                json.dumps(evidence_refs)
            )
    config = {
        "mode": "formal-search",
        "fixture_mode": fixture_mode,
        "protocols": [zero, fixed],
        "common_admission": {
            "artifacts_valid": True,
            "inputs_compatible": True,
            "runner_path": str(runner),
            "runner_sha256": search._sha256(runner),
            "abi_dependencies_usable": True,
            "load_probe_pass": True,
            "paths_confined": True,
            "resources_sufficient": True,
        },
        "performance_admission": {
            "native_linux": False,
            "local_ssd": False,
            "direct_io": False,
            "linux_aio": False,
            "parity_pass": False,
        },
    }
    path = tmp_path / "formal.json"
    path.write_text(json.dumps(config), encoding="utf-8")
    return path, [zero, fixed], files


def test_protocol_normalization_freezes_only_requested_cache_nodes() -> None:
    zero = search.normalize_search_protocol(_protocol("zero"))
    fixed = search.normalize_search_protocol(_protocol("fixed_bfs"))
    assert zero["search_execution"]["cache"] == {"mode": "zero", "requested_nodes": 0}
    assert fixed["search_execution"]["cache"]["requested_nodes"] == 10000
    assert "requested_primary_bytes" not in fixed["search_execution"]["cache"]
    assert fixed["search_execution"]["L_search"] == [100, 200, 400]
    assert fixed["search_execution"]["repetition_count"] == 3


def test_non_fixture_preflight_binds_fresh_pq_layout_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config, declarations, _ = _formal_preflight_config(
        tmp_path, fixture_mode=False, include_evidence=True
    )
    prior_checksum = search.normalize_search_protocol(
        json.loads(json.dumps(declarations[0]))
    )["search_execution_checksum"]
    legacy = json.loads(json.dumps(declarations[0]))
    legacy["search_execution"].pop("admission_evidence")
    assert search.normalize_search_protocol(legacy)[
        "search_execution_checksum"
    ] != prior_checksum
    monkeypatch.setattr(
        search, "_run_argv", lambda argv: pytest.fail("preflight must not search")
    )
    result = search.preflight(
        mode="formal-search", config_path=config, workspace=tmp_path
    )
    assert result["common_search_admission"] is True
    assert len(result["protocol_paths"]) == 2
    emitted = json.loads(Path(result["protocol_paths"][0]).read_text(encoding="utf-8"))
    assert emitted["search_execution"]["admission_evidence"] == declarations[0][
        "search_execution"
    ]["admission_evidence"]
    assert emitted["search_execution_checksum"] == prior_checksum


def test_non_fixture_preflight_rejects_boolean_only_artifact_admission(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config, _, _ = _formal_preflight_config(
        tmp_path, fixture_mode=False, include_evidence=False
    )
    monkeypatch.setattr(
        search, "_run_argv", lambda argv: pytest.fail("preflight must not search")
    )
    result = search.preflight(
        mode="formal-search", config_path=config, workspace=tmp_path
    )
    assert result["common_search_admission"] is False
    assert result["protocol_paths"] == []
    assert result["request_paths"] == []


@pytest.mark.parametrize(
    "failure",
    ["missing", "sha_mismatch", "invalid", "candidate_mismatch", "ref_mismatch"],
)
def test_formal_preflight_rejects_invalid_fresh_admission_evidence(
    tmp_path: Path, failure: str
) -> None:
    config, declarations, files = _formal_preflight_config(
        tmp_path, fixture_mode=False, include_evidence=True
    )
    evidence = files["pq_artifact"]
    if failure == "missing":
        evidence.unlink()
    elif failure == "sha_mismatch":
        declarations[0]["search_execution"]["admission_evidence"]["pq_artifact"][
            "sha256"
        ] = "f" * 64
        declarations[1]["search_execution"]["admission_evidence"]["pq_artifact"][
            "sha256"
        ] = "f" * 64
    elif failure in {"invalid", "candidate_mismatch"}:
        document = json.loads(evidence.read_text(encoding="utf-8"))
        if failure == "invalid":
            document = _fresh_validation_evidence(
                declarations[0]["search_execution"]["pq_artifact"], valid=False
            )
        else:
            document["artifact_candidate"]["claimed_aggregate_checksum"] = "e" * 64
        evidence.write_text(json.dumps(document), encoding="utf-8")
        for declaration in declarations:
            declaration["search_execution"]["admission_evidence"]["pq_artifact"][
                "sha256"
            ] = search._sha256(evidence)
    else:
        for declaration in declarations:
            declaration["search_execution"]["admission_evidence"]["pq_artifact"][
                "expected_artifact"
            ]["aggregate_checksum"] = "d" * 64
    document = json.loads(config.read_text(encoding="utf-8"))
    document["protocols"] = declarations
    config.write_text(json.dumps(document), encoding="utf-8")
    with pytest.raises(search.SearchEvaluationError):
        search.preflight(mode="formal-search", config_path=config, workspace=tmp_path)
    assert not (tmp_path / "protocols").exists()


def test_fixture_preflight_can_bind_explicit_fixture_evidence_for_fast_path(
    tmp_path: Path
) -> None:
    config, declarations, _ = _formal_preflight_config(
        tmp_path, fixture_mode=True, include_evidence=True
    )
    result = search.preflight(
        mode="formal-search", config_path=config, workspace=tmp_path
    )
    emitted = json.loads(Path(result["protocol_paths"][0]).read_text(encoding="utf-8"))
    assert emitted["search_execution"]["admission_evidence"] == declarations[0][
        "search_execution"
    ]["admission_evidence"]


def test_offline_metrics_keep_quality_separate_from_performance() -> None:
    ids = list(range(100))
    observation = {
        "status": "SUCCEEDED", "completed_query_count": 2,
        "query_count": 2, "batch_wall_clock_us": 2000,
        "per_query": [
            {"query_row_index": 0, "source_query_id": "0042", "result_ids": ids, "result_distances": [float(i) for i in ids], "total_us": 10.0, "n_ios": 2},
            {"query_row_index": 1, "source_query_id": "not-a-dataset-id", "result_ids": ids, "result_distances": [float(i) for i in ids], "total_us": 30.0, "n_ios": 4},
        ],
    }
    result = search.evaluate_raw_point(
        observation,
        ground_truth=[ids, ids],
        quality_compatible=True,
        performance_admitted=False,
    )
    assert result["quality_eligible"] is True
    assert result["quality_metrics"] == {"recall_at_10": 100.0, "recall_at_100": 100.0}
    assert result["performance_eligible"] is False
    assert result["display_metrics"]["qps_k100"] == pytest.approx(1000.0)
    assert result["display_metrics"]["mean_latency_us_k100"] == 20.0
    assert result["display_metrics"]["p99_9_latency_us_k100"] == pytest.approx(30.0)
    assert result["display_metrics"]["mean_ios_k100"] == 3.0
    assert result.get("performance_metrics") is None
    assert all(math.isfinite(value) for value in result["display_metrics"].values())


def test_recall_uses_pinned_equal_distance_tie_semantics() -> None:
    result_ids = list(range(9)) + [10] + list(range(11, 101))
    truth_ids = list(range(101))
    truth_distances = [float(index) for index in range(10)] + [9.0] + [float(index) for index in range(11, 101)]
    observation = {
        "status": "SUCCEEDED", "completed_query_count": 1,
        "query_count": 1, "batch_wall_clock_us": 1000,
        "per_query": [{
            "query_row_index": 0, "source_query_id": "opaque",
            "result_ids": result_ids,
            "result_distances": [float(index) for index in range(100)],
            "total_us": 10.0, "n_ios": 2,
        }],
    }
    evaluated = search.evaluate_raw_point(
        observation,
        ground_truth=[truth_ids],
        ground_truth_distances=[truth_distances],
        quality_compatible=True,
        performance_admitted=False,
    )
    assert evaluated["quality_metrics"]["recall_at_10"] == pytest.approx(100.0)


def test_invalid_observation_is_audit_only() -> None:
    result = search.audit_excluded_observation(
        observation_id="bad-point", path="observations/bad.json",
        checksum="c" * 64, status="INVALID_OUTPUT", reason="partial query set"
    )
    assert result["quality_eligible"] is False
    assert result["performance_eligible"] is False
    assert "quality_metrics" not in result
    assert "performance_metrics" not in result


def test_private_ground_truth_invocation_is_shell_free(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    observed: list[list[str]] = []

    def fake_prepare(request_path: str | Path) -> dict:
        assert Path(request_path).name == "ground-truth-request.json"
        observed.append(["private-ground-truth"])
        return {"status": "SUCCEEDED"}

    monkeypatch.setattr(search, "materialize_ground_truth_request", fake_prepare)
    request = tmp_path / "ground-truth-request.json"
    request.write_text("{}", encoding="utf-8")
    assert search.main(["materialize", "--operation", "ground_truth", "--request", str(request)]) == 0
    assert observed == [["private-ground-truth"]]


def test_private_query_set_invocation_does_not_start_cpp_runner(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    observed: list[str] = []

    def fake_prepare(request_path: str | Path) -> dict:
        observed.append(Path(request_path).name)
        return {"status": "SUCCEEDED"}

    monkeypatch.setattr(search, "materialize_query_set_request", fake_prepare)
    monkeypatch.setattr(search, "_run_argv", lambda argv: pytest.fail("C++ runner must not run"))
    request = tmp_path / "query-set-request.json"
    request.write_text("{}", encoding="utf-8")
    assert search.main(["materialize", "--operation", "query_set", "--request", str(request)]) == 0
    assert observed == ["query-set-request.json"]


def test_offline_evaluator_reuses_complete_observations_without_search(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    protocol = _protocol("zero")
    dataset_metadata = tmp_path / "normal/dataset/artifact.json"
    dataset_metadata.parent.mkdir(parents=True)
    dataset_metadata.write_text(
        json.dumps(
            {
                "dataset": {
                    "dataset_id": "small-search-dataset",
                    "dataset_checksum": "d" * 64,
                    "global_node_count": 256,
                    "vector_dimension": 4,
                    "scalar_type": "float32",
                    "distance_metric": "l2",
                }
            }
        ),
        encoding="utf-8",
    )
    truth = protocol["evaluation"]["ground_truth"]
    truth["width"] = 101
    truth["query_count"] = 2
    truth_path = tmp_path / truth["payload"]["path"]
    truth_path.parent.mkdir(parents=True)
    truth_ids = list(range(101)) * 2
    truth_distances = [float(index) for index in range(101)] * 2
    truth_path.write_bytes(
        struct.pack("<II", 2, 101)
        + struct.pack(f"<{len(truth_ids)}I", *truth_ids)
        + struct.pack(f"<{len(truth_distances)}f", *truth_distances)
    )
    truth["payload"]["byte_length"] = truth_path.stat().st_size
    truth["payload"]["sha256"] = search._sha256(truth_path)
    protocol = search.normalize_search_protocol(protocol)
    protocol_path = tmp_path / "protocols/protocol-zero.json"
    protocol_path.parent.mkdir(parents=True)
    protocol_path.write_text(
        json.dumps(protocol, sort_keys=True, separators=(",", ":")), encoding="utf-8"
    )
    environment_path = tmp_path / "reports/environment.json"
    environment_path.parent.mkdir(parents=True)
    environment_path.write_text(
        '{"performance_admitted":false}', encoding="utf-8"
    )
    observations = tmp_path / "reports/observations"
    observations.mkdir()
    ids = list(range(100))
    for repetition in range(3):
        for l_search in (100, 200, 400):
            point_id = f"zero-L{l_search}-r{repetition}"
            document = {
                "format_version": "search-observation/v1",
                "observation_id": point_id,
                "run_id": "small-search",
                "generated_at": "2026-08-21T00:00:00Z",
                "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
                "source_build": {"diskann_repository": "https://github.com/microsoft/DiskANN.git", "diskann_branch": "cpp_main", "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823", "binary_path": "bin/runner", "binary_sha256": "a" * 64, "build_manifest_path": "manifests/build.json", "build_manifest_sha256": "a" * 64},
                "protocol": {"path": protocol_path.relative_to(tmp_path).as_posix(), "protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"]},
                "search_execution": {"execution_id": protocol["search_execution"]["execution_id"], "canonical_checksum": protocol["search_execution_checksum"]},
                "dataset_artifact": protocol["search_execution"]["dataset_artifact"],
                "global_graph_artifact": protocol["search_execution"]["global_graph_artifact"],
                "pq_artifact": protocol["search_execution"]["pq_artifact"],
                "disk_layout_artifact": protocol["search_execution"]["disk_layout_artifact"],
                "query_set_checksum": protocol["search_execution"]["query_set"]["canonical_checksum"],
                "raw_search_point": {"point_id": point_id, "L_search": l_search, "K": 100, "beamwidth": 2, "cache": {"mode": "zero", "requested_nodes": 0, "effective_nodes": 0, "primary_payload_bytes": 0}, "repetition_index": repetition, "query_count": 2, "completed_query_count": 2, "batch_wall_clock_us": 2000, "status": "SUCCEEDED"},
                "environment": {"fingerprint_path": environment_path.relative_to(tmp_path).as_posix(), "fingerprint_sha256": search._sha256(environment_path)},
                "per_query": [
                    {"query_row_index": row, "source_query_id": f"opaque-{row}", "result_ids": ids, "result_distances": [float(index) for index in ids], "total_us": 10.0 + row * 20, "n_ios": 2 + row * 2}
                    for row in range(2)
                ],
            }
            document["observation_checksum"] = search._canonical_digest(document)
            (observations / f"{point_id}.json").write_text(
                json.dumps(document, sort_keys=True, separators=(",", ":")), encoding="utf-8"
            )
    monkeypatch.setattr(search, "_run_argv", lambda argv: pytest.fail("evaluate must not run search"))
    output = search.evaluate_observation_directory(
        protocol_path=protocol_path, observations_path=observations, workspace=tmp_path
    )
    assert len(output["display"]) == 9
    assert output["report"]["aggregates"]["quality"][0]["eligible_count"] == 3
    assert output["report"]["aggregates"]["performance"] == []
    assert all(item["classification"] == "informational_non_formal" for item in output["display"])
    manifest = json.loads(Path(output["build_manifest_path"]).read_text(encoding="utf-8"))
    assert manifest["stage"] == "evaluate"
    assert manifest["implementation"]["implementation_id"] == "cpp_main_search_metrics_evaluator_v1"
    assert "metrics" not in manifest
    assert manifest["parameters"]["output_reference"]["sha256"] == search._sha256(Path(output["report_path"]))


def test_parity_cli_uses_shell_free_oracle_and_runner_argv(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    protocol = tmp_path / "protocol.json"
    protocol.write_text("{}", encoding="utf-8")
    observed: list[list[str]] = []
    monkeypatch.setattr(search, "run_search_runner_parity", lambda **kwargs: observed.append([str(kwargs["oracle"]), str(kwargs["runner"])] ) or {"result": "PASS"})
    assert search.main(["parity", "--protocol", str(protocol), "--oracle", "/bin/oracle", "--runner", "/bin/instrumented", "--workspace", str(tmp_path)]) == 0
    assert observed == [["/bin/oracle", "/bin/instrumented"]]


def test_parity_compares_real_output_contract_and_retains_oracle_manifests(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    protocol = _protocol("zero")
    dataset_metadata = tmp_path / "normal/dataset/artifact.json"
    dataset_metadata.parent.mkdir(parents=True)
    dataset_metadata.write_text(json.dumps({"dataset": {
        "dataset_id": "small-search-dataset", "dataset_checksum": "d" * 64,
        "global_node_count": 256, "vector_dimension": 4,
        "scalar_type": "float32", "distance_metric": "l2",
    }}), encoding="utf-8")
    query = tmp_path / protocol["search_execution"]["query_set"]["native_payload"]["path"]
    query.parent.mkdir(parents=True)
    query.write_bytes(struct.pack("<II", 2, 4) + struct.pack("<8f", *([0.0] * 8)))
    truth = protocol["evaluation"]["ground_truth"]
    truth["width"] = 101
    truth["query_count"] = 2
    truth_path = tmp_path / truth["payload"]["path"]
    truth_path.parent.mkdir(parents=True, exist_ok=True)
    truth_ids = list(range(101)) * 2
    truth_distances = [float(index) for index in range(101)] * 2
    truth_path.write_bytes(
        struct.pack("<II", 2, 101)
        + struct.pack(f"<{len(truth_ids)}I", *truth_ids)
        + struct.pack(f"<{len(truth_distances)}f", *truth_distances)
    )
    truth["payload"].update({
        "byte_length": truth_path.stat().st_size,
        "sha256": search._sha256(truth_path),
    })
    build_manifest = tmp_path / protocol["search_execution"]["runner_build"]["build_manifest_path"]
    build_manifest.parent.mkdir(parents=True)
    build_manifest.write_text(json.dumps({
        "manifest_version": "1.0.0", "run_id": "small-build", "stage": "build",
        "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "implementation_version": "1.0.0"},
        "source": {"repository": "https://example.invalid/project.git", "branch": "feature/search-evaluation", "commit": "b" * 40, "dirty_worktree": False},
        "toolchain": {"os": "linux", "compiler_path": "/usr/bin/c++", "compiler_version": "11", "cmake_version": "3.22", "generator": "Unix Makefiles", "build_type": "Release", "cmake_definitions": ["DISKANN_SEARCH_EVALUATION=ON"], "compile_commands": ["c++ -std=c++17"], "link_commands": ["c++ runner"]},
        "dataset": {"dataset_id": "small-search-dataset", "dataset_checksum": "d" * 64, "global_node_count": 256, "vector_dimension": 4, "scalar_type": "float32", "distance_metric": "l2"},
        "parameters": {}, "resource_budget": {"memory_budget_bytes": 1, "thread_budget": 1},
        "input_artifacts": [], "output_artifacts": [], "status": "SUCCEEDED",
        "started_at": "2026-08-21T00:00:00Z", "ended_at": "2026-08-21T00:00:01Z",
    }), encoding="utf-8")
    protocol["search_execution"]["runner_build"]["build_manifest_sha256"] = search._sha256(build_manifest)
    protocol = search.normalize_search_protocol(protocol)
    protocol_path = tmp_path / "protocols/protocol-zero.json"
    protocol_path.parent.mkdir(parents=True)
    protocol_path.write_text(json.dumps(protocol, sort_keys=True, separators=(",", ":")), encoding="utf-8")
    runner = tmp_path / "bin/runner"
    oracle = tmp_path / "bin/search_disk_index"
    runner.parent.mkdir(parents=True)
    runner.write_bytes(b"runner")
    oracle.write_bytes(b"oracle")
    protocol["search_execution"]["runner_build"]["binary_sha256"] = search._sha256(runner)
    protocol = search.normalize_search_protocol(protocol)
    protocol_path.write_text(
        json.dumps(protocol, sort_keys=True, separators=(",", ":")), encoding="utf-8"
    )
    observations = tmp_path / "reports/search-observations"
    environment = tmp_path / "reports/environment/protocol-zero.json"
    environment.parent.mkdir(parents=True)
    environment.write_text('{"performance_admitted":false}', encoding="utf-8")
    ids = list(range(100))
    distances = [float(index) for index in ids]

    def write_observations() -> None:
        observations.mkdir(parents=True)
        for repetition in range(3):
            for l_search in (100, 200, 400):
                point_id = f"protocol-zero-L{l_search}-r{repetition}"
                document = {
                    "format_version": "search-observation/v1", "observation_id": point_id,
                    "run_id": "protocol-zero-search", "generated_at": "2026-08-21T00:00:00Z",
                    "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
                    "source_build": {"diskann_repository": "https://github.com/microsoft/DiskANN.git", "diskann_branch": "cpp_main", "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823", "binary_path": "bin/runner", "binary_sha256": "a" * 64, "build_manifest_path": protocol["search_execution"]["runner_build"]["build_manifest_path"], "build_manifest_sha256": protocol["search_execution"]["runner_build"]["build_manifest_sha256"]},
                    "protocol": {"path": "protocols/protocol-zero.json", "protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"]},
                    "search_execution": {"execution_id": protocol["search_execution"]["execution_id"], "canonical_checksum": protocol["search_execution_checksum"]},
                    "dataset_artifact": protocol["search_execution"]["dataset_artifact"], "global_graph_artifact": protocol["search_execution"]["global_graph_artifact"], "pq_artifact": protocol["search_execution"]["pq_artifact"], "disk_layout_artifact": protocol["search_execution"]["disk_layout_artifact"],
                    "query_set_checksum": protocol["search_execution"]["query_set"]["canonical_checksum"],
                    "raw_search_point": {"point_id": point_id, "L_search": l_search, "K": 100, "beamwidth": 2, "cache": {"mode": "zero", "requested_nodes": 0, "effective_nodes": 0, "primary_payload_bytes": 0}, "repetition_index": repetition, "query_count": 2, "completed_query_count": 2, "batch_wall_clock_us": 2000, "status": "SUCCEEDED"},
                    "environment": {"fingerprint_path": environment.relative_to(tmp_path).as_posix(), "fingerprint_sha256": search._sha256(environment)},
                    "per_query": [{"query_row_index": row, "source_query_id": f"opaque-{row}", "result_ids": ids, "result_distances": distances, "total_us": 10.0, "n_ios": 3} for row in range(2)],
                }
                document["observation_checksum"] = search._canonical_digest(document)
                (observations / f"{point_id}.json").write_text(json.dumps(document, sort_keys=True, separators=(",", ":")), encoding="utf-8")

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        if Path(argv[0]) == runner:
            write_observations()
            return subprocess.CompletedProcess(argv, 0, '{"status":"SUCCEEDED"}', "")
        l_search = int(argv[argv.index("--search_list") + 1])
        result_prefix = Path(argv[argv.index("--result_path") + 1])
        result_prefix.parent.mkdir(parents=True, exist_ok=True)
        for suffix, code, values in (("idx_uint32", "I", ids * 2), ("dists_float", "f", distances * 2)):
            target = Path(f"{result_prefix}_{l_search}_{suffix}.bin")
            target.write_bytes(struct.pack("<II", 2, 100) + struct.pack(f"<{len(values)}{code}", *values))
        stdout = f"{l_search} 2 1000 10 30 3 0 0 100\n"
        return subprocess.CompletedProcess(argv, 0, stdout, "")

    monkeypatch.setattr(search.subprocess, "run", fake_run)
    result = search.run_search_runner_parity(
        protocol=protocol_path, oracle=oracle, runner=runner, workspace=tmp_path
    )
    assert result["result"] == "PASS"
    evidence = json.loads(Path(result["evidence_path"]).read_text(encoding="utf-8"))
    assert evidence["result"] == "PASS"
    assert len(evidence["points"]) == 9
    manifests = sorted((tmp_path / "manifests").glob("*oracle*.json"))
    assert len(manifests) == 9
    assert all(json.loads(path.read_text(encoding="utf-8"))["stage"] == "search" for path in manifests)
    assert all("metrics" not in json.loads(path.read_text(encoding="utf-8")) for path in manifests)


def test_readiness_preflight_is_non_executing_and_emits_no_request(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    upstream = tmp_path / "upstream/diskann-cpp-main"
    upstream.mkdir(parents=True)
    declared_files = {
        "dataset": tmp_path / "dataset.bin",
        "selected_query": tmp_path / "selected-query.csv",
        "validation_runner": tmp_path / "bin/validation-runner",
        "search_runner": tmp_path / "bin/search-runner",
        "oracle": tmp_path / "bin/search-disk-index",
        "groundtruth": tmp_path / "bin/compute-groundtruth",
    }
    for name, path in declared_files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(f"readiness-{name}".encode())
    project_identity = {
        "repository": "https://example.invalid/project.git",
        "branch": "feature/search-evaluation",
        "commit": "b" * 40,
        "dirty_worktree": False,
    }
    upstream_identity = {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": "78256bbab4685e1774e78d331e081a153be26823",
        "dirty_worktree": False,
    }
    monkeypatch.setattr(
        search,
        "_git_source_identity",
        lambda root: upstream_identity if root == upstream else project_identity,
    )
    monkeypatch.setattr(search, "_physical_memory_bytes", lambda: 8 * 1024**3)
    monkeypatch.setattr(search.os, "cpu_count", lambda: 8)
    config = tmp_path / "readiness.json"
    config.write_text(
        json.dumps(
            {
                "mode": "readiness",
                "known": {
                    "repository_root": str(tmp_path),
                    "project_source": {
                        "source_root": ".",
                        **project_identity,
                    },
                    "diskann_source": {
                        "source_root": "upstream/diskann-cpp-main",
                        **upstream_identity,
                    },
                    "file_identities": [
                        {
                            "name": name,
                            "path": path.relative_to(tmp_path).as_posix(),
                            "byte_length": path.stat().st_size,
                            "sha256": search._sha256(path),
                        }
                        for name, path in declared_files.items()
                        if name in {"dataset", "selected_query"}
                    ],
                    "binary_identities": [
                        {
                            "name": name,
                            "path": path.relative_to(tmp_path).as_posix(),
                            "byte_length": path.stat().st_size,
                            "sha256": search._sha256(path),
                        }
                        for name, path in declared_files.items()
                        if name not in {"dataset", "selected_query"}
                    ],
                    "cache_projection": {
                        "requested_nodes": 10000,
                        "dimension": 128,
                        "scalar_bytes": 4,
                        "declared_max_degree": 16,
                        "expected_primary_bytes": 5800000,
                    },
                    "resources": {
                        "minimum_free_bytes": 1,
                        "minimum_memory_bytes": 1,
                        "thread_budget": 6,
                    },
                    "environment": {
                        "local_ssd_admitted": False,
                        "linux_native_smoke_pass": False,
                        "search_runner_parity_pass": False,
                    },
                    "global_graph_declared": False,
                },
                "frozen_controls": {
                    "K": 100,
                    "L_search": [100, 200, 400],
                    "beamwidth": 2,
                    "threads": 6,
                    "repetitions": 3,
                },
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(search, "_run_argv", lambda argv: pytest.fail("readiness must not execute"))
    result = search.preflight(mode="readiness", config_path=config, workspace=tmp_path)
    assert result["formal_search_ready"] is False
    assert result["request_paths"] == []
    assert set(result["pending_identities"]) == {"pq", "disk_layout", "normalized_query_set", "ground_truth", "zero_cache_protocol", "fixed_cache_protocol"}
    assert result["checks"]["source"]["project_source"]["status"] == "PASS"
    assert result["checks"]["source"]["diskann_source"]["status"] == "PASS"
    assert result["checks"]["cache_projection"]["derived_primary_bytes"] == 5800000
    assert result["checks"]["resources"]["status"] == "PASS"
    assert result["checks"]["environment"]["performance_admission"] == "PENDING"
    assert set(result["pending_prerequisites"]) == {
        "global_graph_identity",
        "normative_performance_environment",
    }
    assert all(value == 0 for value in result["operations_started"].values())
    assert not (tmp_path / "protocols").exists()


def test_formal_search_fixture_preflight_emits_reviewable_requests_without_search(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    zero = _protocol("zero")
    fixed = _protocol("fixed_bfs")
    runner = tmp_path / "bin/runner"
    runner.parent.mkdir()
    runner.write_bytes(b"small-runner")
    for declaration in (zero, fixed):
        declaration["search_execution"]["runner_build"]["binary_path"] = "bin/runner"
        declaration["search_execution"]["runner_build"]["binary_sha256"] = search._sha256(runner)
    config = tmp_path / "formal.json"
    config.write_text(json.dumps({"mode": "formal-search", "fixture_mode": True, "protocols": [zero, fixed], "common_admission": {"artifacts_valid": True, "inputs_compatible": True, "runner_path": str(runner), "runner_sha256": search._sha256(runner), "abi_dependencies_usable": True, "load_probe_pass": True, "paths_confined": True, "resources_sufficient": True}, "performance_admission": {"native_linux": False, "local_ssd": False, "direct_io": False, "linux_aio": False, "parity_pass": False}}), encoding="utf-8")
    monkeypatch.setattr(search, "_run_argv", lambda argv: pytest.fail("preflight must not search"))
    result = search.preflight(mode="formal-search", config_path=config, workspace=tmp_path)
    assert result["common_search_admission"] is True
    assert result["performance_admission"] is False
    assert len(result["protocol_paths"]) == 2
    assert len(result["request_paths"]) == 2
    assert all(Path(path).is_file() for path in result["request_paths"])


@pytest.mark.skipif(
    not os.environ.get("DISKANN_SEARCH_REAL_FIXTURE_ROOT"),
    reason="small-real Linux fixture is opt-in",
)
def test_small_real_linux_ground_truth_search_evaluate_and_parity(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    retained = Path(os.environ["DISKANN_SEARCH_REAL_FIXTURE_ROOT"]).resolve()
    workspace = retained / "workspace"
    fixture = json.loads((retained / "fixture.json").read_text(encoding="utf-8"))
    repository = Path(__file__).resolve().parents[2]
    runner_source = Path(os.environ["DISKANN_SEARCH_REAL_RUNNER"]).resolve()
    oracle = Path(os.environ["DISKANN_SEARCH_REAL_ORACLE"]).resolve()
    groundtruth_binary = Path(os.environ["DISKANN_SEARCH_REAL_GROUNDTRUTH"]).resolve()
    image = os.environ["DISKANN_SEARCH_REAL_IMAGE"]
    runner = workspace / "bin/runner"
    runner.parent.mkdir(parents=True)
    shutil.copyfile(runner_source, runner)
    runner.chmod(0o755)

    dataset_metadata = json.loads(
        (workspace / "normal" / fixture["dataset"]["metadata_path"]).read_text(
            encoding="utf-8"
        )
    )
    dataset_payload = workspace / "normal/dataset/vectors.bin"
    payload = dataset_payload.read_bytes()
    rows, dimension = struct.unpack_from("<II", payload)
    values = struct.unpack_from(f"<{rows * dimension}f", payload, 8)
    csv_path = retained / "selected-query-small-real.csv"
    with csv_path.open("x", encoding="utf-8", newline="") as output:
        output.write("id," + ",".join(f"v{index}" for index in range(dimension)) + "\n")
        for row, source_id in ((0, "opaque-0042"), (1, "opaque-not-dataset-id")):
            vector = values[row * dimension : (row + 1) * dimension]
            output.write(source_id + "," + ",".join(str(value) for value in vector) + "\n")

    source = {
        "repository": "https://example.invalid/diskann-search-fixture.git",
        "branch": "fixture", "commit": "b" * 40, "dirty_worktree": False,
    }
    python_toolchain = {
        "os": "test-host", "compiler_path": "/usr/bin/python3",
        "compiler_version": "3", "cmake_version": "not-applicable",
        "generator": "python-harness", "build_type": "interpreted",
        "cmake_definitions": [], "compile_commands": [], "link_commands": [],
    }
    dataset_identity = dataset_metadata["dataset"]
    common_request = {
        "workspace_root": str(workspace), "source": source,
        "toolchain": python_toolchain, "dataset_identity": dataset_identity,
        "resource_budget": {"memory_budget_bytes": 16 * 1024 * 1024, "thread_budget": 1},
        "started_at": "2026-08-22T00:00:00Z", "ended_at": "2026-08-22T00:00:01Z",
        "input_artifacts": [fixture["dataset"]],
    }
    query_request = {
        **common_request, "run_id": "small-real-query-set",
        "source_csv": csv_path.relative_to(repository).as_posix(),
        "source_byte_length": csv_path.stat().st_size,
        "source_sha256": search._sha256(csv_path), "query_set_id": "small-real-query",
        "dimension": dimension, "query_count": 2,
        "reference_path": "inputs/small-real-query-reference.json",
        "manifest_path": "manifests/small-real-query-set.json",
        "validation_evidence_path": "manifests/evidence/small-real-query-set.json",
    }
    query_request_path = retained / "query-set-request.json"
    query_request_path.write_text(json.dumps(query_request), encoding="utf-8")
    query_result = search.materialize_query_set_request(query_request_path)
    query_reference = json.loads(
        Path(query_result["reference_path"]).read_text(encoding="utf-8")
    )

    real_run = subprocess.run

    def linux_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        linux_binaries = {runner.resolve(), oracle, groundtruth_binary}
        if Path(argv[0]).resolve() not in linux_binaries:
            return real_run(argv, **kwargs)
        command = [
            "docker", "run", "--rm", "--user", "501:20",
            "-v", f"{repository}:{repository}", "-w", str(repository), image,
            *argv,
        ]
        return real_run(
            command, check=False,
            capture_output=bool(kwargs.get("capture_output", False)),
            text=bool(kwargs.get("text", False)),
        )

    monkeypatch.setattr(search.subprocess, "run", linux_run)
    truth_request = {
        **common_request, "run_id": "small-real-ground-truth",
        "compute_groundtruth_binary": str(groundtruth_binary),
        "compute_groundtruth_sha256": search._sha256(groundtruth_binary),
        "dataset_payload": str(dataset_payload),
        "query_payload": str(workspace / query_reference["native_payload"]["path"]),
        "payload_path": "inputs/small-real-truth.bin", "query_count": 2,
        "width": 200, "ground_truth_id": "small-real-truth",
        "dataset_artifact": fixture["dataset"],
        "query_set_checksum": query_reference["canonical_checksum"],
        "reference_path": "inputs/small-real-truth-reference.json",
        "manifest_path": "manifests/small-real-ground-truth.json",
        "validation_evidence_path": "manifests/evidence/small-real-ground-truth.json",
        "stdout_path": "reports/small-real-ground-truth.stdout",
        "stderr_path": "reports/small-real-ground-truth.stderr",
    }
    truth_request_path = retained / "ground-truth-request.json"
    truth_request_path.write_text(json.dumps(truth_request), encoding="utf-8")
    truth_result = search.materialize_ground_truth_request(truth_request_path)
    truth_reference = json.loads(
        Path(truth_result["reference_path"]).read_text(encoding="utf-8")
    )

    runner_manifest_path = workspace / "manifests/small-real-runner-build.json"
    runner_manifest = {
        "manifest_version": "1.0.0", "run_id": "small-real-runner-build", "stage": "build",
        "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "implementation_version": "1.0.0"},
        "source": source,
        "toolchain": {**python_toolchain, "os": "linux", "compiler_path": "/usr/bin/c++", "compiler_version": "11.4.0", "cmake_version": "3.22.1", "generator": "Unix Makefiles", "build_type": "Release", "cmake_definitions": ["DISKANN_CPP_MAIN_BASELINE=ON", "DISKANN_SEARCH_EVALUATION=ON"], "compile_commands": ["c++ -std=c++17 -mavx2 -fopenmp"], "link_commands": ["c++ diskann_search_evaluation_runner"]},
        "dataset": dataset_identity,
        "parameters": {
            "runner_binary": {
                "path": runner.relative_to(workspace).as_posix(),
                "sha256": search._sha256(runner),
            },
            "diskann_source": {
                "repository": "https://github.com/microsoft/DiskANN.git",
                "branch": "cpp_main",
                "commit": "78256bbab4685e1774e78d331e081a153be26823",
                "dirty_worktree": False,
            },
        },
        "resource_budget": {"memory_budget_bytes": runner.stat().st_size, "thread_budget": 1},
        "input_artifacts": [], "output_artifacts": [], "status": "SUCCEEDED",
        "started_at": "2026-08-22T00:00:00Z", "ended_at": "2026-08-22T00:00:01Z",
    }
    runner_manifest_path.write_text(json.dumps(runner_manifest), encoding="utf-8")

    pq_evidence = (
        workspace / "manifests/evidence/search-pq-native-real-pq-published.json"
    )
    layout_evidence = (
        workspace
        / "manifests/evidence/search-layout-native-real-layout-published.json"
    )
    admission_evidence = {
        "pq_artifact": {
            "path": pq_evidence.relative_to(workspace).as_posix(),
            "sha256": search._sha256(pq_evidence),
            "expected_artifact": fixture["pq_artifact"],
        },
        "disk_layout_artifact": {
            "path": layout_evidence.relative_to(workspace).as_posix(),
            "sha256": search._sha256(layout_evidence),
            "expected_artifact": fixture["disk_layout_artifact"],
        },
    }
    for cache_mode in ("zero", "fixed_bfs"):
        protocol = _protocol(cache_mode)
        protocol_id = f"small-real-{cache_mode}"
        protocol["protocol_id"] = protocol_id
        protocol["search_execution"].update({
            "execution_id": protocol_id,
            "dataset_artifact": fixture["dataset"],
            "global_graph_artifact": fixture["global_graph"],
            "pq_artifact": fixture["pq_artifact"],
            "disk_layout_artifact": fixture["disk_layout_artifact"],
            "query_set": query_reference,
        })
        protocol["search_execution"]["runner_build"] = {
            "project_repository": source["repository"], "project_branch": source["branch"],
            "project_commit": source["commit"], "project_dirty": False,
            "binary_path": runner.relative_to(workspace).as_posix(),
            "binary_sha256": search._sha256(runner),
            "build_manifest_path": runner_manifest_path.relative_to(workspace).as_posix(),
            "build_manifest_sha256": search._sha256(runner_manifest_path),
        }
        protocol["search_execution"]["admission_evidence"] = admission_evidence
        protocol["evaluation"]["ground_truth"] = truth_reference
        protocol = search.normalize_search_protocol(protocol)
        bound_evidence = protocol["search_execution"]["admission_evidence"]
        assert bound_evidence["pq_artifact"] == admission_evidence["pq_artifact"]
        assert bound_evidence["disk_layout_artifact"] == admission_evidence[
            "disk_layout_artifact"
        ]
        assert protocol["search_execution_checksum"] == search._canonical_digest(
            protocol["search_execution"]
        )
        protocol_path = workspace / "protocols" / f"{protocol_id}.json"
        protocol_path.parent.mkdir(parents=True, exist_ok=True)
        protocol_path.write_text(
            json.dumps(protocol, sort_keys=True, separators=(",", ":")), encoding="utf-8"
        )
        parity = search.run_search_runner_parity(
            protocol=protocol_path, oracle=oracle, runner=runner, workspace=workspace
        )
        assert parity["result"] == "PASS"
        evaluation = search.evaluate_observation_directory(
            protocol_path=protocol_path,
            observations_path=workspace / "reports/search-observations",
            workspace=workspace,
        )
        assert len(evaluation["display"]) == 9
        assert len(evaluation["report"]["aggregates"]["quality"]) == 3
        assert evaluation["report"]["aggregates"]["performance"] == []
        search_manifest = json.loads(
            (workspace / "manifests" / f"{protocol_id}-search.json").read_text(
                encoding="utf-8"
            )
        )
        assert search_manifest["status"] == "SUCCEEDED"
        assert search_manifest["stage"] == "search"
        assert search_manifest["implementation"]["implementation_id"] == (
            "cpp_main_pqflash_search_instrumented_v1"
        )
        assert search_manifest["resource_budget"]["memory_budget_bytes"] > 1
        assert search_manifest["resource_budget"]["thread_budget"] == 6
        assert search_manifest.get("metrics", {}) == {}
        assert search_manifest["parameters"]["search_execution"] == protocol[
            "search_execution"
        ]
        observation_references = search_manifest["parameters"][
            "observation_references"
        ]
        assert len(observation_references) == 9
        assert all(
            search._sha256(workspace / reference["path"]) == reference["sha256"]
            for reference in observation_references
        )
