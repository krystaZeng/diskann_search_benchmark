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
FEATURE_006_SCHEMAS = (
    REPOSITORY_ROOT
    / "specs"
    / "006-framework-baseline-certification"
    / "contracts"
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


def _feature_006_validator(name: str) -> Draft202012Validator:
    schemas = _load_schemas(RUNTIME_SCHEMAS)
    feature_schema = json.loads((FEATURE_006_SCHEMAS / name).read_text(encoding="utf-8"))
    registry = _registry({**schemas, name: feature_schema})
    return Draft202012Validator(feature_schema, registry=registry)


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


def test_feature_006_design_schemas_are_strict_draft_2020_12() -> None:
    names = {
        "global-graph-artifact-v2.schema.json",
        "native-reference-build-request.schema.json",
        "quality-parity-policy.schema.json",
        "quality-parity-evidence.schema.json",
        "framework-baseline-certification-decision.schema.json",
    }
    assert {path.name for path in FEATURE_006_SCHEMAS.glob("*.schema.json")} == names
    for name in names:
        schema = json.loads((FEATURE_006_SCHEMAS / name).read_text(encoding="utf-8"))
        assert schema["$schema"] == "https://json-schema.org/draft/2020-12/schema"
        assert schema["additionalProperties"] is False
        Draft202012Validator.check_schema(schema)


def test_global_graph_v2_schema_is_additive_and_direct_native_only() -> None:
    document = _artifact_base("global_graph", "global-graph-artifact/v2", "global")
    document["structural"] = {
        "lineage_mode": "direct_native_final",
        "global_node_count": 6,
        "declared_max_degree": 3,
        "observed_max_degree": 2,
        "partition_count": 2,
        "medoid_count": 2,
    }
    document["capabilities"] = [
        "graph.global_adjacency/v1",
        "graph.medoids/v1",
    ]
    document["source_artifacts"] = [
        {
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "d" * 64,
        }
    ]
    document["payloads"] = [
        _search_payload(
            "global_graph",
            "application/vnd.diskann.cpp-main.global-graph;version=1",
        ),
        _search_payload(
            "medoids",
            "application/vnd.diskann.cpp-main.uint32-medoids;version=1",
        ),
    ]
    validator = _feature_006_validator("global-graph-artifact-v2.schema.json")
    validator.validate(document)

    mutations = []
    wrong_version = json.loads(json.dumps(document))
    wrong_version["format_version"] = "global-graph-artifact/v1"
    mutations.append(wrong_version)
    wrong_lineage = json.loads(json.dumps(document))
    wrong_lineage["structural"]["lineage_mode"] = "partition_local_merge"
    mutations.append(wrong_lineage)
    missing_source = json.loads(json.dumps(document))
    missing_source["source_artifacts"] = []
    mutations.append(missing_source)
    wrong_source = json.loads(json.dumps(document))
    wrong_source["source_artifacts"][0]["artifact_type"] = "partition"
    mutations.append(wrong_source)
    wrong_role = json.loads(json.dumps(document))
    wrong_role["payloads"][1]["role"] = "centroids"
    mutations.append(wrong_role)
    wrong_media = json.loads(json.dumps(document))
    wrong_media["payloads"][0]["media_type"] = "application/octet-stream"
    mutations.append(wrong_media)
    additional = json.loads(json.dumps(document))
    additional["unexpected"] = True
    mutations.append(additional)
    for invalid in mutations:
        with pytest.raises(ValidationError):
            validator.validate(invalid)


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


def _f006_artifact(kind: str, version: str, identity: str) -> dict:
    return {
        "metadata_path": f"normal/{identity}/artifact.json",
        "artifact_type": kind,
        "format_version": version,
        "aggregate_checksum": identity[0] * 64,
    }


def _f006_document(name: str, *, canonical: bool = False) -> dict:
    result = {"path": f"evidence/{name}.json", "byte_length": 1, "sha256": "a" * 64}
    if canonical:
        result["canonical_checksum"] = "b" * 64
    return result


def _f006_protocol(role: str, cache: str) -> dict:
    return {
        **_f006_document(f"{role}-{cache}-protocol"),
        "protocol_id": f"{role}-{cache}",
        "protocol_checksum": ("c" if cache == "zero" else "d") * 64,
        "search_execution_checksum": ("e" if role == "reference" else "f") * 64,
        "cache_mode": cache,
    }


def _f006_policy() -> dict:
    return {
        "policy_version": "quality-parity-policy/v1",
        "status": "frozen",
        "scope": "feature_006_sift1m_l2_static_quality_parity",
        "created_at": "2026-08-26T00:00:00Z",
        "roles": {
            "reference": "cpp_main_native_sharded_reference_v1",
            "framework": "cpp_main_p0_b0_m0_framework_candidate_v1",
        },
        "threshold": {
            "metrics": ["recall_at_10", "recall_at_100"],
            "source_report_scale": "percentage_0_to_100",
            "comparison_scale": "fraction_0_to_1",
            "maximum_shortfall": 0.01,
            "equal_or_higher_framework_passes": True,
        },
        "artifacts": {
            "dataset": _f006_artifact("dataset", "diskann-float32-dataset/v1", "a-dataset"),
            "shared_pq": _f006_artifact("pq", "pq-artifact/v1", "b-pq"),
            "reference_graph": _f006_artifact("global_graph", "global-graph-artifact/v2", "c-reference"),
            "reference_disk_layout": _f006_artifact("disk_layout", "disk-layout-artifact/v1", "d-reference-layout"),
            "framework_graph": _f006_artifact("global_graph", "global-graph-artifact/v1", "e-framework"),
            "framework_disk_layout": _f006_artifact("disk_layout", "disk-layout-artifact/v1", "f-framework-layout"),
        },
        "shared_inputs": {
            "query_set": _f006_document("query-set", canonical=True),
            "ground_truth": _f006_document("ground-truth", canonical=True),
        },
        "protocols": {
            role: {
                "zero": _f006_protocol(role, "zero"),
                "fixed": _f006_protocol(role, "fixed_bfs"),
            }
            for role in ("reference", "framework")
        },
        "controls": {
            "K": 100,
            "L_search": [100, 200],
            "beamwidth": 2,
            "thread_count": 6,
            "io_limit": 4294967295,
            "warmup_policy": "none",
            "query_order": "query_row_index_ascending",
            "timing_boundary": "cpp_main_search_disk_index_query_batch_v1",
            "repetition_count": 3,
            "schedule": "repetition_major_then_L_order",
            "entry_policy": "end_to_end",
            "cache_controls": {
                "zero": {"mode": "zero", "requested_nodes": 0},
                "fixed": {
                    "mode": "fixed_bfs",
                    "requested_nodes": 10000,
                    "construction": "medoid_bfs",
                    "shuffle": False,
                },
            },
            "control_projection_checksum": "1" * 64,
        },
        "evaluator": {
            "implementation_id": "cpp_main_search_metrics_evaluator_v1",
            "version": "1.0.0",
            "reproduction_mode": "instrumented",
        },
        "eligibility": {
            "quality_policy": "complete_valid_ground_truth_identity_search_semantics_v1",
            "aggregation": "median_eligible",
            "attempted_repetitions_per_point": 3,
            "minimum_eligible_for_existing_aggregate": 1,
            "replacement_repetitions": False,
        },
        "decision_rule": {
            "point_rule": "all_metrics_at_every_declared_cache_l",
            "cross_point_compensation": False,
            "threshold_violation": "FAIL",
            "missing_or_unevaluable_aggregate": "INCONCLUSIVE",
        },
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "policy_checksum": "2" * 64,
    }


def _f006_report(role: str, cache: str) -> dict:
    return {
        **_f006_document(f"{role}-{cache}-report"),
        "report_id": f"{role}-{cache}-report",
        "report_checksum": "3" * 64,
        "protocol_checksum": _f006_protocol(role, cache)["protocol_checksum"],
        "cache_mode": cache,
    }


def _f006_evidence(policy: dict) -> dict:
    metrics = {"recall_at_10_percent": 95.0, "recall_at_100_percent": 99.0}
    findings = [
        {
            "metric": metric,
            "reference_percent": percent,
            "framework_percent": percent,
            "reference_fraction": percent / 100,
            "framework_fraction": percent / 100,
            "shortfall_fraction": 0.0,
            "maximum_shortfall_fraction": 0.01,
            "result": "PASS",
        }
        for metric, percent in (("recall_at_10", 95.0), ("recall_at_100", 99.0))
    ]
    groups = []
    for cache in ("zero", "fixed_bfs"):
        for l_search in (100, 200):
            groups.append(
                {
                    "cache_mode": cache,
                    "L_search": l_search,
                    "reference": {"attempted_count": 3, "eligible_count": 3, "aggregation": "median_eligible", "metrics": metrics},
                    "framework": {"attempted_count": 3, "eligible_count": 3, "aggregation": "median_eligible", "metrics": metrics},
                    "metric_findings": findings,
                    "result": "PASS",
                    "reason": "both frozen Recall metrics pass",
                }
            )
    return {
        "evidence_version": "quality-parity-evidence/v1",
        "generated_at": "2026-08-26T00:01:00Z",
        "policy": _f006_document("quality-policy", canonical=True),
        "roles": policy["roles"],
        "artifacts": policy["artifacts"],
        "shared_inputs": policy["shared_inputs"],
        "evaluation_reports": {
            role: [_f006_report(role, "zero"), _f006_report(role, "fixed_bfs")]
            for role in ("reference", "framework")
        },
        "observation_attempts": {"reference": [], "framework": []},
        "groups": groups,
        "result": "PASS",
        "audit_exclusions": [
            {"identity": "excluded-fixture", "path": "evidence/excluded.json", "sha256": "4" * 64, "terminal_status": "EXCLUDED", "reason": "fixture audit exclusion"}
        ],
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "evidence_checksum": "5" * 64,
    }


def _f006_decision(policy: dict) -> dict:
    doc = _f006_document
    return {
        "decision_version": "framework-baseline-certification-decision/v1",
        "generated_at": "2026-08-26T00:02:00Z",
        "scope": "sift1m_l2_static_unfiltered_cpp_main_p0_b0_m0_only",
        "roles": policy["roles"],
        "decision": "CERTIFIED",
        "structural_gate": {"policy": doc("structural-policy"), "report": doc("structural-report"), "result": "PASS"},
        "search_runner_parity": {
            "result": "PASS",
            "identity": {
                "project_commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
                "runner_sha256": "97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856",
                "implementation_id": "cpp_main_pqflash_search_instrumented_v1",
                "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823",
            },
            "runner_build_manifest": doc("runner-build"),
            "evidence": {"zero": doc("runner-zero"), "fixed": doc("runner-fixed")},
            "reason": "exact current identity parity passed",
        },
        "mandatory_admission": {"result": "PASS", "checks": [{"check_id": "controlled-inputs", "status": "PASS", "reason": "all exact inputs agree", "evidence": [doc("admission")]}]},
        "artifacts": policy["artifacts"],
        "shared_inputs": {"query_set": doc("query-set"), "ground_truth": doc("ground-truth")},
        "protocols": {role: [doc(f"{role}-zero-protocol"), doc(f"{role}-fixed-protocol")] for role in ("reference", "framework")},
        "observations": {role: [doc(f"{role}-observation")] for role in ("reference", "framework")},
        "evaluation_reports": {role: [doc(f"{role}-zero-report"), doc(f"{role}-fixed-report")] for role in ("reference", "framework")},
        "quality_parity": {"result": "PASS", "policy": doc("quality-policy"), "evidence": doc("quality-evidence"), "reason": "all points pass"},
        "build_manifests": [doc("reference-build"), doc("framework-build")],
        "validation_evidence": [doc("reference-validation"), doc("framework-validation")],
        "audit_exclusions": [{"identity": "audit", "path": "evidence/audit.json", "sha256": "6" * 64, "terminal_status": "EXCLUDED", "reason": "retained audit evidence"}],
        "rationale": [{"reason_code": "all-mandatory-pass", "message": "all frozen mandatory evidence passed"}],
        "claims": {"fixed_scope_framework_certified": True, "graph_byte_or_adjacency_parity": False, "complete_native_framework_equivalence": False, "framework_wide_certification": False, "performance_certification": False, "paper_feature_certification": False},
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "decision_checksum": "7" * 64,
    }


def test_feature006_quality_and_decision_schemas_are_strict_and_local() -> None:
    policy = _f006_policy()
    evidence = _f006_evidence(policy)
    decision = _f006_decision(policy)
    documents = {
        "quality-parity-policy.schema.json": policy,
        "quality-parity-evidence.schema.json": evidence,
        "framework-baseline-certification-decision.schema.json": decision,
    }
    for name, document in documents.items():
        validator = _feature_006_validator(name)
        validator.validate(document)
        unknown = json.loads(json.dumps(document))
        unknown["certification_id"] = "forbidden"
        with pytest.raises(ValidationError):
            validator.validate(unknown)

    stale = json.loads(json.dumps(decision))
    stale["search_runner_parity"]["identity"]["runner_sha256"] = "0" * 64
    with pytest.raises(ValidationError):
        _feature_006_validator(
            "framework-baseline-certification-decision.schema.json"
        ).validate(stale)
    missing_rationale = json.loads(json.dumps(decision))
    del missing_rationale["rationale"]
    with pytest.raises(ValidationError):
        _feature_006_validator(
            "framework-baseline-certification-decision.schema.json"
        ).validate(missing_rationale)

    sources = json.loads((REPOSITORY_ROOT / "contracts/v1/SOURCES.json").read_text())
    serialized = json.dumps(sources)
    assert "quality-parity" not in serialized
    assert "certification-decision" not in serialized

    public_graph = _artifact_base(
        "global_graph", "global-graph-artifact/v2", "global"
    )
    public_graph["structural"] = {
        "lineage_mode": "direct_native_final",
        "global_node_count": 6,
        "declared_max_degree": 3,
        "observed_max_degree": 2,
        "partition_count": 2,
        "medoid_count": 2,
    }
    public_graph["capabilities"] = [
        "graph.global_adjacency/v1",
        "graph.medoids/v1",
    ]
    public_graph["source_artifacts"] = [
        {
            "artifact_type": "dataset",
            "format_version": "diskann-float32-dataset/v1",
            "aggregate_checksum": "d" * 64,
        }
    ]
    public_graph["payloads"] = [
        _search_payload(
            "global_graph",
            "application/vnd.diskann.cpp-main.global-graph;version=1",
        ),
        _search_payload(
            "medoids",
            "application/vnd.diskann.cpp-main.uint32-medoids;version=1",
        ),
    ]
    public_graph["certification_status"] = "CERTIFIED"
    with pytest.raises(ValidationError):
        _feature_006_validator("global-graph-artifact-v2.schema.json").validate(
            public_graph
        )
