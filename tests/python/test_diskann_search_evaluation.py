from __future__ import annotations

import errno
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
from diskann_harness.checksum import aggregate_checksum
from diskann_harness.native_dataset import dataset_identity_projection, MEDIA
from diskann_harness.manifests import ImmutableWriteError


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


@pytest.mark.parametrize("version", ["1.0.0", "1.1.0"])
def test_materialize_cli_uses_one_cpp_runner_invocation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, version: str,
) -> None:
    request = tmp_path / "request.json"
    declaration = _pq_v11_request(tmp_path)
    declaration["implementation"]["implementation_version"] = version
    if version == "1.0.0":
        declaration["parameters"] = dict(search._PQ_PARAMETERS)
    request.write_text(json.dumps(declaration), encoding="utf-8")
    original = request.read_bytes()
    observed: list[list[str]] = []

    def fake_run(argv: list[str]) -> int:
        observed.append(argv)
        forwarded = json.loads(Path(argv[-1]).read_text(encoding="utf-8"))
        assert forwarded == search.normalize_materialization_request(declaration)
        assert forwarded["parameters"] == (
            search._PQ_PARAMETERS if version == "1.0.0" else {"B": 0.03, "QD": 32}
        )
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
    assert len(observed) == 1
    assert observed[0][:3] == [
        "/tmp/diskann_search_evaluation_runner", "materialize", "--request",
    ]
    assert request.read_bytes() == original


def _pq_v11_request(tmp_path: Path) -> dict:
    return {
        "request_id": "pq-derived", "run_id": "pq-derived",
        "workspace": {"root": str(tmp_path)}, "operation": "pq",
        "dataset": {"artifact_type": "dataset", "format_version": "diskann-float32-dataset/v1",
                    "aggregate_checksum": "a" * 64, "metadata_path": "dataset/artifact.json"},
        "implementation": {"implementation_id": "cpp_main_post_graph_pq_v1",
                           "implementation_version": "1.1.0", "reproduction_mode": "upstream_faithful"},
        "parameters": {"B": 0.03, "QD": 32},
        "resource_budget": {"memory_budget_bytes": 67108864, "thread_budget": 1},
        "candidate_path": "temporary/pq-derived/candidate/artifact.json",
    }


def _frozen_materialization_request(tmp_path: Path, operation: str) -> dict:
    request = _pq_v11_request(tmp_path)
    request["operation"] = operation
    request["implementation"]["implementation_version"] = "1.0.0"
    request["parameters"] = dict(search._PQ_PARAMETERS)
    if operation == "disk_layout":
        request["implementation"]["implementation_id"] = "cpp_main_post_graph_disk_layout_v1"
        request["parameters"] = dict(search._LAYOUT_PARAMETERS)
        request["global_graph"] = {
            **request["dataset"], "artifact_type": "global_graph",
            "format_version": "global-graph-artifact/v2",
        }
    return request


@pytest.mark.parametrize("operation", ["pq", "disk_layout"])
@pytest.mark.parametrize("entrypoint", ["normalize", "cli"])
def test_frozen_request_without_identity_is_rejected(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, operation: str, entrypoint: str,
) -> None:
    request = _frozen_materialization_request(tmp_path, operation)
    assert search.normalize_materialization_request(request) == request
    del request["implementation"]  # The only difference from the valid request.
    path = tmp_path / "request.json"
    path.write_text(json.dumps(request), encoding="utf-8")

    def forbidden(_argv: list[str]) -> int:
        pytest.fail("missing implementation reached native runner")

    monkeypatch.setattr(search, "_run_argv", forbidden)
    with pytest.raises(search.SearchEvaluationError):
        if entrypoint == "normalize":
            search.normalize_materialization_request(request)
        else:
            search.main([
                "materialize", "--operation", operation, "--request", str(path),
                "--runner", "unused",
            ])


@pytest.mark.parametrize("operation", ["pq", "disk_layout"])
@pytest.mark.parametrize("change", ["null", "incomplete", "identity", "version"])
def test_explicit_invalid_identity_does_not_use_legacy_parameters(
    tmp_path: Path, operation: str, change: str,
) -> None:
    request = _frozen_materialization_request(tmp_path, operation)
    request["parameters"] = {}
    if change == "null":
        request["implementation"] = None
    elif change == "incomplete":
        del request["implementation"]["implementation_version"]
    elif change == "identity":
        request["implementation"]["implementation_id"] = "unknown"
    else:
        request["implementation"]["implementation_version"] = "unknown"
    with pytest.raises(search.SearchEvaluationError, match="identity"):
        search.normalize_materialization_request(request)


def test_pq_v11_normalization_has_no_python_derivation(tmp_path: Path) -> None:
    request = _pq_v11_request(tmp_path)
    assert search.normalize_materialization_request(request) == request
    request["parameters"]["QD"] = 0
    assert search.normalize_materialization_request(request)["parameters"] == {"B": 0.03, "QD": 0}


@pytest.mark.parametrize("field,value", [
    ("B", 0), ("B", -1), ("B", True), ("B", "0.03"),
    ("B", float("inf")), ("B", float("nan")),
    ("QD", -1), ("QD", True), ("QD", "32"), ("QD", 32.0), ("QD", 1.5), ("QD", 513),
    ("pq_chunks", 32), ("sample_rate", 0.0256), ("opq", True),
])
def test_invalid_pq_controls_do_not_invoke_runner(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, field: str, value: object,
) -> None:
    request = _pq_v11_request(tmp_path)
    request["parameters"][field] = value
    path = tmp_path / "request.json"
    path.write_text(json.dumps(request), encoding="utf-8")
    def forbidden(_argv: list[str]) -> int:
        pytest.fail("invalid materialization reached native runner")
    monkeypatch.setattr(search, "_run_argv", forbidden)
    with pytest.raises(search.SearchEvaluationError):
        search.main(["materialize", "--operation", "pq", "--request", str(path), "--runner", "unused"])


@pytest.mark.parametrize("change", ["version", "identity", "mode", "operation", "missing_B", "missing_QD"])
def test_materialization_identity_and_required_inputs(tmp_path: Path, change: str) -> None:
    value = _pq_v11_request(tmp_path)
    if change == "version":
        value["implementation"]["implementation_version"] = "9.0.0"
    elif change == "identity":
        value["implementation"]["implementation_id"] = "other"
    elif change == "mode":
        value["implementation"]["reproduction_mode"] = "instrumented"
    elif change == "operation":
        value["operation"] = "unknown"
    else:
        value["parameters"].pop(change.removeprefix("missing_"))
    with pytest.raises(search.SearchEvaluationError):
        search.normalize_materialization_request(value)


def test_legacy_implicit_materialization_is_explicitly_frozen(tmp_path: Path) -> None:
    request = _pq_v11_request(tmp_path)
    for field in ("implementation", "request_id", "candidate_path"):
        request.pop(field)
    request["parameters"] = {}
    normalized = search.normalize_materialization_request(request)
    assert normalized["implementation"] == {
        "implementation_id": "cpp_main_post_graph_pq_v1",
        "implementation_version": "1.0.0", "reproduction_mode": "upstream_faithful",
    }
    assert normalized["parameters"] == search._PQ_PARAMETERS
    assert normalized["request_id"] == request["run_id"]
    assert normalized["candidate_path"] == f"temporary/{request['run_id']}/candidate/artifact.json"
    request["parameters"] = {"B": 0.03, "QD": 32}
    with pytest.raises(search.SearchEvaluationError):
        search.normalize_materialization_request(request)


def test_cli_operation_mismatch_never_invokes_runner(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    path = tmp_path / "request.json"
    path.write_text(json.dumps(_pq_v11_request(tmp_path)), encoding="utf-8")
    def forbidden(_argv: list[str]) -> int:
        pytest.fail("mismatched operation reached runner")
    monkeypatch.setattr(search, "_run_argv", forbidden)
    with pytest.raises(search.SearchEvaluationError, match="operation"):
        search.main(["materialize", "--operation", "disk_layout", "--request", str(path), "--runner", "unused"])


@pytest.mark.parametrize("version", [[], {}, True, None])
def test_materialization_version_must_be_string(tmp_path: Path, version: object) -> None:
    request = _pq_v11_request(tmp_path)
    request["implementation"]["implementation_version"] = version
    with pytest.raises(search.SearchEvaluationError):
        search.normalize_materialization_request(request)


@pytest.mark.parametrize("parameters", [{}, {"centroid_policy": "medoid_vector_fallback"}])
def test_legacy_layout_and_frozen_boolean_controls(tmp_path: Path, parameters: dict) -> None:
    request = _pq_v11_request(tmp_path)
    request.pop("implementation")
    request["operation"] = "disk_layout"
    request["global_graph"] = {**request["dataset"], "artifact_type": "global_graph",
                               "format_version": "global-graph-artifact/v2"}
    request["parameters"] = parameters
    normalized = search.normalize_materialization_request(request)
    assert normalized["implementation"] == {
        "implementation_id": "cpp_main_post_graph_disk_layout_v1",
        "implementation_version": "1.0.0", "reproduction_mode": "upstream_faithful",
    }
    assert normalized["parameters"] == search._LAYOUT_PARAMETERS
    for key in ("reorder", "labels"):
        changed = json.loads(json.dumps(normalized))
        changed["parameters"][key] = 0
        with pytest.raises(search.SearchEvaluationError):
            search.normalize_materialization_request(changed)
    pq = _pq_v11_request(tmp_path)
    pq["implementation"]["implementation_version"] = "1.0.0"
    pq["parameters"] = {**search._PQ_PARAMETERS, "opq": 0}
    with pytest.raises(search.SearchEvaluationError):
        search.normalize_materialization_request(pq)


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


def _offline_fixture(
    tmp_path: Path, cache_mode: str = "zero", lengths: tuple[int, ...] = (100, 200, 400),
) -> tuple[Path, Path]:
    protocol = _protocol(cache_mode)
    protocol["search_execution"]["L_search"] = list(lengths)
    dataset_metadata = tmp_path / "normal/dataset/artifact.json"
    dataset_metadata.parent.mkdir(parents=True, exist_ok=True)
    if dataset_metadata.exists():
        metadata = json.loads(dataset_metadata.read_text())
        reference = {key: metadata[key] for key in ("artifact_type", "format_version", "aggregate_checksum")}
        reference["metadata_path"] = "dataset/artifact.json"
        protocol["search_execution"]["dataset_artifact"] = reference
        protocol["evaluation"]["ground_truth"]["dataset_artifact"] = reference
    else:
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
    truth_ids = list(range(202))
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
    for repetition in range(protocol["search_execution"]["repetition_count"]):
        for l_search in lengths:
            point_id = f"{cache_mode}-L{l_search}-r{repetition}"
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
                "raw_search_point": {"point_id": point_id, "L_search": l_search, "K": 100, "beamwidth": 2, "cache": {"mode": cache_mode, "requested_nodes": protocol["search_execution"]["cache"]["requested_nodes"], "effective_nodes": 0, "primary_payload_bytes": 0}, "repetition_index": repetition, "query_count": 2, "completed_query_count": 2, "batch_wall_clock_us": 2000, "status": "SUCCEEDED"},
                "environment": {"fingerprint_path": environment_path.relative_to(tmp_path).as_posix(), "fingerprint_sha256": search._sha256(environment_path)},
                "per_query": [
                    {"query_row_index": row, "source_query_id": f"opaque-{row}", "result_ids": list(range(row * 101, row * 101 + 100)), "result_distances": [float(index) for index in range(100)], "total_us": 10.0 + row * 20, "n_ios": 2 + row * 2}
                    for row in range(2)
                ],
            }
            document["observation_checksum"] = search._canonical_digest(document)
            (observations / f"{point_id}.json").write_text(
                json.dumps(document, sort_keys=True, separators=(",", ":")), encoding="utf-8"
            )
    return protocol_path, observations


def test_offline_evaluator_reuses_complete_observations_without_search(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    protocol_path, observations = _offline_fixture(tmp_path)
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


@pytest.fixture
def offline_only(monkeypatch: pytest.MonkeyPatch):
    calls: list[str] = []
    # Resolve the host fingerprint before forbidding subprocesses: on macOS,
    # platform.platform() can lazily invoke uname in an isolated test run.
    host_platform = search.platform.platform()
    monkeypatch.setattr(search.platform, "platform", lambda: host_platform)
    monkeypatch.setattr(search, "_git_source_identity", lambda root: {
        "repository": "https://example.invalid/current-evaluator.git", "branch": "evaluator-test",
        "commit": "e" * 40, "dirty_worktree": True, "dirty_source_checksum": "f" * 64,
    })

    def forbidden(*args, **kwargs):
        calls.append("execution")
        pytest.fail("offline evaluation must not execute, search, or materialize")

    for name in ("_run_argv", "materialize_query_set_request", "materialize_ground_truth_request",
                 "run_search_runner_parity", "preflight"):
        monkeypatch.setattr(search, name, forbidden)
    monkeypatch.setattr(subprocess, "run", forbidden)
    monkeypatch.setattr(subprocess, "Popen", forbidden)
    yield calls
    assert calls == []


def _save_observation(path: Path, document: dict) -> None:
    document.pop("observation_checksum", None)
    document["observation_checksum"] = search._canonical_digest(document)
    path.write_text(json.dumps(document, sort_keys=True), encoding="utf-8")


def _v2_protocol(root: Path, original: Path) -> Path:
    document = json.loads(original.read_text())
    document["protocol_version"] = "1.1.0"
    document["protocol_id"] += "-reevaluation"
    document["evaluation"]["evaluator"] = {
        "implementation_id": "cpp_main_search_metrics_evaluator_v2",
        "version": "2.0.0", "reproduction_mode": "instrumented",
    }
    document["evaluation"]["performance_eligibility_policy"] = "complete_valid_measurement_single_invocation_v2"
    path = root / "protocols" / f"{document['protocol_id']}.json"
    path.write_text(json.dumps(search.normalize_search_protocol(document)))
    return path


def _search_manifest(root: Path, protocol_path: Path, observations: Path) -> Path:
    protocol = json.loads(protocol_path.read_text())
    execution = protocol["search_execution"]
    dataset = json.loads((root / "normal/dataset/artifact.json").read_text())["dataset"]
    documents = [(p, json.loads(p.read_text())) for p in sorted(observations.glob("*.json"))]
    run_id = documents[0][1]["run_id"]
    value = {
        "manifest_version": "1.0.0", "run_id": run_id, "stage": "search",
        "implementation": {"implementation_id": execution["implementation"]["implementation_id"], "implementation_version": "1.0.0"},
        "source": dict(search._PINNED_DISKANN_SOURCE),
        "toolchain": search._python_harness_toolchain(), "dataset": dataset,
        "parameters": {"protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"],
                       "search_execution_checksum": protocol["search_execution_checksum"], "search_execution": execution,
                       "observation_references": [{"path": p.relative_to(root).as_posix(), "sha256": search._sha256(p)} for p, _ in documents]},
        "resource_budget": {"memory_budget_bytes": 1024, "thread_budget": 6},
        "input_artifacts": [execution[k] for k in ("dataset_artifact", "global_graph_artifact", "pq_artifact", "disk_layout_artifact")],
        "output_artifacts": [], "status": "SUCCEEDED",
        "started_at": "2026-08-21T00:00:00Z", "ended_at": "2026-08-21T00:00:01Z",
    }
    path = root / "manifests" / f"{run_id}.json"
    path.parent.mkdir(exist_ok=True)
    path.write_text(json.dumps(value))
    return path


@pytest.mark.parametrize("os_name", ["Darwin", "Linux", "Linux-container"])
def test_v2_persists_measurements_and_single_invocation(tmp_path: Path, offline_only, os_name: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    environment = tmp_path / "reports/environment.json"
    environment.write_text(json.dumps({"os": os_name}))
    for path in observations.glob("*.json"):
        value = json.loads(path.read_text())
        value["environment"]["fingerprint_sha256"] = search._sha256(environment)
        _save_observation(path, value)
    manifest = _search_manifest(tmp_path, original, observations)
    protocol = _v2_protocol(tmp_path, original)
    before = {p: p.read_bytes() for p in observations.glob("*.json")}
    output = search.evaluate_observation_directory(protocol_path=protocol, observations_path=observations, workspace=tmp_path)
    report = output["report"]
    assert report["report_version"] == "search-evaluation-report/v2"
    metrics = {"qps_k100": 1000.0, "mean_latency_us_k100": 20.0, "p99_9_latency_us_k100": 30.0, "mean_ios_k100": 3.0}
    assert all(p.read_bytes() == data for p, data in before.items())
    for result, display in zip(report["observation_results"], output["display"]):
        assert result["performance_valid"] is True
        assert result["performance_metrics"] == metrics
        assert all(display[key] == val for key, val in metrics.items())
        assert "performance_eligible" not in result
        assert result["original_protocol"]["protocol_id"] == "protocol-zero"
    aggregate = report["aggregates"]["performance"][0]
    assert aggregate["median_performance_metrics"] == metrics
    assert aggregate["eligible_count"] == 3
    assert aggregate["scope"]["search_manifest"]["sha256"] == search._sha256(manifest)
    assert report["search_runner_parity"]["assessment"] == "NOT_VERIFIED"
    assert aggregate["scope"]["limitations"]


def test_v2_no_manifest_keeps_values_without_aggregation(tmp_path: Path, offline_only) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    protocol = _v2_protocol(tmp_path, original)
    report = _offline_call(tmp_path, protocol, observations, "python")
    assert all(r["performance_valid"] for r in report["observation_results"])
    assert report["aggregates"]["performance"] == []
    assert "manifest" in report["performance_aggregation_reason"]


def _parity_fixture(root: Path, original: Path, observations: Path) -> Path:
    manifest_path = _search_manifest(root, original, observations)
    template = json.loads(manifest_path.read_text())
    protocol = json.loads(original.read_text())
    execution = protocol["search_execution"]
    build = execution["runner_build"]
    build_manifest = json.loads(json.dumps(template))
    build_manifest.update(stage="build", run_id="runner-build", source={
        "repository": build["project_repository"], "branch": build["project_branch"],
        "commit": build["project_commit"], "dirty_worktree": False,
    }, parameters={"binary_sha256": build["binary_sha256"], "diskann_source": dict(search._PINNED_DISKANN_SOURCE)})
    build_path = root / build["build_manifest_path"]
    build_path.write_text(json.dumps(build_manifest))
    build["build_manifest_sha256"] = search._sha256(build_path)
    _save_protocol_and_rebind_observations(original, protocol, observations)
    for path in observations.glob("*.json"):
        d = json.loads(path.read_text())
        d["source_build"]["build_manifest_sha256"] = build["build_manifest_sha256"]
        _save_observation(path, d)
    _search_manifest(root, original, observations)
    protocol = json.loads(original.read_text())
    points = []
    for path in sorted(observations.glob("*.json")):
        d = json.loads(path.read_text()); point = d["raw_search_point"]
        l_search, repetition = point["L_search"], point["repetition_index"]
        run_id = f"{execution['execution_id']}-oracle-L{l_search}-r{repetition}"
        oracle = json.loads(json.dumps(template))
        oracle.update(run_id=run_id, implementation={"implementation_id": "cpp_main_search_disk_index_oracle_v1", "implementation_version": "1.0.0"})
        refs = []
        for suffix in ("stdout", "stderr", "ids", "distances"):
            f = root / "reports" / f"{run_id}.{suffix}"; f.write_bytes(b"offline-fixture")
            refs.append({"path": f.relative_to(root).as_posix(), "sha256": search._sha256(f), "byte_length": f.stat().st_size})
        oracle["parameters"] = {
            "protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"],
            "search_execution_checksum": protocol["search_execution_checksum"],
            "oracle_binary": {"path": "bin/oracle", "sha256": "c" * 64},
            "stdout": refs[0], "stderr": refs[1], "result_files": refs[2:],
            "argv": ["oracle", "--recall_at", "100", "--search_list", str(l_search), "--beamwidth", "2", "--num_threads", "6", "--num_nodes_to_cache", "0", "--search_io_limit", "4294967295"],
        }
        oracle_path = root / "manifests" / f"{run_id}.json"; oracle_path.write_text(json.dumps(oracle))
        points.append({"L_search": l_search, "repetition_index": repetition, "result": "PASS",
                       "ordered_ids_equal": True, "distances_within_tolerance": True, "mean_ios_equal": True,
                       "query_stats_mapping": "total_us_and_n_ios_direct", "qps_formula": "query_count/canonical_batch_wall_clock_seconds",
                       "latency_formula": "pinned_QueryStats_total_us", "timing_boundary": execution["timing_boundary"], "persistence_outside_timing": True,
                       "oracle_manifest_path": oracle_path.relative_to(root).as_posix(), "oracle_manifest_sha256": search._sha256(oracle_path),
                       "observation_path": path.relative_to(root).as_posix(), "observation_sha256": search._sha256(path)})
    evidence = {"result": "PASS", "instrumented_exit_code": 0,
                "controls": {"search_execution_checksum": protocol["search_execution_checksum"], **{k: execution[k] for k in ("K", "beamwidth", "thread_count", "cache", "io_limit", "warmup_policy")}}, "points": points}
    path = root / "reports/search-parity" / f"{execution['execution_id']}.json"
    path.parent.mkdir(exist_ok=True); path.write_text(json.dumps(evidence))
    return path


@pytest.mark.parametrize("case", ["pass", "not_reached", "staging", "oracle_process", "oracle_outputs", "semantic", "timing", "unresolved", "fail_all_pass", "fail_missing_point", "fail_no_comparison", "execution", "runner", "observation_sha", "oracle_sha", "simple_pass", "corrupt"])
def test_v2_parity_applicability(tmp_path: Path, offline_only, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    parity_path = _parity_fixture(tmp_path, original, observations)
    evidence = json.loads(parity_path.read_text())
    if case == "not_reached":
        evidence = {"result": "NOT_REACHED"}
    elif case == "staging":
        evidence = {"result": "FAIL", "instrumented_exit_code": 0, "points": [], "reason": "text is intentionally irrelevant"}
    elif case in {"oracle_process", "oracle_outputs"}:
        evidence["result"] = "FAIL"
        for point in evidence["points"]:
            point["result"] = "FAIL"
            oracle_path = tmp_path / point["oracle_manifest_path"]
            oracle = json.loads(oracle_path.read_text())
            oracle["parameters"]["result_files"] = []
            if case == "oracle_process":
                oracle.update(status="FAILED", failure={"category": "process_failed", "reason": "arbitrary", "exit_code": 2})
            oracle_path.write_text(json.dumps(oracle))
            point["oracle_manifest_sha256"] = search._sha256(oracle_path)
            for key in list(point):
                if key not in {"result", "L_search", "repetition_index", "oracle_manifest_path", "oracle_manifest_sha256"}:
                    del point[key]
    elif case in {"semantic", "timing", "unresolved"}:
        evidence["result"] = "FAIL"; evidence["points"][0]["result"] = "FAIL"
        if case != "unresolved":
            evidence["points"][0]["ordered_ids_equal" if case == "semantic" else "persistence_outside_timing"] = False
    elif case in {"fail_all_pass", "fail_missing_point", "fail_no_comparison"}:
        evidence["result"] = "FAIL"
        if case == "fail_missing_point":
            evidence["points"].pop()
        elif case == "fail_no_comparison":
            evidence["points"] = []
            evidence["instrumented_exit_code"] = 1
    elif case == "execution":
        evidence["controls"]["search_execution_checksum"] = "b" * 64
    elif case == "runner":
        build = tmp_path / "manifests/build.json"; build.write_text("{}")
    elif case == "observation_sha":
        evidence["points"][0]["observation_sha256"] = "b" * 64
    elif case == "oracle_sha":
        evidence["points"][0]["oracle_manifest_sha256"] = "b" * 64
    elif case == "simple_pass":
        evidence = {"result": "PASS"}
    parity_path.write_text("{" if case == "corrupt" else json.dumps(evidence))
    protocol = _v2_protocol(tmp_path, original)
    report = _offline_call(tmp_path, protocol, observations, "python")
    valid = [r for r in report["observation_results"] if r["performance_valid"]]
    assert len(valid) == (2 if case in {"semantic", "timing"} else 3)
    if case in {"unresolved", "fail_all_pass", "fail_missing_point", "fail_no_comparison", "execution", "runner", "observation_sha", "oracle_sha", "simple_pass", "corrupt"}:
        assert report["aggregates"]["performance"] == []
        assert "unresolved" in report["performance_aggregation_reason"]
        assert report["search_runner_parity"]["assessment"] != "PASS"
        assert any(r["parity"]["assessment"] == "UNRESOLVED" for r in valid)
    else:
        assert report["aggregates"]["performance"][0]["eligible_count"] == len(valid)
    if case in {"staging", "oracle_process", "oracle_outputs", "not_reached"}:
        assert report["search_runner_parity"]["assessment"] == "NOT_VERIFIED"
    if case == "corrupt":
        assert report["search_runner_parity"]["result"] == "INVALID_EVIDENCE"
    else:
        assert report["search_runner_parity"]["result"] == evidence["result"]


@pytest.mark.parametrize("case", ["missing", "unreadable", "json", "json_array", "schema", "identity", "references", "reference_type", "duplicate_reference", "null_reference"])
def test_v2_aux_manifest_failure_keeps_measurements(tmp_path: Path, offline_only, monkeypatch: pytest.MonkeyPatch, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    manifest_path = _search_manifest(tmp_path, original, observations)
    manifest = json.loads(manifest_path.read_text())
    if case == "missing":
        manifest_path.unlink()
    elif case == "unreadable":
        sha256 = search._sha256

        def denied(path):
            if path == manifest_path:
                raise PermissionError("fixture manifest unreadable")
            return sha256(path)

        monkeypatch.setattr(search, "_sha256", denied)
    elif case == "json":
        manifest_path.write_text("{")
    elif case == "json_array":
        manifest_path.write_text("[]")
    else:
        if case == "schema":
            del manifest["source"]
        elif case == "identity":
            manifest["parameters"]["search_execution_checksum"] = "c" * 64
        elif case == "references":
            manifest["parameters"]["observation_references"] = "invalid"
        elif case == "reference_type":
            manifest["parameters"]["observation_references"][0]["path"] = []
        elif case == "null_reference":
            manifest["parameters"]["observation_references"][0]["path"] += "\x00invalid"
        else:
            manifest["parameters"]["observation_references"].append(manifest["parameters"]["observation_references"][0])
        manifest_path.write_text(json.dumps(manifest))
    report = _offline_call(tmp_path, _v2_protocol(tmp_path, original), observations, "cli")
    assert len(report["observation_results"]) == 3
    assert all(r["performance_valid"] and r["performance_metrics"]["qps_k100"] == 1000.0 for r in report["observation_results"])
    assert all(r["quality_metrics"]["recall_at_100"] == 100.0 for r in report["observation_results"])
    assert report["aggregates"]["performance"] == []
    assert "manifest" in report["performance_aggregation_reason"]
    if case == "null_reference":
        assert "null character" in report["performance_aggregation_reason"]


@pytest.mark.parametrize("case", ["unreadable", "json_array", "integer_limit", "json_depth", "symlink_oracle", "symlink_log", "symlink_result", "invalid_result", "invalid_point", "invalid_slot", "invalid_check_type", "missing_log", "log_checksum", "result_checksum", "malformed_reference", "oracle_binding", "null_oracle", "null_log", "null_result", "null_observation"])
def test_v2_aux_parity_failure_scope(tmp_path: Path, offline_only, monkeypatch: pytest.MonkeyPatch, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100, 200))
    parity_path = _parity_fixture(tmp_path, original, observations)
    evidence = json.loads(parity_path.read_text())
    point = next(p for p in evidence["points"] if (p["L_search"], p["repetition_index"]) == (200, 1))
    oracle_path = tmp_path / point["oracle_manifest_path"]
    oracle = json.loads(oracle_path.read_text())
    unscoped = case in {"unreadable", "json_array", "integer_limit", "json_depth", "invalid_result", "invalid_point", "invalid_slot"}
    if case == "unreadable":
        sha256 = search._sha256

        def denied(path):
            if path == parity_path:
                raise PermissionError("fixture parity unreadable")
            return sha256(path)

        monkeypatch.setattr(search, "_sha256", denied)
    elif case == "json_array":
        evidence = []
    elif case == "invalid_result":
        evidence["result"] = []
    elif case == "invalid_point":
        evidence["points"].append("invalid")
    elif case == "invalid_slot":
        point["L_search"] = []
    elif case == "invalid_check_type":
        point["ordered_ids_equal"] = []
    elif case == "missing_log":
        (tmp_path / oracle["parameters"]["stdout"]["path"]).unlink()
    elif case == "log_checksum":
        (tmp_path / oracle["parameters"]["stderr"]["path"]).write_bytes(b"changed")
    elif case == "result_checksum":
        (tmp_path / oracle["parameters"]["result_files"][0]["path"]).write_bytes(b"changed")
    elif case == "null_oracle":
        point["oracle_manifest_path"] += "\x00invalid"
    elif case == "null_observation":
        point["observation_path"] += "\x00invalid"
    elif case.startswith("symlink_"):
        loop = tmp_path / "reports/auxiliary-loop"
        loop.symlink_to(loop.name)
        relative = loop.relative_to(tmp_path).as_posix()
        if case == "symlink_oracle":
            point["oracle_manifest_path"] = relative
        else:
            reference = oracle["parameters"]["stdout"] if case == "symlink_log" else oracle["parameters"]["result_files"][0]
            reference["path"] = relative
            oracle_path.write_text(json.dumps(oracle))
            point["oracle_manifest_sha256"] = search._sha256(oracle_path)
    elif case not in {"integer_limit", "json_depth"}:
        if case == "malformed_reference":
            oracle["parameters"]["stdout"] = []
        elif case == "null_log":
            oracle["parameters"]["stderr"]["path"] += "\x00invalid"
        elif case == "null_result":
            oracle["parameters"]["result_files"][0]["path"] += "\x00invalid"
        else:
            oracle["parameters"]["search_execution_checksum"] = "b" * 64
        oracle_path.write_text(json.dumps(oracle))
        point["oracle_manifest_sha256"] = search._sha256(oracle_path)
    if case == "integer_limit":
        limit = sys.get_int_max_str_digits()
        if limit == 0:
            pytest.skip("Python integer parsing limit is disabled")
        # Append a numeric literal without constructing a Python integer.
        parity_path.write_text(json.dumps(evidence)[:-1] + ', "integer_limit": ' + "9" * (limit + 1) + "}")
    elif case == "json_depth":
        # Trigger the parser limit without constructing nested Python objects.
        parity_path.write_text(json.dumps(evidence)[:-1] + ', "extra": ' + "[" * 16384 + "0" + "]" * 16384 + "}")
    else:
        parity_path.write_text(json.dumps(evidence))
    protocol = _v2_protocol(tmp_path, original)
    report = _offline_call(tmp_path, protocol, observations, "python")
    assert len(report["observation_results"]) == 6
    assert all(r["performance_valid"] and r["performance_metrics"]["qps_k100"] == 1000.0 for r in report["observation_results"])
    assert all(r["quality_metrics"]["recall_at_100"] == 100.0 for r in report["observation_results"])
    assert {g["L_search"] for g in report["aggregates"]["performance"]} == (set() if unscoped else {100})
    affected = [r for r in report["observation_results"] if r["parity"]["assessment"] == "UNRESOLVED"]
    assert affected and all(r["parity"]["reason"] for r in affected)
    if not unscoped:
        assert {(r["L_search"], r["repetition_index"]) for r in affected} == {(200, 1)}
    if case.startswith("null_"):
        assert "null character" in affected[0]["parity"]["reason"]
    if case.startswith("symlink_"):
        assert "symbolic link" in affected[0]["parity"]["reason"]
        assert loop.is_symlink() and loop.readlink() == Path(loop.name)
    if case in {"unreadable", "json_array", "integer_limit", "json_depth", "invalid_result"}:
        assert report["search_runner_parity"]["result"] == "INVALID_EVIDENCE"
    else:
        assert report["search_runner_parity"]["result"] == "PASS"
    assert report["search_runner_parity"]["assessment"] != "PASS"
    if case in {"integer_limit", "json_depth"}:
        assert report["search_runner_parity"]["assessment"] == "UNRESOLVED"
        assert "invalid Parity evidence:" in affected[0]["parity"]["reason"]
        assert ("integer string conversion" if case == "integer_limit" else "recursion") in affected[0]["parity"]["reason"]
    if case in {"integer_limit", "json_depth"} or case.startswith("symlink_"):
        protocol_id = json.loads(protocol.read_text())["protocol_id"]
        assert json.loads((tmp_path / f"reports/search-evaluation/{protocol_id}-evaluation.json").read_text()) == report


@pytest.mark.parametrize("unscoped", [False, True])
def test_v2_aux_failure_preserves_confirmed_error(tmp_path: Path, offline_only, unscoped: bool) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100, 200))
    parity_path = _parity_fixture(tmp_path, original, observations)
    evidence = json.loads(parity_path.read_text())
    evidence["result"] = "FAIL"
    wrong = evidence["points"][0]
    wrong.update(result="FAIL", ordered_ids_equal=False)
    if unscoped:
        evidence["points"].append({"L_search": 9999, "repetition_index": 0})
    else:
        later = next(p for p in evidence["points"] if (p["L_search"], p["repetition_index"]) == (200, 1))
        oracle = json.loads((tmp_path / later["oracle_manifest_path"]).read_text())
        (tmp_path / oracle["parameters"]["stderr"]["path"]).unlink()
    parity_path.write_text(json.dumps(evidence))
    report = _offline_call(tmp_path, _v2_protocol(tmp_path, original), observations, "python")
    excluded = [r for r in report["observation_results"] if not r["performance_valid"]]
    assert len(excluded) == 1
    assert excluded[0]["parity"]["assessment"] == "ERROR"
    assert excluded[0]["quality_eligible"] is False
    assert "performance_metrics" not in excluded[0]
    assert {g["L_search"] for g in report["aggregates"]["performance"]} == (set() if unscoped else {100})
    if not unscoped:
        assert report["aggregates"]["performance"][0]["eligible_count"] == 2


@pytest.mark.parametrize("case", ["checksum", "query", "runner", "timing"])
def test_v2_aux_failure_does_not_hide_observation_errors(tmp_path: Path, offline_only, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    path = observations / "zero-L100-r0.json"
    d = json.loads(path.read_text())
    if case == "checksum":
        d["observation_checksum"] = "b" * 64
        path.write_text(json.dumps(d))
    else:
        if case == "query": d["per_query"][0]["query_row_index"] = 1
        elif case == "runner": d["source_build"]["binary_sha256"] = "b" * 64
        else: d["raw_search_point"]["batch_wall_clock_us"] = 0
        _save_observation(path, d)
    _search_manifest(tmp_path, original, observations)
    parity = tmp_path / "reports/search-parity/small-zero.json"
    parity.parent.mkdir(); parity.write_text("{")
    protocol = _v2_protocol(tmp_path, original)
    if case == "checksum":
        with pytest.raises(search.SearchEvaluationError, match="observation checksum"):
            _offline_call(tmp_path, protocol, observations, "python")
        _assert_evaluation_failure(tmp_path, protocol, observations, "observation checksum")
    else:
        report = _offline_call(tmp_path, protocol, observations, "python")
        assert len([r for r in report["observation_results"] if r["performance_valid"]]) == 2
        excluded = next(r for r in report["observation_results"] if not r["performance_valid"])
        assert not excluded["quality_eligible"] and "performance_metrics" not in excluded
        assert report["aggregates"]["performance"] == []


@pytest.mark.parametrize("label", ["search manifest", "Parity evidence"])
def test_v2_aux_boundary_does_not_swallow_unexpected_error(tmp_path: Path, offline_only, monkeypatch: pytest.MonkeyPatch, label: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    _parity_fixture(tmp_path, original, observations)
    bound_json = search._bound_json

    def broken(root, reference, actual_label, **kwargs):
        if actual_label == label:
            raise RuntimeError("unexpected implementation error")
        return bound_json(root, reference, actual_label, **kwargs)

    monkeypatch.setattr(search, "_bound_json", broken)
    protocol = _v2_protocol(tmp_path, original)
    with pytest.raises(RuntimeError, match="unexpected implementation error"):
        _offline_call(tmp_path, protocol, observations, "python")
    _assert_evaluation_failure(tmp_path, protocol, observations, "unexpected implementation error")


@pytest.mark.parametrize("label", ["search manifest", "runner build manifest", "oracle manifest"])
@pytest.mark.parametrize("location", ["factory", "validate"])
@pytest.mark.parametrize("error_type", [RuntimeError, ValueError, RecursionError])
def test_v2_aux_wrapped_validator_error_fails_evaluation(
    tmp_path: Path, offline_only, monkeypatch: pytest.MonkeyPatch,
    label: str, location: str, error_type: type[Exception],
) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    _parity_fixture(tmp_path, original, observations)
    protocol = _v2_protocol(tmp_path, original)
    bound_json, schema_validator = search._bound_json, search._schema_validator
    active_label = None
    injected = error_type("unexpected auxiliary validator error")
    injected_count = 0

    def tracking_bound_json(root, reference, actual_label, **kwargs):
        nonlocal active_label
        previous, active_label = active_label, actual_label
        try:
            # Keep the real reader and its exception wrapping in the call chain.
            return bound_json(root, reference, actual_label, **kwargs)
        finally:
            active_label = previous

    class BrokenValidator:
        def validate(self, value):
            nonlocal injected_count
            injected_count += 1
            raise injected

    def broken_schema_validator(*args, **kwargs):
        nonlocal injected_count
        if active_label == label:
            if location == "factory":
                injected_count += 1
                raise injected
            return BrokenValidator()
        return schema_validator(*args, **kwargs)

    monkeypatch.setattr(search, "_bound_json", tracking_bound_json)
    monkeypatch.setattr(search, "_schema_validator", broken_schema_validator)
    with pytest.raises((RuntimeError, ValueError)) as caught:
        _offline_call(tmp_path, protocol, observations, "python")
    assert injected_count == 1
    assert caught.value is injected or caught.value.__cause__ is injected
    _assert_evaluation_failure(tmp_path, protocol, observations, str(injected))


@pytest.mark.parametrize("label", ["oracle manifest", "oracle stdout", "oracle result"])
def test_v2_aux_path_internal_runtime_error_fails_evaluation(
    tmp_path: Path, offline_only, monkeypatch: pytest.MonkeyPatch, label: str,
) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    parity_path = _parity_fixture(tmp_path, original, observations)
    point = json.loads(parity_path.read_text())["points"][0]
    oracle_path = tmp_path / point["oracle_manifest_path"]
    oracle = json.loads(oracle_path.read_text())
    target = {
        "oracle manifest": oracle_path,
        "oracle stdout": tmp_path / oracle["parameters"]["stdout"]["path"],
        "oracle result": tmp_path / oracle["parameters"]["result_files"][0]["path"],
    }[label]
    protocol = _v2_protocol(tmp_path, original)
    resolve = Path.resolve
    injected = RuntimeError("unexpected auxiliary path resolution error")
    injected_count = 0

    def broken_resolve(path, *args, **kwargs):
        nonlocal injected_count
        if path == target:
            injected_count += 1
            raise injected
        return resolve(path, *args, **kwargs)

    monkeypatch.setattr(Path, "resolve", broken_resolve)
    with pytest.raises(RuntimeError) as caught:
        _offline_call(tmp_path, protocol, observations, "python")
    assert injected_count == 1
    assert caught.value is injected and injected.__context__ is None
    _assert_evaluation_failure(tmp_path, protocol, observations, str(injected))


@pytest.mark.parametrize("case", ["same_run_missing_member", "different_invocations", "environment_conflict", "different_environment_refs", "invalid_row", "invalid_runner"])
def test_v2_scope_and_existing_exclusions(tmp_path: Path, offline_only, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    path = observations / "zero-L100-r2.json"
    d = json.loads(path.read_text())
    if case == "different_invocations":
        d["run_id"] = "another-invocation"
    elif case in {"environment_conflict", "different_environment_refs"}:
        env = tmp_path / "reports/another-environment.json"
        env.write_text(json.dumps({"os": "Darwin"} if case == "environment_conflict" else {"performance_admitted": False}))
        if case == "environment_conflict":
            original_env = tmp_path / "reports/environment.json"; original_env.write_text('{"os":"Linux"}')
            for other in observations.glob("*.json"):
                doc = json.loads(other.read_text()); doc["environment"]["fingerprint_sha256"] = search._sha256(original_env); _save_observation(other, doc)
        d["environment"] = {"fingerprint_path": env.relative_to(tmp_path).as_posix(), "fingerprint_sha256": search._sha256(env)}
    elif case == "invalid_row":
        d["per_query"][0]["query_row_index"] = 10
    elif case == "invalid_runner":
        d["source_build"]["binary_sha256"] = "c" * 64
    _save_observation(path, d)
    manifest_path = _search_manifest(tmp_path, original, observations)
    if case == "same_run_missing_member":
        m = json.loads(manifest_path.read_text()); m["parameters"]["observation_references"].pop(); manifest_path.write_text(json.dumps(m))
    protocol = _v2_protocol(tmp_path, original)
    report = _offline_call(tmp_path, protocol, observations, "python")
    if case in {"same_run_missing_member", "different_invocations", "environment_conflict"}:
        assert len([r for r in report["observation_results"] if r["performance_valid"]]) == 3
        assert report["aggregates"]["performance"] == []
        assert report["performance_aggregation_reason"]
    else:
        assert report["aggregates"]["performance"][0]["eligible_count"] == (3 if case == "different_environment_refs" else 2)


def test_v2_provenance_and_immutable_outputs(tmp_path: Path, offline_only) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    protocol = _v2_protocol(tmp_path, original)
    output = search.evaluate_observation_directory(protocol_path=protocol, observations_path=observations, workspace=tmp_path)
    report = output["report"]; manifest = json.loads(Path(output["build_manifest_path"]).read_text())
    provenance = report["evaluator_provenance"]
    assert provenance == manifest["parameters"]["evaluator_provenance"]
    assert provenance["source"] == manifest["source"] == search._git_source_identity(tmp_path)
    assert manifest["source"]["commit"] != manifest["parameters"]["search_runner_build"]["project_commit"]
    assert manifest["source"]["dirty_worktree"] is True
    for ref in provenance["source_files"]:
        assert search._sha256(search._repository_root() / ref["path"]) == ref["sha256"]
    before = {p: p.read_bytes() for p in tmp_path.rglob("*") if p.is_file()}
    with pytest.raises(ImmutableWriteError):
        search.evaluate_observation_directory(protocol_path=protocol, observations_path=observations, workspace=tmp_path)
    assert all(p.read_bytes() == b for p, b in before.items())


@pytest.mark.parametrize("case", ["truth", "incompatible_truth", "execution", "policy", "evaluator", "revision"])
def test_v2_reevaluation_and_version_binding(tmp_path: Path, offline_only, case: str) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    protocol_path = _v2_protocol(tmp_path, original)
    protocol = json.loads(protocol_path.read_text())
    if case in {"policy", "evaluator", "revision"}:
        if case == "policy": protocol["evaluation"]["performance_eligibility_policy"] = "complete_valid_normative_environment_timing_parity_v1"
        elif case == "evaluator": protocol["evaluation"]["evaluator"]["version"] = "1.0.0"
        else: protocol["protocol_version"] = "1.0.0"
        with pytest.raises(search.SearchEvaluationError): search.normalize_search_protocol(protocol)
        return
    if case == "execution":
        protocol["search_execution"]["runner_build"]["binary_sha256"] = "c" * 64
    else:
        truth = protocol["evaluation"]["ground_truth"]
        original_truth = tmp_path / truth["payload"]["path"]
        new_truth = tmp_path / "inputs/new-truth.bin"; new_truth.write_bytes(original_truth.read_bytes())
        truth["payload"]["path"] = "inputs/new-truth.bin"; truth["ground_truth_id"] = "new-truth"
        if case == "incompatible_truth": truth["query_set_checksum"] = "c" * 64
    protocol_path.write_text(json.dumps(search.normalize_search_protocol(protocol)))
    if case in {"execution", "incompatible_truth"}:
        with pytest.raises(search.SearchEvaluationError): _offline_call(tmp_path, protocol_path, observations, "python")
    else:
        report = _offline_call(tmp_path, protocol_path, observations, "python")
        assert all(r["quality_metrics"]["recall_at_100"] == 100 for r in report["observation_results"])
        assert report["search_execution"]["canonical_checksum"] == json.loads(original.read_text())["search_execution_checksum"]


@pytest.mark.parametrize("eligible_count,expected", [(1, 1000.0), (2, 750.0), (3, 500.0)])
def test_v2_preserves_nonconstant_medians(tmp_path: Path, offline_only, eligible_count: int, expected: float) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    for path in observations.glob("*.json"):
        d = json.loads(path.read_text()); r = d["raw_search_point"]["repetition_index"]
        d["raw_search_point"]["batch_wall_clock_us"] *= 2 ** r
        if r >= eligible_count: d["per_query"][0]["query_row_index"] = 99
        _save_observation(path, d)
    _search_manifest(tmp_path, original, observations)
    report = _offline_call(tmp_path, _v2_protocol(tmp_path, original), observations, "python")
    aggregate = report["aggregates"]["performance"][0]
    assert aggregate["eligible_count"] == eligible_count
    assert aggregate["median_performance_metrics"]["qps_k100"] == expected
    assert aggregate["median_performance_metrics"]["mean_latency_us_k100"] == 20.0
    assert len(report["observation_results"]) == 3


def test_v2_parity_remains_execution_bound_on_new_ground_truth(tmp_path: Path, offline_only) -> None:
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    _parity_fixture(tmp_path, original, observations)
    protocol_path = _v2_protocol(tmp_path, original)
    d = json.loads(protocol_path.read_text())
    truth = d["evaluation"]["ground_truth"]
    new_truth = tmp_path / "inputs/new-truth.bin"
    new_truth.write_bytes((tmp_path / truth["payload"]["path"]).read_bytes())
    truth["payload"]["path"] = "inputs/new-truth.bin"; truth["ground_truth_id"] = "other-compatible-truth"
    protocol_path.write_text(json.dumps(search.normalize_search_protocol(d)))
    report = _offline_call(tmp_path, protocol_path, observations, "cli")
    assert report["search_runner_parity"]["assessment"] == "PASS"
    assert report["search_runner_parity"]["recall_scope"] == "original_ground_truth_only"
    assert report["ground_truth"]["ground_truth_id"] == "other-compatible-truth"


def _save_protocol_and_rebind_observations(path: Path, protocol: dict, observations: Path) -> None:
    protocol = search.normalize_search_protocol(protocol)
    path.write_text(json.dumps(protocol), encoding="utf-8")
    for observation_path in observations.glob("*.json"):
        document = json.loads(observation_path.read_text())
        document["protocol"]["protocol_checksum"] = protocol["protocol_checksum"]
        document["search_execution"]["canonical_checksum"] = protocol["search_execution_checksum"]
        _save_observation(observation_path, document)


def _offline_call(root: Path, protocol: Path, observations: Path, entry: str) -> dict:
    # Check all existing input/report bytes even when evaluation raises.
    before = {path: path.read_bytes() for path in root.rglob("*") if path.is_file()}
    try:
        if entry == "cli":
            assert search.main(["evaluate", "--protocol", str(protocol),
                                "--observations", str(observations), "--workspace", str(root)]) == 0
            protocol_id = json.loads(protocol.read_text())["protocol_id"]
            return json.loads((root / f"reports/search-evaluation/{protocol_id}-evaluation.json").read_text())
        return search.evaluate_observation_directory(
            protocol_path=protocol, observations_path=observations, workspace=root
        )["report"]
    finally:
        assert all(path.read_bytes() == content for path, content in before.items())


def _assert_evaluation_failure(root: Path, protocol: Path, observations: Path, reason: str) -> None:
    protocol_id = json.loads(protocol.read_text())["protocol_id"]
    assert not (root / f"reports/search-evaluation/{protocol_id}-evaluation.json").exists()
    manifest = json.loads((root / f"manifests/{protocol_id}-evaluation.json").read_text())
    assert manifest["stage"] == "evaluate"
    assert manifest["status"] == "FAILED"
    assert manifest["failure"]["category"] == "evaluation_failed"
    assert reason in manifest["failure"]["reason"]
    assert "output_reference" not in manifest["parameters"]
    assert manifest["parameters"]["observation_paths"] == sorted(
        path.relative_to(root).as_posix() for path in observations.glob("*.json")
    )


def _assert_audit(result: dict, path: Path, root: Path) -> None:
    document = json.loads(path.read_text())
    assert result["path"] == path.relative_to(root).as_posix()
    assert result["sha256"] == search._sha256(path)
    assert result["observation_id"] == document["observation_id"]
    assert result["terminal_status"] == document["raw_search_point"]["status"]
    assert result["repetition_index"] == document["raw_search_point"]["repetition_index"]
    assert result["L_search"] == document["raw_search_point"]["L_search"]
    assert result["quality_eligible"] is result["performance_eligible"] is False
    assert result["quality_eligibility_reason"] and result["performance_eligibility_reason"]
    assert "quality_metrics" not in result and "performance_metrics" not in result


@pytest.mark.parametrize("performance_admitted", [False, True])
@pytest.mark.parametrize("case,reason", [
    ("duplicate_row", "query_row_index"), ("missing_index", "query_row_index"),
    ("out_of_range_row", "query_row_index"), ("reordered_rows", "query_row_index"),
    ("declared_count", "query_count"), ("missing_row", "incomplete raw observation"),
    ("extra_row", "incomplete raw observation"), ("coherent_wrong_count", "query_count"),
    ("binary_sha256", "binary_sha256"), ("build_manifest_sha256", "build_manifest_sha256"),
])
def test_offline_admission_query_and_build_binding(
    tmp_path: Path, offline_only, case: str, reason: str, performance_admitted: bool,
) -> None:
    protocol, observations = _offline_fixture(tmp_path, lengths=(100,))
    if performance_admitted:
        environment = tmp_path / "reports/environment.json"
        environment.write_text('{"performance_admitted":true}')
        parity = tmp_path / "reports/search-parity/small-zero.json"
        parity.parent.mkdir()
        parity.write_text('{"result":"PASS"}')
        for observation_path in observations.glob("*.json"):
            document = json.loads(observation_path.read_text())
            document["environment"]["fingerprint_sha256"] = search._sha256(environment)
            _save_observation(observation_path, document)
    path = observations / "zero-L100-r2.json"
    document = json.loads(path.read_text())
    point, queries = document["raw_search_point"], document["per_query"]
    assert queries[0]["result_ids"] != queries[1]["result_ids"]
    if case == "duplicate_row":
        queries[1]["query_row_index"] = 0
    elif case == "missing_index":
        del queries[1]["query_row_index"]
    elif case == "out_of_range_row":
        queries[1]["query_row_index"] = 2
    elif case == "reordered_rows":
        queries.reverse()
    elif case == "declared_count":
        point["query_count"] = 3
    elif case == "missing_row":
        queries.pop()
    elif case == "extra_row":
        queries.append(dict(queries[1], query_row_index=2))
    elif case == "coherent_wrong_count":
        queries.pop()
        point.update(query_count=1, completed_query_count=1)
    else:
        document["source_build"][case] = "b" * 64
    _save_observation(path, document)
    report = _offline_call(tmp_path, protocol, observations, "python")
    assert len(report["observation_results"]) == 3
    excluded = next(item for item in report["observation_results"] if item["observation_id"] == document["observation_id"])
    _assert_audit(excluded, path, tmp_path)
    assert reason in excluded["quality_eligibility_reason"]
    assert excluded["performance_eligibility_reason"] == excluded["quality_eligibility_reason"]
    assert report["aggregates"]["quality"] == [{
        "cache_mode": "zero", "L_search": 100, "eligible_count": 2,
        "aggregation": "median_eligible",
        "median_quality_metrics": {"recall_at_10": 100.0, "recall_at_100": 100.0},
    }]
    if performance_admitted:
        assert report["aggregates"]["performance"] == [{
            "cache_mode": "zero", "L_search": 100, "eligible_count": 2,
            "aggregation": "median_eligible",
            "median_performance_metrics": {
                "qps_k100": 1000.0, "mean_latency_us_k100": 20.0,
                "p99_9_latency_us_k100": 30.0, "mean_ios_k100": 3.0,
            },
        }]
    else:
        assert report["aggregates"]["performance"] == []
    if case in {"duplicate_row", "missing_index", "out_of_range_row", "reordered_rows"}:
        truth, distances = search._load_evaluation_ground_truth(tmp_path, json.loads(protocol.read_text()))
        raw = search.evaluate_raw_point(
            {**point, "per_query": queries}, ground_truth=truth,
            ground_truth_distances=distances, quality_compatible=True,
            performance_admitted=performance_admitted,
        )
        assert raw["quality_eligible"] is raw["performance_eligible"] is False
        assert "quality_metrics" not in raw and "display_metrics" not in raw


@pytest.mark.parametrize("case", ["declared_count", "query_set_count", "actual_count", "truncated_rows", "extra_rows"])
def test_offline_ground_truth_query_count_blocks_evaluation(
    tmp_path: Path, offline_only, case: str,
) -> None:
    protocol_path, observations = _offline_fixture(tmp_path, lengths=(100,))
    protocol = json.loads(protocol_path.read_text())
    truth = protocol["evaluation"]["ground_truth"]
    truth_path = tmp_path / truth["payload"]["path"]
    payload = truth_path.read_bytes()
    if case == "declared_count":
        truth["query_count"] = 3
        truth_path.write_bytes(
            struct.pack("<II", 3, 101)
            + struct.pack("<303I", *(list(range(101)) * 3))
            + struct.pack("<303f", *([float(index) for index in range(101)] * 3))
        )
    elif case == "query_set_count":
        protocol["search_execution"]["query_set"]["query_count"] = 3
    elif case == "actual_count":
        truth_path.write_bytes(struct.pack("<II", 1, 101) + payload[8:412] + payload[816:1220])
    elif case == "truncated_rows":
        truth_path.write_bytes(payload[:-404])
    else:
        truth_path.write_bytes(payload + bytes(808))
    truth["payload"].update(byte_length=truth_path.stat().st_size, sha256=search._sha256(truth_path))
    _save_protocol_and_rebind_observations(protocol_path, protocol, observations)
    with pytest.raises(search.SearchEvaluationError, match="GroundTruth"):
        _offline_call(tmp_path, protocol_path, observations, "python")
    _assert_evaluation_failure(tmp_path, protocol_path, observations, "GroundTruth")
    assert all(json.loads(path.read_text())["raw_search_point"]["status"] == "SUCCEEDED"
               for path in observations.glob("*.json"))


@pytest.mark.parametrize("entry", ["python", "cli"])
@pytest.mark.parametrize("cache_mode,lengths", [("zero", (100,)), ("fixed_bfs", (200, 100))])
def test_offline_admission_complete_parameters_and_environment_copies(
    tmp_path: Path, offline_only, entry: str, cache_mode: str, lengths: tuple[int, ...],
) -> None:
    protocol, observations = _offline_fixture(tmp_path, cache_mode, lengths)
    source = tmp_path / "reports/environment.json"
    for index, path in enumerate(sorted(observations.glob("*.json"))):
        copied = tmp_path / f"reports/environment-{index}.json"
        copied.write_bytes(source.read_bytes())
        document = json.loads(path.read_text())
        document["environment"]["fingerprint_path"] = copied.relative_to(tmp_path).as_posix()
        _save_observation(path, document)
    report = _offline_call(tmp_path, protocol, observations, entry)
    assert len(report["observation_results"]) == 3 * len(lengths)
    assert {item["L_search"] for item in report["aggregates"]["quality"]} == set(lengths)
    assert all(item["eligible_count"] == 3 for item in report["aggregates"]["quality"])
    assert report["aggregates"]["performance"] == []
    assert all(item["median_quality_metrics"] == {"recall_at_10": 100.0, "recall_at_100": 100.0}
               for item in report["aggregates"]["quality"])
    evidence = report["environment_fingerprint"]
    assert evidence["sha256"] == search._sha256(tmp_path / evidence["path"])
    assert evidence["sha256"] == search._sha256(source)


@pytest.mark.parametrize("entry", ["python", "cli"])
@pytest.mark.parametrize("excluded", ["FAILED", "INVALID_OUTPUT", "CANCELLED", "negative_time", "incomplete"])
def test_offline_admission_complete_but_excluded_record_needs_no_environment(
    tmp_path: Path, offline_only, entry: str, excluded: str,
) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    path = observations / "zero-L100-r2.json"
    document = json.loads(path.read_text())
    if excluded == "negative_time":
        document["per_query"][0]["total_us"] = -1
    elif excluded == "incomplete":
        document["raw_search_point"]["completed_query_count"] = 1
    else:
        document["raw_search_point"]["status"] = excluded
        document["failure"] = {"category": "test_failure", "reason": "retained attempt"}
        document["per_query"] = []
    document["environment"]["fingerprint_path"] = "reports/unavailable-environment.json"
    _save_observation(path, document)
    report = _offline_call(tmp_path, protocol, observations, entry)
    assert len(report["observation_results"]) == 9
    _assert_audit(next(item for item in report["observation_results"] if item["observation_id"] == document["observation_id"]), path, tmp_path)
    assert next(item for item in report["aggregates"]["quality"] if item["L_search"] == 100)["eligible_count"] == 2
    assert report["environment_fingerprint"]["path"] == "reports/environment.json"


@pytest.mark.parametrize("entry", ["python", "cli"])
@pytest.mark.parametrize("case,reason", [
    ("copy", "duplicate logical trial"), ("copy_changed_ids", "duplicate logical trial"),
    ("copy_excluded", "duplicate logical trial"), ("duplicate_number", "duplicate logical trial"),
    ("missing_number", "missing logical trials"), ("missing_group", "missing logical trials"),
    ("negative_number", "repetition_index"), ("out_of_range", "repetition_index"),
    ("undeclared_l", "undeclared"), ("undeclared_cache", "undeclared"),
    ("missing_identity", "identity"), ("missing_protocol", "identity"),
    ("contradictory_identity", "identity"), ("observation_checksum", "observation checksum"),
    ("environment_content", "environment checksum"), ("environment_claim", "environment checksum"),
    ("same_path_different_claim", "environment checksum"),
    ("environment_missing", "environment"), ("environment_unreadable", "environment"),
    ("environment_json", "environment"),
])
def test_offline_admission_rejects_with_failure_evidence(
    tmp_path: Path, offline_only, entry: str, case: str, reason: str,
) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    path = observations / "zero-L100-r2.json"
    document = json.loads(path.read_text())
    if case.startswith("copy"):
        if case == "copy_excluded":
            document["per_query"][0]["total_us"] = -1
            _save_observation(path, document)
        else:
            # Two eligible repetitions plus a retained failure: copying a good
            # trial can silently restore eligible_count=3 under the old code.
            document["raw_search_point"]["status"] = "FAILED"
            document["failure"] = {"category": "test_failure", "reason": "retained attempt"}
            document["per_query"] = []
            _save_observation(path, document)
            path = observations / "zero-L100-r0.json"
            document = json.loads(path.read_text())
        duplicate = observations / "renamed-copy.json"
        duplicate.write_bytes(path.read_bytes())
        if case == "copy_changed_ids":
            document["observation_id"] = "new-observation-id"
            document["run_id"] = "new-run-id"
            document["raw_search_point"]["point_id"] = "new-point-id"
            _save_observation(duplicate, document)
    elif case == "missing_number":
        path.unlink()
    elif case == "missing_group":
        for removed in observations.glob("zero-L400-*.json"):
            removed.unlink()
    elif case.startswith("environment_"):
        environment = tmp_path / "reports/environment.json"
        if case == "environment_content":
            environment.write_text('{"performance_admitted":false,"changed":true}')
        elif case == "environment_missing":
            environment.unlink()
        elif case == "environment_unreadable":
            environment.unlink()
            environment.mkdir()
        elif case == "environment_json":
            environment.write_bytes(b"not-json")
        for target in observations.glob("*.json"):
            value = json.loads(target.read_text())
            if case == "environment_claim":
                value["environment"]["fingerprint_sha256"] = "0" * 64
            elif case == "environment_json":
                value["environment"]["fingerprint_sha256"] = search._sha256(environment)
            _save_observation(target, value)
    else:
        point = document["raw_search_point"]
        if case == "duplicate_number":
            point["repetition_index"] = 1
        elif case == "negative_number":
            point["repetition_index"] = -1
        elif case == "out_of_range":
            point["repetition_index"] = 3
        elif case == "undeclared_l":
            point["L_search"] = 300
        elif case == "undeclared_cache":
            point["cache"].update(mode="fixed_bfs", requested_nodes=10000)
        elif case == "missing_identity":
            del point["repetition_index"]
        elif case == "missing_protocol":
            del document["protocol"]["protocol_id"]
        elif case == "contradictory_identity":
            document["search_execution"]["execution_id"] = "wrong-execution"
        elif case == "same_path_different_claim":
            document["environment"]["fingerprint_sha256"] = "0" * 64
        _save_observation(path, document)
        if case == "observation_checksum":
            document["observation_checksum"] = "0" * 64
            path.write_text(json.dumps(document))
    with pytest.raises(search.SearchEvaluationError, match=reason):
        _offline_call(tmp_path, protocol, observations, entry)
    _assert_evaluation_failure(tmp_path, protocol, observations, reason)


@pytest.mark.parametrize("all_excluded", [False, True])
def test_offline_admission_zero_eligible_group_and_batch(tmp_path: Path, offline_only, all_excluded: bool) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    for path in observations.glob("*.json" if all_excluded else "zero-L100-*.json"):
        document = json.loads(path.read_text())
        document["per_query"][0]["total_us"] = -1
        _save_observation(path, document)
    if all_excluded:
        with pytest.raises(search.SearchEvaluationError, match="no complete valid observation"):
            _offline_call(tmp_path, protocol, observations, "python")
        _assert_evaluation_failure(tmp_path, protocol, observations, "no complete valid observation")
    else:
        report = _offline_call(tmp_path, protocol, observations, "python")
        assert len(report["observation_results"]) == 9
        assert {item["L_search"] for item in report["aggregates"]["quality"]} == {200, 400}
        for item in report["observation_results"]:
            if item["L_search"] == 100:
                _assert_audit(item, tmp_path / item["path"], tmp_path)


@pytest.mark.parametrize("eligible_count,median", [(1, 100.0), (2, 50.0), (3, 0.0)])
def test_offline_admission_preserves_eligible_medians(
    tmp_path: Path, offline_only, eligible_count: int, median: float,
) -> None:
    protocol, observations = _offline_fixture(tmp_path, lengths=(100,))
    for repetition in range(3):
        path = observations / f"zero-L100-r{repetition}.json"
        document = json.loads(path.read_text())
        if repetition >= eligible_count:
            document["raw_search_point"]["status"] = "CANCELLED"
            document["failure"] = {"category": "cancelled", "reason": "retained attempt"}
            document["per_query"] = []
        elif repetition != 0:
            for query in document["per_query"]:
                query["result_ids"] = list(range(101, 201)) if query["query_row_index"] == 0 else list(range(100))
        _save_observation(path, document)
    report = _offline_call(tmp_path, protocol, observations, "python")
    assert len(report["observation_results"]) == 3
    assert report["aggregates"]["quality"] == [{
        "cache_mode": "zero", "L_search": 100, "eligible_count": eligible_count,
        "aggregation": "median_eligible",
        "median_quality_metrics": {"recall_at_10": median, "recall_at_100": median},
    }]
    for item in report["observation_results"]:
        if not item["quality_eligible"]:
            _assert_audit(item, tmp_path / item["path"], tmp_path)


@pytest.mark.parametrize("replace_current", [False, True])
def test_offline_admission_foreign_protocol_is_audit_only(tmp_path: Path, offline_only, replace_current: bool) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    path = observations / "zero-L100-r2.json"
    document = json.loads(path.read_text())
    document["protocol"].update(protocol_id="foreign-protocol", protocol_checksum="b" * 64)
    document["search_execution"].update(execution_id="foreign-execution", canonical_checksum="c" * 64)
    document["observation_id"] = "foreign-observation"
    document["environment"]["fingerprint_path"] = "reports/unavailable-environment.json"
    if not replace_current:
        path = observations / "foreign.json"
    _save_observation(path, document)
    if replace_current:
        with pytest.raises(search.SearchEvaluationError, match="missing logical trials"):
            _offline_call(tmp_path, protocol, observations, "cli")
        _assert_evaluation_failure(tmp_path, protocol, observations, "missing logical trials")
    else:
        report = _offline_call(tmp_path, protocol, observations, "cli")
        assert len(report["observation_results"]) == 10
        assert all(item["eligible_count"] == 3 for item in report["aggregates"]["quality"])
        _assert_audit(next(item for item in report["observation_results"] if item["observation_id"] == "foreign-observation"), path, tmp_path)


@pytest.mark.parametrize("case,reason", [("truth", "GroundTruth checksum"), ("empty", "no SearchObservation"), ("parity", "PASS")])
def test_offline_admission_existing_rejections(tmp_path: Path, offline_only, case: str, reason: str) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    if case == "truth":
        (tmp_path / "inputs/truth.bin").write_bytes(b"corrupted")
    elif case == "empty":
        for path in observations.glob("*.json"):
            path.unlink()
    else:
        environment = tmp_path / "reports/environment.json"
        environment.write_text('{"performance_admitted":true}')
        for path in observations.glob("*.json"):
            document = json.loads(path.read_text())
            document["environment"]["fingerprint_sha256"] = search._sha256(environment)
            _save_observation(path, document)
    with pytest.raises(search.SearchEvaluationError, match=reason):
        _offline_call(tmp_path, protocol, observations, "cli")
    _assert_evaluation_failure(tmp_path, protocol, observations, reason)


def test_offline_admission_preserves_immutable_output(tmp_path: Path, offline_only) -> None:
    protocol, observations = _offline_fixture(tmp_path)
    _offline_call(tmp_path, protocol, observations, "python")
    with pytest.raises(ImmutableWriteError, match="immutable target already exists"):
        _offline_call(tmp_path, protocol, observations, "cli")
    manifest = json.loads((tmp_path / "manifests/protocol-zero-evaluation.json").read_text())
    assert manifest["status"] == "SUCCEEDED"


def test_parity_cli_uses_shell_free_oracle_and_runner_argv(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    protocol = tmp_path / "protocol.json"
    protocol.write_text("{}", encoding="utf-8")
    observed: list[list[str]] = []
    monkeypatch.setattr(search, "run_search_runner_parity", lambda **kwargs: observed.append([str(kwargs["oracle"]), str(kwargs["runner"])] ) or {"result": "PASS"})
    assert search.main(["parity", "--protocol", str(protocol), "--oracle", "/bin/oracle", "--runner", "/bin/instrumented", "--workspace", str(tmp_path)]) == 0
    assert observed == [["/bin/oracle", "/bin/instrumented"]]


@pytest.mark.parametrize(
    "staging_failure",
    [None, "copy_fallback", "oracle_failure", "missing_payload", "directory_conflict"],
)
def test_parity_compares_real_output_contract_and_retains_oracle_manifests(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, staging_failure: str | None
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

    payload_sources: dict[str, Path] = {}
    for artifact_key, payloads in {
        "pq_artifact": (
            ("pq_pivots", "pivots.bin", b"pq-pivots"),
            ("pq_compressed_vectors", "compressed.bin", b"pq-compressed"),
        ),
        "disk_layout_artifact": (
            ("disk_index", "disk.index", b"disk-index"),
            ("medoids", "medoids.bin", b"medoids"),
        ),
    }.items():
        artifact = protocol["search_execution"][artifact_key]
        metadata_path = tmp_path / "normal" / artifact["metadata_path"]
        metadata_path.parent.mkdir(parents=True, exist_ok=True)
        metadata_payloads = []
        for role, name, content in payloads:
            source = metadata_path.parent / name
            source.write_bytes(content)
            payload_sources[role] = source
            metadata_payloads.append({
                "role": role, "path": name, "byte_length": len(content),
                "sha256": search._sha256(source), "media_type": "application/octet-stream",
            })
        metadata_path.write_text(json.dumps({
            "artifact_type": artifact["artifact_type"],
            "format_version": artifact["format_version"],
            "aggregate_checksum": artifact["aggregate_checksum"],
            "payloads": metadata_payloads,
        }), encoding="utf-8")

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
    source_facts = {
        role: (path.exists(), path.stat().st_size, search._sha256(path))
        for role, path in payload_sources.items()
    }
    oracle_stage = (
        tmp_path / "temporary"
        / f"{protocol['search_execution']['execution_id']}-oracle-native-index"
    )
    if staging_failure == "missing_payload":
        payload_sources["pq_pivots"].unlink()
    elif staging_failure == "directory_conflict":
        oracle_stage.mkdir(parents=True)
    elif staging_failure == "copy_fallback":
        real_link = search.os.link

        def cross_device_oracle_link(source: Path, destination: Path) -> None:
            if "oracle-native-index" in str(destination):
                raise OSError(errno.EXDEV, "fixture cross-device link")
            real_link(source, destination)

        monkeypatch.setattr(search.os, "link", cross_device_oracle_link)
    oracle_invocations = 0

    def write_observations() -> None:
        observations.mkdir(parents=True)
        for repetition in range(3):
            for l_search in (100, 200, 400):
                point_id = f"protocol-zero-L{l_search}-r{repetition}"
                document = {
                    "format_version": "search-observation/v1", "observation_id": point_id,
                    "run_id": "protocol-zero-search", "generated_at": "2026-08-21T00:00:00Z",
                    "implementation": {"implementation_id": "cpp_main_pqflash_search_instrumented_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
                    "source_build": {"diskann_repository": "https://github.com/microsoft/DiskANN.git", "diskann_branch": "cpp_main", "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823", "binary_path": "bin/runner", "binary_sha256": protocol["search_execution"]["runner_build"]["binary_sha256"], "build_manifest_path": protocol["search_execution"]["runner_build"]["build_manifest_path"], "build_manifest_sha256": protocol["search_execution"]["runner_build"]["build_manifest_sha256"]},
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
        nonlocal oracle_invocations
        if Path(argv[0]) == runner:
            instrumented_stage = tmp_path / "temporary/protocol-zero-search-native-index"
            instrumented_stage.mkdir(parents=True)
            (instrumented_stage / "runner-owned").write_text("temporary", encoding="utf-8")
            shutil.rmtree(instrumented_stage)
            write_observations()
            return subprocess.CompletedProcess(argv, 0, '{"status":"SUCCEEDED"}', "")
        oracle_invocations += 1
        prefix = Path(argv[argv.index("--index_path_prefix") + 1])
        assert prefix == oracle_stage / "index"
        for role, suffix in {
            "disk_index": "_disk.index",
            "medoids": "_disk.index_medoids.bin",
            "pq_pivots": "_pq_pivots.bin",
            "pq_compressed_vectors": "_pq_compressed.bin",
        }.items():
            alias = Path(f"{prefix}{suffix}")
            assert alias.is_file()
            assert not alias.is_symlink()
            assert alias.read_bytes() == payload_sources[role].read_bytes()
        if staging_failure == "oracle_failure":
            return subprocess.CompletedProcess(argv, 1, "", "fixture oracle failure")
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
    if staging_failure in {"missing_payload", "directory_conflict"}:
        assert result["result"] == "FAIL"
        assert oracle_invocations == 0
        evidence = json.loads(Path(result["evidence_path"]).read_text(encoding="utf-8"))
        assert evidence["result"] == "FAIL"
        assert "oracle staging failed" in evidence["reason"]
        if staging_failure == "directory_conflict":
            assert oracle_stage.is_dir()
        return
    if staging_failure == "oracle_failure":
        assert result["result"] == "FAIL"
        assert oracle_invocations == 9
        assert not oracle_stage.exists()
        assert {
            role: (path.exists(), path.stat().st_size, search._sha256(path))
            for role, path in payload_sources.items()
        } == source_facts
        return
    assert result["result"] == "PASS"
    assert oracle_invocations == 9
    assert not oracle_stage.exists()
    assert {
        role: (path.exists(), path.stat().st_size, search._sha256(path))
        for role, path in payload_sources.items()
    } == source_facts
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


def _import_dataset(root: Path) -> tuple[dict, dict]:
    """A real, small native DatasetArtifact; no admission or generator mock."""
    directory = root / "normal/dataset"
    directory.mkdir(parents=True)
    vectors = directory / "vectors.bin"
    vectors.write_bytes(struct.pack("<II", 256, 4) + struct.pack("<1024f", *range(1024)))
    structural = {"global_node_count": 256, "vector_dimension": 4,
                  "scalar_type": "float32", "distance_metric": "l2"}
    payload = {"role": "vectors", "path": "vectors.bin", "media_type": MEDIA,
               "byte_length": vectors.stat().st_size, "sha256": search._sha256(vectors)}
    projection = dataset_identity_projection(payload["sha256"], structural, payload)
    identity = {"dataset_id": "small", "dataset_checksum": payload["sha256"], **structural}
    metadata = {k: v for k, v in projection.items() if k != "dataset_checksum"}
    metadata.update(dataset=identity, payloads=[payload], aggregate_checksum=aggregate_checksum(projection))
    schema = json.loads((search._repository_root() / "contracts/baseline-v1/schemas/cpp-main-dataset-artifact.schema.json").read_text())
    search.Draft202012Validator(schema).validate(metadata)
    (directory / "artifact.json").write_text(json.dumps(metadata))
    reference = {k: metadata[k] for k in ("artifact_type", "format_version", "aggregate_checksum")}
    reference["metadata_path"] = "dataset/artifact.json"
    return identity, reference


@pytest.fixture
def imported_inputs(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    """Offline native inputs; invoking any external tool is a test failure."""
    host = search.platform.platform()
    monkeypatch.setattr(search.platform, "platform", lambda: host)
    monkeypatch.setattr(search, "_git_source_identity", lambda root: {
        "repository": "https://example.invalid/project.git", "branch": "fixture",
        "commit": "a" * 40, "dirty_worktree": False,
    })
    monkeypatch.setattr(search.subprocess, "run", lambda *a, **k: pytest.fail("import must not execute tools"))
    source = tmp_path / "existing/query.bin"
    source.parent.mkdir()
    source.write_bytes(struct.pack("<II8f", 2, 4, 9, -0.0, 4, 1, -3, 2, 8, 7))
    dataset, artifact = _import_dataset(tmp_path)
    common = {
        "workspace_root": str(tmp_path), "input_mode": "import_existing",
        "dataset_identity": dataset,
        "dataset_artifact": artifact,
        "resource_budget": {"memory_budget_bytes": 4096, "thread_budget": 1},
    }
    query = {**common, "run_id": "import-query", "query_set_id": "import-query",
             "source_file": {"path": "existing/query.bin", "format": "diskann-float32-bin",
                             "byte_length": source.stat().st_size, "sha256": search._sha256(source)},
             "reference_path": "inputs/import-query.json", "manifest_path": "manifests/import-query.json",
             "validation_evidence_path": "manifests/evidence/import-query.json"}
    return common, query


def _import_request(root: Path, operation: str, request: dict) -> dict:
    path = root / f"{operation}-import-request.json"
    path.write_text(json.dumps(request))
    original = path.read_bytes()
    assert search.main(["materialize", "--operation", operation, "--request", str(path)]) == 0
    assert path.read_bytes() == original
    return json.loads((root / request["reference_path"]).read_text())


def _import_truth_request(root: Path, common: dict, query: dict, width: int = 100) -> dict:
    source = root / "existing/truth.bin"
    ids = list(range(width)) + list(range(101, 101 + width))
    # For wide fixtures use repeated rows to stay within the 256-node Dataset.
    if width > 155:
        ids = list(range(width)) * 2
    source.write_bytes(struct.pack("<II", 2, width) + struct.pack(f"<{len(ids)}I", *ids)
                       + struct.pack(f"<{len(ids)}f", *([float(i) for i in range(width)] * 2)))
    return {**common, "run_id": "import-truth", "ground_truth_id": "import-truth",
            "reference_version": "1.1.0", "query_set": query,
            "query_set_checksum": query["canonical_checksum"],
            "source_file": {"path": "existing/truth.bin", "format": "diskann-truthset",
                            "byte_length": source.stat().st_size, "sha256": search._sha256(source)},
            "reference_path": "inputs/import-truth.json", "manifest_path": "manifests/import-truth.json",
            "validation_evidence_path": "manifests/evidence/import-truth.json"}


def test_native_import_preserves_bytes_rows_and_provenance(tmp_path: Path, imported_inputs) -> None:
    common, request = imported_inputs
    before = (tmp_path / "existing/query.bin").read_bytes()
    query = _import_request(tmp_path, "query_set", request)
    assert (tmp_path / query["native_payload"]["path"]).read_bytes() == before
    mapping = json.loads((tmp_path / query["row_mapping"]["path"]).read_text())
    assert mapping == [{"query_row_index": i, "source_query_id": f"query_row_{i}"} for i in range(2)]
    assert query["query_identity"] == "query_row_index"
    assert query["source_query_id_semantics"] == "opaque"
    assert (query["query_count"], query["dimension"]) == (2, 4)
    truth_request = _import_truth_request(tmp_path, common, query)
    truth_before = (tmp_path / "existing/truth.bin").read_bytes()
    truth = _import_request(tmp_path, "ground_truth", truth_request)
    assert (tmp_path / truth["payload"]["path"]).read_bytes() == truth_before
    assert truth["width"] == 100
    assert "rank_100_tie_group_closed" not in truth
    assert 'null' not in json.dumps(truth)
    for declaration in (request, truth_request):
        manifest = json.loads((tmp_path / declaration["manifest_path"]).read_text())
        search._schema_validator("build-manifest.schema.json", search._repository_root()).validate(manifest)
        parameters = manifest["parameters"]
        assert parameters["input_mode"] == "import_existing"
        assert parameters["source_file"]["sha256"] == declaration["source_file"]["sha256"]
        assert parameters["source_file"]["path"] == str((tmp_path / declaration["source_file"]["path"]).resolve())
        assert parameters["dataset_artifact"] == common["dataset_artifact"]
        assert not {"argv", "compute_groundtruth_binary", "stdout", "stderr"} & parameters.keys()
        evidence = json.loads((tmp_path / declaration["validation_evidence_path"]).read_text())
        search._schema_validator("validation-evidence.schema.json", search._repository_root()).validate(evidence)
        assert evidence["result"] == "VALID"
    assert parameters["query_set_checksum"] == query["canonical_checksum"]
    assert parameters["exact_neighbors_recomputed"] is False


def test_native_import_gt100_evaluates_without_closure(tmp_path: Path, imported_inputs) -> None:
    common, request = imported_inputs
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    query = _import_request(tmp_path, "query_set", request)
    truth = _import_request(tmp_path, "ground_truth", _import_truth_request(tmp_path, common, query))
    revised = _v2_protocol(tmp_path, original)
    protocol = json.loads(revised.read_text())
    protocol["search_execution"]["query_set"] = query
    protocol["evaluation"]["ground_truth"] = truth
    protocol = search.normalize_search_protocol(protocol)
    revised.write_text(json.dumps(protocol))
    for path in observations.glob("*.json"):
        observation = json.loads(path.read_text())
        observation["query_set_checksum"] = query["canonical_checksum"]
        observation["search_execution"]["canonical_checksum"] = protocol["search_execution_checksum"]
        observation["protocol"] = {"path": revised.relative_to(tmp_path).as_posix(), "protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"]}
        _save_observation(path, observation)
    _search_manifest(tmp_path, revised, observations)
    report = search.evaluate_observation_directory(protocol_path=revised, observations_path=observations, workspace=tmp_path)["report"]
    for item in report["observation_results"]:
        assert item["quality_eligible"] and item["performance_valid"]
        assert "100" in item["quality_eligibility_reason"] and "not verified" in item["quality_eligibility_reason"]
        assert item["quality_metrics"] == {"recall_at_10": 100.0, "recall_at_100": 100.0}
        assert item["performance_metrics"] == {"qps_k100": 1000.0, "mean_latency_us_k100": 20.0, "p99_9_latency_us_k100": 30.0, "mean_ios_k100": 3.0}
    assert report["aggregates"]["quality"][0]["eligible_count"] == 3
    assert report["aggregates"]["performance"][0]["median_performance_metrics"]["qps_k100"] == 1000.0


@pytest.mark.parametrize("case", ["checksum", "count", "dimension", "nan", "inf", "length", "source-escape", "output-escape", "upstream", "existing-output", "mixed", "no-mode", "format"])
def test_native_import_query_rejections(tmp_path: Path, imported_inputs, case: str) -> None:
    _, request = imported_inputs
    source = tmp_path / request["source_file"]["path"]
    if case == "checksum": request["source_file"]["sha256"] = "0" * 64
    if case == "count": request["query_count"] = 3
    if case == "dimension": request["dataset_identity"]["vector_dimension"] = 3
    if case in ("nan", "inf", "length"):
        payload = bytearray(source.read_bytes())
        if case == "length": payload.pop()
        else: struct.pack_into("<f", payload, 8, float(case))
        source.write_bytes(payload)
        request["source_file"].update(byte_length=len(payload), sha256=search._sha256(source))
    if case == "source-escape": request["source_file"]["path"] = "../query.bin"
    if case == "output-escape": request["reference_path"] = "../escaped-reference.json"
    if case == "upstream": request["reference_path"] = "upstream/forbidden.json"
    if case == "existing-output":
        p = tmp_path / request["reference_path"]; p.parent.mkdir(); p.write_bytes(b"preserve")
    if case == "mixed": request["source_csv"] = "unused.csv"
    if case == "no-mode": request.pop("input_mode")
    if case == "format": request["source_file"]["format"] = "csv"
    path = tmp_path / "invalid-import.json"; path.write_text(json.dumps(request))
    with pytest.raises(search.SearchEvaluationError): search.materialize_query_set_request(path)
    if case == "existing-output": assert p.read_bytes() == b"preserve"
    assert not (tmp_path / "inputs/import-query-rows.json").exists()
    manifest = json.loads((tmp_path / request["manifest_path"]).read_text())
    assert manifest["status"] == "FAILED"
    evidence_path = tmp_path / request["validation_evidence_path"]
    if evidence_path.exists():
        search._schema_validator("validation-evidence.schema.json", search._repository_root()).validate(json.loads(evidence_path.read_text()))


@pytest.mark.parametrize("case", ["id-range", "nan", "inf", "order", "length", "checksum", "query-count", "query-dimension", "query-checksum", "mixed", "null", "false", "old-unknown"])
def test_native_import_truth_rejections(tmp_path: Path, imported_inputs, case: str) -> None:
    common, request = imported_inputs
    query = _import_request(tmp_path, "query_set", request)
    request = _import_truth_request(tmp_path, common, query)
    source = tmp_path / request["source_file"]["path"]
    payload = bytearray(source.read_bytes())
    if case == "id-range": struct.pack_into("<I", payload, 8, 256)
    if case in ("nan", "inf"): struct.pack_into("<f", payload, 808, float(case))
    if case == "order": struct.pack_into("<f", payload, 808, 5.0)
    if case == "length": payload.pop()
    if case == "query-count": struct.pack_into("<I", payload, 0, 1)
    source.write_bytes(payload)
    request["source_file"].update(byte_length=len(payload), sha256=search._sha256(source))
    if case == "checksum": request["source_file"]["sha256"] = "0" * 64
    if case == "query-checksum": request["query_set_checksum"] = "0" * 64
    if case == "query-dimension": request["dataset_identity"]["vector_dimension"] = 3
    if case == "mixed": request["compute_groundtruth_binary"] = "unused"
    if case in ("null", "false"): request["rank_100_tie_group_closed"] = None if case == "null" else False
    if case == "old-unknown": request["reference_version"] = "1.0.0"
    path = tmp_path / "invalid-truth-import.json"; path.write_text(json.dumps(request))
    with pytest.raises(search.SearchEvaluationError): search.materialize_ground_truth_request(path)
    assert not (tmp_path / request["reference_path"]).exists()
    assert json.loads((tmp_path / request["manifest_path"]).read_text())["status"] == "FAILED"


@pytest.mark.parametrize("reference_version,protocol_version,closure,valid", [
    ("1.1.0", "1.1.0", "omit", True), ("1.1.0", "1.1.0", True, True),
    ("1.1.0", "1.1.0", None, False), ("1.1.0", "1.1.0", False, False),
    ("1.1.0", "1.0.0", "omit", False), ("1.1.0", "1.0.0", True, False),
    ("1.0.0", "1.0.0", "omit", False), ("1.0.0", "1.1.0", "omit", False),
    ("1.0.0", "1.0.0", True, True), ("1.0.0", "1.1.0", True, True),
])
def test_native_import_protocol_revision(reference_version, protocol_version, closure, valid) -> None:
    protocol = _protocol("zero")
    protocol["protocol_version"] = protocol_version
    if protocol_version == "1.1.0":
        protocol["evaluation"]["evaluator"].update(implementation_id="cpp_main_search_metrics_evaluator_v2", version="2.0.0")
        protocol["evaluation"]["performance_eligibility_policy"] = "complete_valid_measurement_single_invocation_v2"
    truth = protocol["evaluation"]["ground_truth"]
    truth["reference_version"] = reference_version
    if closure == "omit": truth.pop("rank_100_tie_group_closed")
    else: truth["rank_100_tie_group_closed"] = closure
    if valid:
        assert search.normalize_search_protocol(protocol)["evaluation"]["ground_truth"] == truth
    else:
        with pytest.raises(search.SearchEvaluationError): search.normalize_search_protocol(protocol)


def test_native_import_content_validation_is_separate_from_closure(tmp_path: Path) -> None:
    path = tmp_path / "ties.bin"
    path.write_bytes(struct.pack("<II", 1, 101) + struct.pack("<101I", *range(101)) + struct.pack("<101f", *([0.0] * 101)))
    search._validate_truth_payload(path, query_count=1, width=101, node_count=256)


def _existing_query_reference(root: Path) -> dict:
    query = _protocol("zero")["search_execution"]["query_set"]
    source = root / "existing/query.bin"
    query["source"] = query["native_payload"] = {
        "path": "existing/query.bin", "format": "diskann-float32-bin",
        "byte_length": source.stat().st_size, "sha256": search._sha256(source),
    }
    mapping = root / "existing/rows.json"
    mapping.write_text(json.dumps([{"query_row_index": i, "source_query_id": f"row-{i}"} for i in range(2)]))
    query["row_mapping"] = {"path": "existing/rows.json", "format": "json", "byte_length": mapping.stat().st_size, "sha256": search._sha256(mapping)}
    query.pop("canonical_checksum")
    query["canonical_checksum"] = search._canonical_digest(query)
    return query


def test_native_import_gt100_dispatch_before_generator_fields(tmp_path: Path, imported_inputs) -> None:
    common, _ = imported_inputs
    query = _existing_query_reference(tmp_path)
    request = _import_truth_request(tmp_path, common, query)
    reference = _import_request(tmp_path, "ground_truth", request)
    assert reference["width"] == 100 and reference["query_count"] == 2
    assert "rank_100_tie_group_closed" not in reference


@pytest.mark.parametrize("version,width,claim,accepted", [
    ("1.0.0", 100, False, False), ("1.0.0", 200, False, True),
    ("1.1.0", 100, False, True), ("1.1.0", 200, False, True),
    ("1.1.0", 100, True, False), ("1.1.0", 200, True, True),
])
def test_native_import_generated_truth_closure_policy(tmp_path: Path, imported_inputs, monkeypatch: pytest.MonkeyPatch, version, width, claim, accepted) -> None:
    common, _ = imported_inputs
    request = _import_truth_request(tmp_path, common, _existing_query_reference(tmp_path), width)
    source_bytes = (tmp_path / request.pop("source_file")["path"]).read_bytes()
    request.pop("query_set")
    request.update(input_mode="generate", reference_version=version, width=width, query_count=2,
                   compute_groundtruth_binary=str(tmp_path / "fixture-binary"),
                   dataset_payload="fixture-base.bin", query_payload="fixture-query.bin",
                   payload_path="inputs/generated.bin", stdout_path="logs/gt.stdout", stderr_path="logs/gt.stderr",
                   toolchain=search._python_harness_toolchain())
    binary = Path(request["compute_groundtruth_binary"]); binary.write_bytes(b"offline stub")
    request["compute_groundtruth_sha256"] = search._sha256(binary)
    if claim: request["rank_100_tie_group_closed"] = True
    calls = []
    def generate(argv, **kwargs):
        calls.append(argv)
        Path(argv[argv.index("--gt_file") + 1]).write_bytes(source_bytes)
        return subprocess.CompletedProcess(argv, 0, "stub generation", "")
    monkeypatch.setattr(search.subprocess, "run", generate)
    if not accepted:
        with pytest.raises(search.SearchEvaluationError, match="closure"):
            _import_request(tmp_path, "ground_truth", request)
        assert not (tmp_path / request["reference_path"]).exists()
    else:
        reference = _import_request(tmp_path, "ground_truth", request)
        assert (tmp_path / reference["payload"]["path"]).read_bytes() == source_bytes
        assert ("rank_100_tie_group_closed" in reference) == (version == "1.0.0" or claim)
        manifest = json.loads((tmp_path / request["manifest_path"]).read_text())
        assert manifest["parameters"]["argv"] == calls[0]
        assert manifest["parameters"]["input_mode"] == "generate"
        ids, distances = search._read_ground_truth(tmp_path / reference["payload"]["path"], 2, width)
        assert search._diskann_recall_one(ids[0], distances[0], list(range(100)), 100) == 100.0
    assert len(calls) == 1


@pytest.mark.parametrize("claim,accepted", [(False, True), (True, False)])
def test_native_import_unclosed_wide_truth_is_valid_without_claim(tmp_path: Path, imported_inputs, claim, accepted) -> None:
    common, _ = imported_inputs
    request = _import_truth_request(tmp_path, common, _existing_query_reference(tmp_path), 101)
    source = tmp_path / request["source_file"]["path"]
    payload = bytearray(source.read_bytes())
    for row in range(2):
        struct.pack_into("<f", payload, 8 + 202 * 4 + (row * 101 + 100) * 4, 99.0)
    source.write_bytes(payload)
    request["source_file"]["sha256"] = search._sha256(source)
    if claim: request["rank_100_tie_group_closed"] = True
    if accepted:
        reference = _import_request(tmp_path, "ground_truth", request)
        assert "rank_100_tie_group_closed" not in reference
    else:
        with pytest.raises(search.SearchEvaluationError, match="closure"):
            _import_request(tmp_path, "ground_truth", request)


@pytest.mark.parametrize("case", ["symlink", "mapping-checksum", "mapping-order", "existing-reference", "existing-manifest", "source-as-output"])
def test_native_import_preserves_path_and_mapping_guards(tmp_path: Path, imported_inputs, case) -> None:
    common, _ = imported_inputs
    query = _existing_query_reference(tmp_path)
    request = _import_truth_request(tmp_path, common, query)
    preserved = None
    if case == "symlink":
        (tmp_path / "outside").symlink_to(tmp_path.parent, target_is_directory=True)
        request["reference_path"] = "outside/forbidden.json"
    if case in ("mapping-checksum", "mapping-order"):
        mapping = tmp_path / query["row_mapping"]["path"]
        mapping.write_text(json.dumps([{"query_row_index": 1, "source_query_id": "a"}] * 2))
        if case == "mapping-order":
            query["row_mapping"].update(byte_length=mapping.stat().st_size, sha256=search._sha256(mapping))
            query.pop("canonical_checksum")
            query["canonical_checksum"] = search._canonical_digest(query)
            request["query_set_checksum"] = query["canonical_checksum"]
    if case in ("existing-reference", "existing-manifest"):
        key = "reference_path" if case == "existing-reference" else "manifest_path"
        preserved = tmp_path / request[key]
        preserved.parent.mkdir(parents=True, exist_ok=True); preserved.write_bytes(b"keep original")
    if case == "source-as-output":
        request["reference_path"] = request["source_file"]["path"]
        preserved = tmp_path / request["reference_path"]
    before = preserved.read_bytes() if preserved else None
    with pytest.raises(search.SearchEvaluationError):
        _import_request(tmp_path, "ground_truth", request)
    if preserved: assert preserved.read_bytes() == before


@pytest.mark.parametrize("case", ["id", "nan", "order", "length", "checksum", "query-binding", "dataset-binding"])
def test_native_import_evaluation_revalidates_new_truth(tmp_path: Path, imported_inputs, case) -> None:
    common, _ = imported_inputs
    original, observations = _offline_fixture(tmp_path, lengths=(100,))
    query = _existing_query_reference(tmp_path)
    truth = _import_request(tmp_path, "ground_truth", _import_truth_request(tmp_path, common, query))
    revised = _v2_protocol(tmp_path, original)
    protocol = json.loads(revised.read_text())
    protocol["search_execution"]["query_set"] = query
    protocol["evaluation"]["ground_truth"] = truth
    source = tmp_path / truth["payload"]["path"]
    payload = bytearray(source.read_bytes())
    if case == "id": struct.pack_into("<I", payload, 8, 256)
    if case == "nan": struct.pack_into("<f", payload, 808, float("nan"))
    if case == "order": struct.pack_into("<f", payload, 808, 2.0)
    if case == "length": payload.pop()
    source.write_bytes(payload)
    truth["payload"].update(sha256=search._sha256(source), byte_length=len(payload))
    if case == "checksum": truth["payload"]["sha256"] = "0" * 64
    if case == "query-binding": truth["query_set_checksum"] = "0" * 64
    if case == "dataset-binding": truth["dataset_artifact"] = {**truth["dataset_artifact"], "aggregate_checksum": "0" * 64}
    truth.pop("canonical_checksum")
    truth["canonical_checksum"] = search._canonical_digest(truth)
    revised.write_text(json.dumps(search.normalize_search_protocol(protocol)))
    with pytest.raises(search.SearchEvaluationError):
        search.evaluate_observation_directory(protocol_path=revised, observations_path=observations, workspace=tmp_path)
    assert not (tmp_path / "reports/search-evaluation").exists()


def test_native_import_evaluation_rejects_symlink_before_read(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    original, _ = _offline_fixture(tmp_path, lengths=(100,))
    protocol = json.loads(original.read_text())
    truth = protocol["evaluation"]["ground_truth"]
    outside = tmp_path.parent / f"{tmp_path.name}-outside.bin"
    outside.write_bytes((tmp_path / truth["payload"]["path"]).read_bytes())
    link = tmp_path / "inputs/outside-link.bin"
    link.symlink_to(outside)
    truth["payload"]["path"] = "inputs/outside-link.bin"
    truth["reference_version"] = "1.1.0"
    truth.pop("rank_100_tie_group_closed")
    truth.pop("canonical_checksum")
    truth["canonical_checksum"] = search._canonical_digest(truth)
    original_sha = search._sha256
    def guarded_sha(path):
        assert Path(path).resolve() != outside, "must reject outside source before reading bytes"
        return original_sha(path)
    monkeypatch.setattr(search, "_sha256", guarded_sha)
    with pytest.raises(search.SearchEvaluationError, match="escapes workspace"):
        search._load_evaluation_ground_truth(tmp_path, protocol)


@pytest.mark.parametrize("case", ["node-count", "dimension", "metadata-checksum", "reference-checksum", "structural-identity"])
def test_native_import_bound_dataset_rejections(tmp_path: Path, imported_inputs, case: str) -> None:
    common, query_request = imported_inputs
    operation = "query_set"
    request = query_request
    metadata_path = tmp_path / "normal/dataset/artifact.json"
    if case == "node-count":
        query = _import_request(tmp_path, "query_set", query_request)
        request = _import_truth_request(tmp_path, common, query)
        operation = "ground_truth"
        request["dataset_identity"] = {**common["dataset_identity"], "global_node_count": 512}
        source = tmp_path / request["source_file"]["path"]
        payload = bytearray(source.read_bytes())
        struct.pack_into("<I", payload, 8, 300)
        source.write_bytes(payload)
        request["source_file"]["sha256"] = search._sha256(source)
    elif case == "dimension":
        # The native query agrees with the request, but not the bound Dataset.
        request["dataset_identity"] = {**common["dataset_identity"], "vector_dimension": 8}
        source = tmp_path / request["source_file"]["path"]
        source.write_bytes(struct.pack("<II16f", 2, 8, *range(16)))
        request["source_file"].update(byte_length=source.stat().st_size, sha256=search._sha256(source))
    elif case == "reference-checksum":
        request["dataset_artifact"] = {**common["dataset_artifact"], "aggregate_checksum": "0" * 64}
    else:
        metadata = json.loads(metadata_path.read_text())
        if case == "metadata-checksum":
            metadata["structural"]["global_node_count"] = 512
            metadata["dataset"]["global_node_count"] = 512
            request["dataset_identity"] = dict(metadata["dataset"])
        else:
            metadata["dataset"]["vector_dimension"] = 8
        metadata_path.write_text(json.dumps(metadata))
    source = tmp_path / request["source_file"]["path"]
    before = source.read_bytes()
    with pytest.raises(search.SearchEvaluationError, match="Dataset"):
        _import_request(tmp_path, operation, request)
    assert source.read_bytes() == before
    assert not (tmp_path / request["reference_path"]).exists()
    assert json.loads((tmp_path / request["manifest_path"]).read_text())["status"] == "FAILED"


def test_native_import_output_parent_file_is_rejected_before_publication(tmp_path: Path, imported_inputs) -> None:
    _, request = imported_inputs
    blocked = tmp_path / "blocked"
    blocked.write_bytes(b"preserve parent")
    request["reference_path"] = "blocked/reference.json"
    with pytest.raises((search.SearchEvaluationError, OSError)):
        _import_request(tmp_path, "query_set", request)
    assert blocked.read_bytes() == b"preserve parent"
    manifest = json.loads((tmp_path / request["manifest_path"]).read_text())
    assert manifest["status"] == "FAILED"
    assert not (tmp_path / "inputs/import-query-rows.json").exists()
    evidence = json.loads((tmp_path / request["validation_evidence_path"]).read_text())
    assert evidence["result"] == "INVALID"


@pytest.mark.parametrize("failed_output", ["reference_path", "validation_evidence_path", "manifest_path"])
def test_native_import_publication_failure_keeps_original_failure(
    tmp_path: Path, imported_inputs, monkeypatch: pytest.MonkeyPatch, failed_output: str,
) -> None:
    _, request = imported_inputs
    original_write = search.write_immutable_json
    injected = False
    def fail_once(path, document):
        nonlocal injected
        if Path(path) == tmp_path / request[failed_output] and not injected:
            injected = True
            raise OSError("injected publication failure")
        return original_write(path, document)
    monkeypatch.setattr(search, "write_immutable_json", fail_once)
    with pytest.raises(search.SearchEvaluationError, match="injected publication failure"):
        _import_request(tmp_path, "query_set", request)
    assert injected
    manifest = json.loads((tmp_path / request["manifest_path"]).read_text())
    assert manifest["status"] == "FAILED"
    assert "injected publication failure" in manifest["failure"]["reason"]
    if failed_output == "reference_path":
        assert not (tmp_path / request["reference_path"]).exists()
        assert json.loads((tmp_path / request["validation_evidence_path"]).read_text())["result"] == "INVALID"


@pytest.mark.parametrize("output", ["reference_path", "manifest_path", "validation_evidence_path", "row_mapping"])
def test_native_import_dangling_outputs_are_never_followed(tmp_path: Path, imported_inputs, output: str) -> None:
    _, request = imported_inputs
    relative = "inputs/import-query-rows.json" if output == "row_mapping" else request[output]
    link = tmp_path / relative
    link.parent.mkdir(parents=True, exist_ok=True)
    target = tmp_path / "must-not-be-created.json"
    link.symlink_to(target)
    original_target = os.readlink(link)
    with pytest.raises(search.SearchEvaluationError, match="already exists"):
        _import_request(tmp_path, "query_set", request)
    assert link.is_symlink() and os.readlink(link) == original_target
    assert not target.exists()
    assert not (tmp_path / request["reference_path"]).exists()


@pytest.mark.parametrize("kind", ["file", "directory", "live-link"])
def test_native_import_occupied_reference_is_preserved(tmp_path: Path, imported_inputs, kind: str) -> None:
    _, request = imported_inputs
    target = tmp_path / request["reference_path"]
    target.parent.mkdir()
    if kind == "directory": target.mkdir()
    elif kind == "file": target.write_bytes(b"existing")
    else:
        (tmp_path / "existing-target").write_bytes(b"existing")
        target.symlink_to(tmp_path / "existing-target")
    with pytest.raises(search.SearchEvaluationError, match="already exists"):
        _import_request(tmp_path, "query_set", request)
    if kind == "directory": assert target.is_dir()
    else: assert target.read_bytes() == b"existing"
    if kind == "live-link": assert target.is_symlink()


def test_native_import_failure_record_write_cannot_mask_original_error(
    tmp_path: Path, imported_inputs, monkeypatch: pytest.MonkeyPatch,
) -> None:
    _, request = imported_inputs
    request["source_file"]["sha256"] = "0" * 64
    original_write = search.write_immutable_json
    def unavailable_manifest(path, document):
        if Path(path) == tmp_path / request["manifest_path"]:
            raise PermissionError("failure record destination unavailable")
        return original_write(path, document)
    monkeypatch.setattr(search, "write_immutable_json", unavailable_manifest)
    with pytest.raises(search.SearchEvaluationError, match="native input checksum/length mismatch"):
        _import_request(tmp_path, "query_set", request)
    assert not (tmp_path / request["reference_path"]).exists()


def test_native_import_success_requires_all_published_outputs(
    tmp_path: Path, imported_inputs, monkeypatch: pytest.MonkeyPatch,
) -> None:
    _, request = imported_inputs
    original_write = search.write_immutable_json
    observed_success = []
    def check_success(path, document):
        if document.get("status") == "SUCCEEDED":
            assert (tmp_path / request["reference_path"]).is_file()
            assert (tmp_path / "inputs/import-query-rows.json").is_file()
            assert json.loads((tmp_path / request["validation_evidence_path"]).read_text())["result"] == "VALID"
            observed_success.append(path)
        return original_write(path, document)
    monkeypatch.setattr(search, "write_immutable_json", check_success)
    _import_request(tmp_path, "query_set", request)
    assert observed_success == [tmp_path / request["manifest_path"]]


@pytest.mark.parametrize("reference", ["manifests", "inputs"])
def test_native_import_nested_outputs_are_rejected_before_writing(tmp_path: Path, imported_inputs, reference) -> None:
    _, request = imported_inputs
    request["reference_path"] = reference
    with pytest.raises(search.SearchEvaluationError, match="nested|overlaps"):
        _import_request(tmp_path, "query_set", request)
    assert not (tmp_path / reference).exists()
    assert not (tmp_path / "inputs/import-query-rows.json").exists()


def test_native_import_generation_failure_also_preserves_dangling_manifest(tmp_path: Path, imported_inputs) -> None:
    _, request = imported_inputs
    request.pop("source_file")
    request["input_mode"] = "generate"
    manifest = tmp_path / request["manifest_path"]
    manifest.parent.mkdir()
    target = tmp_path / "unwritten.json"
    manifest.symlink_to(target)
    with pytest.raises(search.SearchEvaluationError, match="already exists"):
        _import_request(tmp_path, "query_set", request)
    assert manifest.is_symlink() and not target.exists()
