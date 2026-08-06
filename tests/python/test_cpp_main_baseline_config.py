from __future__ import annotations

import copy
import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError

from diskann_harness.config import ConfigurationError, load_yaml_file, load_yaml_text


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
