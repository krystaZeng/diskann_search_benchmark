"""Bounded Feature 006 baseline-certification orchestration.

This module owns only the six operations specified by Feature 006.  It is not
a public validation, graph, search, or certification framework.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

from . import cpp_main_structural_parity as structural_parity
from . import diskann_search_evaluation as search_evaluation
from .manifests import ImmutableWriteError, write_immutable_json
from .workspace import WorkspaceError, resolve_confined_path


SCHEMA_FILENAMES = {
    "global-graph-artifact-v2.schema.json",
    "native-reference-build-request.schema.json",
    "quality-parity-policy.schema.json",
    "quality-parity-evidence.schema.json",
    "framework-baseline-certification-decision.schema.json",
}


class FeatureCertificationError(RuntimeError):
    """Fail-closed Feature 006 input, evidence, or persistence error."""


PINNED_DISKANN = {
    "repository": "https://github.com/microsoft/DiskANN.git",
    "branch": "cpp_main",
    "commit": "78256bbab4685e1774e78d331e081a153be26823",
}

FROZEN_RUNNER_PARITY_IDENTITY = {
    "project_commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
    "runner_sha256": "97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856",
    "implementation_id": "cpp_main_pqflash_search_instrumented_v1",
    "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823",
}

CERTIFICATION_ROLES = {
    "reference": "cpp_main_native_sharded_reference_v1",
    "framework": "cpp_main_p0_b0_m0_framework_candidate_v1",
}


def _schema_directory(repository_root: str | Path) -> Path:
    return (
        Path(repository_root)
        / "specs"
        / "006-framework-baseline-certification"
        / "contracts"
    )


def _read_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeError) as error:
        raise FeatureCertificationError(f"cannot read JSON document {path}: {error}") from error
    if type(value) is not dict:
        raise FeatureCertificationError(f"JSON document must be an object: {path}")
    return value


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise FeatureCertificationError(f"cannot hash file {path}: {error}") from error
    return digest.hexdigest()


def _runtime_validator(
    repository_root: Path, name: str
) -> Draft202012Validator:
    schemas = {
        item.name: _read_object(item)
        for item in sorted(
            (repository_root / "contracts" / "v1" / "schemas").glob("*.json")
        )
    }
    if name not in schemas:
        raise FeatureCertificationError(f"runtime schema is missing: {name}")
    resources = [
        (schema["$id"], Resource.from_contents(schema))
        for schema in schemas.values()
        if "$id" in schema
    ]
    schema = schemas[name]
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema, registry=Registry().with_resources(resources))


def _validate_with(
    validator: Draft202012Validator, document: dict[str, Any], label: str
) -> None:
    errors = sorted(validator.iter_errors(document), key=lambda item: list(item.absolute_path))
    if errors:
        raise FeatureCertificationError(f"{label} is invalid: {errors[0].message}")


def _run_git(repository: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", "-c", f"safe.directory={repository}", "-C", str(repository), *arguments],
        check=False,
        capture_output=True,
        text=True,
        shell=False,
    )
    if completed.returncode != 0:
        raise FeatureCertificationError(
            f"source identity command failed: {' '.join(arguments)}"
        )
    return completed.stdout.strip()


def _assert_source_identities(
    request: dict[str, Any], repository_root: Path
) -> None:
    source = request["source"]
    upstream = repository_root / source["source_path"]
    if (
        source["repository"] != PINNED_DISKANN["repository"]
        or source["branch"] != PINNED_DISKANN["branch"]
        or source["commit"] != PINNED_DISKANN["commit"]
        or source["dirty_worktree"] is not False
        or _run_git(upstream, "rev-parse", "HEAD") != source["commit"]
        or _run_git(upstream, "branch", "--show-current") != source["branch"]
        or _run_git(upstream, "status", "--porcelain=v1", "--untracked-files=all")
    ):
        raise FeatureCertificationError("fixed DiskANN source identity is invalid")
    owner = request["project_operation_owner"]
    if (
        _run_git(repository_root, "rev-parse", "HEAD") != owner["commit"]
        or _run_git(repository_root, "branch", "--show-current") != owner["branch"]
        or _run_git(
            repository_root, "status", "--porcelain=v1", "--untracked-files=no"
        )
        or owner["dirty_worktree"] is not False
    ):
        raise FeatureCertificationError("Project operation-owner identity is invalid")


def _confined_document_path(workspace: Path, reference: dict[str, Any]) -> Path:
    return resolve_feature_output(workspace, reference["path"])


def _verify_document_reference(workspace: Path, reference: dict[str, Any]) -> Path:
    path = _confined_document_path(workspace, reference)
    if (
        not path.is_file()
        or path.stat().st_size != reference["byte_length"]
        or _sha256_file(path) != reference["sha256"]
    ):
        raise FeatureCertificationError(f"document identity mismatch: {path}")
    return path


def _verify_binary_identity(workspace: Path, identity: dict[str, Any]) -> Path:
    path = Path(identity["path"])
    if (
        not path.is_absolute()
        or not path.is_file()
        or path.stat().st_size != identity["byte_length"]
        or _sha256_file(path) != identity["sha256"]
    ):
        raise FeatureCertificationError(f"binary identity mismatch: {path}")
    _verify_document_reference(workspace, identity["build_manifest"])
    return path


def _fresh_validate_dataset(
    request: dict[str, Any], repository_root: Path
) -> None:
    workspace = Path(request["workspace_root"]).resolve()
    reference = request["dataset_artifact"]
    metadata_path = resolve_feature_output(workspace / "normal", reference["metadata_path"])
    metadata = _read_object(metadata_path)
    _validate_with(
        _runtime_validator(repository_root, "dataset-artifact.schema.json"),
        metadata,
        "DatasetArtifact",
    )
    if (
        reference["artifact_type"] != "dataset"
        or metadata["artifact_type"] != reference["artifact_type"]
        or metadata["format_version"] != reference["format_version"]
        or metadata["aggregate_checksum"] != reference["aggregate_checksum"]
    ):
        raise FeatureCertificationError("DatasetArtifact identity mismatch")
    for record in metadata["payloads"]:
        payload = resolve_feature_output(metadata_path.parent, record["path"])
        if (
            not payload.is_file()
            or payload.stat().st_size != record["byte_length"]
            or _sha256_file(payload) != record["sha256"]
        ):
            raise FeatureCertificationError("DatasetArtifact payload identity mismatch")
    evidence_path = _verify_document_reference(
        workspace, request["dataset_validation_evidence"]
    )
    evidence = _read_object(evidence_path)
    _validate_with(
        _runtime_validator(repository_root, "validation-evidence.schema.json"),
        evidence,
        "Dataset ValidationEvidence",
    )
    candidate = evidence["artifact_candidate"]
    if (
        evidence["result"] != "VALID"
        or candidate["artifact_type"] != reference["artifact_type"]
        or candidate["format_version"] != reference["format_version"]
        or candidate["metadata_path"] != reference["metadata_path"]
        or candidate.get("claimed_aggregate_checksum")
        != reference["aggregate_checksum"]
    ):
        raise FeatureCertificationError("Dataset ValidationEvidence identity mismatch")


def _all_output_paths(request: dict[str, Any]) -> list[str]:
    return [
        request["capture"]["captured_graph_path"],
        request["capture"]["captured_medoids_path"],
        *request["outputs"].values(),
    ]


def _require_outputs_absent(request: dict[str, Any]) -> None:
    workspace = Path(request["workspace_root"]).resolve()
    for relative in _all_output_paths(request):
        if resolve_feature_output(workspace, relative).exists():
            raise FeatureCertificationError(f"native Reference output already exists: {relative}")


def _prove_hard_link_capability(request: dict[str, Any]) -> None:
    workspace = Path(request["workspace_root"]).resolve()
    source_parent = workspace / "temporary"
    destination_parent = resolve_feature_output(
        workspace, request["capture"]["captured_graph_path"]
    ).parent
    source_parent.mkdir(parents=True, exist_ok=True)
    destination_parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".reference-hardlink-source-", dir=source_parent
    )
    os.close(descriptor)
    source = Path(temporary_name)
    destination = destination_parent / f".{source.name}.link-probe"
    try:
        if source.stat().st_dev != destination_parent.stat().st_dev:
            raise FeatureCertificationError("native capture paths are on different filesystems")
        os.link(source, destination)
        if source.stat().st_ino != destination.stat().st_ino:
            raise FeatureCertificationError("hard-link admission did not retain one inode")
    except OSError as error:
        raise FeatureCertificationError("same-filesystem hard-link admission failed") from error
    finally:
        destination.unlink(missing_ok=True)
        source.unlink(missing_ok=True)


def load_feature_schema(
    repository_root: str | Path, name: str
) -> tuple[dict[str, Any], Draft202012Validator]:
    """Load one explicitly admitted design schema with local/public references."""

    if name not in SCHEMA_FILENAMES:
        raise FeatureCertificationError(f"Feature 006 schema is not admitted: {name}")
    root = Path(repository_root)
    feature_schemas = {
        item.name: _read_object(item)
        for item in sorted(_schema_directory(root).glob("*.schema.json"))
        if item.name in SCHEMA_FILENAMES
    }
    runtime_schemas = {
        item.name: _read_object(item)
        for item in sorted((root / "contracts" / "v1" / "schemas").glob("*.json"))
    }
    if name not in feature_schemas:
        raise FeatureCertificationError(f"Feature 006 schema is missing: {name}")
    resources = [
        (schema["$id"], Resource.from_contents(schema))
        for schema in (*runtime_schemas.values(), *feature_schemas.values())
        if "$id" in schema
    ]
    registry = Registry().with_resources(resources)
    schema = feature_schemas[name]
    Draft202012Validator.check_schema(schema)
    return schema, Draft202012Validator(schema, registry=registry)


def canonical_document_checksum(document: Any, checksum_field: str) -> str:
    """Hash the canonical JSON projection excluding exactly its checksum field."""

    if type(document) is not dict or checksum_field not in document:
        projection = dict(document) if type(document) is dict else document
    else:
        projection = {key: value for key, value in document.items() if key != checksum_field}
    try:
        payload = json.dumps(
            projection,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        raise FeatureCertificationError("document cannot be canonicalized") from error
    return hashlib.sha256(payload).hexdigest()


def verify_document_checksum(document: Any, checksum_field: str) -> None:
    if type(document) is not dict or type(document.get(checksum_field)) is not str:
        raise FeatureCertificationError(f"document checksum field is missing: {checksum_field}")
    if document[checksum_field] != canonical_document_checksum(document, checksum_field):
        raise FeatureCertificationError(f"document checksum mismatch: {checksum_field}")


def resolve_feature_output(workspace: str | Path, relative_path: str | Path) -> Path:
    try:
        return resolve_confined_path(workspace, relative_path)
    except WorkspaceError as error:
        raise FeatureCertificationError(f"output path escapes workspace: {relative_path}") from error


def write_verified_document(
    workspace: str | Path,
    relative_path: str | Path,
    document: dict[str, Any],
    *,
    repository_root: str | Path | None = None,
    schema_name: str | None = None,
    checksum_field: str | None = None,
) -> Path:
    """Validate, exclusive-create, re-read, and revalidate one local document."""

    target = resolve_feature_output(workspace, relative_path)
    validator: Draft202012Validator | None = None
    if schema_name is not None:
        if repository_root is None:
            raise FeatureCertificationError("repository root is required for schema validation")
        _, validator = load_feature_schema(repository_root, schema_name)
        errors = sorted(validator.iter_errors(document), key=lambda item: list(item.absolute_path))
        if errors:
            raise FeatureCertificationError(f"document schema is invalid: {errors[0].message}")
    if checksum_field is not None:
        verify_document_checksum(document, checksum_field)
    try:
        write_immutable_json(target, document)
    except ImmutableWriteError as error:
        raise FeatureCertificationError(str(error)) from error
    reread = _read_object(target)
    if reread != document:
        raise FeatureCertificationError("immutable document changed after publication")
    if validator is not None:
        errors = sorted(validator.iter_errors(reread), key=lambda item: list(item.absolute_path))
        if errors:
            raise FeatureCertificationError(
                f"published document schema is invalid: {errors[0].message}"
            )
    if checksum_field is not None:
        verify_document_checksum(reread, checksum_field)
    return target


def reference_preflight(
    config_path: str | Path,
    request_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Admit and freeze one non-executing native Reference request."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    config = _read_object(Path(config_path).resolve())
    if set(config) != {"request"} or type(config["request"]) is not dict:
        raise FeatureCertificationError("reference preflight config must contain one request")
    request = config["request"]
    _, validator = load_feature_schema(
        repository, "native-reference-build-request.schema.json"
    )
    _validate_with(validator, request, "native Reference request")
    workspace = Path(request["workspace_root"]).resolve()
    requested = Path(request_path).resolve()
    try:
        relative_request = requested.relative_to(workspace)
    except ValueError as error:
        raise FeatureCertificationError("request path escapes workspace") from error
    if requested.exists():
        raise FeatureCertificationError(f"immutable target already exists: {requested}")
    _assert_source_identities(request, repository)
    profile = repository / request["profile"]["path"]
    if not profile.is_file() or _sha256_file(profile) != request["profile"]["sha256"]:
        raise FeatureCertificationError("fixed native profile identity mismatch")
    _verify_binary_identity(workspace, request["project_operation_owner"]["binary"])
    _verify_binary_identity(workspace, request["native_binary"])
    _fresh_validate_dataset(request, repository)
    _require_outputs_absent(request)
    _prove_hard_link_capability(request)
    published = write_verified_document(
        workspace,
        relative_request,
        request,
        repository_root=repository,
        schema_name="native-reference-build-request.schema.json",
    )
    return {
        "request_path": str(published),
        "byte_length": published.stat().st_size,
        "sha256": _sha256_file(published),
        "execution_authorized": False,
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
    }


def _write_exclusive_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8") as output:
            output.write(value)
            output.flush()
            os.fsync(output.fileno())
    except FileExistsError as error:
        raise FeatureCertificationError(f"immutable target already exists: {path}") from error


def reference_execute(
    request_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Dispatch the externally approved immutable request exactly once."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    immutable_request = Path(request_path).resolve()
    request_bytes = immutable_request.read_bytes()
    request = _read_object(immutable_request)
    _, validator = load_feature_schema(
        repository, "native-reference-build-request.schema.json"
    )
    _validate_with(validator, request, "native Reference request")
    workspace = Path(request["workspace_root"]).resolve()
    _assert_source_identities(request, repository)
    runner = _verify_binary_identity(
        workspace, request["project_operation_owner"]["binary"]
    )
    _verify_binary_identity(workspace, request["native_binary"])
    _fresh_validate_dataset(request, repository)
    _require_outputs_absent(request)
    completed = subprocess.run(
        [str(runner), "--request", str(immutable_request)],
        check=False,
        capture_output=True,
        text=True,
        shell=False,
    )
    stdout_path = resolve_feature_output(workspace, request["outputs"]["stdout_path"])
    stderr_path = resolve_feature_output(workspace, request["outputs"]["stderr_path"])
    _write_exclusive_text(stdout_path, completed.stdout)
    _write_exclusive_text(stderr_path, completed.stderr)
    if immutable_request.read_bytes() != request_bytes:
        raise FeatureCertificationError("capture owner mutated immutable request")
    return {
        "returncode": completed.returncode,
        "request_path": str(immutable_request),
        "request_byte_length": len(request_bytes),
        "request_sha256": hashlib.sha256(request_bytes).hexdigest(),
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
    }


def _validate_structural_role_admission(
    role: dict[str, Any],
    workspace: Path,
    repository_root: Path,
    *,
    reference: bool,
) -> dict[str, Any]:
    required = {
        "artifact",
        "artifact_document",
        "build_manifest",
        "validation_evidence",
        "projection",
    }
    if set(role) != required:
        raise FeatureCertificationError("structural role admission fields are invalid")
    artifact = role["artifact"]
    expected_version = (
        "global-graph-artifact/v2" if reference else "global-graph-artifact/v1"
    )
    if (
        type(artifact) is not dict
        or artifact.get("artifact_type") != "global_graph"
        or artifact.get("format_version") != expected_version
    ):
        raise FeatureCertificationError("structural graph role/version identity mismatch")
    artifact_path = _verify_document_reference(workspace, role["artifact_document"])
    expected_metadata_path = resolve_feature_output(
        workspace / "normal", artifact["metadata_path"]
    )
    if artifact_path != expected_metadata_path:
        raise FeatureCertificationError("structural ArtifactRef path identity mismatch")
    metadata = _read_object(artifact_path)
    schema_name = (
        "global-graph-artifact-v2.schema.json"
        if reference
        else "global-graph-artifact.schema.json"
    )
    _validate_with(
        _runtime_validator(repository_root, schema_name), metadata, "GlobalGraphArtifact"
    )
    if (
        metadata["artifact_type"] != artifact["artifact_type"]
        or metadata["format_version"] != artifact["format_version"]
        or metadata["aggregate_checksum"] != artifact["aggregate_checksum"]
    ):
        raise FeatureCertificationError("structural ArtifactRef checksum identity mismatch")
    manifest = _read_object(_verify_document_reference(workspace, role["build_manifest"]))
    _validate_with(
        _runtime_validator(repository_root, "build-manifest.schema.json"),
        manifest,
        "structural BuildManifest",
    )
    if manifest["status"] != "SUCCEEDED" or artifact not in manifest["output_artifacts"]:
        raise FeatureCertificationError("structural BuildManifest does not publish graph")
    evidence = _read_object(
        _verify_document_reference(workspace, role["validation_evidence"])
    )
    _validate_with(
        _runtime_validator(repository_root, "validation-evidence.schema.json"),
        evidence,
        "structural ValidationEvidence",
    )
    candidate = evidence["artifact_candidate"]
    if (
        evidence["result"] != "VALID"
        or candidate["artifact_type"] != artifact["artifact_type"]
        or candidate["format_version"] != artifact["format_version"]
        or candidate["metadata_path"] != artifact["metadata_path"]
        or candidate.get("claimed_aggregate_checksum") != artifact["aggregate_checksum"]
    ):
        raise FeatureCertificationError("structural ValidationEvidence identity mismatch")
    projection = role["projection"]
    expected_role = (
        "cpp_main_native_sharded_reference_v1"
        if reference
        else "cpp_main_p0_b0_m0_framework_candidate_v1"
    )
    if type(projection) is not dict or projection.get("baseline_id") != expected_role:
        raise FeatureCertificationError("structural projection role identity mismatch")
    return projection


def derive_structural(
    input_path: str | Path,
    output_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Call Feature 002's frozen helper and retain one immutable report snapshot."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    inputs = _read_object(Path(input_path).resolve())
    if set(inputs) != {
        "workspace_root",
        "report_id",
        "generated_at",
        "reference",
        "framework",
    }:
        raise FeatureCertificationError("structural derivation input fields are invalid")
    workspace = Path(inputs["workspace_root"]).resolve()
    reference = _validate_structural_role_admission(
        inputs["reference"], workspace, repository, reference=True
    )
    framework = _validate_structural_role_admission(
        inputs["framework"], workspace, repository, reference=False
    )
    policy = structural_parity.load_policy(repository)
    frozen_checksum = structural_parity.policy_checksum(repository)
    report = structural_parity.derive_report(
        policy,
        reference,
        framework,
        report_id=inputs["report_id"],
        generated_at=inputs["generated_at"],
        repository_root=repository,
    )
    structural_parity.validate_report(report, repository)
    if report["policy"]["sha256"] != frozen_checksum:
        raise FeatureCertificationError("derived structural policy checksum drifted")
    output = Path(output_path).resolve()
    try:
        relative_output = output.relative_to(workspace)
    except ValueError as error:
        raise FeatureCertificationError("structural report path escapes workspace") from error
    published = write_verified_document(workspace, relative_output, report)
    reread = _read_object(published)
    structural_parity.validate_report(reread, repository)
    return {
        "report_path": str(published),
        "byte_length": published.stat().st_size,
        "sha256": _sha256_file(published),
        "gate": reread["gate"]["result"],
        "allowed_claim": reread["allowed_claim"],
    }


def _output_relative_to_workspace(output_path: str | Path, workspace: Path) -> Path:
    output = Path(output_path).resolve()
    try:
        return output.relative_to(workspace)
    except ValueError as error:
        raise FeatureCertificationError("Feature 006 output path escapes workspace") from error


def _canonical_document_reference(
    workspace: Path, reference: dict[str, Any], label: str
) -> tuple[Path, dict[str, Any]]:
    path = _verify_document_reference(workspace, reference)
    document = _read_object(path)
    claimed = document.get("canonical_checksum")
    if (
        type(claimed) is not str
        or claimed != reference.get("canonical_checksum")
        or claimed != canonical_document_checksum(document, "canonical_checksum")
    ):
        raise FeatureCertificationError(f"{label} canonical identity mismatch")
    return path, document


def _protocol_reference(
    workspace: Path, declaration: dict[str, Any], expected_cache: str
) -> tuple[dict[str, Any], dict[str, Any]]:
    if type(declaration) is not dict or set(declaration) != {"path"}:
        raise FeatureCertificationError("quality policy Protocol declaration is invalid")
    path = resolve_feature_output(workspace, declaration["path"])
    if not path.is_file():
        raise FeatureCertificationError(f"frozen SearchProtocol is missing: {path}")
    document = _read_object(path)
    try:
        normalized = search_evaluation.normalize_search_protocol(document)
    except Exception as error:
        raise FeatureCertificationError("frozen SearchProtocol is invalid") from error
    if normalized != document:
        raise FeatureCertificationError("frozen SearchProtocol checksum drifted")
    cache_mode = document["search_execution"]["cache"]["mode"]
    if cache_mode != expected_cache:
        raise FeatureCertificationError("SearchProtocol cache role mismatch")
    reference = {
        "path": path.relative_to(workspace).as_posix(),
        "byte_length": path.stat().st_size,
        "sha256": _sha256_file(path),
        "protocol_id": document["protocol_id"],
        "protocol_checksum": document["protocol_checksum"],
        "search_execution_checksum": document["search_execution_checksum"],
        "cache_mode": cache_mode,
    }
    return document, reference


def _control_projection(protocol: dict[str, Any]) -> dict[str, Any]:
    execution = protocol["search_execution"]
    return {
        "K": execution["K"],
        "L_search": execution["L_search"],
        "beamwidth": execution["beamwidth"],
        "thread_count": execution["thread_count"],
        "io_limit": execution["io_limit"],
        "warmup_policy": execution["warmup_policy"],
        "query_order": execution["query_order"],
        "timing_boundary": execution["timing_boundary"],
        "repetition_count": execution["repetition_count"],
        "schedule": execution["schedule"],
        "entry_policy": execution["entry_policy"],
        "evaluator": protocol["evaluation"]["evaluator"],
        "quality_eligibility_policy": protocol["evaluation"][
            "quality_eligibility_policy"
        ],
        "aggregation_policy": protocol["evaluation"]["aggregation_policy"],
        "replacement_repetitions": protocol["evaluation"][
            "replacement_repetitions"
        ],
    }


def freeze_quality_policy(
    input_path: str | Path,
    output_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Freeze exact two-role Feature 005 protocols before any quality search."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    inputs = _read_object(Path(input_path).resolve())
    if set(inputs) != {
        "workspace_root",
        "created_at",
        "artifacts",
        "shared_inputs",
        "protocols",
    }:
        raise FeatureCertificationError("quality policy input fields are invalid")
    workspace = Path(inputs["workspace_root"]).resolve()
    artifacts = inputs["artifacts"]
    if type(artifacts) is not dict or set(artifacts) != {
        "dataset",
        "shared_pq",
        "reference_graph",
        "reference_disk_layout",
        "framework_graph",
        "framework_disk_layout",
    }:
        raise FeatureCertificationError("quality policy Artifact projection is invalid")
    if (
        artifacts["reference_graph"].get("format_version")
        != "global-graph-artifact/v2"
        or artifacts["framework_graph"].get("format_version")
        != "global-graph-artifact/v1"
        or artifacts["reference_graph"] == artifacts["framework_graph"]
        or artifacts["reference_disk_layout"] == artifacts["framework_disk_layout"]
    ):
        raise FeatureCertificationError("quality policy graph/Layout roles are not distinct")
    shared = inputs["shared_inputs"]
    if type(shared) is not dict or set(shared) != {"query_set", "ground_truth"}:
        raise FeatureCertificationError("quality policy shared inputs are invalid")
    _, query_set = _canonical_document_reference(
        workspace, shared["query_set"], "QuerySetReference"
    )
    _, ground_truth = _canonical_document_reference(
        workspace, shared["ground_truth"], "GroundTruthReference"
    )
    protocols_input = inputs["protocols"]
    if type(protocols_input) is not dict or set(protocols_input) != {
        "reference",
        "framework",
    }:
        raise FeatureCertificationError("quality policy role Protocol set is invalid")
    protocol_documents: dict[str, dict[str, dict[str, Any]]] = {}
    protocol_references: dict[str, dict[str, dict[str, Any]]] = {}
    control_projection: dict[str, Any] | None = None
    protocol_ids: set[str] = set()
    for role in ("reference", "framework"):
        role_input = protocols_input[role]
        if type(role_input) is not dict or set(role_input) != {"zero", "fixed"}:
            raise FeatureCertificationError("quality policy cache Protocol pair is invalid")
        role_documents: dict[str, dict[str, Any]] = {}
        role_references: dict[str, dict[str, Any]] = {}
        for cache_key, cache_mode in (("zero", "zero"), ("fixed", "fixed_bfs")):
            document, reference = _protocol_reference(
                workspace, role_input[cache_key], cache_mode
            )
            execution = document["search_execution"]
            role_artifacts = {
                "dataset": execution["dataset_artifact"],
                "shared_pq": execution["pq_artifact"],
                f"{role}_graph": execution["global_graph_artifact"],
                f"{role}_disk_layout": execution["disk_layout_artifact"],
            }
            for name, actual in role_artifacts.items():
                if actual != artifacts[name]:
                    raise FeatureCertificationError(
                        f"SearchProtocol {name} Artifact identity mismatch"
                    )
            if (
                execution["query_set"].get("canonical_checksum")
                != query_set["canonical_checksum"]
                or document["evaluation"]["ground_truth"].get("canonical_checksum")
                != ground_truth["canonical_checksum"]
                or ground_truth.get("query_set_checksum")
                != query_set["canonical_checksum"]
            ):
                raise FeatureCertificationError(
                    "SearchProtocol shared QuerySet/GroundTruth identity mismatch"
                )
            projected = _control_projection(document)
            if control_projection is None:
                control_projection = projected
            elif projected != control_projection:
                raise FeatureCertificationError("SearchProtocol frozen controls differ")
            if document["protocol_id"] in protocol_ids:
                raise FeatureCertificationError("SearchProtocol identities are not independent")
            protocol_ids.add(document["protocol_id"])
            role_documents[cache_key] = document
            role_references[cache_key] = reference
        protocol_documents[role] = role_documents
        protocol_references[role] = role_references
    assert control_projection is not None
    controls = {
        key: control_projection[key]
        for key in (
            "K",
            "L_search",
            "beamwidth",
            "thread_count",
            "io_limit",
            "warmup_policy",
            "query_order",
            "timing_boundary",
            "repetition_count",
            "schedule",
            "entry_policy",
        )
    }
    controls["cache_controls"] = {
        "zero": protocol_documents["reference"]["zero"]["search_execution"]["cache"],
        "fixed": protocol_documents["reference"]["fixed"]["search_execution"]["cache"],
    }
    controls["control_projection_checksum"] = canonical_document_checksum(
        controls, "control_projection_checksum"
    )
    policy = {
        "policy_version": "quality-parity-policy/v1",
        "status": "frozen",
        "scope": "feature_006_sift1m_l2_static_quality_parity",
        "created_at": inputs["created_at"],
        "roles": CERTIFICATION_ROLES,
        "threshold": {
            "metrics": ["recall_at_10", "recall_at_100"],
            "source_report_scale": "percentage_0_to_100",
            "comparison_scale": "fraction_0_to_1",
            "maximum_shortfall": 0.01,
            "equal_or_higher_framework_passes": True,
        },
        "artifacts": artifacts,
        "shared_inputs": shared,
        "protocols": protocol_references,
        "controls": controls,
        "evaluator": control_projection["evaluator"],
        "eligibility": {
            "quality_policy": control_projection["quality_eligibility_policy"],
            "aggregation": control_projection["aggregation_policy"],
            "attempted_repetitions_per_point": 3,
            "minimum_eligible_for_existing_aggregate": 1,
            "replacement_repetitions": control_projection[
                "replacement_repetitions"
            ],
        },
        "decision_rule": {
            "point_rule": "all_metrics_at_every_declared_cache_l",
            "cross_point_compensation": False,
            "threshold_violation": "FAIL",
            "missing_or_unevaluable_aggregate": "INCONCLUSIVE",
        },
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "policy_checksum": "",
    }
    policy["policy_checksum"] = canonical_document_checksum(policy, "policy_checksum")
    relative_output = _output_relative_to_workspace(output_path, workspace)
    published = write_verified_document(
        workspace,
        relative_output,
        policy,
        repository_root=repository,
        schema_name="quality-parity-policy.schema.json",
        checksum_field="policy_checksum",
    )
    return {
        "policy_path": str(published),
        "byte_length": published.stat().st_size,
        "sha256": _sha256_file(published),
        "policy_checksum": policy["policy_checksum"],
        "protocol_count": len(protocol_ids),
        "search_invocations": 0,
    }


def _quality_report_reference(
    workspace: Path,
    path: Path,
    report: dict[str, Any],
    cache_mode: str,
) -> dict[str, Any]:
    return {
        "path": path.relative_to(workspace).as_posix(),
        "byte_length": path.stat().st_size,
        "sha256": _sha256_file(path),
        "report_id": report["report_id"],
        "report_checksum": report["report_checksum"],
        "protocol_checksum": report["protocol"]["protocol_checksum"],
        "cache_mode": cache_mode,
    }


def _audit_reference(
    *, identity: str, path: str, sha256: str, status: str, reason: str
) -> dict[str, Any]:
    return {
        "identity": identity,
        "path": path,
        "sha256": sha256,
        "terminal_status": status,
        "reason": reason,
    }


def compare_quality(
    policy_path: str | Path,
    input_path: str | Path,
    output_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Compare only Feature 005 report aggregates; never recompute Recall."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    inputs = _read_object(Path(input_path).resolve())
    if set(inputs) != {"workspace_root", "generated_at", "evaluation_reports"}:
        raise FeatureCertificationError("quality comparison input fields are invalid")
    workspace = Path(inputs["workspace_root"]).resolve()
    policy_file = Path(policy_path).resolve()
    try:
        policy_file.relative_to(workspace)
    except ValueError as error:
        raise FeatureCertificationError("quality policy path escapes workspace") from error
    policy = _read_object(policy_file)
    _, policy_validator = load_feature_schema(
        repository, "quality-parity-policy.schema.json"
    )
    _validate_with(policy_validator, policy, "QualityParityPolicy")
    verify_document_checksum(policy, "policy_checksum")
    policy_reference = {
        "path": policy_file.relative_to(workspace).as_posix(),
        "byte_length": policy_file.stat().st_size,
        "sha256": _sha256_file(policy_file),
        "canonical_checksum": policy["policy_checksum"],
    }
    report_declarations = inputs["evaluation_reports"]
    if type(report_declarations) is not dict or set(report_declarations) != {
        "reference",
        "framework",
    }:
        raise FeatureCertificationError("quality comparison report roles are invalid")
    admitted_reports: dict[str, list[dict[str, Any]]] = {
        "reference": [],
        "framework": [],
    }
    report_groups: dict[tuple[str, str, int], dict[str, Any]] = {}
    attempts: dict[str, list[dict[str, Any]]] = {"reference": [], "framework": []}
    audit_exclusions: list[dict[str, Any]] = []
    for role in ("reference", "framework"):
        declarations = report_declarations[role]
        if type(declarations) is not list or len(declarations) != 2:
            raise FeatureCertificationError("each role must declare two evaluation reports")
        expected_by_checksum = {
            policy["protocols"][role][cache]["protocol_checksum"]: (
                cache,
                policy["protocols"][role][cache],
            )
            for cache in ("zero", "fixed")
        }
        admitted_cache_modes: set[str] = set()
        for relative in declarations:
            if type(relative) is not str:
                raise FeatureCertificationError("evaluation report path is invalid")
            path = resolve_feature_output(workspace, relative)
            if not path.is_file():
                audit_exclusions.append(
                    _audit_reference(
                        identity=f"{role}-missing-report",
                        path=relative,
                        sha256="0" * 64,
                        status="IDENTITY_MISMATCHED",
                        reason="declared Feature 005 EvaluationReport is missing",
                    )
                )
                continue
            try:
                report = _read_object(path)
                _validate_with(
                    search_evaluation._feature_validator(
                        "search-evaluation-report.schema.json"
                    ),
                    report,
                    "Feature 005 SearchEvaluationReport",
                )
                if report["report_checksum"] != canonical_document_checksum(
                    report, "report_checksum"
                ):
                    raise FeatureCertificationError("EvaluationReport checksum mismatch")
                expected = expected_by_checksum.get(
                    report.get("protocol", {}).get("protocol_checksum")
                )
                if expected is None:
                    raise FeatureCertificationError("EvaluationReport Protocol role mismatch")
                cache_key, protocol_reference = expected
                cache_mode = "zero" if cache_key == "zero" else "fixed_bfs"
                if cache_mode in admitted_cache_modes:
                    raise FeatureCertificationError("duplicate cache EvaluationReport")
                if (
                    report["protocol"]["path"] != protocol_reference["path"]
                    or report["dataset_artifact"] != policy["artifacts"]["dataset"]
                    or report["pq_artifact"] != policy["artifacts"]["shared_pq"]
                    or report["global_graph_artifact"]
                    != policy["artifacts"][f"{role}_graph"]
                    or report["disk_layout_artifact"]
                    != policy["artifacts"][f"{role}_disk_layout"]
                    or report["query_set_checksum"]
                    != policy["shared_inputs"]["query_set"]["canonical_checksum"]
                    or report["ground_truth"]["query_set_checksum"]
                    != policy["shared_inputs"]["query_set"]["canonical_checksum"]
                    or report["evaluator"] != policy["evaluator"]
                ):
                    raise FeatureCertificationError(
                        "EvaluationReport controlled-input identity mismatch"
                    )
            except Exception as error:
                audit_exclusions.append(
                    _audit_reference(
                        identity=path.stem,
                        path=relative,
                        sha256=_sha256_file(path),
                        status="IDENTITY_MISMATCHED",
                        reason=str(error),
                    )
                )
                continue
            admitted_cache_modes.add(cache_mode)
            admitted_reports[role].append(
                _quality_report_reference(workspace, path, report, cache_mode)
            )
            result_groups: dict[int, list[dict[str, Any]]] = {}
            for result in report["observation_results"]:
                l_search = result["L_search"]
                result_groups.setdefault(l_search, []).append(result)
                observation_path = resolve_feature_output(workspace, result["path"])
                actual_sha = (
                    _sha256_file(observation_path)
                    if observation_path.is_file()
                    else "0" * 64
                )
                identity_valid = (
                    observation_path.is_file() and actual_sha == result["sha256"]
                )
                eligible = bool(result["quality_eligible"] and identity_valid)
                attempt = {
                    "observation_id": result["observation_id"],
                    "path": result["path"],
                    "byte_length": (
                        observation_path.stat().st_size
                        if observation_path.is_file()
                        else 1
                    ),
                    "sha256": actual_sha,
                    "cache_mode": result["cache_mode"],
                    "L_search": l_search,
                    "repetition_index": result["repetition_index"],
                    "terminal_status": result["terminal_status"],
                    "quality_eligible": eligible,
                    "quality_eligibility_reason": (
                        result["quality_eligibility_reason"]
                        if identity_valid
                        else "observation file identity mismatch"
                    ),
                }
                if eligible:
                    attempt["quality_metrics"] = {
                        "recall_at_10_percent": result["quality_metrics"][
                            "recall_at_10"
                        ],
                        "recall_at_100_percent": result["quality_metrics"][
                            "recall_at_100"
                        ],
                    }
                else:
                    audit_exclusions.append(
                        _audit_reference(
                            identity=result["observation_id"],
                            path=result["path"],
                            sha256=actual_sha,
                            status="EXCLUDED",
                            reason=attempt["quality_eligibility_reason"],
                        )
                    )
                attempts[role].append(attempt)
            aggregates = {
                (item["cache_mode"], item["L_search"]): item
                for item in report["aggregates"]["quality"]
            }
            for l_search in policy["controls"]["L_search"]:
                group_attempts = result_groups.get(l_search, [])
                aggregate = aggregates.get((cache_mode, l_search))
                complete_attempts = (
                    len(group_attempts) == 3
                    and {item["repetition_index"] for item in group_attempts}
                    == {0, 1, 2}
                )
                eligible_count = sum(
                    item["quality_eligible"]
                    for item in attempts[role]
                    if item["cache_mode"] == cache_mode
                    and item["L_search"] == l_search
                )
                valid_aggregate = (
                    aggregate is not None
                    and aggregate["aggregation"] == "median_eligible"
                    and aggregate["eligible_count"] == eligible_count
                    and eligible_count > 0
                )
                report_groups[(role, cache_mode, l_search)] = {
                    "complete_attempts": complete_attempts,
                    "eligible_count": eligible_count,
                    "aggregate": aggregate if valid_aggregate else None,
                }
    findings: list[dict[str, Any]] = []
    for cache_mode in ("zero", "fixed_bfs"):
        for l_search in policy["controls"]["L_search"]:
            role_summaries: dict[str, dict[str, Any]] = {}
            comparable = True
            for role in ("reference", "framework"):
                source = report_groups.get((role, cache_mode, l_search))
                summary: dict[str, Any] = {
                    "attempted_count": 3,
                    "eligible_count": source["eligible_count"] if source else 0,
                    "aggregation": "median_eligible",
                }
                if source and source["complete_attempts"] and source["aggregate"]:
                    metrics = source["aggregate"]["median_quality_metrics"]
                    summary["metrics"] = {
                        "recall_at_10_percent": metrics["recall_at_10"],
                        "recall_at_100_percent": metrics["recall_at_100"],
                    }
                else:
                    comparable = False
                role_summaries[role] = summary
            metric_findings: list[dict[str, Any]] = []
            if comparable:
                for metric, schema_metric in (
                    ("recall_at_10", "recall_at_10_percent"),
                    ("recall_at_100", "recall_at_100_percent"),
                ):
                    reference_percent = role_summaries["reference"]["metrics"][
                        schema_metric
                    ]
                    framework_percent = role_summaries["framework"]["metrics"][
                        schema_metric
                    ]
                    reference_fraction = reference_percent / 100.0
                    framework_fraction = framework_percent / 100.0
                    shortfall = round(reference_fraction - framework_fraction, 12)
                    metric_findings.append(
                        {
                            "metric": metric,
                            "reference_percent": reference_percent,
                            "framework_percent": framework_percent,
                            "reference_fraction": reference_fraction,
                            "framework_fraction": framework_fraction,
                            "shortfall_fraction": shortfall,
                            "maximum_shortfall_fraction": 0.01,
                            "result": "PASS" if shortfall <= 0.01 else "FAIL",
                        }
                    )
                group_result = (
                    "PASS"
                    if all(item["result"] == "PASS" for item in metric_findings)
                    else "FAIL"
                )
                reason = (
                    "both frozen Recall metrics pass"
                    if group_result == "PASS"
                    else "at least one frozen Recall metric exceeds the 0.010 shortfall"
                )
            else:
                group_result = "INCONCLUSIVE"
                reason = "missing, incomplete, or unevaluable Feature 005 aggregate"
            findings.append(
                {
                    "cache_mode": cache_mode,
                    "L_search": l_search,
                    "reference": role_summaries["reference"],
                    "framework": role_summaries["framework"],
                    "metric_findings": metric_findings,
                    "result": group_result,
                    "reason": reason,
                }
            )
    if any(item["result"] == "FAIL" for item in findings):
        overall = "FAIL"
    elif any(item["result"] == "INCONCLUSIVE" for item in findings):
        overall = "INCONCLUSIVE"
    else:
        overall = "PASS"
    evidence = {
        "evidence_version": "quality-parity-evidence/v1",
        "generated_at": inputs["generated_at"],
        "policy": policy_reference,
        "roles": CERTIFICATION_ROLES,
        "artifacts": policy["artifacts"],
        "shared_inputs": policy["shared_inputs"],
        "evaluation_reports": admitted_reports,
        "observation_attempts": attempts,
        "groups": findings,
        "result": overall,
        "audit_exclusions": audit_exclusions,
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "evidence_checksum": "",
    }
    evidence["evidence_checksum"] = canonical_document_checksum(
        evidence, "evidence_checksum"
    )
    published = write_verified_document(
        workspace,
        _output_relative_to_workspace(output_path, workspace),
        evidence,
        repository_root=repository,
        schema_name="quality-parity-evidence.schema.json",
        checksum_field="evidence_checksum",
    )
    return {
        "evidence_path": str(published),
        "byte_length": published.stat().st_size,
        "sha256": _sha256_file(published),
        "evidence_checksum": evidence["evidence_checksum"],
        "result": overall,
        "search_invocations": 0,
    }


def _verify_decision_document_ref(
    workspace: Path, reference: dict[str, Any] | None
) -> tuple[bool, dict[str, Any] | None]:
    if reference is None:
        return False, None
    try:
        path = _verify_document_reference(workspace, reference)
        return True, _read_object(path)
    except (FeatureCertificationError, KeyError, TypeError):
        return False, None


def decide(
    input_path: str | Path,
    output_path: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    """Derive the fixed-scope three-state decision from immutable evidence."""

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    inputs = _read_object(Path(input_path).resolve())
    required = {
        "workspace_root",
        "generated_at",
        "structural_gate",
        "search_runner_parity",
        "mandatory_admission",
        "artifacts",
        "shared_inputs",
        "protocols",
        "observations",
        "evaluation_reports",
        "quality_parity",
        "build_manifests",
        "validation_evidence",
        "audit_exclusions",
    }
    if set(inputs) != required:
        raise FeatureCertificationError("certification decision input fields are invalid")
    workspace = Path(inputs["workspace_root"]).resolve()
    structural = dict(inputs["structural_gate"])
    structural_ok = True
    for key in ("policy", "report"):
        valid, document = _verify_decision_document_ref(workspace, structural.get(key))
        structural_ok = structural_ok and valid
        if key == "report" and valid and document is not None:
            structural_ok = (
                structural_ok
                and document.get("gate", {}).get("result") == structural.get("result")
            )
    structural_result = (
        structural["result"] if structural_ok else "INCONCLUSIVE"
    )
    structural["result"] = structural_result

    parity_input = dict(inputs["search_runner_parity"])
    identity_match = parity_input.get("identity") == FROZEN_RUNNER_PARITY_IDENTITY
    runner_valid, runner_document = _verify_decision_document_ref(
        workspace, parity_input.get("runner_build_manifest")
    )
    if runner_valid and runner_document is not None:
        runner_valid = (
            runner_document.get("runner_identity") == parity_input.get("identity")
        )
    parity_evidence_valid = True
    for cache in ("zero", "fixed"):
        valid, document = _verify_decision_document_ref(
            workspace, parity_input.get("evidence", {}).get(cache)
        )
        parity_evidence_valid = parity_evidence_valid and valid
        if valid and document is not None:
            parity_evidence_valid = (
                parity_evidence_valid
                and document.get("runner_identity") == parity_input.get("identity")
                and document.get("result") == parity_input.get("result")
            )
    parity_result = parity_input.get("result", "INCONCLUSIVE")
    if (
        not identity_match
        or not runner_valid
        or not parity_evidence_valid
        or parity_result not in {"PASS", "FAIL", "INCONCLUSIVE"}
    ):
        parity_result = "INCONCLUSIVE"
    parity = {
        **parity_input,
        "result": parity_result,
        "identity": FROZEN_RUNNER_PARITY_IDENTITY,
        "reason": (
            parity_input.get("reason", "runner parity evidence admitted")
            if parity_result == parity_input.get("result")
            else "runner parity evidence is missing, stale, or identity-mismatched"
        ),
    }

    mandatory = dict(inputs["mandatory_admission"])
    checks = mandatory.get("checks", [])
    check_evidence_valid = True
    for check in checks:
        for reference in check.get("evidence", []):
            valid, _ = _verify_decision_document_ref(workspace, reference)
            check_evidence_valid = check_evidence_valid and valid
    statuses = {check.get("status") for check in checks}
    derived_mandatory = (
        "FAIL"
        if "FAIL" in statuses
        else "INCONCLUSIVE"
        if "MISSING" in statuses or not checks or not check_evidence_valid
        else "PASS"
    )
    if mandatory.get("result") != derived_mandatory:
        derived_mandatory = "INCONCLUSIVE"
    mandatory["result"] = derived_mandatory

    quality_input = dict(inputs["quality_parity"])
    policy_valid, policy_document = _verify_decision_document_ref(
        workspace, quality_input.get("policy")
    )
    evidence_valid, quality_document = _verify_decision_document_ref(
        workspace, quality_input.get("evidence")
    )
    if policy_valid and policy_document is not None:
        try:
            _validate_with(
                load_feature_schema(repository, "quality-parity-policy.schema.json")[1],
                policy_document,
                "QualityParityPolicy",
            )
            verify_document_checksum(policy_document, "policy_checksum")
        except FeatureCertificationError:
            policy_valid = False
    if evidence_valid and quality_document is not None:
        try:
            _validate_with(
                load_feature_schema(repository, "quality-parity-evidence.schema.json")[1],
                quality_document,
                "QualityParityEvidence",
            )
            verify_document_checksum(quality_document, "evidence_checksum")
        except FeatureCertificationError:
            evidence_valid = False
    quality_result = quality_input.get("result", "INCONCLUSIVE")
    if (
        not policy_valid
        or not evidence_valid
        or quality_document is None
        or quality_document.get("result") != quality_result
    ):
        quality_result = "INCONCLUSIVE"
    quality = {
        **quality_input,
        "result": quality_result,
        "reason": (
            quality_input.get("reason", "quality evidence admitted")
            if quality_result == quality_input.get("result")
            else "quality evidence is missing, invalid, or identity-mismatched"
        ),
    }

    all_reference_sets_valid = True
    for field in ("protocols", "observations", "evaluation_reports"):
        role_set = inputs[field]
        if type(role_set) is not dict or set(role_set) != {"reference", "framework"}:
            all_reference_sets_valid = False
            continue
        for role in ("reference", "framework"):
            for reference in role_set[role]:
                valid, _ = _verify_decision_document_ref(workspace, reference)
                all_reference_sets_valid = all_reference_sets_valid and valid
    for field in ("build_manifests", "validation_evidence"):
        for reference in inputs[field]:
            valid, _ = _verify_decision_document_ref(workspace, reference)
            all_reference_sets_valid = all_reference_sets_valid and valid
    if not all_reference_sets_valid and derived_mandatory == "PASS":
        mandatory["result"] = "INCONCLUSIVE"
        derived_mandatory = "INCONCLUSIVE"

    if structural_result == "FAIL":
        decision = "NOT_CERTIFIED"
        reason_code = "structural-gate-fail"
        reason = "the frozen Structural/Contract Gate is FAIL"
    elif structural_result in {"INCONCLUSIVE", "NOT_REACHED"}:
        decision = "INCONCLUSIVE"
        reason_code = "structural-gate-insufficient"
        reason = "the frozen Structural/Contract Gate is not conclusive PASS"
    elif "FAIL" in {parity_result, derived_mandatory, quality_result}:
        decision = "NOT_CERTIFIED"
        reason_code = "mandatory-negative-evidence"
        reason = "at least one exact-current mandatory certification input is FAIL"
    elif {parity_result, derived_mandatory, quality_result} == {"PASS"}:
        decision = "CERTIFIED"
        reason_code = "all-mandatory-pass"
        reason = "all frozen fixed-scope mandatory evidence passed"
    else:
        decision = "INCONCLUSIVE"
        reason_code = "mandatory-evidence-insufficient"
        reason = "mandatory evidence is missing, stale, mismatched, invalid, or ambiguous"

    shared_inputs = {
        key: (
            {name: value for name, value in reference.items() if name != "canonical_checksum"}
            if reference is not None
            else None
        )
        for key, reference in inputs["shared_inputs"].items()
    }
    document = {
        "decision_version": "framework-baseline-certification-decision/v1",
        "generated_at": inputs["generated_at"],
        "scope": "sift1m_l2_static_unfiltered_cpp_main_p0_b0_m0_only",
        "roles": CERTIFICATION_ROLES,
        "decision": decision,
        "structural_gate": structural,
        "search_runner_parity": parity,
        "mandatory_admission": mandatory,
        "artifacts": inputs["artifacts"],
        "shared_inputs": shared_inputs,
        "protocols": inputs["protocols"],
        "observations": inputs["observations"],
        "evaluation_reports": inputs["evaluation_reports"],
        "quality_parity": quality,
        "build_manifests": inputs["build_manifests"],
        "validation_evidence": inputs["validation_evidence"],
        "audit_exclusions": inputs["audit_exclusions"],
        "rationale": [{"reason_code": reason_code, "message": reason}],
        "claims": {
            "fixed_scope_framework_certified": decision == "CERTIFIED",
            "graph_byte_or_adjacency_parity": False,
            "complete_native_framework_equivalence": False,
            "framework_wide_certification": False,
            "performance_certification": False,
            "paper_feature_certification": False,
        },
        "canonicalization": "diskann-json-sort-keys-sha256-v1",
        "decision_checksum": "",
    }
    document["decision_checksum"] = canonical_document_checksum(
        document, "decision_checksum"
    )
    published = write_verified_document(
        workspace,
        _output_relative_to_workspace(output_path, workspace),
        document,
        repository_root=repository,
        schema_name="framework-baseline-certification-decision.schema.json",
        checksum_field="decision_checksum",
    )
    return {
        "decision_path": str(published),
        "byte_length": published.stat().st_size,
        "sha256": _sha256_file(published),
        "decision_checksum": document["decision_checksum"],
        "decision": decision,
        "formal_operations_started": 0,
    }


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="framework-baseline-certification")
    operations = parser.add_subparsers(dest="operation", required=True)

    reference_preflight = operations.add_parser("reference-preflight")
    reference_preflight.add_argument("--config", required=True)
    reference_preflight.add_argument("--request", required=True)

    reference_execute = operations.add_parser("reference-execute")
    reference_execute.add_argument("--request", required=True)

    derive_structural = operations.add_parser("derive-structural")
    derive_structural.add_argument("--input", required=True)
    derive_structural.add_argument("--output", required=True)

    freeze_policy = operations.add_parser("freeze-quality-policy")
    freeze_policy.add_argument("--input", required=True)
    freeze_policy.add_argument("--output", required=True)

    compare_quality = operations.add_parser("compare-quality")
    compare_quality.add_argument("--policy", required=True)
    compare_quality.add_argument("--input", required=True)
    compare_quality.add_argument("--output", required=True)

    decide = operations.add_parser("decide")
    decide.add_argument("--input", required=True)
    decide.add_argument("--output", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_argument_parser().parse_args(argv)
    if arguments.operation == "reference-preflight":
        print(
            json.dumps(
                reference_preflight(arguments.config, arguments.request), sort_keys=True
            )
        )
        return 0
    if arguments.operation == "reference-execute":
        result = reference_execute(arguments.request)
        print(json.dumps(result, sort_keys=True))
        return int(result["returncode"])
    if arguments.operation == "derive-structural":
        print(
            json.dumps(
                derive_structural(arguments.input, arguments.output), sort_keys=True
            )
        )
        return 0
    if arguments.operation == "freeze-quality-policy":
        print(
            json.dumps(
                freeze_quality_policy(arguments.input, arguments.output), sort_keys=True
            )
        )
        return 0
    if arguments.operation == "compare-quality":
        print(
            json.dumps(
                compare_quality(arguments.policy, arguments.input, arguments.output),
                sort_keys=True,
            )
        )
        return 0
    if arguments.operation == "decide":
        print(json.dumps(decide(arguments.input, arguments.output), sort_keys=True))
        return 0
    raise FeatureCertificationError(f"unknown operation: {arguments.operation}")


if __name__ == "__main__":  # pragma: no cover - exercised through CLI tests
    raise SystemExit(main())
