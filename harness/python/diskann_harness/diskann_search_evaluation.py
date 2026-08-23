"""Feature-private DiskANN post-graph search/evaluation lifecycle."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator

from .manifests import write_immutable_json
from .runner import _schema_validator


class SearchEvaluationError(RuntimeError):
    """Fail-closed Feature 005 request error."""


_PQ_PARAMETERS = {
    "B": 0.03,
    "QD": 0,
    "pq_chunks": 32,
    "sample_rate": 0.256,
    "centroid_count": 256,
    "opq": False,
    "distance_metric": "l2",
}
_LAYOUT_PARAMETERS = {
    "scalar_type": "float32",
    "pq_disk_bytes": 0,
    "reorder": False,
    "labels": False,
    "entry_policy": "end_to_end",
    "centroid_policy": "medoid_vector_fallback",
}

_PINNED_DISKANN_SOURCE = {
    "repository": "https://github.com/microsoft/DiskANN.git",
    "branch": "cpp_main",
    "commit": "78256bbab4685e1774e78d331e081a153be26823",
    "dirty_worktree": False,
}


def _repository_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _now_utc() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _python_harness_toolchain() -> dict[str, Any]:
    return {
        "os": platform.platform(),
        "compiler_path": sys.executable,
        "compiler_version": platform.python_version(),
        "cmake_version": "not-applicable-python-harness",
        "generator": "python-harness",
        "build_type": "interpreted",
        "cmake_definitions": [],
        "compile_commands": [],
        "link_commands": [],
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_digest(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    ).hexdigest()


def _git_source_identity(repository_root: Path) -> dict[str, Any]:
    def git(*arguments: str) -> str:
        result = subprocess.run(
            ["git", "-C", str(repository_root), *arguments],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise SearchEvaluationError(
                f"git source identity check failed: {result.stderr.strip()}"
            )
        return result.stdout.strip()

    status = git("status", "--porcelain=v1", "--untracked-files=all")
    identity = {
        "repository": git("remote", "get-url", "origin"),
        "branch": git("branch", "--show-current"),
        "commit": git("rev-parse", "HEAD"),
        "dirty_worktree": bool(status),
    }
    if status:
        tracked = git("diff", "--binary", "HEAD")
        material = f"status:\n{status}\ntracked:\n{tracked}"
        for relative in git("ls-files", "--others", "--exclude-standard").splitlines():
            path = repository_root / relative
            if path.is_file():
                material += f"\nuntracked:{relative}:{_sha256(path)}"
        identity["dirty_source_checksum"] = hashlib.sha256(
            material.encode()
        ).hexdigest()
    return identity


def _physical_memory_bytes() -> int:
    try:
        return int(os.sysconf("SC_PAGE_SIZE")) * int(os.sysconf("SC_PHYS_PAGES"))
    except (AttributeError, OSError, ValueError):
        return 0


def _known_file_check(repository_root: Path, declaration: Any) -> dict[str, Any]:
    if type(declaration) is not dict or set(declaration) != {
        "name", "path", "byte_length", "sha256"
    }:
        raise SearchEvaluationError("readiness file identity declaration is invalid")
    path = Path(declaration["path"])
    path = path.resolve() if path.is_absolute() else (repository_root / path).resolve()
    try:
        path.relative_to(repository_root)
    except ValueError as error:
        raise SearchEvaluationError("readiness file path escapes repository") from error
    if not path.is_file():
        raise SearchEvaluationError(f"readiness file is missing: {declaration['name']}")
    size = path.stat().st_size
    checksum = _sha256(path)
    if size != declaration["byte_length"] or checksum != declaration["sha256"]:
        raise SearchEvaluationError(
            f"readiness file identity mismatch: {declaration['name']}"
        )
    return {
        "name": declaration["name"],
        "path": str(path),
        "byte_length": size,
        "sha256": checksum,
        "status": "PASS",
    }


def _write_immutable_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError as error:
            raise SearchEvaluationError(f"immutable target already exists: {path}") from error
    finally:
        temporary.unlink(missing_ok=True)


def _feature_validator(name: str) -> Draft202012Validator:
    schema = json.loads(
        (_repository_root() / "specs/005-diskann-search-evaluation/contracts" / name).read_text(
            encoding="utf-8"
        )
    )
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def normalize_query_set_fixture(
    source: str | Path,
    *,
    workspace: str | Path,
    query_set_id: str,
    expected_dimension: int,
) -> dict[str, Any]:
    """Normalize a selected-query-style CSV for deterministic/small tests only."""
    source_path = Path(source)
    with source_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header = next(reader)
        if header != ["id"] + [f"v{index}" for index in range(expected_dimension)]:
            raise SearchEvaluationError("selected query header/dimension is invalid")
        rows = list(reader)
    if not rows:
        raise SearchEvaluationError("selected query set is empty")
    mapping: list[dict[str, Any]] = []
    values: list[float] = []
    for row_index, row in enumerate(rows):
        if len(row) != expected_dimension + 1:
            raise SearchEvaluationError("selected query row width is invalid")
        mapping.append(
            {"query_row_index": row_index, "source_query_id": row[0]}
        )
        try:
            converted = [float(value) for value in row[1:]]
        except ValueError as error:
            raise SearchEvaluationError("selected query vector is not float32") from error
        if not all(math.isfinite(value) for value in converted):
            raise SearchEvaluationError("selected query vector is not finite")
        values.extend(converted)
    root = Path(workspace).resolve()
    native = root / "inputs" / f"{query_set_id}.bin"
    mapping_path = root / "inputs" / f"{query_set_id}-rows.json"
    payload = struct.pack("<II", len(rows), expected_dimension) + struct.pack(
        f"<{len(values)}f", *values
    )
    _write_immutable_bytes(native, payload)
    write_immutable_json(mapping_path, mapping)
    reference = {
        "reference_version": "1.0.0",
        "query_set_id": query_set_id,
        "logical_source": source_path.as_posix(),
        "source": {
            "path": source_path.as_posix(), "format": "selected-query-csv/v1",
            "byte_length": source_path.stat().st_size, "sha256": _sha256(source_path),
        },
        "query_count": len(rows), "dimension": expected_dimension,
        "scalar_type": "float32", "query_identity": "query_row_index",
        "source_query_id_semantics": "opaque",
        "self_match_policy": "no_id_based_exclusion",
        "native_payload": {"path": native.relative_to(root).as_posix(), "format": "diskann-float32-bin", "byte_length": len(payload), "sha256": _sha256(native)},
        "row_mapping": {"path": mapping_path.relative_to(root).as_posix(), "format": "json", "byte_length": mapping_path.stat().st_size, "sha256": _sha256(mapping_path)},
        "conversion_manifest_path": "manifests/query-set-fixture.json",
        "validation_evidence_path": "manifests/evidence/query-set-fixture.json",
    }
    reference["canonical_checksum"] = _canonical_digest(reference)
    return {"reference": reference, "mapping": mapping, "native_header": (len(rows), expected_dimension)}


def normalize_search_protocol(value: Any) -> dict[str, Any]:
    """Sole canonical normalization path used by formal-search preflight."""
    document = json.loads(json.dumps(value))
    execution = document.get("search_execution")
    if type(execution) is not dict:
        raise SearchEvaluationError("SearchProtocol search_execution is invalid")
    execution["L_search"] = list(execution.get("L_search", []))
    document["search_execution_checksum"] = _canonical_digest(execution)
    protocol_projection = dict(document)
    protocol_projection.pop("protocol_checksum", None)
    document["protocol_checksum"] = _canonical_digest(protocol_projection)
    errors = sorted(
        _feature_validator("search-protocol.schema.json").iter_errors(document),
        key=lambda item: list(item.path),
    )
    if errors:
        raise SearchEvaluationError(f"SearchProtocol is invalid: {errors[0].message}")
    return document


def audit_excluded_observation(
    *, observation_id: str, path: str, checksum: str, status: str, reason: str
) -> dict[str, Any]:
    return {
        "observation_id": observation_id, "path": path, "sha256": checksum,
        "terminal_status": status, "quality_eligible": False,
        "quality_eligibility_reason": reason, "performance_eligible": False,
        "performance_eligibility_reason": reason,
    }


def _percentile_999(values: list[float]) -> float:
    if not values:
        raise SearchEvaluationError("latency observation is empty")
    ordered = sorted(values)
    rank = min(len(ordered) - 1, int(0.999 * len(ordered)))
    return ordered[rank]


def evaluate_raw_point(
    observation: dict[str, Any], *, ground_truth: list[list[int]],
    quality_compatible: bool, performance_admitted: bool,
    ground_truth_distances: list[list[float]] | None = None,
) -> dict[str, Any]:
    queries = observation.get("per_query", [])
    complete = (
        observation.get("status") == "SUCCEEDED"
        and observation.get("completed_query_count") == observation.get("query_count")
        and len(queries) == observation.get("query_count") == len(ground_truth)
        and all(len(item.get("result_ids", [])) == 100 and len(item.get("result_distances", [])) == 100 for item in queries)
    )
    if not complete:
        return audit_excluded_observation(
            observation_id=observation.get("observation_id", "invalid-point"),
            path=observation.get("path", "observations/invalid.json"),
            checksum=observation.get("checksum", "0" * 64),
            status=observation.get("status", "INVALID_OUTPUT"),
            reason="incomplete or invalid raw observation",
        )
    if ground_truth_distances is not None and len(ground_truth_distances) != len(ground_truth):
        raise SearchEvaluationError("GroundTruth distance rows are incompatible")
    recall10 = []
    recall100 = []
    for row, (query, truth) in enumerate(zip(queries, ground_truth)):
        distances = None if ground_truth_distances is None else ground_truth_distances[row]
        recall10.append(_diskann_recall_one(truth, distances, query["result_ids"], 10))
        recall100.append(_diskann_recall_one(truth, distances, query["result_ids"], 100))
    elapsed = observation["batch_wall_clock_us"] / 1_000_000.0
    if elapsed <= 0:
        raise SearchEvaluationError("batch timing is invalid")
    display = {
        "qps_k100": len(queries) / elapsed,
        "mean_latency_us_k100": sum(item["total_us"] for item in queries) / len(queries),
        "p99_9_latency_us_k100": _percentile_999([item["total_us"] for item in queries]),
        "mean_ios_k100": sum(item["n_ios"] for item in queries) / len(queries),
    }
    result = {
        "quality_eligible": bool(quality_compatible),
        "quality_eligibility_reason": "compatible" if quality_compatible else "quality identity mismatch",
        "performance_eligible": bool(performance_admitted),
        "performance_eligibility_reason": "admitted" if performance_admitted else "non-normative environment or parity pending",
        "display_metrics": display,
    }
    if quality_compatible:
        result["quality_metrics"] = {
            "recall_at_10": sum(recall10) / len(recall10),
            "recall_at_100": sum(recall100) / len(recall100),
        }
    if performance_admitted:
        result["performance_metrics"] = display
        result["performance_metric_classification"] = "formal"
    return result


def _read_request(path: str | Path) -> dict[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SearchEvaluationError("input-materialization request cannot be read") from error
    if type(value) is not dict:
        raise SearchEvaluationError("input-materialization request must be an object")
    return value


def _manifest_document(
    request: dict[str, Any], *, implementation_id: str, parameters: dict[str, Any],
    source: dict[str, Any], toolchain: dict[str, Any], started_at: str,
    ended_at: str, status: str, failure: dict[str, Any] | None = None,
) -> dict[str, Any]:
    manifest = {
        "manifest_version": "1.0.0", "run_id": request["run_id"], "stage": "build",
        "implementation": {"implementation_id": implementation_id, "implementation_version": "1.0.0"},
        "source": source, "toolchain": toolchain,
        "dataset": request["dataset_identity"], "parameters": parameters,
        "resource_budget": request["resource_budget"], "input_artifacts": request.get("input_artifacts", []),
        "output_artifacts": [], "status": status,
        "started_at": started_at, "ended_at": ended_at,
    }
    if failure is not None:
        manifest["failure"] = failure
    return manifest


def _input_evidence(
    *, candidate_path: str, format_version: str, checksum: str,
    valid: bool, message: str,
) -> dict[str, Any]:
    evidence = {
        "evidence_version": "1.0.0",
        "validator_version": "feature-input-validator/1.0.0",
        "artifact_candidate": {
            "artifact_type": "dataset",
            "format_version": format_version,
            "metadata_path": candidate_path,
            "claimed_aggregate_checksum": checksum,
        },
        "checks": [{"check_id": "feature.input.contract/v1", "passed": valid, "message": message}],
        "result": "VALID" if valid else "INVALID",
    }
    if not valid:
        evidence["failure_category"] = "schema_invalid"
    return evidence


def materialize_query_set_request(request_path: str | Path) -> dict[str, Any]:
    request = _read_request(request_path)
    root = Path(request["workspace_root"]).resolve()
    project_source = _git_source_identity(_repository_root())
    toolchain = _python_harness_toolchain()
    started_at = _now_utc()
    try:
        source = Path(request["source_csv"])
        if source.stat().st_size != request["source_byte_length"] or _sha256(source) != request["source_sha256"]:
            raise SearchEvaluationError("QuerySet source identity mismatch")
        result = normalize_query_set_fixture(
            source, workspace=root, query_set_id=request["query_set_id"],
            expected_dimension=request["dimension"],
        )
        if result["reference"]["query_count"] != request["query_count"]:
            raise SearchEvaluationError("QuerySet count mismatch")
        reference_path = root / request["reference_path"]
        manifest_path = root / request["manifest_path"]
        evidence_path = root / request["validation_evidence_path"]
        result["reference"]["conversion_manifest_path"] = request["manifest_path"]
        result["reference"]["validation_evidence_path"] = request["validation_evidence_path"]
        result["reference"]["canonical_checksum"] = _canonical_digest(
            {key: value for key, value in result["reference"].items() if key != "canonical_checksum"}
        )
        write_immutable_json(reference_path, result["reference"])
        write_immutable_json(
            evidence_path,
            _input_evidence(
                candidate_path=request["reference_path"], format_version="query-set-reference/v1",
                checksum=result["reference"]["canonical_checksum"], valid=True,
                message="CSV, native payload, row mapping, count, dimension, and checksum passed",
            ),
        )
        write_immutable_json(
            manifest_path,
            _manifest_document(
                request, implementation_id="selected_query_csv_to_diskann_float32_v1",
                parameters={
                    "source_csv": {
                        "path": str(source),
                        "byte_length": source.stat().st_size,
                        "sha256": _sha256(source),
                    },
                    "query_identity": "query_row_index",
                    "source_query_id_semantics": "opaque",
                    "self_match_policy": "no_id_based_exclusion",
                    "output_reference": request["reference_path"],
                },
                source=project_source, toolchain=toolchain,
                started_at=started_at, ended_at=_now_utc(),
                status="SUCCEEDED",
            ),
        )
        return {"status": "SUCCEEDED", "reference_path": str(reference_path), "manifest_path": str(manifest_path), "validation_evidence_path": str(evidence_path)}
    except Exception as error:
        manifest_path = root / request["manifest_path"]
        if not manifest_path.exists():
            write_immutable_json(
                manifest_path,
                _manifest_document(
                    request, implementation_id="selected_query_csv_to_diskann_float32_v1",
                    parameters={"source_csv": request.get("source_csv", "")},
                    source=project_source, toolchain=toolchain,
                    started_at=started_at, ended_at=_now_utc(), status="FAILED",
                    failure={"category": "schema_invalid", "reason": str(error)},
                ),
            )
        raise


def _validate_truth_payload(path: Path, *, query_count: int, width: int, node_count: int) -> None:
    payload = path.read_bytes()
    expected = 8 + query_count * width * 8
    if len(payload) != expected:
        raise SearchEvaluationError("GroundTruth file size is invalid")
    actual_queries, actual_width = struct.unpack_from("<II", payload)
    if (actual_queries, actual_width) != (query_count, width):
        raise SearchEvaluationError("GroundTruth header is invalid")
    count = query_count * width
    ids = struct.unpack_from(f"<{count}I", payload, 8)
    distances = struct.unpack_from(f"<{count}f", payload, 8 + count * 4)
    if any(node >= node_count for node in ids) or not all(math.isfinite(value) and value >= 0 for value in distances):
        raise SearchEvaluationError("GroundTruth ID/distance is invalid")
    for row in range(query_count):
        values = distances[row * width : (row + 1) * width]
        if any(values[index] > values[index + 1] for index in range(width - 1)):
            raise SearchEvaluationError("GroundTruth distances are not ordered")
        if width > 100 and values[99] == values[-1]:
            raise SearchEvaluationError("GroundTruth rank-100 tie group is not closed")


def materialize_ground_truth_request(request_path: str | Path) -> dict[str, Any]:
    request = _read_request(request_path)
    root = Path(request["workspace_root"]).resolve()
    project_source = _git_source_identity(_repository_root())
    binary = Path(request["compute_groundtruth_binary"])
    output = root / request["payload_path"]
    argv = [str(binary), "--data_type", "float", "--dist_fn", "l2", "--base_file", request["dataset_payload"], "--query_file", request["query_payload"], "--gt_file", str(output), "--K", str(request["width"])]
    stdout_path = root / request["stdout_path"]
    stderr_path = root / request["stderr_path"]
    manifest_path = root / request["manifest_path"]
    binary_identity = {
        "path": str(binary),
        "expected_sha256": request["compute_groundtruth_sha256"],
    }
    started_at = _now_utc()
    completed: subprocess.CompletedProcess[str] | None = None

    def parameters(output_reference: str | None = None) -> dict[str, Any]:
        value: dict[str, Any] = {
            "argv": argv,
            "stdout": request["stdout_path"],
            "stderr": request["stderr_path"],
            "diskann_source": _PINNED_DISKANN_SOURCE,
            "compute_groundtruth_binary": binary_identity,
        }
        if output_reference is not None:
            value["output_reference"] = output_reference
        return value

    try:
        if not binary.is_file():
            raise SearchEvaluationError("compute_groundtruth binary is missing")
        actual_binary_sha256 = _sha256(binary)
        binary_identity["actual_sha256"] = actual_binary_sha256
        if actual_binary_sha256 != request["compute_groundtruth_sha256"]:
            raise SearchEvaluationError(
                "compute_groundtruth binary checksum mismatch"
            )
        binary_identity["sha256"] = actual_binary_sha256
        output.parent.mkdir(parents=True, exist_ok=True)
        completed = subprocess.run(argv, check=False, capture_output=True, text=True)
        _write_immutable_bytes(stdout_path, completed.stdout.encode())
        _write_immutable_bytes(stderr_path, completed.stderr.encode())
        if completed.returncode != 0:
            raise SearchEvaluationError("compute_groundtruth returned nonzero")
        _validate_truth_payload(output, query_count=request["query_count"], width=request["width"], node_count=request["dataset_identity"]["global_node_count"])
        reference = {
            "reference_version": "1.0.0", "ground_truth_id": request["ground_truth_id"],
            "payload": {"path": request["payload_path"], "format": "diskann-truthset", "byte_length": output.stat().st_size, "sha256": _sha256(output)},
            "query_count": request["query_count"], "width": request["width"], "id_space": "global",
            "distance_metric": "l2_squared_float32", "dataset_artifact": request["dataset_artifact"],
            "query_set_checksum": request["query_set_checksum"], "self_match_policy": "no_id_based_exclusion",
            "rank_100_tie_group_closed": True, "generation_manifest_path": request["manifest_path"],
            "validation_evidence_path": request["validation_evidence_path"],
        }
        reference["canonical_checksum"] = _canonical_digest(reference)
        reference_path = root / request["reference_path"]
        evidence_path = root / request["validation_evidence_path"]
        write_immutable_json(reference_path, reference)
        write_immutable_json(evidence_path, _input_evidence(candidate_path=request["reference_path"], format_version="ground-truth-reference/v1", checksum=reference["canonical_checksum"], valid=True, message="exact L2 truth header, width, ID range, distance order, checksum, and tie closure passed"))
        write_immutable_json(
            manifest_path,
            _manifest_document(
                request,
                implementation_id="cpp_main_compute_groundtruth_exact_l2_v1",
                parameters=parameters(request["reference_path"]),
                source=project_source, toolchain=request["toolchain"],
                started_at=started_at, ended_at=_now_utc(), status="SUCCEEDED",
            ),
        )
        return {"status": "SUCCEEDED", "reference_path": str(reference_path), "manifest_path": str(manifest_path), "validation_evidence_path": str(evidence_path)}
    except Exception as error:
        if not manifest_path.exists():
            failure: dict[str, Any] = {
                "category": "process_failed" if completed is not None and completed.returncode != 0 else "schema_invalid",
                "reason": str(error),
            }
            if completed is not None:
                failure["exit_code"] = completed.returncode
            write_immutable_json(
                manifest_path,
                _manifest_document(
                    request,
                    implementation_id="cpp_main_compute_groundtruth_exact_l2_v1",
                    parameters=parameters(), source=project_source,
                    toolchain=request["toolchain"], started_at=started_at,
                    ended_at=_now_utc(), status="FAILED", failure=failure,
                ),
            )
        raise


def _diskann_recall_one(
    truth_ids: list[int], truth_distances: list[float] | None,
    result_ids: list[int], recall_at: int,
) -> float:
    if len(truth_ids) < recall_at or len(result_ids) < recall_at:
        raise SearchEvaluationError("Recall input width is insufficient")
    tie_end = recall_at
    if truth_distances is not None:
        if len(truth_distances) != len(truth_ids):
            raise SearchEvaluationError("GroundTruth ID/distance widths differ")
        boundary = truth_distances[recall_at - 1]
        tie_end = recall_at - 1
        while tie_end < len(truth_distances) and truth_distances[tie_end] == boundary:
            tie_end += 1
    matched = len(set(truth_ids[:tie_end]) & set(result_ids[:recall_at]))
    return matched * (100.0 / recall_at)


def _read_ground_truth(
    path: Path, expected_count: int, expected_width: int,
) -> tuple[list[list[int]], list[list[float]]]:
    payload = path.read_bytes()
    count, width = struct.unpack_from("<II", payload)
    if count != expected_count or width != expected_width:
        raise SearchEvaluationError("GroundTruth identity is incompatible")
    item_count = count * width
    ids = struct.unpack_from(f"<{item_count}I", payload, 8)
    distances = struct.unpack_from(f"<{item_count}f", payload, 8 + item_count * 4)
    return (
        [list(ids[row * width : (row + 1) * width]) for row in range(count)],
        [list(distances[row * width : (row + 1) * width]) for row in range(count)],
    )


def _median(values: list[float]) -> float:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def _evaluate_observation_directory(
    *, protocol_path: str | Path, observations_path: str | Path,
    workspace: str | Path,
) -> dict[str, Any]:
    """Offline-only evaluator; never invokes the C++ runner or SSD search."""
    root = Path(workspace).resolve()
    protocol_file = Path(protocol_path).resolve()
    protocol = json.loads(protocol_file.read_text(encoding="utf-8"))
    normalized = normalize_search_protocol(protocol)
    if normalized != protocol:
        raise SearchEvaluationError("frozen SearchProtocol checksum/identity mismatch")
    execution = protocol["search_execution"]
    truth_ref = protocol["evaluation"]["ground_truth"]
    truth_path = root / truth_ref["payload"]["path"]
    if _sha256(truth_path) != truth_ref["payload"]["sha256"]:
        raise SearchEvaluationError("GroundTruth checksum mismatch")
    ground_truth, ground_truth_distances = _read_ground_truth(
        truth_path, truth_ref["query_count"], truth_ref["width"]
    )
    observation_dir = Path(observations_path).resolve()
    documents: list[tuple[Path, dict[str, Any]]] = []
    for path in sorted(observation_dir.glob("*.json")):
        document = json.loads(path.read_text(encoding="utf-8"))
        documents.append((path, document))
    if not documents:
        raise SearchEvaluationError("no SearchObservation was supplied")

    results: list[dict[str, Any]] = []
    display: list[dict[str, Any]] = []
    quality_groups: dict[tuple[str, int], list[dict[str, float]]] = {}
    performance_groups: dict[tuple[str, int], list[dict[str, float]]] = {}
    environment_path = None
    environment_sha = None
    for path, document in documents:
        point = document.get("raw_search_point", {})
        cache_mode = point.get("cache", {}).get("mode", "zero")
        l_search = point.get("L_search", 100)
        repetition = point.get("repetition_index", 0)
        file_checksum = _sha256(path)
        errors = list(
            _feature_validator("search-observation.schema.json").iter_errors(document)
        )
        checksum_projection = dict(document)
        claimed_checksum = checksum_projection.pop("observation_checksum", None)
        checksum_valid = claimed_checksum == _canonical_digest(checksum_projection)
        identity_compatible = (
            document.get("protocol", {}).get("protocol_checksum")
            == protocol["protocol_checksum"]
            and document.get("search_execution", {}).get("canonical_checksum")
            == protocol["search_execution_checksum"]
            and document.get("query_set_checksum")
            == execution["query_set"]["canonical_checksum"]
        )
        relative = path.relative_to(root).as_posix()
        if errors or not checksum_valid or not identity_compatible or point.get("status") != "SUCCEEDED":
            result = audit_excluded_observation(
                observation_id=document.get("observation_id", path.stem),
                path=relative, checksum=file_checksum,
                status=point.get("status", "INVALID_OUTPUT"),
                reason="invalid, incomplete, or identity-mismatched observation",
            )
            result.update({"cache_mode": cache_mode, "L_search": l_search, "repetition_index": repetition})
            results.append(result)
            continue
        environment_path = document["environment"]["fingerprint_path"]
        environment_sha = document["environment"]["fingerprint_sha256"]
        actual_environment = json.loads((root / environment_path).read_text(encoding="utf-8"))
        performance_admitted = bool(actual_environment.get("performance_admitted", False))
        raw = {
            "status": point["status"], "query_count": point["query_count"],
            "completed_query_count": point["completed_query_count"],
            "batch_wall_clock_us": point["batch_wall_clock_us"],
            "per_query": document["per_query"],
        }
        evaluated = evaluate_raw_point(
            raw, ground_truth=ground_truth,
            ground_truth_distances=ground_truth_distances,
            quality_compatible=True,
            performance_admitted=performance_admitted,
        )
        result = {
            "observation_id": document["observation_id"], "path": relative,
            "sha256": file_checksum, "cache_mode": cache_mode,
            "L_search": l_search, "repetition_index": repetition,
            "terminal_status": "SUCCEEDED",
            "quality_eligible": evaluated["quality_eligible"],
            "quality_eligibility_reason": evaluated["quality_eligibility_reason"],
            "performance_eligible": evaluated["performance_eligible"],
            "performance_eligibility_reason": evaluated["performance_eligibility_reason"],
            "quality_metrics": evaluated["quality_metrics"],
        }
        if performance_admitted:
            result["performance_metrics"] = evaluated["display_metrics"]
            result["performance_metric_classification"] = "formal"
            performance_groups.setdefault((cache_mode, l_search), []).append(
                evaluated["display_metrics"]
            )
        quality_groups.setdefault((cache_mode, l_search), []).append(
            evaluated["quality_metrics"]
        )
        display.append(
            {"observation_id": document["observation_id"], **evaluated["display_metrics"],
             "classification": "formal" if performance_admitted else "informational_non_formal"}
        )
        results.append(result)

    quality_aggregates = []
    for (cache_mode, l_search), values in sorted(quality_groups.items()):
        quality_aggregates.append({
            "cache_mode": cache_mode, "L_search": l_search,
            "eligible_count": len(values), "aggregation": "median_eligible",
            "median_quality_metrics": {
                key: _median([item[key] for item in values])
                for key in ("recall_at_10", "recall_at_100")
            },
        })
    performance_aggregates = []
    for (cache_mode, l_search), values in sorted(performance_groups.items()):
        performance_aggregates.append({
            "cache_mode": cache_mode, "L_search": l_search,
            "eligible_count": len(values), "aggregation": "median_eligible",
            "median_performance_metrics": {
                key: _median([item[key] for item in values])
                for key in ("qps_k100", "mean_latency_us_k100", "p99_9_latency_us_k100", "mean_ios_k100")
            },
        })
    if environment_path is None or environment_sha is None:
        raise SearchEvaluationError("no complete valid observation is eligible for a report")

    parity_path = root / "reports" / "search-parity" / f"{execution['execution_id']}.json"
    if parity_path.exists():
        parity = json.loads(parity_path.read_text(encoding="utf-8"))
    else:
        parity = {"result": "NOT_REACHED", "reason": "actual Search Runner Parity pending"}
        write_immutable_json(parity_path, parity)
    non_reexecution_path = root / "reports" / "search-evaluation" / f"{execution['execution_id']}-non-reexecution.json"
    non_reexecution = {"partition_invoked": False, "local_build_invoked": False, "merge_invoked": False, "proof": "Feature runner exposes no graph-stage operation"}
    if not non_reexecution_path.exists():
        write_immutable_json(non_reexecution_path, non_reexecution)
    report = {
        "report_version": "search-evaluation-report/v1",
        "report_id": f"{protocol['protocol_id']}-evaluation",
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "evaluator": {"implementation_id": "cpp_main_search_metrics_evaluator_v1", "version": "1.0.0", "reproduction_mode": "instrumented"},
        "diskann_source_mapping": {"repository": "https://github.com/microsoft/DiskANN.git", "branch": "cpp_main", "commit": "78256bbab4685e1774e78d331e081a153be26823", "research_path": "specs/005-diskann-search-evaluation/research.md"},
        "protocol": {"path": protocol_file.relative_to(root).as_posix(), "protocol_id": protocol["protocol_id"], "protocol_checksum": protocol["protocol_checksum"]},
        "search_execution": {"execution_id": execution["execution_id"], "canonical_checksum": protocol["search_execution_checksum"]},
        "dataset_artifact": execution["dataset_artifact"], "global_graph_artifact": execution["global_graph_artifact"],
        "pq_artifact": execution["pq_artifact"], "disk_layout_artifact": execution["disk_layout_artifact"],
        "query_set_checksum": execution["query_set"]["canonical_checksum"],
        "ground_truth": {"ground_truth_id": truth_ref["ground_truth_id"], "path": truth_ref["payload"]["path"], "sha256": truth_ref["payload"]["sha256"], "width": truth_ref["width"], "query_set_checksum": truth_ref["query_set_checksum"], "dataset_artifact": truth_ref["dataset_artifact"]},
        "entry_policy": "end_to_end",
        "graph_baseline_context": {"graph_structural_parity_gate": "NOT_REACHED", "framework_state": "candidate", "search_runner_parity_is_graph_parity": False, "final_baseline_certified": False},
        "environment_fingerprint": {"path": environment_path, "sha256": environment_sha},
        "search_runner_parity": {"evidence_path": parity_path.relative_to(root).as_posix(), "evidence_sha256": _sha256(parity_path), "result": parity["result"]},
        "non_reexecution_evidence": {**{key: non_reexecution[key] for key in ("partition_invoked", "local_build_invoked", "merge_invoked")}, "evidence_path": non_reexecution_path.relative_to(root).as_posix(), "evidence_sha256": _sha256(non_reexecution_path)},
        "observation_results": results,
        "aggregates": {"quality": quality_aggregates, "performance": performance_aggregates},
    }
    report["report_checksum"] = _canonical_digest(report)
    errors = list(_feature_validator("search-evaluation-report.schema.json").iter_errors(report))
    if errors:
        raise SearchEvaluationError(f"SearchEvaluationReport is invalid: {errors[0].message}")
    report_path = root / "reports" / "search-evaluation" / f"{report['report_id']}.json"
    write_immutable_json(report_path, report)
    return {"report_path": str(report_path), "report": report, "display": display}


def _evaluation_manifest(
    *, protocol: dict[str, Any], workspace: Path, started_at: str,
    ended_at: str, status: str, report_path: Path | None,
    observation_root: Path,
    failure: dict[str, Any] | None = None,
) -> dict[str, Any]:
    execution = protocol["search_execution"]
    dataset_ref = execution["dataset_artifact"]
    dataset_metadata = json.loads(
        (workspace / "normal" / dataset_ref["metadata_path"]).read_text(
            encoding="utf-8"
        )
    )
    runner_build = execution["runner_build"]
    observations = sorted(
        path.relative_to(workspace).as_posix()
        for path in observation_root.glob("*.json")
    )
    parameters: dict[str, Any] = {
        "protocol": {
            "protocol_id": protocol["protocol_id"],
            "protocol_checksum": protocol["protocol_checksum"],
            "search_execution_checksum": protocol["search_execution_checksum"],
        },
        "ground_truth": {
            "ground_truth_id": protocol["evaluation"]["ground_truth"]["ground_truth_id"],
            "canonical_checksum": protocol["evaluation"]["ground_truth"]["canonical_checksum"],
        },
        "evaluation_policy": {
            key: protocol["evaluation"][key]
            for key in (
                "metrics", "quality_eligibility_policy",
                "performance_eligibility_policy", "aggregation_policy",
                "replacement_repetitions",
            )
        },
        "observation_paths": observations,
    }
    if report_path is not None:
        parameters["output_reference"] = {
            "path": report_path.relative_to(workspace).as_posix(),
            "sha256": _sha256(report_path),
        }
    manifest: dict[str, Any] = {
        "manifest_version": "1.0.0",
        "run_id": f"{protocol['protocol_id']}-evaluation",
        "stage": "evaluate",
        "implementation": {
            "implementation_id": "cpp_main_search_metrics_evaluator_v1",
            "implementation_version": "1.0.0",
        },
        "source": {
            "repository": runner_build["project_repository"],
            "branch": runner_build["project_branch"],
            "commit": runner_build["project_commit"],
            "dirty_worktree": runner_build["project_dirty"],
        },
        "toolchain": {
            "os": platform.platform(),
            "compiler_path": sys.executable,
            "compiler_version": platform.python_version(),
            "cmake_version": "not-applicable-python-evaluator",
            "generator": "python-harness",
            "build_type": "interpreted",
            "cmake_definitions": [],
            "compile_commands": [],
            "link_commands": [],
        },
        "dataset": dataset_metadata["dataset"],
        "parameters": parameters,
        "resource_budget": {
            "memory_budget_bytes": max(
                1,
                sum(
                    path.stat().st_size
                    for path in observation_root.glob("*.json")
                ),
            ),
            "thread_budget": 1,
        },
        "input_artifacts": [
            execution[key]
            for key in (
                "dataset_artifact", "global_graph_artifact", "pq_artifact",
                "disk_layout_artifact",
            )
        ],
        "output_artifacts": [],
        "status": status,
        "started_at": started_at,
        "ended_at": ended_at,
    }
    if failure is not None:
        manifest["failure"] = failure
    _schema_validator("build-manifest.schema.json", _repository_root()).validate(
        manifest
    )
    return manifest


def evaluate_observation_directory(
    *, protocol_path: str | Path, observations_path: str | Path,
    workspace: str | Path,
) -> dict[str, Any]:
    """Evaluate offline and retain this Python-owned invocation Manifest."""
    started_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
    root = Path(workspace).resolve()
    observation_root = Path(observations_path).resolve()
    protocol = json.loads(Path(protocol_path).read_text(encoding="utf-8"))
    manifest_path = (
        root / "manifests" / f"{protocol['protocol_id']}-evaluation.json"
    )
    try:
        result = _evaluate_observation_directory(
            protocol_path=protocol_path,
            observations_path=observations_path,
            workspace=workspace,
        )
        ended_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        manifest = _evaluation_manifest(
            protocol=protocol, workspace=root, started_at=started_at,
            ended_at=ended_at, status="SUCCEEDED",
            report_path=Path(result["report_path"]),
            observation_root=observation_root,
        )
        write_immutable_json(manifest_path, manifest)
        result["build_manifest_path"] = str(manifest_path)
        return result
    except Exception as error:
        ended_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        if not manifest_path.exists():
            try:
                manifest = _evaluation_manifest(
                    protocol=protocol, workspace=root, started_at=started_at,
                    ended_at=ended_at, status="FAILED", report_path=None,
                    observation_root=observation_root,
                    failure={"category": "evaluation_failed", "reason": str(error)},
                )
                write_immutable_json(manifest_path, manifest)
            except Exception:
                # Preserve the original evaluation failure if admission was too
                # incomplete to construct a schema-valid terminal manifest.
                pass
        raise


def _read_native_matrix(path: Path, code: str) -> tuple[int, int, list[Any]]:
    payload = path.read_bytes()
    if len(payload) < 8:
        raise SearchEvaluationError("oracle result matrix is truncated")
    rows, columns = struct.unpack_from("<II", payload)
    item_size = struct.calcsize(code)
    if rows == 0 or columns == 0 or len(payload) != 8 + rows * columns * item_size:
        raise SearchEvaluationError("oracle result matrix shape is invalid")
    return rows, columns, list(
        struct.unpack_from(f"<{rows * columns}{code}", payload, 8)
    )


def _oracle_summary(stdout: str, l_search: int) -> dict[str, float]:
    matches = []
    for line in stdout.splitlines():
        fields = line.split()
        if len(fields) >= 9 and fields[0] == str(l_search) and fields[1] == "2":
            matches.append(fields)
    if len(matches) != 1:
        raise SearchEvaluationError("oracle metric summary is missing or ambiguous")
    fields = matches[0]
    try:
        return {
            "qps": float(fields[2]), "mean_latency_us": float(fields[3]),
            "p99_9_latency_us": float(fields[4]), "mean_ios": float(fields[5]),
            "recall_at_100": float(fields[8]),
        }
    except ValueError as error:
        raise SearchEvaluationError("oracle metric summary is invalid") from error


def _oracle_manifest(
    *, protocol: dict[str, Any], workspace: Path, run_id: str,
    argv: list[str], oracle_path: Path, stdout_path: Path, stderr_path: Path,
    result_paths: list[Path], started_at: str, ended_at: str, status: str,
    failure: dict[str, Any] | None = None,
) -> dict[str, Any]:
    execution = protocol["search_execution"]
    build_ref = execution["runner_build"]
    build_path = workspace / build_ref["build_manifest_path"]
    if not build_path.is_file() or _sha256(build_path) != build_ref["build_manifest_sha256"]:
        raise SearchEvaluationError("admitted build manifest identity mismatch")
    admitted_build = json.loads(build_path.read_text(encoding="utf-8"))
    _schema_validator("build-manifest.schema.json", _repository_root()).validate(
        admitted_build
    )
    dataset_ref = execution["dataset_artifact"]
    dataset_metadata = json.loads(
        (workspace / "normal" / dataset_ref["metadata_path"]).read_text(
            encoding="utf-8"
        )
    )
    result_references = [
        {
            "path": path.relative_to(workspace).as_posix(),
            "byte_length": path.stat().st_size,
            "sha256": _sha256(path),
        }
        for path in result_paths
    ]
    manifest: dict[str, Any] = {
        "manifest_version": "1.0.0", "run_id": run_id, "stage": "search",
        "implementation": {
            "implementation_id": "cpp_main_search_disk_index_oracle_v1",
            "implementation_version": "1.0.0",
        },
        "source": {
            "repository": "https://github.com/microsoft/DiskANN.git",
            "branch": "cpp_main",
            "commit": "78256bbab4685e1774e78d331e081a153be26823",
            "dirty_worktree": False,
        },
        "toolchain": admitted_build["toolchain"],
        "dataset": dataset_metadata["dataset"],
        "parameters": {
            "argv": argv,
            "oracle_binary": {
                "path": oracle_path.as_posix(), "sha256": _sha256(oracle_path),
            },
            "protocol_id": protocol["protocol_id"],
            "protocol_checksum": protocol["protocol_checksum"],
            "search_execution_checksum": protocol["search_execution_checksum"],
            "stdout": {
                "path": stdout_path.relative_to(workspace).as_posix(),
                "sha256": _sha256(stdout_path),
            },
            "stderr": {
                "path": stderr_path.relative_to(workspace).as_posix(),
                "sha256": _sha256(stderr_path),
            },
            "result_files": result_references,
        },
        "resource_budget": {
            "memory_budget_bytes": max(
                1,
                (workspace / execution["query_set"]["native_payload"]["path"]).stat().st_size
                + sum(path.stat().st_size for path in result_paths),
            ),
            "thread_budget": execution["thread_count"],
        },
        "input_artifacts": [
            execution[key]
            for key in (
                "dataset_artifact", "global_graph_artifact", "pq_artifact",
                "disk_layout_artifact",
            )
        ],
        "output_artifacts": [], "status": status,
        "started_at": started_at, "ended_at": ended_at,
    }
    if failure is not None:
        manifest["failure"] = failure
    _schema_validator("build-manifest.schema.json", _repository_root()).validate(
        manifest
    )
    return manifest


def run_search_runner_parity(
    *, protocol: str | Path, oracle: str | Path, runner: str | Path,
    workspace: str | Path,
) -> dict[str, Any]:
    """Run and compare the fixed upstream oracle and instrumented path."""
    protocol_path = Path(protocol).resolve()
    workspace_path = Path(workspace).resolve()
    oracle_path = Path(oracle).resolve()
    runner_path = Path(runner).resolve()
    if not oracle_path.is_file() or not runner_path.is_file():
        raise SearchEvaluationError("parity binary is missing")
    document = json.loads(protocol_path.read_text(encoding="utf-8"))
    if normalize_search_protocol(document) != document:
        raise SearchEvaluationError("parity protocol is not frozen")
    execution = document["search_execution"]
    expected_runner = (workspace_path / execution["runner_build"]["binary_path"]).resolve()
    if (
        runner_path != expected_runner
        or _sha256(runner_path) != execution["runner_build"]["binary_sha256"]
    ):
        raise SearchEvaluationError("instrumented runner identity mismatch")
    evidence_path = (
        workspace_path / "reports" / "search-parity" /
        f"{execution['execution_id']}.json"
    )
    runner_result = subprocess.run(
        [str(runner_path), "search", "--protocol", str(protocol_path), "--workspace", str(workspace_path)],
        check=False, capture_output=True, text=True,
    )
    if runner_result.returncode != 0:
        write_immutable_json(
            evidence_path,
            {"result": "FAIL", "reason": "instrumented parity run failed",
             "instrumented_exit_code": runner_result.returncode, "points": []},
        )
        return {"result": "FAIL", "evidence_path": str(evidence_path)}
    truth_ids, truth_distances = _read_ground_truth(
        workspace_path / document["evaluation"]["ground_truth"]["payload"]["path"],
        document["evaluation"]["ground_truth"]["query_count"],
        document["evaluation"]["ground_truth"]["width"],
    )
    points: list[dict[str, Any]] = []
    parity_pass = True
    prefix = (
        workspace_path / "temporary" /
        f"{document['protocol_id']}-search-native-index" / "index"
    )
    query = workspace_path / execution["query_set"]["native_payload"]["path"]
    truth = workspace_path / document["evaluation"]["ground_truth"]["payload"]["path"]
    for repetition in range(3):
        for l_search in execution["L_search"]:
            result_prefix = (
                workspace_path / "reports" / "search-parity" /
                f"{document['protocol_id']}-oracle-L{l_search}-r{repetition}"
            )
            result_prefix.parent.mkdir(parents=True, exist_ok=True)
            argv = [str(oracle_path), "--data_type", "float", "--dist_fn", "l2", "--index_path_prefix", str(prefix), "--result_path", str(result_prefix), "--query_file", str(query), "--gt_file", str(truth), "--recall_at", "100", "--search_list", str(l_search), "--beamwidth", "2", "--num_nodes_to_cache", str(execution["cache"]["requested_nodes"]), "--search_io_limit", "4294967295", "--num_threads", "6"]
            started_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            completed = subprocess.run(argv, check=False, capture_output=True, text=True)
            ended_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            evidence_root = (
                workspace_path / "reports" / "search-parity" /
                f"{document['protocol_id']}-oracle-L{l_search}-r{repetition}"
            )
            stdout_path = evidence_root.with_suffix(".stdout")
            stderr_path = evidence_root.with_suffix(".stderr")
            _write_immutable_bytes(stdout_path, completed.stdout.encode())
            _write_immutable_bytes(stderr_path, completed.stderr.encode())
            ids_path = Path(f"{result_prefix}_{l_search}_idx_uint32.bin")
            distances_path = Path(f"{result_prefix}_{l_search}_dists_float.bin")
            result_paths = [path for path in (ids_path, distances_path) if path.is_file()]
            run_id = f"{execution['execution_id']}-oracle-L{l_search}-r{repetition}"
            manifest = _oracle_manifest(
                protocol=document, workspace=workspace_path, run_id=run_id,
                argv=argv, oracle_path=oracle_path, stdout_path=stdout_path,
                stderr_path=stderr_path, result_paths=result_paths,
                started_at=started_at, ended_at=ended_at,
                status="SUCCEEDED" if completed.returncode == 0 else "FAILED",
                failure=None if completed.returncode == 0 else {
                    "category": "process_failed",
                    "reason": "search_disk_index returned nonzero",
                    "exit_code": completed.returncode,
                },
            )
            manifest_path = workspace_path / "manifests" / f"{run_id}.json"
            write_immutable_json(manifest_path, manifest)
            if completed.returncode != 0 or len(result_paths) != 2:
                parity_pass = False
                points.append({
                    "L_search": l_search, "repetition_index": repetition,
                    "result": "FAIL", "reason": "oracle invocation failed",
                    "oracle_manifest_path": manifest_path.relative_to(workspace_path).as_posix(),
                    "oracle_manifest_sha256": _sha256(manifest_path),
                })
                continue
            rows, width, oracle_ids = _read_native_matrix(ids_path, "I")
            distance_rows, distance_width, oracle_distances = _read_native_matrix(
                distances_path, "f"
            )
            observation_path = (
                workspace_path / "reports" / "search-observations" /
                f"{document['protocol_id']}-L{l_search}-r{repetition}.json"
            )
            observation = json.loads(observation_path.read_text(encoding="utf-8"))
            observation_errors = list(
                _feature_validator("search-observation.schema.json").iter_errors(observation)
            )
            projection = dict(observation)
            claimed_checksum = projection.pop("observation_checksum", None)
            observation_valid = (
                not observation_errors
                and claimed_checksum == _canonical_digest(projection)
            )
            instrumented_ids = [
                node for row in observation.get("per_query", [])
                for node in row["result_ids"]
            ]
            instrumented_distances = [
                value for row in observation.get("per_query", [])
                for value in row["result_distances"]
            ]
            distance_equal = (
                len(oracle_distances) == len(instrumented_distances)
                and all(
                    math.isclose(lhs, rhs, abs_tol=1.0e-5, rel_tol=1.0e-6)
                    for lhs, rhs in zip(oracle_distances, instrumented_distances)
                )
            )
            summary = _oracle_summary(completed.stdout, l_search)
            recall10 = sum(
                _diskann_recall_one(
                    truth_ids[row], truth_distances[row],
                    observation["per_query"][row]["result_ids"], 10,
                )
                for row in range(rows)
            ) / rows
            recall100 = sum(
                _diskann_recall_one(
                    truth_ids[row], truth_distances[row],
                    observation["per_query"][row]["result_ids"], 100,
                )
                for row in range(rows)
            ) / rows
            mean_ios = sum(row["n_ios"] for row in observation["per_query"]) / rows
            point_pass = (
                observation_valid
                and rows == distance_rows == len(observation["per_query"])
                and width == distance_width == 100
                and oracle_ids == instrumented_ids
                and distance_equal
                and math.isclose(summary["recall_at_100"], recall100, abs_tol=1.0e-6)
                and math.isclose(summary["mean_ios"], mean_ios, abs_tol=1.0e-6)
                and observation["raw_search_point"]["L_search"] == l_search
                and observation["raw_search_point"]["repetition_index"] == repetition
                and observation["raw_search_point"]["beamwidth"] == 2
            )
            parity_pass = parity_pass and point_pass
            points.append({
                "L_search": l_search, "repetition_index": repetition,
                "result": "PASS" if point_pass else "FAIL",
                "ordered_ids_equal": oracle_ids == instrumented_ids,
                "distances_within_tolerance": distance_equal,
                "recall_at_10": recall10, "recall_at_100": recall100,
                "mean_ios_equal": math.isclose(
                    summary["mean_ios"], mean_ios, abs_tol=1.0e-6
                ),
                "query_stats_mapping": "total_us_and_n_ios_direct",
                "qps_formula": "query_count/canonical_batch_wall_clock_seconds",
                "latency_formula": "pinned_QueryStats_total_us",
                "timing_boundary": execution["timing_boundary"],
                "persistence_outside_timing": True,
                "oracle_manifest_path": manifest_path.relative_to(workspace_path).as_posix(),
                "oracle_manifest_sha256": _sha256(manifest_path),
                "observation_path": observation_path.relative_to(workspace_path).as_posix(),
                "observation_sha256": _sha256(observation_path),
            })
    evidence = {
        "result": "PASS" if parity_pass else "FAIL",
        "instrumented_exit_code": runner_result.returncode,
        "controls": {
            "search_execution_checksum": document["search_execution_checksum"],
            "K": 100, "beamwidth": 2, "thread_count": 6,
            "cache": execution["cache"], "io_limit": 4294967295,
            "warmup_policy": "none",
        },
        "points": points,
        "graph_structural_parity_gate": "NOT_REACHED",
        "framework_state": "candidate",
    }
    write_immutable_json(evidence_path, evidence)
    return {"result": evidence["result"], "evidence_path": str(evidence_path)}


def preflight(
    *, mode: str, config_path: str | Path, workspace: str | Path,
) -> dict[str, Any]:
    """One non-executing preflight with readiness and formal-search modes."""
    if mode not in {"readiness", "formal-search"}:
        raise SearchEvaluationError("preflight mode is invalid")
    root = Path(workspace).resolve()
    config = json.loads(Path(config_path).read_text(encoding="utf-8"))
    if config.get("mode") != mode:
        raise SearchEvaluationError("preflight config/mode mismatch")
    if mode == "readiness":
        controls = config.get("frozen_controls", {})
        if controls != {
            "K": 100, "L_search": [100, 200, 400], "beamwidth": 2,
            "threads": 6, "repetitions": 3,
        }:
            raise SearchEvaluationError("readiness frozen controls drifted")
        known = config.get("known")
        if type(known) is not dict:
            raise SearchEvaluationError("readiness known-input declaration is invalid")
        repository_root = Path(known.get("repository_root", "")).resolve()
        if not repository_root.is_dir():
            raise SearchEvaluationError("readiness repository root is missing")

        source_checks: dict[str, Any] = {}
        pending_prerequisites: list[str] = []
        for name in ("project_source", "diskann_source"):
            declaration = known.get(name)
            if type(declaration) is not dict:
                raise SearchEvaluationError(f"readiness {name} declaration is invalid")
            source_root = Path(declaration.get("source_root", ""))
            source_root = (
                source_root.resolve()
                if source_root.is_absolute()
                else (repository_root / source_root).resolve()
            )
            actual = _git_source_identity(source_root)
            expected = {
                "repository": declaration.get("repository"),
                "branch": declaration.get("branch"),
                "commit": declaration.get("commit"),
                "dirty_worktree": False,
            }
            matched = actual == expected
            source_checks[name] = {
                "source_root": str(source_root),
                "expected": expected,
                "actual": actual,
                "status": "PASS" if matched else "PENDING",
            }
            if not matched:
                pending_prerequisites.append(name)

        file_checks = [
            _known_file_check(repository_root, value)
            for value in known.get("file_identities", [])
        ]
        if {value["name"] for value in file_checks} != {
            "dataset", "selected_query"
        }:
            raise SearchEvaluationError(
                "readiness requires exact Dataset and selected-query identities"
            )

        binary_checks = [
            _known_file_check(repository_root, value)
            for value in known.get("binary_identities", [])
        ]
        if {value["name"] for value in binary_checks} != {
            "validation_runner", "search_runner", "oracle", "groundtruth"
        }:
            raise SearchEvaluationError("readiness binary identity set is incomplete")

        projection = known.get("cache_projection", {})
        if set(projection) != {
            "requested_nodes", "dimension", "scalar_bytes",
            "declared_max_degree", "expected_primary_bytes",
        }:
            raise SearchEvaluationError("readiness cache projection is invalid")
        derived_primary_bytes = projection["requested_nodes"] * (
            projection["dimension"] * projection["scalar_bytes"]
            + (projection["declared_max_degree"] + 1) * 4
        )
        if (
            projection["requested_nodes"] != 10000
            or projection["dimension"] != 128
            or projection["scalar_bytes"] != 4
            or projection["declared_max_degree"] <= 0
            or derived_primary_bytes != projection["expected_primary_bytes"]
        ):
            raise SearchEvaluationError("readiness fixed-cache projection drifted")

        resources = known.get("resources", {})
        if set(resources) != {
            "minimum_free_bytes", "minimum_memory_bytes", "thread_budget"
        } or any(type(value) is not int or value <= 0 for value in resources.values()):
            raise SearchEvaluationError("readiness resource declaration is invalid")
        if not root.is_dir():
            raise SearchEvaluationError("readiness workspace root is missing")
        free_bytes = shutil.disk_usage(root).free
        memory_bytes = _physical_memory_bytes()
        cpu_count = os.cpu_count() or 0
        resource_pass = (
            free_bytes >= resources["minimum_free_bytes"]
            and memory_bytes >= resources["minimum_memory_bytes"]
            and cpu_count >= resources["thread_budget"]
        )
        if not resource_pass:
            pending_prerequisites.append("resources")

        environment = known.get("environment", {})
        if set(environment) != {"local_ssd_admitted", "linux_native_smoke_pass", "search_runner_parity_pass"}:
            raise SearchEvaluationError("readiness environment declaration is invalid")
        environment_checks = {
            "native_linux": platform.system() == "Linux",
            "direct_io_api_available": hasattr(os, "O_DIRECT"),
            "local_ssd_admitted": environment["local_ssd_admitted"] is True,
            "linux_native_smoke_pass": environment["linux_native_smoke_pass"] is True,
            "search_runner_parity_pass": environment["search_runner_parity_pass"] is True,
        }
        performance_environment_pass = all(environment_checks.values())
        if not performance_environment_pass:
            pending_prerequisites.append("normative_performance_environment")
        if known.get("global_graph_declared") is not True:
            pending_prerequisites.append("global_graph_identity")
        return {
            "mode": "readiness", "formal_search_ready": False,
            "pending_identities": ["pq", "disk_layout", "normalized_query_set", "ground_truth", "zero_cache_protocol", "fixed_cache_protocol"],
            "protocol_paths": [], "request_paths": [],
            "checks": {
                "source": source_checks,
                "files": file_checks,
                "binaries": binary_checks,
                "workspace": {"root": str(root), "confined": True},
                "resources": {
                    "status": "PASS" if resource_pass else "PENDING",
                    "free_bytes": free_bytes,
                    "physical_memory_bytes": memory_bytes,
                    "cpu_count": cpu_count,
                },
                "cache_projection": {
                    "requested_nodes": 10000,
                    "derived_primary_bytes": derived_primary_bytes,
                    "status": "PASS",
                },
                "environment": {
                    **environment_checks,
                    "performance_admission": (
                        "PASS" if performance_environment_pass else "PENDING"
                    ),
                },
            },
            "pending_prerequisites": sorted(set(pending_prerequisites)),
            "operations_started": {"pq": 0, "disk_layout": 0, "query_set": 0, "ground_truth": 0, "search": 0, "partition": 0, "local_build": 0, "merge": 0},
        }

    declarations = config.get("protocols")
    if type(declarations) is not list or len(declarations) != 2:
        raise SearchEvaluationError("formal-search requires exactly two cache protocols")
    common = config.get("common_admission", {})
    runner = Path(common.get("runner_path", ""))
    runner_ok = runner.is_file() and _sha256(runner) == common.get("runner_sha256")
    common_keys = (
        "artifacts_valid", "inputs_compatible", "abi_dependencies_usable",
        "load_probe_pass", "paths_confined", "resources_sufficient",
    )
    common_pass = runner_ok and all(common.get(key) is True for key in common_keys)
    fixture_mode = config.get("fixture_mode") is True
    if not fixture_mode:
        # A real formal-search preflight cannot substitute declaration booleans for
        # the exact input files and evidence it binds.
        for declaration in declarations:
            execution = declaration.get("search_execution", {})
            for name in ("dataset_artifact", "pq_artifact", "disk_layout_artifact", "global_graph_artifact"):
                ref = execution.get(name, {})
                metadata = root / "normal" / ref.get("metadata_path", "")
                common_pass = common_pass and metadata.is_file()
            query_set = execution.get("query_set", {})
            common_pass = common_pass and (root / query_set.get("native_payload", {}).get("path", "")).is_file()
            ground_truth = declaration.get("evaluation", {}).get("ground_truth", {})
            common_pass = common_pass and (root / ground_truth.get("payload", {}).get("path", "")).is_file()
    performance = config.get("performance_admission", {})
    performance_pass = common_pass and all(
        performance.get(key) is True
        for key in ("native_linux", "local_ssd", "direct_io", "linux_aio", "parity_pass")
    )
    if not common_pass:
        return {"mode": mode, "common_search_admission": False, "performance_admission": False, "protocol_paths": [], "request_paths": [], "operations_started": {"search": 0}}

    normalized = [normalize_search_protocol(value) for value in declarations]
    cache_modes = {value["search_execution"]["cache"]["mode"] for value in normalized}
    if cache_modes != {"zero", "fixed_bfs"}:
        raise SearchEvaluationError("formal-search cache protocols are not distinct")
    protocol_paths = []
    request_paths = []
    for document in normalized:
        runner_build = document["search_execution"]["runner_build"]
        if runner_build["binary_sha256"] != common["runner_sha256"]:
            raise SearchEvaluationError("Protocol runner binary identity mismatch")
        protocol_target = root / "protocols" / f"{document['protocol_id']}.json"
        write_immutable_json(protocol_target, document)
        reread = json.loads(protocol_target.read_text(encoding="utf-8"))
        if normalize_search_protocol(reread) != document:
            raise SearchEvaluationError("persisted SearchProtocol failed immediate validation")
        request = {
            "request_version": "diskann-search-request/v1",
            "request_id": f"{document['protocol_id']}-request",
            "protocol_path": protocol_target.relative_to(root).as_posix(),
            "protocol_checksum": document["protocol_checksum"],
            "search_execution_checksum": document["search_execution_checksum"],
            "runner_path": runner_build["binary_path"],
            "runner_sha256": runner_build["binary_sha256"],
            "workspace_root": str(root),
            "quality_execution": True,
            "performance_admitted": performance_pass,
            "execution_authorized": False,
        }
        request_target = root / "requests" / f"{request['request_id']}.json"
        write_immutable_json(request_target, request)
        protocol_paths.append(str(protocol_target))
        request_paths.append(str(request_target))
    return {
        "mode": mode, "common_search_admission": True,
        "performance_admission": performance_pass,
        "protocol_paths": protocol_paths, "request_paths": request_paths,
        "operations_started": {"search": 0, "partition": 0, "local_build": 0, "merge": 0},
    }


def _artifact_ref(value: Any, expected_type: str) -> dict[str, Any]:
    fields = {"artifact_type", "format_version", "aggregate_checksum", "metadata_path"}
    if type(value) is not dict or set(value) != fields:
        raise SearchEvaluationError("ArtifactRef shape is invalid")
    if value["artifact_type"] != expected_type:
        raise SearchEvaluationError("ArtifactRef type is invalid")
    path = Path(value["metadata_path"])
    if path.is_absolute() or ".." in path.parts:
        raise SearchEvaluationError("ArtifactRef path escapes workspace")
    checksum = value["aggregate_checksum"]
    if type(checksum) is not str or len(checksum) != 64:
        raise SearchEvaluationError("ArtifactRef checksum is invalid")
    return value


def normalize_materialization_request(value: Any) -> dict[str, Any]:
    if type(value) is not dict:
        raise SearchEvaluationError("materialization request must be an object")
    allowed = {
        "request_id", "run_id", "workspace", "operation", "dataset",
        "global_graph", "implementation", "parameters", "resource_budget",
        "candidate_path",
    }
    required = allowed - {"global_graph"}
    if not required.issubset(value) or not set(value).issubset(allowed):
        raise SearchEvaluationError("materialization request fields are invalid")
    operation = value["operation"]
    if operation not in {"pq", "disk_layout"}:
        raise SearchEvaluationError("materialization operation is invalid")
    expected = _PQ_PARAMETERS if operation == "pq" else _LAYOUT_PARAMETERS
    if value["parameters"] != expected:
        raise SearchEvaluationError("materialization parameters drifted")
    _artifact_ref(value["dataset"], "dataset")
    if operation == "pq" and "global_graph" in value:
        raise SearchEvaluationError("PQ request must not bind a GlobalGraph")
    if operation == "disk_layout":
        if "global_graph" not in value:
            raise SearchEvaluationError("DiskLayout request requires a GlobalGraph")
        _artifact_ref(value["global_graph"], "global_graph")
    implementation = value["implementation"]
    expected_id = (
        "cpp_main_post_graph_pq_v1"
        if operation == "pq"
        else "cpp_main_post_graph_disk_layout_v1"
    )
    if implementation != {
        "implementation_id": expected_id,
        "implementation_version": "1.0.0",
        "reproduction_mode": "upstream_faithful",
    }:
        raise SearchEvaluationError("materialization implementation identity is invalid")
    budget = value["resource_budget"]
    if (
        type(budget) is not dict
        or set(budget) != {"memory_budget_bytes", "thread_budget"}
        or type(budget["memory_budget_bytes"]) is not int
        or budget["memory_budget_bytes"] <= 0
        or type(budget["thread_budget"]) is not int
        or budget["thread_budget"] <= 0
    ):
        raise SearchEvaluationError("resource budget is invalid")
    workspace = value["workspace"]
    if type(workspace) is not dict or set(workspace) != {"root"}:
        raise SearchEvaluationError("workspace is invalid")
    return json.loads(json.dumps(value, sort_keys=True))


def _run_argv(argv: list[str]) -> int:
    return subprocess.run(argv, check=False).returncode


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="diskann-search-evaluation")
    operations = parser.add_subparsers(dest="command", required=True)
    materialize = operations.add_parser("materialize")
    materialize.add_argument("--operation", required=True)
    materialize.add_argument("--request", required=True)
    materialize.add_argument("--runner")
    load = operations.add_parser("load-probe")
    load.add_argument("--pq-artifact", required=True)
    load.add_argument("--disk-layout-artifact", required=True)
    load.add_argument("--workspace", required=True)
    load.add_argument("--runner", required=True)
    search_command = operations.add_parser("search")
    search_command.add_argument("--protocol", required=True)
    search_command.add_argument("--workspace", required=True)
    search_command.add_argument("--runner", required=True)
    evaluate = operations.add_parser("evaluate")
    evaluate.add_argument("--protocol", required=True)
    evaluate.add_argument("--observations", required=True)
    evaluate.add_argument("--workspace", required=True)
    parity = operations.add_parser("parity")
    parity.add_argument("--protocol", required=True)
    parity.add_argument("--oracle", required=True)
    parity.add_argument("--runner", required=True)
    parity.add_argument("--workspace", required=True)
    preflight_command = operations.add_parser("preflight")
    preflight_command.add_argument("--mode", required=True, choices=("readiness", "formal-search"))
    preflight_command.add_argument("--config", required=True)
    preflight_command.add_argument("--workspace", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "materialize":
        if args.operation == "query_set":
            materialize_query_set_request(args.request)
            return 0
        if args.operation == "ground_truth":
            materialize_ground_truth_request(args.request)
            return 0
        if args.operation not in {"pq", "disk_layout"} or not args.runner:
            raise SearchEvaluationError("native materialization requires pq/disk_layout and runner")
        return _run_argv([args.runner, "materialize", "--request", args.request])
    if args.command == "search":
        return _run_argv(
            [args.runner, "search", "--protocol", args.protocol, "--workspace", args.workspace]
        )
    if args.command == "evaluate":
        output = evaluate_observation_directory(
            protocol_path=args.protocol,
            observations_path=args.observations,
            workspace=args.workspace,
        )
        print(json.dumps(output["display"], sort_keys=True))
        return 0
    if args.command == "parity":
        result = run_search_runner_parity(
            protocol=args.protocol, oracle=args.oracle, runner=args.runner,
            workspace=args.workspace,
        )
        return 0 if result.get("result") == "PASS" else 2
    if args.command == "preflight":
        result = preflight(mode=args.mode, config_path=args.config, workspace=args.workspace)
        print(json.dumps(result, sort_keys=True))
        return 0
    return _run_argv(
        [
            args.runner,
            "load-probe",
            "--pq-artifact",
            args.pq_artifact,
            "--disk-layout-artifact",
            args.disk_layout_artifact,
            "--workspace",
            args.workspace,
        ]
    )


if __name__ == "__main__":
    raise SystemExit(main())
