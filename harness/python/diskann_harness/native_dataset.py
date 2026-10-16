"""Admission of existing little-endian float32/L2 matrices; no conversion or execution."""

from __future__ import annotations

import array
import hashlib
import math
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

from .checksum import aggregate_checksum
from .manifests import write_immutable_json
from .workspace import normalize_workspace, reject_upstream_target, resolve_confined_path


FORMAT = "diskann-float32-dataset/v1"
MEDIA = "application/vnd.diskann.cpp-main.float32-bin;version=1"
CAPABILITY = "dataset.diskann_float32_bin/v1"


class NativeDatasetError(RuntimeError):
    """Invalid native input or failed immutable admission."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def scan_dataset(source: Path, expected: dict[str, Any], *, label: str = "native") -> dict[str, Any]:
    for name in ("global_node_count", "vector_dimension"):
        if type(expected.get(name)) is not int or not 1 <= expected[name] <= 0xFFFFFFFF:
            raise NativeDatasetError(f"{label} {name} must fit a positive uint32 header")
    if "byte_length" in expected and (type(expected["byte_length"]) is not int or expected["byte_length"] < 12):
        raise NativeDatasetError(f"{label} Dataset byte length is invalid")
    if "checksum" in expected and (not isinstance(expected["checksum"], str) or not re.fullmatch(r"[0-9a-f]{64}", expected["checksum"])):
        raise NativeDatasetError(f"{label} Dataset SHA-256 declaration is invalid")
    if not source.is_file():
        raise NativeDatasetError(f"{label} Dataset is missing or not a regular file")
    actual_bytes = source.stat().st_size
    if "byte_length" in expected and actual_bytes != expected["byte_length"]:
        raise NativeDatasetError(f"{label} Dataset byte length differs")
    digest = hashlib.sha256()
    non_finite = value_count = 0
    with source.open("rb") as stream:
        header = stream.read(8)
        digest.update(header)
        if len(header) != 8:
            raise NativeDatasetError(f"{label} Dataset header is truncated")
        npts = int.from_bytes(header[:4], "little")
        dimension = int.from_bytes(header[4:], "little")
        if npts != expected["global_node_count"] or dimension != expected["vector_dimension"]:
            raise NativeDatasetError(f"{label} Dataset header identity differs")
        length = 8 + npts * dimension * 4
        if length > 0xFFFFFFFFFFFFFFFF:
            raise NativeDatasetError(f"{label} Dataset byte length overflows uint64")
        if actual_bytes != length:
            raise NativeDatasetError(f"{label} Dataset byte length differs")
        for block in iter(lambda: stream.read(64 * 1024 * 1024), b""):
            digest.update(block)
            if len(block) % 4:
                raise NativeDatasetError(f"{label} float32 payload is misaligned")
            values = array.array("f")
            values.frombytes(block)
            if sys.byteorder != "little":
                values.byteswap()
            non_finite += sum(1 for value in values if not math.isfinite(value))
            value_count += len(values)
    if value_count != npts * dimension or non_finite:
        raise NativeDatasetError(f"{label} payload count or finiteness check failed")
    checksum = digest.hexdigest()
    if "checksum" in expected and checksum != expected["checksum"]:
        raise NativeDatasetError(f"{label} Dataset SHA-256 differs")
    return {"npts": npts, "dimension": dimension, "byte_length": length,
            "sha256": checksum, "non_finite_count": non_finite, "value_count": value_count}


def write_immutable_bytes(target: Path, source: Path) -> None:
    if target.exists() or target.is_symlink():
        raise NativeDatasetError(f"immutable target already exists: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f".{target.name}.", suffix=".tmp", dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output, length=8 * 1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        try:
            os.link(temporary, target)
        except FileExistsError as error:
            raise NativeDatasetError(f"immutable target already exists: {target}") from error
        except OSError as error:
            raise NativeDatasetError(f"cannot publish immutable Dataset payload: {target}") from error
        target.chmod(0o444)
    finally:
        temporary.unlink(missing_ok=True)


def dataset_identity_projection(dataset_checksum: str, structural: dict[str, Any], payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": "1.0.0", "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": "dataset", "format_version": FORMAT,
        "dataset_checksum": dataset_checksum, "id_space": "none", "capabilities": [CAPABILITY],
        "payloads": [{k: payload[k] for k in ("role", "byte_length", "sha256")}],
        "source_artifacts": [], "structural": structural,
    }


def _unused_output_path(root: Path, relative_path: str | Path, repository_root: Path) -> Path:
    # Check the original leaf before resolve() can hide a dangling symlink.
    original = root / relative_path
    if original.is_symlink() or original.exists():
        raise NativeDatasetError(f"immutable target already exists: {original}")
    target = resolve_confined_path(root, relative_path)
    reject_upstream_target(target, repository_root)
    if target.exists() or target.is_symlink():
        raise NativeDatasetError(f"immutable target already exists: {target}")
    return target


def publish_dataset(
    source: Path, workspace: Path, dataset_id: str, facts: dict[str, Any], *,
    repository_root: Path, directory: str, evidence_path: str,
) -> dict[str, Any]:
    """Publish scanned bytes. The copy must match the scan before metadata is written."""
    if not isinstance(dataset_id, str) or not dataset_id.strip():
        raise NativeDatasetError("dataset_id must be a non-empty string")
    roots = normalize_workspace(workspace, repository_root=repository_root)
    workspace = Path(roots["root"])
    normal = resolve_confined_path(workspace, "normal")
    directory_path = resolve_confined_path(normal, directory)
    # Check every target before writing; payload references stay within this directory.
    targets = [_unused_output_path(directory_path, name, repository_root)
               for name in ("vectors.bin", "artifact.json")]
    evidence_file = _unused_output_path(workspace, evidence_path, repository_root)
    destination, metadata_file = targets
    write_immutable_bytes(destination, source)
    destination_checksum = sha256_file(destination)
    if destination_checksum != facts["sha256"]:
        raise NativeDatasetError("admitted Dataset checksum differs from source")
    structural = {"global_node_count": facts["npts"], "vector_dimension": facts["dimension"],
                  "scalar_type": "float32", "distance_metric": "l2"}
    payload = {"role": "vectors", "path": "vectors.bin", "media_type": MEDIA,
               "byte_length": facts["byte_length"], "sha256": facts["sha256"]}
    projection = dataset_identity_projection(facts["sha256"], structural, payload)
    checksum = aggregate_checksum(projection)
    metadata = {k: v for k, v in projection.items() if k != "dataset_checksum"}
    metadata.update(dataset={"dataset_id": dataset_id, "dataset_checksum": facts["sha256"], **structural},
                    payloads=[payload], aggregate_checksum=checksum)
    write_immutable_json(metadata_file, metadata)
    artifact_ref = {"metadata_path": metadata_file.relative_to(normal).as_posix(),
                    "artifact_type": "dataset", "format_version": FORMAT, "aggregate_checksum": checksum}
    evidence = {
        "evidence_version": "1.0.0", "validator_version": "1.0.0",
        "artifact_candidate": {"artifact_type": "dataset", "format_version": FORMAT,
                               "metadata_path": artifact_ref["metadata_path"], "claimed_aggregate_checksum": checksum},
        "checks": [
            {"check_id": "header", "passed": True, "message": f"{facts['npts']}x{facts['dimension']} float32 header is valid"},
            {"check_id": "byte_length", "passed": True, "message": "exact Dataset byte length is valid"},
            {"check_id": "payload_checksum", "passed": True, "message": "source and admitted SHA-256 match"},
            {"check_id": "finite_payload", "passed": True, "message": "all float32 payload values are finite"},
        ], "result": "VALID",
    }
    write_immutable_json(evidence_file, evidence)
    return {"artifact_ref": artifact_ref, "validation_evidence_path": evidence_file.relative_to(workspace).as_posix(),
            "source_sha256": facts["sha256"], "destination_sha256": destination_checksum,
            "non_finite_count": facts["non_finite_count"], "facts": facts}


def admit_dataset(
    dataset: dict[str, Any], workspace: str | Path, *, repository_root: str | Path,
    source_dir: str | Path | None = None,
) -> dict[str, Any]:
    required = {"path", "dataset_id", "global_node_count", "vector_dimension", "scalar_type", "distance_metric"}
    if not isinstance(dataset, dict) or not required <= dataset.keys() or dataset.keys() - required - {"checksum", "byte_length"}:
        raise NativeDatasetError("native Dataset declaration has missing or unknown fields")
    if dataset["scalar_type"] != "float32" or dataset["distance_metric"] != "l2":
        raise NativeDatasetError("native Dataset admission requires float32/L2")
    if not isinstance(dataset["path"], str) or not dataset["path"] or "\x00" in dataset["path"]:
        raise NativeDatasetError("native Dataset source path is invalid")
    if not isinstance(dataset["dataset_id"], str) or not dataset["dataset_id"].strip():
        raise NativeDatasetError("dataset_id must be a non-empty string")
    repository = Path(repository_root).resolve()
    roots = normalize_workspace(workspace, repository_root=repository)
    root = Path(roots["root"])
    input_root = repository if source_dir is None else Path(source_dir).resolve()
    source = Path(dataset["path"])
    source = source.resolve() if source.is_absolute() else resolve_confined_path(input_root, source)
    facts = scan_dataset(source, dataset)
    # Names depend on the complete data identity, never on a caller-supplied path fragment.
    key = hashlib.sha256((dataset["dataset_id"] + "\x00" + facts["sha256"]).encode("utf-8")).hexdigest()
    record_dir = f"manifests/dataset-admission/{key}"
    record_path = _unused_output_path(root, f"{record_dir}/admission.json", repository)
    reference_path = _unused_output_path(root, f"{record_dir}/artifact-ref.json", repository)
    result = publish_dataset(source, root, dataset["dataset_id"], facts, repository_root=repository,
                             directory=f"datasets/{key}", evidence_path=f"{record_dir}/validation.json")
    result.update(source_path=str(source), declared_dataset=dataset,
                  admission_source={"path": str(Path(__file__).resolve()), "sha256": sha256_file(Path(__file__))},
                  artifact_ref_path=reference_path.relative_to(root).as_posix(),
                  admission_record_path=record_path.relative_to(root).as_posix())
    write_immutable_json(reference_path, result["artifact_ref"])
    write_immutable_json(record_path, result)
    return result
