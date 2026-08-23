from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNTIME_SCHEMAS = REPOSITORY_ROOT / "contracts" / "v1" / "schemas"
FEATURE_005_SCHEMAS = (
    REPOSITORY_ROOT / "specs" / "005-diskann-search-evaluation" / "contracts"
)


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
    inventory = json.loads(
        (REPOSITORY_ROOT / "contracts/v1/SOURCES.json").read_text(
            encoding="utf-8"
        )
    )
    schema_entries = [
        entry
        for entry in inventory["entries"]
        if entry["target"].startswith("contracts/v1/schemas/")
    ]
    runtime = sorted(path.name for path in RUNTIME_SCHEMAS.glob("*.json"))
    assert runtime == sorted(Path(entry["target"]).name for entry in schema_entries)
    for entry in schema_entries:
        assert (REPOSITORY_ROOT / entry["target"]).read_bytes() == (
            REPOSITORY_ROOT / entry["source"]
        ).read_bytes()


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


def test_feature_005_local_schemas_are_valid_but_not_public_artifacts() -> None:
    for name in (
        "search-protocol.schema.json",
        "search-observation.schema.json",
        "search-evaluation-report.schema.json",
    ):
        schema = json.loads((FEATURE_005_SCHEMAS / name).read_text(encoding="utf-8"))
        Draft202012Validator.check_schema(schema)
        assert name not in {path.name for path in RUNTIME_SCHEMAS.glob("*.json")}


def test_build_manifest_document_stage_accepts_search() -> None:
    schema = json.loads(
        (RUNTIME_SCHEMAS / "build-manifest.schema.json").read_text(encoding="utf-8")
    )
    assert "search" in schema["properties"]["stage"]["enum"]


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


def _search_payload(role: str, media_type: str) -> dict:
    value = _payload(role)
    value["media_type"] = media_type
    return value


def test_pq_artifact_schema_is_strict_and_dataset_derived() -> None:
    document = _artifact_base("pq", "pq-artifact/v1", "none")
    document["structural"] = {
        "point_count": 6,
        "dimension": 2,
        "scalar_type": "float32",
        "distance_metric": "l2",
        "centroid_count": 256,
        "pq_chunks": 2,
        "opq": False,
        "compressed_code_scalar": "uint8",
    }
    document["capabilities"] = [
        "search.pq_in_memory/v1",
        "search.pqflash_loadable/v1",
    ]
    document["source_artifacts"] = [
        {
            "artifact_type": "dataset",
            "format_version": "fake-dataset-json/v1",
            "aggregate_checksum": "d" * 64,
        }
    ]
    document["payloads"] = [
        _search_payload(
            "pq_pivots",
            "application/vnd.diskann.pq-pivots+binary;version=1",
        ),
        _search_payload(
            "pq_compressed_vectors",
            "application/vnd.diskann.pq-codes+binary;version=1",
        ),
    ]
    validator = _validator("pq-artifact.schema.json")
    validator.validate(document)

    wrong_source = json.loads(json.dumps(document))
    wrong_source["source_artifacts"][0]["artifact_type"] = "global_graph"
    with pytest.raises(ValidationError):
        validator.validate(wrong_source)

    additional = json.loads(json.dumps(document))
    additional["unexpected"] = True
    with pytest.raises(ValidationError):
        validator.validate(additional)


def test_disk_layout_centroid_policy_controls_payload_cardinality() -> None:
    document = _artifact_base(
        "disk_layout", "disk-layout-artifact/v1", "global"
    )
    document["structural"] = {
        "global_node_count": 6,
        "dimension": 2,
        "scalar_type": "float32",
        "distance_metric": "l2",
        "disk_vector_encoding": "float32_full_precision",
        "pq_disk_bytes": 0,
        "reorder": False,
        "declared_max_degree": 3,
        "medoid_count": 1,
        "entry_policy": "end_to_end",
        "centroid_policy": "medoid_vector_fallback",
        "frozen_point_count": 0,
    }
    document["capabilities"] = [
        "graph.global_ids/v1",
        "search.disk_layout/v1",
        "search.disk_vectors_full_precision/v1",
        "search.entry_end_to_end/v1",
        "search.pqflash_loadable/v1",
    ]
    document["source_artifacts"] = [
        {
            "artifact_type": "dataset",
            "format_version": "fake-dataset-json/v1",
            "aggregate_checksum": "d" * 64,
        },
        {
            "artifact_type": "global_graph",
            "format_version": "global-graph-artifact/v1",
            "aggregate_checksum": "e" * 64,
        },
    ]
    document["payloads"] = [
        _search_payload(
            "disk_index",
            "application/vnd.diskann.pqflash-disk-index+binary;version=1",
        ),
        _search_payload(
            "medoids", "application/vnd.diskann.uint32-matrix+binary;version=1"
        ),
    ]
    validator = _validator("disk-layout-artifact.schema.json")
    validator.validate(document)

    missing_centroids = json.loads(json.dumps(document))
    missing_centroids["structural"]["centroid_policy"] = "declared_centroids"
    with pytest.raises(ValidationError):
        validator.validate(missing_centroids)

    forbidden_centroids = json.loads(json.dumps(document))
    forbidden_centroids["payloads"].append(
        _search_payload(
            "centroids", "application/vnd.diskann.float32-matrix+binary;version=1"
        )
    )
    with pytest.raises(ValidationError):
        validator.validate(forbidden_centroids)


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
