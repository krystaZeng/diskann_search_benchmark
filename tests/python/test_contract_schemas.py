from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DESIGN_SCHEMAS = (
    REPOSITORY_ROOT
    / "specs"
    / "001-modular-cpp-diskann-pipeline"
    / "contracts"
    / "schemas"
)
RUNTIME_SCHEMAS = REPOSITORY_ROOT / "contracts" / "v1" / "schemas"


def _load_schemas(directory: Path) -> dict[str, dict]:
    return {
        path.name: json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(directory.glob("*.json"))
    }


def _registry(schemas: dict[str, dict]) -> Registry:
    return Registry().with_resources(
        (schema["$id"], Resource.from_contents(schema))
        for schema in schemas.values()
        if "$id" in schema
    )


def _validator(name: str) -> Draft202012Validator:
    schemas = _load_schemas(RUNTIME_SCHEMAS)
    return Draft202012Validator(schemas[name], registry=_registry(schemas))


def test_every_schema_is_draft_2020_12_and_metaschema_valid() -> None:
    for name, schema in _load_schemas(RUNTIME_SCHEMAS).items():
        assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        Draft202012Validator.check_schema(schema)


def test_design_and_runtime_schema_bytes_are_identical() -> None:
    design = sorted(path.name for path in DESIGN_SCHEMAS.glob("*.json"))
    runtime = sorted(path.name for path in RUNTIME_SCHEMAS.glob("*.json"))
    assert runtime == design
    for name in design:
        assert (RUNTIME_SCHEMAS / name).read_bytes() == (DESIGN_SCHEMAS / name).read_bytes()


def test_local_ref_and_strict_unknown_field_rejection() -> None:
    valid = {
        "artifact_type": "dataset",
        "format_version": "fake-dataset-json/v1",
        "aggregate_checksum": "a" * 64,
        "metadata_path": "artifacts/dataset/metadata.json",
    }
    validator = _validator("artifact-ref.schema.json")
    validator.validate(valid)
    with pytest.raises(ValidationError):
        validator.validate({**valid, "build_manifest_path": "manifests/run.json"})


def test_base_manifest_schema_accepts_complete_provenance() -> None:
    manifest = {
        "manifest_version": "1.0.0",
        "run_id": "run-phase1",
        "stage": "build",
        "implementation": {
            "implementation_id": "phase1_contracts",
            "implementation_version": "1.0.0",
        },
        "source": {
            "repository": "local-repository",
            "branch": "master",
            "commit": "0" * 40,
            "dirty_worktree": False,
        },
        "toolchain": {
            "os": "test-os",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": "test",
            "cmake_version": "4.3.3",
            "generator": "test",
            "build_type": "Debug",
            "cmake_definitions": [],
            "compile_commands": [],
            "link_commands": [],
        },
        "dataset": {
            "dataset_id": "fake-six-node-v1",
            "dataset_checksum": "a" * 64,
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "parameters": {"nullable": None, "finite": 1.5},
        "resource_budget": {"memory_budget_bytes": 1024, "thread_budget": 1},
        "input_artifacts": [],
        "output_artifacts": [],
        "status": "SUCCEEDED",
        "started_at": "2026-08-05T00:00:00Z",
        "ended_at": "2026-08-05T00:00:01Z",
    }
    _validator("build-manifest.schema.json").validate(manifest)


def _artifact_base(artifact_type: str, format_version: str, id_space: str) -> dict:
    return {
        "schema_version": "1.0.0",
        "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": artifact_type,
        "format_version": format_version,
        "dataset": {
            "dataset_id": "dataset",
            "dataset_checksum": "a" * 64,
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "id_space": id_space,
        "capabilities": [],
        "source_artifacts": [],
        "payloads": [],
        "aggregate_checksum": "b" * 64,
    }


def _payload(role: str) -> dict:
    return {
        "role": role,
        "path": f"{role}.bin",
        "media_type": "application/octet-stream",
        "byte_length": 1,
        "sha256": "c" * 64,
    }


def test_partition_schema_accepts_optional_vectors_and_centroids() -> None:
    document = _artifact_base("partition", "partition-artifact/v1", "local")
    document["capabilities"] = ["partition.complete_local_to_global_map/v1"]
    document["source_artifacts"] = [
        {
            "artifact_type": "dataset",
            "format_version": "dataset/v1",
            "aggregate_checksum": "d" * 64,
        }
    ]
    document["structural"] = {
        "partition_count": 1,
        "partitions": [
            {
                "partition_id": 0,
                "local_node_count": 1,
                "local_to_global_map_role": "partition/0/map",
            }
        ],
    }
    document["payloads"] = [_payload("partition/0/map")]
    validator = _validator("partition-artifact.schema.json")
    validator.validate(document)

    with_vectors = json.loads(json.dumps(document))
    with_vectors["structural"]["partitions"][0]["shard_vectors_role"] = (
        "partition/0/vectors"
    )
    with_vectors["payloads"] = [
        _payload("partition/0/vectors"),
        _payload("partition/0/map"),
    ]
    validator.validate(with_vectors)

    with_centroids = json.loads(json.dumps(document))
    with_centroids["capabilities"] = [
        "partition.centroids/v1",
        "partition.complete_local_to_global_map/v1",
    ]
    with_centroids["payloads"].append(_payload("centroids"))
    validator.validate(with_centroids)

    missing_map = json.loads(json.dumps(document))
    del missing_map["structural"]["partitions"][0]["local_to_global_map_role"]
    with pytest.raises(ValidationError):
        validator.validate(missing_map)


def test_global_graph_schema_accepts_graph_with_optional_medoids() -> None:
    document = _artifact_base(
        "global_graph", "global-graph-artifact/v1", "global"
    )
    source_partition = {
        "artifact_type": "partition",
        "format_version": "partition-artifact/v1",
        "aggregate_checksum": "d" * 64,
    }
    source_local = {
        "artifact_type": "local_graph",
        "format_version": "local-graph-artifact/v1",
        "aggregate_checksum": "e" * 64,
    }
    document["structural"] = {
        "global_node_count": 6,
        "declared_max_degree": 3,
        "observed_max_degree": 2,
        "source_partition_artifact": source_partition,
        "source_local_graph_artifacts": [source_local],
    }
    document["capabilities"] = ["graph.global_adjacency/v1"]
    document["source_artifacts"] = [source_partition, source_local]
    document["payloads"] = [_payload("global_graph")]
    validator = _validator("global-graph-artifact.schema.json")
    validator.validate(document)

    document["capabilities"].append("graph.medoids/v1")
    document["payloads"].append(_payload("medoids"))
    validator.validate(document)
