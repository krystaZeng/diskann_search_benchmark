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

