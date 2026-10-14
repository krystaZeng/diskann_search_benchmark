"""Two-pass compactness for complete, disjoint native float32/L2 partitions.

This evaluator's admission checks do not redefine PartitionArtifact validity.
No native stage is executed. Only the independent report is published.
"""

from __future__ import annotations

import argparse
from array import array
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import platform
import struct
import sys
import time
from typing import Iterator

from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource

from .checksum import canonicalize_restricted_json, sha256_file
from .framework_baseline_certification import (
    _aggregate_artifact_metadata,
    _dataset_artifact_validator,
)
from .manifests import write_immutable_json
from .runner import _schema_validator
from .workspace import reject_upstream_target, resolve_confined_path


REPOSITORY = Path(__file__).resolve().parents[3]
SCHEMA = REPOSITORY / "contracts/partition/compactness-v1/evaluation-report.schema.json"
IMPLEMENTATION_ID = "partition_full_mean_squared_l2_v1"
VERSION = "1.0.0"
SOURCE_SHA256 = sha256_file(__file__)
DATASET_FORMAT = "diskann-float32-dataset/v1"
FLOAT_MEDIA = "application/vnd.diskann.cpp-main.float32-bin;version=1"
MAP_MEDIA = "application/vnd.diskann.cpp-main.uint32-id-map;version=1"
CENTROID_MEDIA = "application/vnd.diskann.cpp-main.float32-centroids;version=1"
DEFINITIONS = {
    "mean": "mu_j = sum(x for all nodes in partition j) / n_j",
    "per_partition": "C_j = sum(sum_d((x_d - mu_j_d)^2)) / n_j",
    "overall": "C = sum(n_j * C_j) / sum(n_j); sum(n_j) = dataset node count",
    "distance": "squared L2; no square root or division by dimension",
    "training_centroids": "validated when present; never used in compactness",
}


class CompactnessError(RuntimeError):
    """Evaluation failed; no successful report may be published."""


class UnsupportedInput(CompactnessError):
    """Outside this evaluator's scope, not a public Artifact invalidity claim."""


def _unsupported(reason: str) -> None:
    raise UnsupportedInput(f"不在当前评估器支持范围 (unsupported input): {reason}")


def _document(path: Path) -> dict:
    # Existing canonical JSON parsing rejects duplicate keys and non-integer
    # metadata values, including NaN/Inf. Reports themselves contain floats.
    value = json.loads(canonicalize_restricted_json(path.read_bytes()))
    if type(value) is not dict:
        raise CompactnessError(f"document must be an object: {path}")
    return value


def _stamp(path: Path) -> tuple:
    stat = path.stat()
    return (str(path.resolve()), stat.st_dev, stat.st_ino, stat.st_size,
            stat.st_mtime_ns, stat.st_ctime_ns)


def _remember(path: Path, snapshots: dict, expected: dict | None = None) -> None:
    before = _stamp(path)
    digest = sha256_file(path)
    if _stamp(path) != before:
        raise CompactnessError(f"input changed while hashing: {path}")
    if expected and (before[3] != expected["byte_length"] or digest != expected["sha256"]):
        raise CompactnessError(f"payload length/checksum mismatch: {path}")
    identity = (before, digest)
    if path in snapshots and snapshots[path] != identity:
        raise CompactnessError(f"input changed during admission: {path}")
    snapshots[path] = identity


def _path(root: Path, relative: str) -> Path:
    resolve_confined_path(root, relative)
    # Retain the lexical path so changing a symlink is also detected at closure.
    return root / relative


def _artifact(workspace: Path, reference: dict, kind: str, snapshots: dict) -> tuple:
    _schema_validator("artifact-ref.schema.json", REPOSITORY).validate(reference)
    version = DATASET_FORMAT if kind == "dataset" else "partition-artifact/v1"
    if reference["artifact_type"] != kind:
        raise CompactnessError(f"expected {kind} ArtifactRef")
    if reference["format_version"] != version:
        _unsupported(f"{kind} format {reference['format_version']}")
    path = _path(workspace / "normal", reference["metadata_path"])
    _remember(path, snapshots)
    metadata = _document(path)
    identity = metadata.get("dataset", {})
    if not isinstance(identity, dict):
        raise CompactnessError("metadata.dataset must be an object")
    if identity.get("scalar_type") != "float32" or identity.get("distance_metric") != "l2":
        _unsupported("only float32/L2 inputs are supported")
    validator = (_dataset_artifact_validator(REPOSITORY, version) if kind == "dataset"
                 else _schema_validator("partition-artifact.schema.json", REPOSITORY))
    validator.validate(metadata)
    capabilities = metadata["capabilities"]
    if any(not isinstance(capability, str) or not capability for capability in capabilities):
        raise CompactnessError("capabilities must contain only non-empty strings")
    if any(left >= right for left, right in zip(capabilities, capabilities[1:])):
        raise CompactnessError("capabilities must be strictly increasing (sorted and unique)")
    if any(metadata[field] != reference[field]
           for field in ("artifact_type", "format_version", "aggregate_checksum")):
        raise CompactnessError(f"{kind} ArtifactRef identity mismatch")
    if _aggregate_artifact_metadata(metadata) != reference["aggregate_checksum"]:
        raise CompactnessError(f"{kind} aggregate checksum mismatch")
    payloads = {}
    for payload in metadata["payloads"]:
        if payload["role"] in payloads:
            raise CompactnessError("duplicate payload role")
        payload_path = _path(path.parent, payload["path"])
        _remember(payload_path, snapshots, payload)
        payloads[payload["role"]] = (payload_path, payload)
    return metadata, payloads


def _check_header(path: Path, header: bytes, rows: int, dimension: int) -> None:
    if path.stat().st_size != 8 + rows * dimension * 4:
        raise CompactnessError(f"native matrix length mismatch: {path}")
    if len(header) != 8 or struct.unpack("<II", header) != (rows, dimension):
        raise CompactnessError(f"native matrix header mismatch: {path}")


def _rows(path: Path, rows: int, dimension: int, code: str,
          batch_rows: int, expected_sha256: str) -> Iterator[tuple]:
    digest = hashlib.sha256()
    unpacker = struct.Struct("<" + code * dimension)
    with path.open("rb") as source:
        header = source.read(8)
        _check_header(path, header, rows, dimension)
        digest.update(header)
        for start in range(0, rows, batch_rows):
            size = min(batch_rows, rows - start) * dimension * 4
            block = source.read(size)
            if len(block) != size:
                raise CompactnessError(f"native matrix truncated during evaluation: {path}")
            digest.update(block)
            for row in unpacker.iter_unpack(block):
                if code == "f" and not all(map(math.isfinite, row)):
                    raise CompactnessError(f"non-finite vector value: {path}")
                yield row
        if source.read(1) or digest.hexdigest() != expected_sha256:
            raise CompactnessError(f"input changed or checksum mismatch during read: {path}")


def _admit(request: dict, snapshots: dict) -> tuple:
    workspace = Path(request["workspace"]).resolve()
    dataset, data_payloads = _artifact(workspace, request["dataset"], "dataset", snapshots)
    partition, payloads = _artifact(workspace, request["partition"], "partition", snapshots)
    identity = dataset["dataset"]
    if partition["dataset"] != identity:
        raise CompactnessError("Dataset/Partition dataset identity mismatch")
    source = {key: request["dataset"][key] for key in
              ("artifact_type", "format_version", "aggregate_checksum")}
    if partition["source_artifacts"] != [source]:
        raise CompactnessError("Partition source Dataset reference mismatch")
    expected_structural = {key: identity[key] for key in
                           ("global_node_count", "vector_dimension", "scalar_type", "distance_metric")}
    if dataset["structural"] != expected_structural:
        raise CompactnessError("Dataset structural identity mismatch")
    vectors, vector_payload = data_payloads["vectors"]
    if vector_payload["sha256"] != identity["dataset_checksum"]:
        raise CompactnessError("Dataset content checksum differs from vectors payload")
    if "partition.overlap/v1" in partition["capabilities"]:
        _unsupported("overlapping partitions")
    records = partition["structural"]["partitions"]
    nodes, dimension = identity["global_node_count"], identity["vector_dimension"]
    # Do not size the ownership table or accumulators from an unverified header.
    with vectors.open("rb") as source_file:
        _check_header(vectors, source_file.read(8), nodes, dimension)
    if len(records) != partition["structural"]["partition_count"]:
        raise CompactnessError("partition_count differs from records")
    if len(records) > nodes:
        _unsupported("nonempty disjoint partition count exceeds Dataset node count")
    owners = array("I", [0]) * nodes  # One uint32 per node; not constant memory.
    if owners.itemsize != 4:
        _unsupported("runtime uint32 array representation")
    expected_roles = []
    for index, record in enumerate(records):
        if record["partition_id"] != index:
            raise CompactnessError("Partition IDs must be contiguous and ordered")
        count = record["local_node_count"]
        role = record["local_to_global_map_role"]
        if role == "centroids" or role not in payloads:
            raise CompactnessError("required partition map role is missing or reserved")
        path, payload = payloads[role]
        if payload["media_type"] != MAP_MEDIA:
            _unsupported("only native uint32 ID mappings are supported")
        previous = -1
        for (node,) in _rows(path, count, 1, "I", request["batch_rows"], payload["sha256"]):
            if node >= nodes:
                raise CompactnessError("GlobalNodeId out of range")
            if owners[node] == index + 1:
                raise CompactnessError("duplicate GlobalNodeId within a partition")
            if owners[node]:
                _unsupported("GlobalNodeId occurs in multiple partitions")
            if "partition.local_to_global_ascending/v1" in partition["capabilities"] and node <= previous:
                raise CompactnessError("mapping violates declared ascending capability")
            owners[node] = index + 1
            previous = node
        if "shard_vectors_role" in record:
            shard_role = record["shard_vectors_role"]
            if shard_role == "centroids" or shard_role not in payloads:
                raise CompactnessError("shard vectors role is missing or reserved")
            shard, declaration = payloads[shard_role]
            if declaration["media_type"] != FLOAT_MEDIA:
                _unsupported("shard vector encoding")
            for _ in _rows(shard, count, dimension, "f", request["batch_rows"], declaration["sha256"]):
                pass
            expected_roles.append(shard_role)
        expected_roles.append(role)
    if 0 in owners or sum(record["local_node_count"] for record in records) != nodes:
        _unsupported("partition must cover every Dataset node exactly once; missing IDs")
    if "centroids" in payloads:
        centroids, declaration = payloads["centroids"]
        if declaration["media_type"] != CENTROID_MEDIA:
            _unsupported("centroid encoding")
        for _ in _rows(centroids, len(records), dimension, "f", request["batch_rows"], declaration["sha256"]):
            pass
        expected_roles.append("centroids")
    if ("partition.centroids/v1" in partition["capabilities"]) != ("centroids" in payloads):
        raise CompactnessError("centroids capability and payload must be declared together")
    if list(payloads) != expected_roles or len(set(expected_roles)) != len(expected_roles):
        raise CompactnessError("partition payload roles/order differ from structural records")
    return identity, records, vectors, owners


def _add(sums: list, corrections: list, index: int, value: float) -> None:
    """Neumaier compensated float64 summation, including mixed-sign values."""
    previous = sums[index]
    total = previous + value
    corrections[index] += ((previous - total) + value if abs(previous) >= abs(value)
                           else (value - total) + previous)
    sums[index] = total


def _compute(identity: dict, records: list, vectors: Path, owners: array,
             batch_rows: int) -> tuple[list, float]:
    nodes, dimension = identity["global_node_count"], identity["vector_dimension"]
    sums = [[0.0] * dimension for _ in records]
    corrections = [[0.0] * dimension for _ in records]
    # Pass one: all vectors, no training centroids or E[x^2] - E[x]^2.
    for node, row in enumerate(_rows(vectors, nodes, dimension, "f", batch_rows, identity["dataset_checksum"])):
        index = owners[node] - 1
        for column, value in enumerate(row):
            _add(sums[index], corrections[index], column, value)
    means = [[(s + c) / record["local_node_count"] for s, c in zip(total, correction)]
             for total, correction, record in zip(sums, corrections, records)]
    if not all(math.isfinite(value) for mean in means for value in mean):
        raise CompactnessError("non-finite full-partition mean")
    squared_sums, squared_corrections = [0.0] * len(records), [0.0] * len(records)
    # Pass two: squared distances about the full means, fsum across dimensions.
    for node, row in enumerate(_rows(vectors, nodes, dimension, "f", batch_rows, identity["dataset_checksum"])):
        index = owners[node] - 1
        distance = math.fsum((value - mean) ** 2 for value, mean in zip(row, means[index]))
        _add(squared_sums, squared_corrections, index, distance)
    findings = [{"partition_id": record["partition_id"], "node_count": record["local_node_count"],
                 "compactness": (s + c) / record["local_node_count"]}
                for record, s, c in zip(records, squared_sums, squared_corrections)]
    overall = math.fsum(item["node_count"] * item["compactness"] for item in findings) / nodes
    return findings, overall


def report_validator() -> Draft202012Validator:
    schema = json.loads(SCHEMA.read_text())
    Draft202012Validator.check_schema(schema)
    common = _schema_validator("common.schema.json", REPOSITORY).schema
    registry = Registry().with_resource(common["$id"], Resource.from_contents(common))
    return Draft202012Validator(schema, registry=registry)


def evaluate_request(request_path: str | Path) -> dict:
    """Publish one new report, or raise without publishing successful metrics.

    Scale approval is an operator prerequisite under AGENTS.md, not an algorithm
    capacity bound. This evaluator does not add or bypass a pipeline run gate.
    """
    started = time.monotonic()
    started_at = datetime.now(timezone.utc).isoformat()
    if sha256_file(__file__) != SOURCE_SHA256:
        raise CompactnessError("evaluator source changed since module load")
    snapshots: dict = {}
    request_path = Path(request_path).absolute()
    _remember(request_path, snapshots)
    request = _document(request_path)
    if set(request) != {"request_version", "workspace", "dataset", "partition", "batch_rows", "report_path"}:
        raise CompactnessError("unexpected or missing compactness request fields")
    if request["request_version"] != "partition-compactness-request/v1":
        raise CompactnessError("unsupported request version")
    if type(request["batch_rows"]) is not int or request["batch_rows"] <= 0:
        raise CompactnessError("batch_rows must be a positive integer")
    for field in ("workspace", "report_path"):
        if type(request[field]) is not str or not Path(request[field]).is_absolute():
            raise CompactnessError(f"{field} must be an absolute path")
    output = reject_upstream_target(request["report_path"], REPOSITORY)
    workspace = Path(request["workspace"]).resolve()
    for directory in ("normal", "manifests", "temporary", "quarantine"):
        if output.is_relative_to(workspace / directory):
            raise CompactnessError("report must be separate from Artifact/BuildManifest storage")
    if output.exists() or output.is_symlink():
        raise CompactnessError(f"immutable target already exists: {output}")
    _remember(SCHEMA, snapshots)
    identity, records, vectors, owners = _admit(request, snapshots)
    findings, overall = _compute(identity, records, vectors, owners, request["batch_rows"])
    if any(not math.isfinite(value) or value < 0 for value in
           [overall, *(item["compactness"] for item in findings)]):
        raise CompactnessError("non-finite or negative compactness result")
    for path, (stamp, digest) in snapshots.items():
        if _stamp(path) != stamp or sha256_file(path) != digest or _stamp(path) != stamp:
            raise CompactnessError(f"input changed during evaluation: {path}")
    if sha256_file(__file__) != SOURCE_SHA256:
        raise CompactnessError("evaluator source changed during evaluation")
    report = {
        "report_version": "partition-compactness/v1", "status": "SUCCEEDED",
        "evaluator": {"implementation_id": IMPLEMENTATION_ID, "version": VERSION,
                      "source_path": str(Path(__file__).resolve().relative_to(REPOSITORY)),
                      "source_sha256": SOURCE_SHA256},
        "inputs": {"workspace": str(workspace), "dataset": request["dataset"],
                   "partition": request["partition"], "dataset_identity": identity,
                   "dataset_content_sha256": identity["dataset_checksum"]},
        "parameters": {"batch_rows": request["batch_rows"], "passes": 2,
                       "accumulator": "float64_neumaier_and_math_fsum"},
        "metric_definitions": DEFINITIONS,
        "partition_count": len(records), "node_count": identity["global_node_count"],
        "partitions": findings, "weighted_compactness": overall,
        "input_validation": {"status": "VALID", "scope": "complete_disjoint_native_float32_l2",
                             "checks": ["existing_metadata_schemas", "artifact_refs_and_lineage",
                                        "payload_lengths_and_sha256", "aggregate_checksums",
                                        "native_headers", "complete_unique_coverage", "nonempty_partitions",
                                        "declared_mapping_order", "finite_vectors_and_optional_centroids",
                                        "input_stamps_and_sha256_unchanged"],
                             "files": [{"path": str(path), "byte_length": stamp[3], "sha256": digest}
                                       for path, (stamp, digest) in snapshots.items()]},
        "execution": {"started_at": started_at, "ended_at": datetime.now(timezone.utc).isoformat(),
                      "elapsed_seconds": time.monotonic() - started, "python": platform.python_version(),
                      "platform": platform.platform(), "ownership_map_bytes": len(owners) * owners.itemsize},
    }
    report_validator().validate(report)
    write_immutable_json(output, report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True)
    args = parser.parse_args(argv)
    try:
        report = evaluate_request(args.request)
        print(json.dumps({"status": report["status"], "weighted_compactness": report["weighted_compactness"]}))
        return 0
    except (RuntimeError, ValueError, OSError, KeyError, TypeError, OverflowError, ValidationError) as error:
        print(json.dumps({"outcome": "FAILED", "failure": {
            "category": "unsupported_input" if isinstance(error, UnsupportedInput) else "evaluation_failed",
            "reason": str(error)}}, ensure_ascii=False), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
