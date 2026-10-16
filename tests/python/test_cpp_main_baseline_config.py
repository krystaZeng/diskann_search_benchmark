from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError

from diskann_harness.config import ConfigurationError, load_yaml_file, load_yaml_text
from diskann_harness import cpp_main_baseline
from diskann_harness.checksum import aggregate_checksum


ROOT = Path(__file__).resolve().parents[2]
PROFILE = (
    ROOT
    / "experiments"
    / "configs"
    / "cpp-main-upstream-baseline"
    / "baseline-run-profile.yaml"
)
SCHEMA = (
    ROOT
    / "contracts"
    / "baseline-v1"
    / "schemas"
    / "cpp-main-run-request.schema.json"
)
DATASET_SCHEMA = (
    ROOT
    / "contracts"
    / "baseline-v1"
    / "schemas"
    / "cpp-main-dataset-artifact.schema.json"
)
PINNED_COMMIT = "78256bbab4685e1774e78d331e081a153be26823"
DATASET_SHA256 = "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e"


def validator() -> Draft202012Validator:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def load_profile() -> dict[str, object]:
    profile = load_yaml_file(PROFILE)
    validator().validate(profile)
    return profile


def test_draft_profile_is_safe_schema_valid_and_pinned() -> None:
    profile = load_profile()
    assert profile["study_class"] == "baseline_validation"
    assert profile["reproduction_mode"] == "upstream_faithful"
    assert profile["replacement_scope"] == "none"
    assert profile["status"] == "draft"
    assert profile["source"] == {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": PINNED_COMMIT,
        "source_path": "upstream/diskann-cpp-main",
        "require_clean": True,
    }
    assert profile["dataset"]["checksum"] == DATASET_SHA256
    assert profile["profile"] == {
        "R": 16,
        "L": 32,
        "build_DRAM_budget_gib": 0.6,
        "num_threads": 6,
        "data_type": "float32",
        "metric": "l2",
        "filtered": False,
        "labels_enabled": False,
        "build_pq_bytes": 0,
        "disk_pq_bytes": 0,
        "opq_enabled": False,
    }
    assert profile["execution"] == {
        "workspace": "build/cpp-main-baseline/runs/sift1m-unified",
        "runner": (
            "build/cpp-main-baseline/src/baseline/cpp_main/"
            "cpp_main_baseline_runner"
        ),
        "report": "specs/002-cpp-main-upstream-baseline/unified-sharded-report.json",
    }


def test_profile_uses_exact_stage_identities_and_derived_values() -> None:
    profile = load_profile()
    assert profile["implementations"] == {
        "partition": f"cpp_main_{PINNED_COMMIT}_p0_callable_v1",
        "local_build": f"cpp_main_{PINNED_COMMIT}_b0_callable_v1",
        "merge": f"cpp_main_{PINNED_COMMIT}_m0_callable_v1",
    }
    assert profile["derived"] == {
        "p0_sample_probability": 0.256,
        "p0_local_degree": 10,
        "p0_replication_factor": 2,
        "b0_local_degree": 10,
        "b0_saturation": False,
        "b0_alpha": 1.2,
        "b0_max_occlusion_size": 750,
        "m0_degree_bound": 16,
    }


@pytest.mark.parametrize(
    ("path", "value"),
    [
        (("study_class",), "infrastructure_validation"),
        (("source", "commit"), "0" * 40),
        (("dataset", "checksum"), "0" * 64),
        (("dataset", "dataset_id"), "other-native-dataset"),
        (("dataset", "path"), "another/vectors.bin"),
        (("dataset", "global_node_count"), 96),
        (("implementations", "partition"), "fake_partition_v1"),
        (("status",), "frozen"),
        (("profile", "R"), 0),
    ],
)
def test_profile_rejects_identity_scope_and_parameter_drift(
    path: tuple[str, ...], value: object
) -> None:
    profile = load_profile()
    mutated = copy.deepcopy(profile)
    cursor = mutated
    for component in path[:-1]:
        cursor = cursor[component]
    cursor[path[-1]] = value
    with pytest.raises(ValidationError):
        validator().validate(mutated)


def test_profile_rejects_unplanned_fields() -> None:
    profile = load_profile()
    for field in (
        "parity_protocol",
        "repetitions",
        "performance_metrics",
        "automatic_retry",
        "validated_graph_baseline_registered",
    ):
        mutated = copy.deepcopy(profile)
        mutated[field] = True
        with pytest.raises(ValidationError):
            validator().validate(mutated)


def test_profile_loader_keeps_ordinary_safe_yaml_and_rejects_unsafe_yaml() -> None:
    assert load_yaml_text("status: draft\nvalues: [one, 2, false, null]\n") == {
        "status": "draft",
        "values": ["one", 2, False, None],
    }
    with pytest.raises(ConfigurationError):
        load_yaml_text("status: draft\nstatus: frozen\n")


def test_cpp_main_dataset_schema_accepts_only_reviewed_native_media() -> None:
    schema = json.loads(DATASET_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    dataset_validator = Draft202012Validator(schema)
    checksum = "1" * 64
    metadata = {
        "schema_version": "1.0.0",
        "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": "dataset",
        "format_version": "diskann-float32-dataset/v1",
        "dataset": {
            "dataset_id": "small-native-fixture",
            "dataset_checksum": checksum,
            "global_node_count": 1,
            "vector_dimension": 1,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "id_space": "none",
        "structural": {
            "global_node_count": 1,
            "vector_dimension": 1,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "capabilities": ["dataset.diskann_float32_bin/v1"],
        "source_artifacts": [],
        "payloads": [
            {
                "role": "vectors",
                "path": "vectors.bin",
                "media_type": (
                    "application/vnd.diskann.cpp-main.float32-bin;version=1"
                ),
                "byte_length": 12,
                "sha256": checksum,
            }
        ],
        "aggregate_checksum": "2" * 64,
    }
    dataset_validator.validate(metadata)
    metadata["payloads"][0]["media_type"] = "application/octet-stream"
    with pytest.raises(ValidationError):
        dataset_validator.validate(metadata)


def test_phase3_profile_normalization_uses_exact_stage_parameters() -> None:
    profile = load_profile()
    normalized = cpp_main_baseline.normalize_profile(profile)
    assert normalized["stage_parameters"] == {
        "partition": {
            "R": 16,
            "build_DRAM_budget_gib": 0.6,
            "replication_factor": 2,
            "sample_probability": 0.256,
        },
        "local_build": {
            "L": 32,
            "R": 16,
            "alpha": 1.2,
            "build_pq_bytes": 0,
            "max_occlusion_size": 750,
            "num_threads": 6,
            "saturation": False,
            "use_opq": False,
        },
        "merge": {"R": 16, "use_filters": False},
    }


def test_dataset_projection_matches_public_cpp_identity_contract() -> None:
    projection = cpp_main_baseline._dataset_identity_projection(
        DATASET_SHA256,
        {
            "global_node_count": 1_000_000,
            "vector_dimension": 128,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        {
            "role": "vectors",
            "byte_length": 512_000_008,
            "sha256": DATASET_SHA256,
        },
    )
    assert projection["schema_version"] == "1.0.0"
    assert projection["canonicalization"] == "diskann-artifact-c14n-v1"
    assert aggregate_checksum(projection) == (
        "4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e"
    )


def test_frozen_scanner_rejects_other_valid_native_bytes(tmp_path: Path) -> None:
    import struct
    source = tmp_path / "small.bin"
    source.write_bytes(struct.pack("<IIf", 1, 1, 1.0))
    expected = {"global_node_count": 1, "vector_dimension": 1, "byte_length": 12,
                "checksum": hashlib.sha256(source.read_bytes()).hexdigest()}
    assert cpp_main_baseline.native_dataset.scan_dataset(source, expected)["npts"] == 1
    with pytest.raises(cpp_main_baseline.BaselineHarnessError, match="SIFT1M Dataset SHA-256 differs"):
        cpp_main_baseline._scan_dataset(source, expected)


def test_frozen_scanner_delegates_without_relaxing_pinned_checksum(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    expected = load_profile()["dataset"]
    facts = {"sha256": DATASET_SHA256, "npts": 1000000, "dimension": 128}
    seen = []
    def scanned(source, declaration, *, label):
        seen.append((source, declaration, label))
        return facts
    monkeypatch.setattr(cpp_main_baseline.native_dataset, "scan_dataset", scanned)
    assert cpp_main_baseline._scan_dataset(tmp_path / "source", expected) is facts
    assert seen == [(tmp_path / "source", expected, "SIFT1M")]
    facts["sha256"] = "0" * 64
    with pytest.raises(cpp_main_baseline.BaselineHarnessError, match="SIFT1M Dataset SHA-256 differs"):
        cpp_main_baseline._scan_dataset(tmp_path / "source", expected)


def test_preflight_writes_one_immutable_nonexecuting_request(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace = tmp_path / "workspace"
    dataset_ref = {
        "metadata_path": "datasets/sift1m/artifact.json",
        "artifact_type": "dataset",
        "format_version": "diskann-float32-dataset/v1",
        "aggregate_checksum": "a" * 64,
    }

    monkeypatch.setattr(
        cpp_main_baseline,
        "_admit_source_build",
        lambda _root: {"commit": PINNED_COMMIT, "build_identity": {"os": "Linux"}},
    )
    monkeypatch.setattr(
        cpp_main_baseline,
        "_admit_dataset",
        lambda *_args, **_kwargs: {
            "artifact_ref": dataset_ref,
            "validation_evidence_path": "manifests/evidence/dataset.json",
            "source_sha256": DATASET_SHA256,
            "destination_sha256": DATASET_SHA256,
            "non_finite_count": 0,
        },
    )
    monkeypatch.setattr(
        cpp_main_baseline.subprocess,
        "Popen",
        lambda *_args, **_kwargs: pytest.fail("preflight must not start runner"),
    )
    response = cpp_main_baseline.preflight(
        PROFILE, workspace, repository_root=ROOT
    )
    request_path = Path(response["request_path"])
    assert request_path.is_file()
    request = json.loads(request_path.read_text(encoding="utf-8"))
    assert request["operation"] == "execute"
    assert request["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 0,
    }
    assert request["dataset"] == dataset_ref
    original = request_path.read_bytes()
    with pytest.raises(cpp_main_baseline.BaselineHarnessError):
        cpp_main_baseline.preflight(PROFILE, workspace, repository_root=ROOT)
    assert request_path.read_bytes() == original


def test_execute_consumes_request_once_and_writes_structural_report(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace = tmp_path / "workspace"
    request_path = workspace / "requests" / "unified-sharded-request.json"
    request_path.parent.mkdir(parents=True)
    profile_checksum = hashlib.sha256(PROFILE.read_bytes()).hexdigest()
    request = {
        "request_version": "1.0.0",
        "request_id": "unit-closure",
        "operation": "execute",
        "study_class": "baseline_validation",
        "workspace": {"root": str(workspace)},
        "profile_ref": (
            "experiments/configs/cpp-main-upstream-baseline/"
            "baseline-run-profile.yaml"
        ),
        "profile_checksum": profile_checksum,
        "profile_status": "draft",
        "source": {"commit": PINNED_COMMIT},
        "dataset_identity": {"checksum": DATASET_SHA256},
        "dataset": {
            "metadata_path": "datasets/sift1m/artifact.json",
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "a" * 64,
        },
        "implementations": load_profile()["implementations"],
        "stage_parameters": cpp_main_baseline.normalize_profile(load_profile())[
            "stage_parameters"
        ],
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
        "admission": {"sharded_path_required": True},
    }
    request_path.write_text(json.dumps(request), encoding="utf-8")
    reports = workspace / "reports"
    reports.mkdir(parents=True)
    evaluation_path = reports / "unit-evaluation.json"
    evaluation_path.write_text(
        json.dumps(
            {
                "evaluator": {
                    "implementation_id": "cpp_main_native_graph_integrity_evaluator",
                    "version": "1.0.0",
                },
                "findings": {"passed": True},
            }
        ),
        encoding="utf-8",
    )
    result = {
        "outcome": "SUCCEEDED",
        "stage_invocations": {"partition": 1, "local_build": 3, "merge": 1},
        "local_partition_ids": [0, 1, 2],
        "stage_results": [
            {"stage": "partition", "status": "SUCCEEDED"},
            {"stage": "local_build", "partition_id": 0, "status": "SUCCEEDED"},
            {"stage": "local_build", "partition_id": 1, "status": "SUCCEEDED"},
            {"stage": "local_build", "partition_id": 2, "status": "SUCCEEDED"},
            {"stage": "merge", "status": "SUCCEEDED"},
        ],
        "artifacts": {
            "dataset": request["dataset"],
            "partition": {"aggregate_checksum": "b" * 64},
            "local_graphs": [{"aggregate_checksum": value * 64} for value in "cde"],
            "global_graph": {"aggregate_checksum": "f" * 64},
        },
        "build_manifest_paths": ["manifests/build.json"],
        "validation_evidence_paths": ["manifests/evidence/global.json"],
        "evaluation_report_path": "reports/unit-evaluation.json",
        "partition_count": 3,
        "upstream_clean_before": True,
        "upstream_clean_after": True,
    }
    calls: list[tuple[Path, Path]] = []

    def invoke(_runner: Path, observed_request: Path, result_path: Path) -> dict[str, object]:
        calls.append((observed_request, result_path))
        return result

    monkeypatch.setattr(cpp_main_baseline, "_invoke_runner", invoke)
    report_path = tmp_path / "unified-sharded-report.json"
    request_bytes = request_path.read_bytes()
    report = cpp_main_baseline.execute(
        request_path,
        workspace,
        tmp_path / "runner",
        repository_root=ROOT,
        report_path=report_path,
    )
    assert len(calls) == 1
    assert request_path.read_bytes() == request_bytes
    assert report["run_class"] == "unified_sharded_closure"
    assert report["formal_graph_parity"] is False
    assert report["validated_graph_baseline_registered"] is False
    assert report["profile_status"] == "draft"
    assert report["structural_validation"] == "PASS"
    assert report["partition_count"] == 3
    assert report["evaluation_report_ref"] == "reports/unit-evaluation.json"


def test_execute_retains_a_failure_report_without_publishing_success(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace = tmp_path / "workspace"
    request_path = workspace / "requests" / "unified-sharded-request.json"
    request_path.parent.mkdir(parents=True)
    request = {
        "request_version": "1.0.0",
        "request_id": "unit-failed-closure",
        "operation": "execute",
        "study_class": "baseline_validation",
        "workspace": {"root": str(workspace)},
        "profile_ref": cpp_main_baseline.PROFILE_REF,
        "profile_checksum": hashlib.sha256(PROFILE.read_bytes()).hexdigest(),
        "profile_status": "draft",
        "source": {"commit": PINNED_COMMIT, "clean": True},
        "dataset_identity": {"checksum": DATASET_SHA256},
        "dataset": {
            "metadata_path": "datasets/sift1m/artifact.json",
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "a" * 64,
        },
        "dataset_validation_evidence_path": "manifests/evidence/dataset.json",
        "implementations": load_profile()["implementations"],
        "stage_parameters": cpp_main_baseline.normalize_profile(load_profile())[
            "stage_parameters"
        ],
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
        "admission": {"sharded_path_required": True},
    }
    request_path.write_text(json.dumps(request), encoding="utf-8")
    monkeypatch.setattr(
        cpp_main_baseline,
        "_invoke_runner",
        lambda *_args: {
            "outcome": "FAILED",
            "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
            "failure": {"category": "CHECKSUM_MISMATCH", "reason": "rejected"},
        },
    )
    report_path = tmp_path / "unified-sharded-report.json"
    request_bytes = request_path.read_bytes()
    with pytest.raises(cpp_main_baseline.BaselineHarnessError):
        cpp_main_baseline.execute(
            request_path,
            workspace,
            tmp_path / "runner",
            repository_root=ROOT,
            report_path=report_path,
        )
    report = json.loads(report_path.read_text(encoding="utf-8"))
    assert request_path.read_bytes() == request_bytes
    assert report["structural_validation"] == "FAIL"
    assert report["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 0,
    }
    assert "evaluation_report_ref" not in report
    assert report["formal_graph_parity"] is False
    assert report["validated_graph_baseline_registered"] is False


def test_execute_cli_requires_request_and_rejects_config() -> None:
    parser = cpp_main_baseline.build_argument_parser()
    with pytest.raises(SystemExit):
        parser.parse_args(
            [
                "execute",
                "--config",
                str(PROFILE),
                "--workspace",
                "build/cpp-main-baseline/runs/sift1m-unified",
                "--runner",
                "runner",
            ]
        )
