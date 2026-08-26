from __future__ import annotations

import copy
import json
import hashlib
import struct
import subprocess
from pathlib import Path

import pytest

from diskann_harness import framework_baseline_certification as fbc
from diskann_harness import diskann_search_evaluation as search
from diskann_harness.framework_baseline_certification import (
    FeatureCertificationError,
    SCHEMA_FILENAMES,
    build_argument_parser,
    canonical_document_checksum,
    load_feature_schema,
    resolve_feature_output,
    verify_document_checksum,
    write_verified_document,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _reference_request_fixture(tmp_path: Path) -> tuple[Path, dict[str, object]]:
    workspace = tmp_path / "workspace"
    workspace.mkdir()
    binary_dir = workspace / "binaries"
    binary_dir.mkdir()
    owner = binary_dir / "native_reference_capture_runner"
    native = binary_dir / "build_disk_index"
    owner.write_bytes(b"owner-fixture")
    native.write_bytes(b"native-fixture")
    owner.chmod(0o755)
    native.chmod(0o755)
    manifests = workspace / "manifests" / "build"
    manifests.mkdir(parents=True)
    owner_manifest = manifests / "owner.json"
    native_manifest = manifests / "native.json"
    owner_manifest.write_text("{}", encoding="utf-8")
    native_manifest.write_text("{}", encoding="utf-8")
    evidence = workspace / "manifests" / "evidence" / "dataset.json"
    evidence.parent.mkdir(parents=True)
    evidence.write_text("{}", encoding="utf-8")
    profile = (
        REPOSITORY_ROOT
        / "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml"
    )
    request: dict[str, object] = {
        "request_version": "native-reference-build-request/v1",
        "run_id": "native-reference-fixture",
        "study_class": "baseline_validation",
        "reference_role": "cpp_main_native_sharded_reference_v1",
        "reproduction_mode": "upstream_faithful",
        "execution_authorized": False,
        "workspace_root": str(workspace.resolve()),
        "source": {
            "repository": "https://github.com/microsoft/DiskANN.git",
            "branch": "cpp_main",
            "commit": "78256bbab4685e1774e78d331e081a153be26823",
            "source_path": "upstream/diskann-cpp-main",
            "dirty_worktree": False,
        },
        "project_operation_owner": {
            "implementation_id": "framework_baseline_native_reference_capture_v1",
            "implementation_version": "1.0.0",
            "repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
            "branch": "feature/framework-baseline-certification",
            "commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
            "dirty_worktree": False,
            "binary": {
                "implementation_id": "framework_baseline_native_reference_capture_v1",
                "path": str(owner.resolve()),
                "byte_length": owner.stat().st_size,
                "sha256": _sha256(owner),
                "build_manifest": {
                    "path": owner_manifest.relative_to(workspace).as_posix(),
                    "byte_length": owner_manifest.stat().st_size,
                    "sha256": _sha256(owner_manifest),
                },
            },
        },
        "native_binary": {
            "implementation_id": "cpp_main_build_disk_index_native_v1",
            "path": str(native.resolve()),
            "byte_length": native.stat().st_size,
            "sha256": _sha256(native),
            "build_manifest": {
                "path": native_manifest.relative_to(workspace).as_posix(),
                "byte_length": native_manifest.stat().st_size,
                "sha256": _sha256(native_manifest),
            },
        },
        "dataset_artifact": {
            "metadata_path": "datasets/fixture/artifact.json",
            "artifact_type": "dataset",
            "format_version": "dataset-artifact/v1",
            "aggregate_checksum": "1" * 64,
        },
        "dataset_validation_evidence": {
            "path": evidence.relative_to(workspace).as_posix(),
            "byte_length": evidence.stat().st_size,
            "sha256": _sha256(evidence),
        },
        "profile": {
            "path": "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml",
            "sha256": _sha256(profile),
            "status": "draft",
        },
        "controls": {
            "data_type": "float",
            "distance_metric": "l2",
            "branch_mode": "sharded",
            "R": 16,
            "L": 32,
            "B": 0.03,
            "build_DRAM_budget_gib": 0.6,
            "num_threads": 6,
            "QD": 0,
            "disk_PQ_bytes": 0,
            "build_PQ_bytes": 0,
            "opq": False,
            "labels": False,
        },
        "capture": {
            "mechanism": "prearmed_inotify_same_filesystem_hardlink",
            "source_graph_name": "fixture_mem.index",
            "source_medoids_name": "fixture_disk.index_medoids.bin",
            "captured_graph_path": "normal/reference/global.index",
            "captured_medoids_path": "normal/reference/medoids.bin",
            "same_filesystem_required": True,
            "copy_fallback_allowed": False,
            "exclusive_create": True,
        },
        "resource_budget": {
            "memory_budget_bytes": 1024,
            "thread_budget": 6,
            "disk_budget_bytes": 2048,
        },
        "outputs": {
            "artifact_metadata_path": "normal/reference/artifact.json",
            "build_manifest_path": "manifests/reference.json",
            "validation_evidence_path": "manifests/evidence/reference.json",
            "stdout_path": "reports/reference.stdout",
            "stderr_path": "reports/reference.stderr",
            "observer_evidence_path": "reports/reference-observer.json",
            "structural_projection_path": "reports/reference-structural.json",
        },
    }
    return workspace, request


def test_feature_local_schema_loader_is_bounded_and_strict() -> None:
    assert SCHEMA_FILENAMES == {
        "global-graph-artifact-v2.schema.json",
        "native-reference-build-request.schema.json",
        "quality-parity-policy.schema.json",
        "quality-parity-evidence.schema.json",
        "framework-baseline-certification-decision.schema.json",
    }
    for name in SCHEMA_FILENAMES:
        schema, validator = load_feature_schema(REPOSITORY_ROOT, name)
        assert schema["additionalProperties"] is False
        assert validator.schema is schema

    with pytest.raises(FeatureCertificationError, match="not admitted"):
        load_feature_schema(REPOSITORY_ROOT, "search-protocol.schema.json")


def test_canonical_checksum_verification_ignores_only_its_own_field() -> None:
    document = {"identity": "fixture", "items": [2, 1]}
    checksum = canonical_document_checksum(document, "document_checksum")
    sealed = {**document, "document_checksum": checksum}
    verify_document_checksum(sealed, "document_checksum")

    with pytest.raises(FeatureCertificationError, match="checksum"):
        verify_document_checksum({**sealed, "identity": "changed"}, "document_checksum")


def test_confined_exclusive_write_rereads_and_rejects_existing_output(
    tmp_path: Path,
) -> None:
    target = resolve_feature_output(tmp_path, "reports/result.json")
    document = {"identity": "fixture", "document_checksum": ""}
    document["document_checksum"] = canonical_document_checksum(
        document, "document_checksum"
    )
    written = write_verified_document(
        tmp_path,
        "reports/result.json",
        document,
        checksum_field="document_checksum",
    )
    assert written == target
    assert json.loads(target.read_text(encoding="utf-8")) == document

    with pytest.raises(FeatureCertificationError, match="already exists"):
        write_verified_document(
            tmp_path,
            "reports/result.json",
            document,
            checksum_field="document_checksum",
        )
    with pytest.raises(FeatureCertificationError, match="workspace"):
        resolve_feature_output(tmp_path, "../escape.json")
    with pytest.raises(FeatureCertificationError, match="workspace"):
        resolve_feature_output(tmp_path, target)


def test_cli_exposes_exactly_the_six_feature_operations() -> None:
    parser = build_argument_parser()
    subparsers = next(
        action for action in parser._actions if getattr(action, "choices", None)
    )
    assert set(subparsers.choices) == {
        "reference-preflight",
        "reference-execute",
        "derive-structural",
        "freeze-quality-policy",
        "compare-quality",
        "decide",
    }
    assert {"validate", "certify", "protocol"}.isdisjoint(subparsers.choices)


def test_reference_lifecycle_has_one_preflight_and_execution_owner() -> None:
    assert callable(fbc.reference_preflight)
    assert callable(fbc.reference_execute)


def test_reference_preflight_is_immutable_nonexecuting_and_execute_is_shell_free(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    config = tmp_path / "reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(fbc, "_fresh_validate_dataset", lambda *args: None)

    def no_launch(*args: object, **kwargs: object) -> object:
        raise AssertionError("preflight launched a process")

    monkeypatch.setattr(fbc.subprocess, "run", no_launch)
    result = fbc.reference_preflight(
        config,
        workspace / "requests" / "reference.json",
        repository_root=REPOSITORY_ROOT,
    )
    request_path = Path(result["request_path"])
    assert result["execution_authorized"] is False
    assert result["byte_length"] == request_path.stat().st_size
    assert result["sha256"] == _sha256(request_path)
    assert json.loads(request_path.read_text(encoding="utf-8")) == request
    with pytest.raises(FeatureCertificationError, match="already exists"):
        fbc.reference_preflight(
            config, request_path, repository_root=REPOSITORY_ROOT
        )

    observed: list[list[str]] = []

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        observed.append(argv)
        assert kwargs["shell"] is False
        return subprocess.CompletedProcess(argv, 0, "capture-ok\n", "")

    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    execution = fbc.reference_execute(
        request_path, repository_root=REPOSITORY_ROOT
    )
    assert execution["returncode"] == 0
    assert observed == [
        [
            request["project_operation_owner"]["binary"]["path"],
            "--request",
            str(request_path.resolve()),
        ]
    ]
    assert (workspace / request["outputs"]["stdout_path"]).read_text() == "capture-ok\n"


def test_reference_preflight_and_execute_fail_closed_before_launch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    config = tmp_path / "reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(fbc, "_fresh_validate_dataset", lambda *args: None)
    request_path = workspace / "requests" / "reference.json"
    fbc.reference_preflight(config, request_path, repository_root=REPOSITORY_ROOT)

    calls = 0

    def forbidden(*args: object, **kwargs: object) -> object:
        nonlocal calls
        calls += 1
        raise AssertionError("invalid request launched capture")

    monkeypatch.setattr(fbc.subprocess, "run", forbidden)
    owner = Path(request["project_operation_owner"]["binary"]["path"])
    owner.write_bytes(b"changed")
    with pytest.raises(FeatureCertificationError, match="binary identity"):
        fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert calls == 0


def _structural_projection(*, reference: bool) -> dict[str, object]:
    return {
        "baseline_id": (
            "cpp_main_native_sharded_reference_v1"
            if reference
            else "cpp_main_p0_b0_m0_framework_candidate_v1"
        ),
        "execution_status": "SUCCEEDED",
        "source": {
            "repository": "https://github.com/microsoft/DiskANN.git",
            "branch": "cpp_main",
            "commit": "78256bbab4685e1774e78d331e081a153be26823",
            "clean_before": True,
            "clean_after": True,
        },
        "dataset_checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
        "profile_checksum": "6dc8009b9654b01ae61fb32ab3af6cff3c9465a5a4e0e821cfdea06793d74262",
        "branch_mode": "sharded",
        "partition_count": 3,
        **(
            {"native_stage_sequence_observed": True}
            if reference
            else {
                "stage_invocations": {"partition": 1, "local_build": 3, "merge": 1},
                "artifact_handoffs_valid": True,
            }
        ),
        "global_graph": {
            "available": True,
            "validation_status": "PASS",
            "id_space": "global",
            "node_count": 1_000_000,
            "invalid_neighbor_count": 0,
            "self_edge_count": 0,
            "observed_max_degree": 16,
            "medoids_valid": True,
            "dataset_lineage_checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
        },
        "evidence_refs": ["fixture/evidence.json"],
    }


@pytest.mark.parametrize(
    ("mutation", "expected"),
    [
        ("pass", "PASS"),
        ("fail", "FAIL"),
        ("inconclusive", "INCONCLUSIVE"),
        ("not_reached", "NOT_REACHED"),
    ],
)
def test_derive_structural_reuses_frozen_feature002_four_state_helper(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    mutation: str,
    expected: str,
) -> None:
    reference = _structural_projection(reference=True)
    framework = _structural_projection(reference=False)
    if mutation == "fail":
        framework["global_graph"]["observed_max_degree"] = 17
    elif mutation == "inconclusive":
        del reference["global_graph"]["medoids_valid"]
    elif mutation == "not_reached":
        reference["global_graph"] = {"available": False}
    input_document = {
        "workspace_root": str(tmp_path),
        "report_id": f"fixture-{mutation}",
        "generated_at": "2026-08-26T00:00:00Z",
        "reference": {"projection": reference},
        "framework": {"projection": framework},
    }
    input_path = tmp_path / "input.json"
    input_path.write_text(json.dumps(input_document), encoding="utf-8")
    monkeypatch.setattr(
        fbc,
        "_validate_structural_role_admission",
        lambda role, *args, **kwargs: role["projection"],
    )
    output = tmp_path / "reports" / f"{mutation}.json"
    result = fbc.derive_structural(
        input_path, output, repository_root=REPOSITORY_ROOT
    )
    report = json.loads(output.read_text(encoding="utf-8"))
    assert result["gate"] == expected
    assert report["gate"]["result"] == expected
    assert report["claims"]["structural_and_contract_parity"] is (
        expected == "PASS"
    )
    assert report["claims"]["search_semantics_parity"] is False
    assert report["claims"]["quality_parity"] is False
    assert report["claims"]["final_framework_baseline_certified"] is False


FROZEN_RUNNER_IDENTITY = {
    "project_commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
    "runner_sha256": "97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856",
    "implementation_id": "cpp_main_pqflash_search_instrumented_v1",
    "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823",
}


def _artifact_ref(
    artifact_type: str, format_version: str, metadata_path: str, marker: str
) -> dict[str, object]:
    return {
        "metadata_path": metadata_path,
        "artifact_type": artifact_type,
        "format_version": format_version,
        "aggregate_checksum": marker * 64,
    }


def _canonical_fixture_document(path: Path, document: dict[str, object]) -> dict[str, object]:
    value = copy.deepcopy(document)
    value["canonical_checksum"] = search._canonical_digest(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, sort_keys=True, separators=(",", ":")), encoding="utf-8"
    )
    return value


def _document_ref(
    workspace: Path, path: Path, *, canonical_checksum: str | None = None
) -> dict[str, object]:
    reference: dict[str, object] = {
        "path": path.relative_to(workspace).as_posix(),
        "byte_length": path.stat().st_size,
        "sha256": _sha256(path),
    }
    if canonical_checksum is not None:
        reference["canonical_checksum"] = canonical_checksum
    return reference


def _protocol_declaration(
    *,
    role: str,
    cache_mode: str,
    runner_sha: str,
    query_set: dict[str, object],
    ground_truth: dict[str, object],
) -> dict[str, object]:
    dataset = _artifact_ref(
        "dataset", "diskann-float32-dataset/v1", "dataset/artifact.json", "d"
    )
    shared_pq = _artifact_ref("pq", "pq-artifact/v1", "pq/artifact.json", "b")
    if role == "reference":
        graph = _artifact_ref(
            "global_graph",
            "global-graph-artifact/v2",
            "reference/graph/artifact.json",
            "c",
        )
        layout = _artifact_ref(
            "disk_layout",
            "disk-layout-artifact/v1",
            "reference/layout/artifact.json",
            "e",
        )
    else:
        graph = _artifact_ref(
            "global_graph",
            "global-graph-artifact/v1",
            "framework/graph/artifact.json",
            "f",
        )
        layout = _artifact_ref(
            "disk_layout",
            "disk-layout-artifact/v1",
            "framework/layout/artifact.json",
            "a",
        )
    cache: dict[str, object] = {"mode": "zero", "requested_nodes": 0}
    if cache_mode == "fixed_bfs":
        cache = {
            "mode": "fixed_bfs",
            "requested_nodes": 10000,
            "construction": "medoid_bfs",
            "shuffle": False,
        }
    execution = {
        "execution_id": f"{role}-{cache_mode}",
        "dataset_artifact": dataset,
        "pq_artifact": shared_pq,
        "disk_layout_artifact": layout,
        "global_graph_artifact": graph,
        "query_set": query_set,
        "distance_metric": "l2",
        "K": 100,
        "L_search": [100, 200],
        "beamwidth": 2,
        "thread_count": 6,
        "cache": cache,
        "io_limit": 4294967295,
        "warmup_policy": "none",
        "query_order": "query_row_index_ascending",
        "timing_boundary": "cpp_main_search_disk_index_query_batch_v1",
        "storage_placement": "small-fixture",
        "repetition_count": 3,
        "schedule": "repetition_major_then_L_order",
        "entry_policy": "end_to_end",
        "environment": {
            "os": "linux",
            "storage": "local_ssd",
            "direct_io": "required_and_probed",
            "linux_aio": "required_and_probed",
            "remote_storage_allowed": False,
        },
        "implementation": {
            "implementation_id": FROZEN_RUNNER_IDENTITY["implementation_id"],
            "version": "1.0.0",
            "reproduction_mode": "instrumented",
        },
        "runner_build": {
            "project_repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
            "project_branch": "feature/search-evaluation",
            "project_commit": FROZEN_RUNNER_IDENTITY["project_commit"],
            "project_dirty": False,
            "binary_path": "bin/runner",
            "binary_sha256": runner_sha,
            "build_manifest_path": "manifests/runner-build.json",
            "build_manifest_sha256": "b" * 64,
        },
        "diskann_source_commit": FROZEN_RUNNER_IDENTITY["diskann_commit"],
    }
    return {
        "protocol_version": "1.0.0",
        "protocol_id": f"{role}-{cache_mode}",
        "status": "frozen",
        "search_execution": execution,
        "search_execution_checksum": "0" * 64,
        "evaluation": {
            "ground_truth": ground_truth,
            "evaluator": {
                "implementation_id": "cpp_main_search_metrics_evaluator_v1",
                "version": "1.0.0",
                "reproduction_mode": "instrumented",
            },
            "metrics": [
                "recall_at_10",
                "recall_at_100",
                "qps_k100",
                "mean_latency_us_k100",
                "p99_9_latency_us_k100",
                "mean_ios_k100",
            ],
            "quality_eligibility_policy": "complete_valid_ground_truth_identity_search_semantics_v1",
            "performance_eligibility_policy": "complete_valid_normative_environment_timing_parity_v1",
            "aggregation_policy": "median_eligible",
            "replacement_repetitions": False,
        },
        "canonicalization": "diskann-search-protocol-c14n-v1",
        "protocol_checksum": "0" * 64,
    }


def _write_feature005_observations(
    *,
    workspace: Path,
    protocol_path: Path,
    missed_rows: int,
    degrade_recall100: bool,
    omit_l: int | None = None,
    invalidate: tuple[int, int] | None = None,
) -> Path:
    protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
    execution = protocol["search_execution"]
    cache = execution["cache"]
    observation_root = workspace / "reports" / "observations" / protocol["protocol_id"]
    observation_root.mkdir(parents=True)
    environment = workspace / "reports" / "environment.json"
    if not environment.exists():
        environment.parent.mkdir(parents=True, exist_ok=True)
        environment.write_text('{"performance_admitted":false}', encoding="utf-8")
    full_results = list(range(100))
    recall100_threshold_results = list(range(99)) + [100]
    shifted_results = list(range(1, 101))
    for repetition in range(3):
        for l_search in execution["L_search"]:
            if l_search == omit_l:
                continue
            point_id = f"{protocol['protocol_id']}-L{l_search}-r{repetition}"
            document = {
                "format_version": "search-observation/v1",
                "observation_id": point_id,
                "run_id": f"{protocol['protocol_id']}-fixture",
                "generated_at": "2026-08-26T01:00:00Z",
                "implementation": execution["implementation"],
                "source_build": {
                    "diskann_repository": "https://github.com/microsoft/DiskANN.git",
                    "diskann_branch": "cpp_main",
                    "diskann_commit": FROZEN_RUNNER_IDENTITY["diskann_commit"],
                    "binary_path": execution["runner_build"]["binary_path"],
                    "binary_sha256": execution["runner_build"]["binary_sha256"],
                    "build_manifest_path": execution["runner_build"]["build_manifest_path"],
                    "build_manifest_sha256": execution["runner_build"]["build_manifest_sha256"],
                },
                "protocol": {
                    "path": protocol_path.relative_to(workspace).as_posix(),
                    "protocol_id": protocol["protocol_id"],
                    "protocol_checksum": protocol["protocol_checksum"],
                },
                "search_execution": {
                    "execution_id": execution["execution_id"],
                    "canonical_checksum": protocol["search_execution_checksum"],
                },
                "dataset_artifact": execution["dataset_artifact"],
                "global_graph_artifact": execution["global_graph_artifact"],
                "pq_artifact": execution["pq_artifact"],
                "disk_layout_artifact": execution["disk_layout_artifact"],
                "query_set_checksum": execution["query_set"]["canonical_checksum"],
                "raw_search_point": {
                    "point_id": point_id,
                    "L_search": l_search,
                    "K": 100,
                    "beamwidth": 2,
                    "cache": {
                        "mode": cache["mode"],
                        "requested_nodes": cache["requested_nodes"],
                        "effective_nodes": cache["requested_nodes"],
                        "primary_payload_bytes": 0,
                    },
                    "repetition_index": repetition,
                    "query_count": 10,
                    "completed_query_count": 10,
                    "batch_wall_clock_us": 10000,
                    "status": "SUCCEEDED",
                },
                "environment": {
                    "fingerprint_path": environment.relative_to(workspace).as_posix(),
                    "fingerprint_sha256": _sha256(environment),
                },
                "per_query": [
                    {
                        "query_row_index": row,
                        "source_query_id": f"opaque-{row}",
                        "result_ids": (
                            shifted_results
                            if row < missed_rows
                            else recall100_threshold_results
                            if degrade_recall100
                            else full_results
                        ),
                        "result_distances": [float(index) for index in range(100)],
                        "total_us": 10.0 + row,
                        "n_ios": 2 + row,
                    }
                    for row in range(10)
                ],
            }
            document["observation_checksum"] = search._canonical_digest(document)
            if invalidate == (l_search, repetition):
                document["observation_checksum"] = "0" * 64
            (observation_root / f"{point_id}.json").write_text(
                json.dumps(document, sort_keys=True, separators=(",", ":")),
                encoding="utf-8",
            )
    return observation_root


def _feature005_two_graph_fixture(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    *,
    framework_missed_rows: int = 1,
    omit_framework_l: int | None = None,
    invalidate_framework: tuple[int, int] | None = None,
) -> dict[str, object]:
    workspace = tmp_path / "quality-workspace"
    workspace.mkdir()
    runner = workspace / "bin" / "runner"
    runner.parent.mkdir(parents=True)
    runner.write_bytes(b"feature-005-two-graph-fixture-runner")
    actual_sha = search._sha256

    def fixture_sha(path: Path) -> str:
        if Path(path).resolve() == runner.resolve():
            return FROZEN_RUNNER_IDENTITY["runner_sha256"]
        return actual_sha(Path(path))

    monkeypatch.setattr(search, "_sha256", fixture_sha)
    dataset_metadata = workspace / "normal" / "dataset" / "artifact.json"
    dataset_metadata.parent.mkdir(parents=True)
    dataset_metadata.write_text(
        json.dumps(
            {
                "dataset": {
                    "dataset_id": "small-certification-dataset",
                    "dataset_checksum": "e" * 64,
                    "global_node_count": 256,
                    "vector_dimension": 4,
                    "scalar_type": "float32",
                    "distance_metric": "l2",
                }
            }
        ),
        encoding="utf-8",
    )
    query_reference_path = workspace / "inputs" / "query-reference.json"
    query_reference = _canonical_fixture_document(
        query_reference_path,
        {
            "reference_version": "1.0.0",
            "query_set_id": "shared-query",
            "logical_source": "tests/fixtures/search/selected_query_small.csv",
            "source": {
                "path": "inputs/query.csv",
                "format": "csv",
                "byte_length": 1,
                "sha256": "1" * 64,
            },
            "query_count": 10,
            "dimension": 4,
            "scalar_type": "float32",
            "query_identity": "query_row_index",
            "source_query_id_semantics": "opaque",
            "self_match_policy": "no_id_based_exclusion",
            "native_payload": {
                "path": "inputs/query.bin",
                "format": "diskann-float32-bin",
                "byte_length": 168,
                "sha256": "2" * 64,
            },
            "row_mapping": {
                "path": "inputs/query-map.json",
                "format": "json",
                "byte_length": 1,
                "sha256": "3" * 64,
            },
            "conversion_manifest_path": "manifests/query.json",
            "validation_evidence_path": "manifests/evidence/query.json",
        },
    )
    truth_path = workspace / "inputs" / "truth.bin"
    truth_ids = list(range(101)) * 10
    truth_distances = [float(index) for index in range(101)] * 10
    truth_path.write_bytes(
        struct.pack("<II", 10, 101)
        + struct.pack(f"<{len(truth_ids)}I", *truth_ids)
        + struct.pack(f"<{len(truth_distances)}f", *truth_distances)
    )
    ground_truth_reference_path = workspace / "inputs" / "truth-reference.json"
    ground_truth = _canonical_fixture_document(
        ground_truth_reference_path,
        {
            "reference_version": "1.0.0",
            "ground_truth_id": "shared-truth",
            "payload": {
                "path": truth_path.relative_to(workspace).as_posix(),
                "format": "diskann-truthset",
                "byte_length": truth_path.stat().st_size,
                "sha256": actual_sha(truth_path),
            },
            "query_count": 10,
            "width": 101,
            "id_space": "global",
            "distance_metric": "l2_squared_float32",
            "dataset_artifact": _artifact_ref(
                "dataset", "diskann-float32-dataset/v1", "dataset/artifact.json", "d"
            ),
            "query_set_checksum": query_reference["canonical_checksum"],
            "self_match_policy": "no_id_based_exclusion",
            "rank_100_tie_group_closed": True,
            "generation_manifest_path": "manifests/truth.json",
            "validation_evidence_path": "manifests/evidence/truth.json",
        },
    )
    protocol_paths: dict[str, dict[str, Path]] = {}
    report_paths: dict[str, dict[str, Path]] = {}
    manifest_paths: dict[str, dict[str, Path]] = {}
    for role in ("reference", "framework"):
        declarations = [
            _protocol_declaration(
                role=role,
                cache_mode=cache,
                runner_sha=FROZEN_RUNNER_IDENTITY["runner_sha256"],
                query_set=query_reference,
                ground_truth=ground_truth,
            )
            for cache in ("zero", "fixed_bfs")
        ]
        config = {
            "mode": "formal-search",
            "fixture_mode": True,
            "protocols": declarations,
            "common_admission": {
                "artifacts_valid": True,
                "inputs_compatible": True,
                "runner_path": str(runner),
                "runner_sha256": FROZEN_RUNNER_IDENTITY["runner_sha256"],
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
        config_path = workspace / f"{role}-formal-search.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        preflight = search.preflight(
            mode="formal-search", config_path=config_path, workspace=workspace
        )
        assert preflight["common_search_admission"] is True
        assert preflight["operations_started"]["search"] == 0
        role_protocols: dict[str, Path] = {}
        role_reports: dict[str, Path] = {}
        role_manifests: dict[str, Path] = {}
        for raw_path in preflight["protocol_paths"]:
            protocol_path = Path(raw_path)
            protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
            cache_mode = protocol["search_execution"]["cache"]["mode"]
            cache_key = "zero" if cache_mode == "zero" else "fixed"
            observations = _write_feature005_observations(
                workspace=workspace,
                protocol_path=protocol_path,
                missed_rows=0 if role == "reference" else framework_missed_rows,
                degrade_recall100=role == "framework",
                omit_l=omit_framework_l if role == "framework" else None,
                invalidate=invalidate_framework if role == "framework" else None,
            )
            evaluated = search.evaluate_observation_directory(
                protocol_path=protocol_path,
                observations_path=observations,
                workspace=workspace,
            )
            role_protocols[cache_key] = protocol_path
            role_reports[cache_key] = Path(evaluated["report_path"])
            role_manifests[cache_key] = Path(evaluated["build_manifest_path"])
        protocol_paths[role] = role_protocols
        report_paths[role] = role_reports
        manifest_paths[role] = role_manifests
    return {
        "workspace": workspace,
        "query_reference_path": query_reference_path,
        "query_reference": query_reference,
        "ground_truth_reference_path": ground_truth_reference_path,
        "ground_truth": ground_truth,
        "protocol_paths": protocol_paths,
        "report_paths": report_paths,
        "manifest_paths": manifest_paths,
    }


def _freeze_input(fixture: dict[str, object]) -> dict[str, object]:
    workspace = fixture["workspace"]
    protocol_paths = fixture["protocol_paths"]
    reference_zero = json.loads(protocol_paths["reference"]["zero"].read_text())
    framework_zero = json.loads(protocol_paths["framework"]["zero"].read_text())
    return {
        "workspace_root": str(workspace),
        "created_at": "2026-08-26T02:00:00Z",
        "artifacts": {
            "dataset": reference_zero["search_execution"]["dataset_artifact"],
            "shared_pq": reference_zero["search_execution"]["pq_artifact"],
            "reference_graph": reference_zero["search_execution"]["global_graph_artifact"],
            "reference_disk_layout": reference_zero["search_execution"]["disk_layout_artifact"],
            "framework_graph": framework_zero["search_execution"]["global_graph_artifact"],
            "framework_disk_layout": framework_zero["search_execution"]["disk_layout_artifact"],
        },
        "shared_inputs": {
            "query_set": _document_ref(
                workspace,
                fixture["query_reference_path"],
                canonical_checksum=fixture["query_reference"]["canonical_checksum"],
            ),
            "ground_truth": _document_ref(
                workspace,
                fixture["ground_truth_reference_path"],
                canonical_checksum=fixture["ground_truth"]["canonical_checksum"],
            ),
        },
        "protocols": {
            role: {
                cache: {"path": path.relative_to(workspace).as_posix()}
                for cache, path in protocol_paths[role].items()
            }
            for role in ("reference", "framework")
        },
    }


def _comparison_input(fixture: dict[str, object]) -> dict[str, object]:
    workspace = fixture["workspace"]
    return {
        "workspace_root": str(workspace),
        "generated_at": "2026-08-26T03:00:00Z",
        "evaluation_reports": {
            role: [
                paths[cache].relative_to(workspace).as_posix()
                for cache in ("zero", "fixed")
            ]
            for role, paths in fixture["report_paths"].items()
        },
    }


def test_two_graph_fixture_uses_feature005_preflight_and_evaluator_for_all_four_reports(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    workspace = fixture["workspace"]
    policy_path = workspace / "evidence" / "quality-policy.json"
    freeze_path = workspace / "freeze-input.json"
    freeze_path.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    frozen = fbc.freeze_quality_policy(
        freeze_path, policy_path, repository_root=REPOSITORY_ROOT
    )
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    assert frozen["protocol_count"] == 4
    assert len({
        policy["protocols"][role][cache]["protocol_id"]
        for role in ("reference", "framework")
        for cache in ("zero", "fixed")
    }) == 4
    assert policy["artifacts"]["reference_graph"]["format_version"] == "global-graph-artifact/v2"
    assert policy["artifacts"]["framework_graph"]["format_version"] == "global-graph-artifact/v1"
    assert policy["artifacts"]["shared_pq"] == _freeze_input(fixture)["artifacts"]["shared_pq"]

    compare_input = workspace / "compare-input.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    evidence_path = workspace / "evidence" / "quality-evidence.json"
    result = fbc.compare_quality(
        policy_path, compare_input, evidence_path, repository_root=REPOSITORY_ROOT
    )
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    assert result["result"] == "PASS"
    assert len(evidence["evaluation_reports"]["reference"]) == 2
    assert len(evidence["evaluation_reports"]["framework"]) == 2
    assert len(evidence["groups"]) == 4
    assert all(group["result"] == "PASS" for group in evidence["groups"])
    assert all(
        finding["shortfall_fraction"] == pytest.approx(0.01)
        for group in evidence["groups"]
        for finding in group["metric_findings"]
    )
    assert "performance" not in json.dumps(evidence)


@pytest.mark.parametrize(
    ("missed_rows", "expected"), [(0, "PASS"), (1, "PASS"), (2, "FAIL")]
)
def test_quality_threshold_is_pointwise_without_compensation(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    missed_rows: int,
    expected: str,
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, framework_missed_rows=missed_rows
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    output = workspace / "quality.json"
    result = fbc.compare_quality(
        policy, compare_input, output, repository_root=REPOSITORY_ROOT
    )
    assert result["result"] == expected


def test_quality_missing_group_and_identity_mismatch_are_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, omit_framework_l=200
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_document = _comparison_input(fixture)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(compare_document), encoding="utf-8")
    missing_output = workspace / "missing-quality.json"
    assert fbc.compare_quality(
        policy, compare_input, missing_output, repository_root=REPOSITORY_ROOT
    )["result"] == "INCONCLUSIVE"

    mismatched = copy.deepcopy(compare_document)
    mismatched["evaluation_reports"]["framework"][0] = (
        fixture["report_paths"]["reference"]["zero"]
        .relative_to(workspace)
        .as_posix()
    )
    mismatch_input = workspace / "mismatch.json"
    mismatch_input.write_text(json.dumps(mismatched), encoding="utf-8")
    mismatch_output = workspace / "mismatch-quality.json"
    assert fbc.compare_quality(
        policy, mismatch_input, mismatch_output, repository_root=REPOSITORY_ROOT
    )["result"] == "INCONCLUSIVE"


def test_quality_retains_exclusion_without_replacement_repetition(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, invalidate_framework=(200, 2)
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    output = workspace / "quality.json"
    assert fbc.compare_quality(
        policy, compare_input, output, repository_root=REPOSITORY_ROOT
    )["result"] == "PASS"
    evidence = json.loads(output.read_text(encoding="utf-8"))
    group = next(
        item
        for item in evidence["groups"]
        if item["cache_mode"] == "fixed_bfs" and item["L_search"] == 200
    )
    assert group["framework"]["attempted_count"] == 3
    assert group["framework"]["eligible_count"] == 2
    assert any(item["terminal_status"] == "EXCLUDED" for item in evidence["audit_exclusions"])


def _write_json_ref(workspace: Path, relative: str, value: dict[str, object]) -> dict[str, object]:
    path = workspace / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")
    return _document_ref(workspace, path)


def _decision_input(
    fixture: dict[str, object],
    quality_path: Path,
    *,
    structural: str = "PASS",
    parity: str = "PASS",
    mandatory: str = "PASS",
    quality: str = "PASS",
    parity_identity: dict[str, str] | None = None,
) -> dict[str, object]:
    workspace = fixture["workspace"]
    policy_path = workspace / "policy.json"
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    structural_policy = _write_json_ref(
        workspace, "evidence/structural-policy.json", {"policy": "frozen-feature-002"}
    )
    structural_report = _write_json_ref(
        workspace, "evidence/structural-report.json", {"gate": {"result": structural}}
    )
    runner_manifest = _write_json_ref(
        workspace,
        "evidence/runner-build.json",
        {"runner_identity": parity_identity or FROZEN_RUNNER_IDENTITY},
    )
    parity_refs = {
        cache: _write_json_ref(
            workspace,
            f"evidence/parity-{cache}.json",
            {"result": parity, "runner_identity": parity_identity or FROZEN_RUNNER_IDENTITY},
        )
        for cache in ("zero", "fixed")
    }
    admission_ref = _write_json_ref(
        workspace, "evidence/admission.json", {"result": mandatory}
    )
    validation_ref = _write_json_ref(
        workspace, "evidence/input-validation.json", {"result": "VALID"}
    )
    protocol_refs = {
        role: [
            {
                key: value
                for key, value in policy["protocols"][role][cache].items()
                if key in {"path", "byte_length", "sha256"}
            }
            for cache in ("zero", "fixed")
        ]
        for role in ("reference", "framework")
    }
    quality_evidence = json.loads(quality_path.read_text(encoding="utf-8"))
    report_refs = {
        role: [
            {key: value for key, value in item.items() if key in {"path", "byte_length", "sha256"}}
            for item in quality_evidence["evaluation_reports"][role]
        ]
        for role in ("reference", "framework")
    }
    observation_refs = {
        role: [
            {key: item[key] for key in ("path", "byte_length", "sha256")}
            for item in quality_evidence["observation_attempts"][role]
        ]
        for role in ("reference", "framework")
    }
    manifests = [
        _document_ref(workspace, path)
        for paths in fixture["manifest_paths"].values()
        for path in paths.values()
    ]
    return {
        "workspace_root": str(workspace),
        "generated_at": "2026-08-26T04:00:00Z",
        "structural_gate": {
            "policy": structural_policy,
            "report": structural_report,
            "result": structural,
        },
        "search_runner_parity": {
            "result": parity,
            "identity": parity_identity or FROZEN_RUNNER_IDENTITY,
            "runner_build_manifest": runner_manifest,
            "evidence": parity_refs,
            "reason": "fixture parity evidence",
        },
        "mandatory_admission": {
            "result": mandatory,
            "checks": [
                {
                    "check_id": "controlled-inputs",
                    "status": "PASS" if mandatory == "PASS" else ("FAIL" if mandatory == "FAIL" else "MISSING"),
                    "reason": "fixture admission",
                    "evidence": [admission_ref],
                }
            ],
        },
        "artifacts": policy["artifacts"],
        "shared_inputs": {
            key: {
                name: value
                for name, value in reference.items()
                if name in {"path", "byte_length", "sha256"}
            }
            for key, reference in policy["shared_inputs"].items()
        },
        "protocols": protocol_refs,
        "observations": observation_refs,
        "evaluation_reports": report_refs,
        "quality_parity": {
            "result": quality,
            "policy": _document_ref(workspace, policy_path),
            "evidence": _document_ref(workspace, quality_path),
            "reason": "fixture quality evidence",
        },
        "build_manifests": manifests,
        "validation_evidence": [validation_ref],
        "audit_exclusions": quality_evidence["audit_exclusions"],
    }


@pytest.mark.parametrize(
    ("structural", "parity", "mandatory", "quality", "expected"),
    [
        ("PASS", "PASS", "PASS", "PASS", "CERTIFIED"),
        ("FAIL", "PASS", "PASS", "PASS", "NOT_CERTIFIED"),
        ("NOT_REACHED", "FAIL", "FAIL", "FAIL", "INCONCLUSIVE"),
        ("PASS", "FAIL", "PASS", "PASS", "NOT_CERTIFIED"),
        ("PASS", "PASS", "FAIL", "PASS", "NOT_CERTIFIED"),
        ("PASS", "PASS", "PASS", "FAIL", "NOT_CERTIFIED"),
        ("PASS", "INCONCLUSIVE", "PASS", "PASS", "INCONCLUSIVE"),
    ],
)
def test_final_decision_precedence_and_exact_runner_parity_identity(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    structural: str,
    parity: str,
    mandatory: str,
    quality: str,
    expected: str,
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, framework_missed_rows=2 if quality == "FAIL" else 1
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy_path = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy_path, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    quality_path = workspace / "quality.json"
    fbc.compare_quality(
        policy_path, compare_input, quality_path, repository_root=REPOSITORY_ROOT
    )
    decision_input = workspace / "decision-input.json"
    decision_input.write_text(
        json.dumps(
            _decision_input(
                fixture,
                quality_path,
                structural=structural,
                parity=parity,
                mandatory=mandatory,
                quality=quality,
            )
        ),
        encoding="utf-8",
    )
    decision_path = workspace / "decision.json"
    assert fbc.decide(
        decision_input, decision_path, repository_root=REPOSITORY_ROOT
    )["decision"] == expected


def test_stale_runner_identity_is_inconclusive_not_reusable(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy_path = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy_path, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    quality_path = workspace / "quality.json"
    fbc.compare_quality(
        policy_path, compare_input, quality_path, repository_root=REPOSITORY_ROOT
    )
    stale = {**FROZEN_RUNNER_IDENTITY, "runner_sha256": "0" * 64}
    decision_input = workspace / "decision-input.json"
    decision_input.write_text(
        json.dumps(
            _decision_input(
                fixture,
                quality_path,
                parity="PASS",
                parity_identity=stale,
            )
        ),
        encoding="utf-8",
    )
    output = workspace / "decision.json"
    result = fbc.decide(decision_input, output, repository_root=REPOSITORY_ROOT)
    assert result["decision"] == "INCONCLUSIVE"
    assert json.loads(output.read_text(encoding="utf-8"))["search_runner_parity"]["result"] == "INCONCLUSIVE"
