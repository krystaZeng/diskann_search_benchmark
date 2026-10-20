"""Feature-private DiskANN post-graph search/evaluation lifecycle."""

from __future__ import annotations

import argparse
import csv
import errno
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

from jsonschema import Draft202012Validator, ValidationError

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


def _confined_existing_file(root: Path, value: Any, label: str) -> Path:
    if type(value) is not str:
        raise SearchEvaluationError(f"{label} path is invalid")
    relative = Path(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise SearchEvaluationError(f"{label} path escapes workspace")
    path = (root / relative).resolve()
    try:
        path.relative_to(root)
    except ValueError as error:
        raise SearchEvaluationError(f"{label} path escapes workspace") from error
    if not path.is_file():
        raise SearchEvaluationError(f"{label} file is missing")
    return path


def _validate_search_admission_evidence(
    *, root: Path, declaration: Any, artifact: Any, expected_type: str
) -> None:
    if type(declaration) is not dict or set(declaration) != {
        "path", "sha256", "expected_artifact"
    }:
        raise SearchEvaluationError(
            f"{expected_type} admission evidence reference is invalid"
        )
    expected_artifact = _artifact_ref(
        declaration["expected_artifact"], expected_type
    )
    admitted_artifact = _artifact_ref(artifact, expected_type)
    if expected_artifact != admitted_artifact:
        raise SearchEvaluationError(
            f"{expected_type} admission evidence expected ArtifactRef mismatch"
        )
    path = _confined_existing_file(
        root, declaration["path"], f"{expected_type} admission evidence"
    )
    if type(declaration["sha256"]) is not str or _sha256(path) != declaration[
        "sha256"
    ]:
        raise SearchEvaluationError(
            f"{expected_type} admission evidence checksum mismatch"
        )
    try:
        evidence = json.loads(path.read_text(encoding="utf-8"))
        _schema_validator(
            "validation-evidence.schema.json", _repository_root()
        ).validate(evidence)
    except Exception as error:
        raise SearchEvaluationError(
            f"{expected_type} admission evidence schema is invalid"
        ) from error
    candidate = evidence.get("artifact_candidate")
    expected_candidate = {
        "artifact_type": admitted_artifact["artifact_type"],
        "format_version": admitted_artifact["format_version"],
        "metadata_path": admitted_artifact["metadata_path"],
        "claimed_aggregate_checksum": admitted_artifact["aggregate_checksum"],
    }
    if evidence.get("result") != "VALID" or candidate != expected_candidate:
        raise SearchEvaluationError(
            f"{expected_type} admission evidence does not grant the ArtifactRef"
        )


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


def report_validator(report: dict[str, Any]) -> Draft202012Validator:
    schemas = {
        "search-evaluation-report/v1": "search-evaluation-report.schema.json",
        "search-evaluation-report/v2": "search-evaluation-report-v2.schema.json",
    }
    name = schemas.get(report.get("report_version"))
    if name is None:
        raise SearchEvaluationError("unknown SearchEvaluationReport version")
    return _feature_validator(name)


def _evaluator_provenance() -> dict[str, Any]:
    root = _repository_root()
    paths = [
        Path(__file__).resolve(), root / "harness/python/diskann_harness/manifests.py",
        root / "harness/python/diskann_harness/runner.py",
        *sorted((root / "specs/005-diskann-search-evaluation/contracts").glob("*.schema.json")),
    ]
    return {
        "source": _git_source_identity(root),
        "source_files": [{"path": p.relative_to(root).as_posix(), "sha256": _sha256(p)} for p in paths],
    }


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


def _query_alignment_exclusion_reason(
    point: dict[str, Any], queries: list[dict[str, Any]], query_count: int,
) -> str | None:
    if point.get("query_count") != query_count:
        return f"query_count differs from frozen QuerySet: expected {query_count}, got {point.get('query_count')}"
    if not (point.get("completed_query_count") == point["query_count"] == len(queries)):
        return "incomplete raw observation: completed_query_count, query_count and per_query length differ"
    for row, query in enumerate(queries):
        if query.get("query_row_index") != row:
            return f"invalid query_row_index at per_query[{row}]: expected {row}, got {query.get('query_row_index')}"
    return None


def evaluate_raw_point(
    observation: dict[str, Any], *, ground_truth: list[list[int]],
    quality_compatible: bool, performance_admitted: bool,
    ground_truth_distances: list[list[float]] | None = None,
) -> dict[str, Any]:
    queries = observation.get("per_query", [])
    alignment_reason = _query_alignment_exclusion_reason(observation, queries, len(ground_truth))
    complete = (
        observation.get("status") == "SUCCEEDED"
        and alignment_reason is None
        and all(len(item.get("result_ids", [])) == 100 and len(item.get("result_distances", [])) == 100 for item in queries)
    )
    if not complete:
        return audit_excluded_observation(
            observation_id=observation.get("observation_id", "invalid-point"),
            path=observation.get("path", "observations/invalid.json"),
            checksum=observation.get("checksum", "0" * 64),
            status=observation.get("status", "INVALID_OUTPUT"),
            reason=alignment_reason or "incomplete or invalid raw observation",
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
    if len(payload) < 8:
        raise SearchEvaluationError("GroundTruth header is incomplete")
    count, width = struct.unpack_from("<II", payload)
    if count != expected_count or width != expected_width:
        raise SearchEvaluationError("GroundTruth identity is incompatible")
    item_count = count * width
    if len(payload) != 8 + item_count * 8:
        raise SearchEvaluationError("GroundTruth payload row count/byte length is incompatible")
    ids = struct.unpack_from(f"<{item_count}I", payload, 8)
    distances = struct.unpack_from(f"<{item_count}f", payload, 8 + item_count * 4)
    return (
        [list(ids[row * width : (row + 1) * width]) for row in range(count)],
        [list(distances[row * width : (row + 1) * width]) for row in range(count)],
    )


def _load_evaluation_ground_truth(
    root: Path, protocol: dict[str, Any],
) -> tuple[list[list[int]], list[list[float]]]:
    # FR-044: incompatible truth blocks evaluation, not raw search validity.
    query_count = protocol["search_execution"]["query_set"]["query_count"]
    truth_ref = protocol["evaluation"]["ground_truth"]
    execution = protocol["search_execution"]
    if truth_ref["query_set_checksum"] != execution["query_set"]["canonical_checksum"] or any(
        truth_ref["dataset_artifact"][key] != execution["dataset_artifact"][key]
        for key in ("artifact_type", "format_version", "aggregate_checksum")
    ):
        raise SearchEvaluationError("GroundTruth Dataset/QuerySet identity is incompatible")
    if truth_ref["query_count"] != query_count:
        raise SearchEvaluationError("GroundTruth query_count differs from frozen QuerySet")
    truth_path = root / truth_ref["payload"]["path"]
    if _sha256(truth_path) != truth_ref["payload"]["sha256"]:
        raise SearchEvaluationError("GroundTruth checksum mismatch")
    return _read_ground_truth(truth_path, query_count, truth_ref["width"])


def _median(values: list[float]) -> float:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def _admit_observation_set(
    protocol: dict[str, Any], documents: list[tuple[Path, dict[str, Any]]],
    validator: Draft202012Validator,
) -> set[Path]:
    """Check retained trial identities independently of metric/schema validity."""
    schema = validator.schema
    identity_fields = (
        "format_version", "observation_id", "run_id", "protocol", "search_execution",
        "dataset_artifact", "global_graph_artifact", "pq_artifact", "disk_layout_artifact",
        "query_set_checksum", "observation_checksum", "raw_search_point",
    )
    point_fields = ("point_id", "L_search", "K", "beamwidth", "cache", "repetition_index", "status")
    point_properties = schema["$defs"]["rawSearchPoint"]["properties"]
    # Reuse existing field constraints, but omit metrics, completion counters,
    # effective cache measurements and environment availability from identity.
    identity_schema = {
        "type": "object", "$defs": schema["$defs"],
        "required": list(identity_fields),
        "properties": {
            **{key: schema["properties"][key] for key in identity_fields},
            "raw_search_point": {
                "type": "object", "required": list(point_fields),
                "properties": {
                    **{key: point_properties[key] for key in point_fields},
                    "cache": {
                        "type": "object", "required": ["mode", "requested_nodes"],
                        "properties": {
                            key: schema["$defs"]["cacheState"]["properties"][key]
                            for key in ("mode", "requested_nodes")
                        },
                    },
                },
            },
        },
    }
    identity_validator = Draft202012Validator(identity_schema)
    execution = protocol["search_execution"]
    expected = {
        (execution["cache"]["mode"], l_search, repetition)
        for l_search in execution["L_search"]
        for repetition in range(execution["repetition_count"])
    }
    retained: dict[tuple[str, int, int], Path] = {}
    admitted: set[Path] = set()
    for path, document in documents:
        error = next(identity_validator.iter_errors(document), None)
        if error is not None:
            location = ".".join(str(item) for item in error.absolute_path)
            raise SearchEvaluationError(f"observation identity invalid: {path}: {location}: {error.message}")
        projection = dict(document)
        claimed = projection.pop("observation_checksum")
        if claimed != _canonical_digest(projection):
            raise SearchEvaluationError(f"observation checksum mismatch: {path}")
        protocol_matches = document["protocol"]["protocol_checksum"] == protocol["protocol_checksum"]
        execution_matches = document["search_execution"]["canonical_checksum"] == protocol["search_execution_checksum"]
        if (protocol_matches and document["protocol"]["protocol_id"] != protocol["protocol_id"]) or (
            execution_matches and document["search_execution"]["execution_id"] != execution["execution_id"]
        ):
            raise SearchEvaluationError(f"observation identity contradicts checksum: {path}")
        if not protocol_matches and not execution_matches:
            # Identifiable foreign-protocol records remain audit references;
            # they cannot fill a slot in this protocol's declared trial set.
            continue
        if not execution_matches or document["query_set_checksum"] != execution["query_set"]["canonical_checksum"]:
            raise SearchEvaluationError(f"observation identity differs from frozen protocol: {path}")
        for artifact in ("dataset_artifact", "global_graph_artifact", "pq_artifact", "disk_layout_artifact"):
            if any(document[artifact][key] != execution[artifact][key]
                   for key in ("artifact_type", "format_version", "aggregate_checksum")):
                raise SearchEvaluationError(f"observation identity differs from {artifact}: {path}")
        point = document["raw_search_point"]
        key = (point["cache"]["mode"], point["L_search"], point["repetition_index"])
        if key not in expected:
            raise SearchEvaluationError(f"undeclared logical trial {key}: {path}")
        if any(point[key] != execution[key] for key in ("K", "beamwidth")) or (
            point["cache"]["requested_nodes"] != execution["cache"]["requested_nodes"]
        ):
            raise SearchEvaluationError(f"observation control identity differs from frozen protocol: {path}")
        # Protocol and execution identities are already bound above. File paths,
        # run_id, point_id and observation_id do not distinguish this slot.
        if key in retained:
            raise SearchEvaluationError(f"duplicate logical trial {key}: {retained[key]} and {path}")
        retained[key] = path
        admitted.add(path)
    missing = expected - retained.keys()
    if missing:
        raise SearchEvaluationError(f"missing logical trials for {protocol['protocol_id']}: {sorted(missing)}")
    return admitted


def _observation_exclusion_reason(
    document: dict[str, Any], validator: Draft202012Validator, *,
    identity_compatible: bool, execution: dict[str, Any],
) -> str | None:
    if not identity_compatible:
        return "observation belongs to a different protocol"
    error = next(validator.iter_errors(document), None)
    if error is not None:
        location = ".".join(str(item) for item in error.absolute_path)
        return f"invalid observation: {location}: {error.message}"
    point = document["raw_search_point"]
    if point["status"] != "SUCCEEDED":
        return f"retained terminal observation: {point['status']}"
    if document["implementation"] != execution["implementation"]:
        return "observation implementation differs from frozen SearchProtocol"
    if document["source_build"]["diskann_commit"] != execution["diskann_source_commit"]:
        return "observation diskann_commit differs from frozen SearchProtocol"
    for field in ("binary_sha256", "build_manifest_sha256"):
        if document["source_build"][field] != execution["runner_build"][field]:
            return f"observation source_build.{field} differs from frozen runner_build.{field}"
    alignment_reason = _query_alignment_exclusion_reason(
        point, document["per_query"], execution["query_set"]["query_count"],
    )
    if alignment_reason is not None:
        return alignment_reason
    if point["batch_wall_clock_us"] <= 0:
        return "batch timing is invalid"
    return None


def _verified_observation_environment(
    root: Path, reference: dict[str, Any], observation_path: Path,
) -> tuple[dict[str, Any], str]:
    path = root / reference["fingerprint_path"]
    try:
        payload = path.read_bytes()
    except OSError as error:
        raise SearchEvaluationError(f"cannot read environment for {observation_path}: {path}: {error}") from error
    actual_sha = hashlib.sha256(payload).hexdigest()
    if actual_sha != reference["fingerprint_sha256"]:
        raise SearchEvaluationError(
            f"environment checksum mismatch for {observation_path}: {path}: "
            f"declared {reference['fingerprint_sha256']}, actual {actual_sha}"
        )
    try:
        environment = json.loads(payload)
    except (ValueError, UnicodeError) as error:
        raise SearchEvaluationError(f"invalid environment JSON for {observation_path}: {path}: {error}") from error
    if not isinstance(environment, dict):
        raise SearchEvaluationError(f"invalid environment object for {observation_path}: {path}")
    return environment, actual_sha


def _bound_json(root: Path, reference: dict[str, Any], label: str, *, manifest: bool = False) -> dict[str, Any]:
    path = _auxiliary_existing_file(root, reference.get("path"), label)
    payload = path.read_bytes()
    if hashlib.sha256(payload).hexdigest() != reference.get("sha256"):
        raise SearchEvaluationError(f"{label} checksum mismatch")
    input_error = False
    try:
        try:
            value = json.loads(payload)
        except (ValueError, RecursionError):
            input_error = True
            raise
        if not isinstance(value, dict):
            input_error = True
            raise ValueError("not an object")
        if manifest:
            validator = _schema_validator("build-manifest.schema.json", _repository_root())
            try:
                validator.validate(value)
            except ValidationError:
                input_error = True
                raise
    except Exception as error:
        wrapped = SearchEvaluationError(f"invalid {label}: {error}")
        # Preserve the shared reader's exception contract; only auxiliary handlers
        # use this local origin marker to distinguish bad input from reader bugs.
        wrapped._unexpected_bound_json_error = not input_error
        raise wrapped from error
    return value


def _check_auxiliary_path(value: Any, label: str) -> None:
    if isinstance(value, str) and "\x00" in value:
        raise SearchEvaluationError(f"{label} path contains a null character")


def _auxiliary_existing_file(root: Path, value: Any, label: str) -> Path:
    _check_auxiliary_path(value, label)
    try:
        return _confined_existing_file(root, value, label)
    except RuntimeError as error:
        if (type(error) is RuntimeError
                and isinstance(error.__context__, OSError)
                and error.__context__.errno == errno.ELOOP):
            raise SearchEvaluationError(f"{label} path contains a symbolic link loop") from error
        raise


def _original_protocol(root: Path, observation: dict[str, Any], protocol: dict[str, Any]) -> dict[str, Any]:
    ref = observation["protocol"]
    path = _confined_existing_file(root, ref["path"], "original protocol")
    try:
        original = json.loads(path.read_bytes())
        valid = normalize_search_protocol(original) == original
    except Exception as error:
        raise SearchEvaluationError(f"invalid original protocol: {path}") from error
    if not valid or any(original[k] != ref[k] for k in ("protocol_id", "protocol_checksum")):
        raise SearchEvaluationError("original protocol identity mismatch")
    if original["search_execution"] != protocol["search_execution"]:
        raise SearchEvaluationError("original protocol search execution mismatch")
    return original


def _single_invocation(
    root: Path, protocol: dict[str, Any], documents: list[tuple[Path, dict[str, Any]]],
    checksums: dict[Path, str],
) -> tuple[dict[str, Any] | None, str]:
    """The runner's existing terminal Manifest, not run_id, establishes membership."""
    if len({d["run_id"] for _, d in documents}) != 1:
        return None, "multiple search invocations; no cross-invocation aggregation"
    run_id = documents[0][1]["run_id"]
    relative = f"manifests/{run_id}.json"
    candidate = root / relative
    try:
        if not candidate.is_file():
            return None, "search manifest unavailable; invocation membership unverified"
        reference = {"path": relative, "sha256": _sha256(candidate)}
        manifest = _bound_json(root, reference, "search manifest", manifest=True)
        execution = protocol["search_execution"]
        params = manifest["parameters"]
        expected_impl = {"implementation_id": execution["implementation"]["implementation_id"], "implementation_version": execution["implementation"]["version"]}
        if (manifest["stage"] != "search" or manifest["run_id"] != run_id
                or manifest["status"] not in {"SUCCEEDED", "FAILED", "CANCELLED"}
                or manifest["implementation"] != expected_impl
                or manifest["source"] != _PINNED_DISKANN_SOURCE
                or params.get("search_execution_checksum") != protocol["search_execution_checksum"]
                or params.get("search_execution") != execution
                or manifest["resource_budget"]["thread_budget"] != execution["thread_count"]
                or manifest["input_artifacts"] != [execution[k] for k in ("dataset_artifact", "global_graph_artifact", "pq_artifact", "disk_layout_artifact")]):
            return None, "search manifest identity/controls do not establish this invocation"
        references = params.get("observation_references", [])
        if not isinstance(references, list) or any(
            not isinstance(r, dict) or set(r) != {"path", "sha256"}
            or not isinstance(r["path"], str) or not isinstance(r["sha256"], str)
            for r in references
        ):
            raise SearchEvaluationError("invalid search manifest observation references")
        if len({r["path"] for r in references}) != len(references):
            raise SearchEvaluationError("duplicate search manifest observation references")
        for path, document in documents:
            original = document["protocol"]
            if any(params.get(k) != original[k] for k in ("protocol_id", "protocol_checksum")):
                return None, "search manifest does not bind every original protocol"
            # Evaluation may read an immutable copy; verify the producer's original too.
            matches = [r for r in references if r["sha256"] == checksums[path]]
            if len(matches) != 1:
                return None, "search manifest does not bind every observation checksum"
            _bound_json(root, matches[0], "search manifest observation")
        return {"search_manifest": reference, "search_execution_checksum": protocol["search_execution_checksum"], "run_id": run_id}, "verified single search invocation"
    except (OSError, SearchEvaluationError) as error:
        if getattr(error, "_unexpected_bound_json_error", False):
            raise
        return None, f"search manifest unavailable; invocation membership unverified: {error}"


def _environment_limits(environments: list[dict[str, Any]]) -> tuple[list[str], bool]:
    # These are recorded facts only. No platform probing or inferred hardware equality.
    ignored = {"performance_admitted", "performance_classification"}
    facts: dict[str, list[Any]] = {}
    for environment in environments:
        for key, value in environment.items():
            if key not in ignored and value is not None and value != "unknown":
                facts.setdefault(key, []).append(value)
    conflicts = sorted(key for key, values in facts.items() if any(v != values[0] for v in values[1:]))
    unknown = [key for key in ("hardware", "storage", "filesystem", "container", "resource_limits", "load")
               if any(not env.get(key) or env.get(key) == "unknown" for env in environments)]
    limits = ["Descriptive summary of this verified invocation only; no cross-invocation or hardware equivalence inferred."]
    if unknown:
        limits.append("Environment facts unknown or incomplete: " + ", ".join(unknown))
    if conflicts:
        limits.append("Conflicting recorded environment conditions: " + ", ".join(conflicts))
    return limits, bool(conflicts)


def _parity_findings(
    root: Path, protocol: dict[str, Any], documents: list[tuple[Path, dict[str, Any]]],
    checksums: dict[Path, str], invocation: dict[str, Any] | None,
) -> tuple[dict[str, Any], dict[Path, dict[str, str]]]:
    default = {"assessment": "NOT_VERIFIED", "reason": "Search Runner Parity not reached"}
    findings = {path: dict(default) for path, _ in documents}
    execution = protocol["search_execution"]
    relative = f"reports/search-parity/{execution['execution_id']}.json"
    path = root / relative
    summary: dict[str, Any] = {"result": "INVALID_EVIDENCE", **default, "recall_scope": "original_ground_truth_only"}
    try:
        if not path.exists():
            summary["result"] = "MISSING"
            return summary, findings
        ref = {"path": relative, "sha256": _sha256(path)}
        summary["evidence"] = ref
        evidence = _bound_json(root, ref, "Parity evidence")
        result = evidence.get("result")
        if not isinstance(result, str) or result not in {"PASS", "FAIL", "NOT_REACHED"}:
            raise SearchEvaluationError("invalid Parity result")
        summary["result"] = result
        if result == "PASS" and evidence.get("instrumented_exit_code") != 0:
            raise SearchEvaluationError("Parity PASS has no successful instrumented invocation")
        controls = evidence.get("controls")
        if controls is not None:
            expected = {"search_execution_checksum": protocol["search_execution_checksum"], **{k: execution[k] for k in ("K", "beamwidth", "thread_count", "cache", "io_limit", "warmup_policy")}}
            if controls != expected:
                raise SearchEvaluationError("Parity search execution/control identity mismatch")
        if result == "NOT_REACHED":
            return summary, findings
        points = evidence.get("points")
        # The existing preparation-failure record has exit=0 and no comparison points.
        # A verified successful search Manifest establishes that measured search ran;
        # no result/measurement comparison exists here. Never inspect reason text.
        if result == "FAIL" and points == [] and evidence.get("instrumented_exit_code") == 0 and invocation:
            reason = "Parity incomplete: instrumented search retained; no oracle comparison points"
            summary.update(assessment="NOT_VERIFIED", reason=reason)
            return summary, {p: {"assessment": "NOT_VERIFIED", "reason": reason} for p, _ in documents}
        if not isinstance(points, list) or not controls:
            if points and isinstance(points, list):
                raise SearchEvaluationError("Parity points lack execution binding")
            if result == "PASS":
                raise SearchEvaluationError("Parity PASS lacks bound structured evidence")
            reason = "unresolved Parity FAIL: comparison evidence insufficient"
            summary.update(assessment="UNRESOLVED", reason=reason)
            return summary, {p: {"assessment": "UNRESOLVED", "reason": reason} for p, _ in documents}
        build = execution["runner_build"]
        build_manifest = _bound_json(root, {"path": build["build_manifest_path"], "sha256": build["build_manifest_sha256"]}, "runner build manifest", manifest=True)
        build_source = build_manifest["source"]
        bp = build_manifest["parameters"]
        if not isinstance(bp.get("runner_binary", {}), dict):
            raise SearchEvaluationError("invalid Parity runner binary reference")
        if (build_manifest["status"] != "SUCCEEDED"
                or any(build_source.get(k) != build["project_" + k] for k in ("repository", "branch", "commit"))
                or build_source["dirty_worktree"] != build["project_dirty"]
                or bp.get("runner_binary", {}).get("sha256", bp.get("binary_sha256")) != build["binary_sha256"]
                or bp.get("diskann_source") != _PINNED_DISKANN_SOURCE):
            raise SearchEvaluationError("Parity runner/build identity mismatch")
    except (OSError, SearchEvaluationError) as error:
        if getattr(error, "_unexpected_bound_json_error", False):
            raise
        reason = f"Parity evidence unavailable: {error}"
        summary.update(assessment="UNRESOLVED", reason=reason)
        return summary, {p: {"assessment": "UNRESOLVED", "reason": reason} for p, _ in documents}
    by_slot = {(d["raw_search_point"]["L_search"], d["raw_search_point"]["repetition_index"]): (p, d) for p, d in documents}
    seen: set[tuple[int, int]] = set()
    unscoped_reason = None
    for point in points:
        observation_path = None
        try:
            if not isinstance(point, dict):
                raise SearchEvaluationError("invalid Parity point")
            slot = (point.get("L_search"), point.get("repetition_index"))
            if any(type(value) is not int for value in slot) or slot not in by_slot or slot in seen:
                raise SearchEvaluationError("Parity undeclared/duplicate point")
            seen.add(slot)
            observation_path, observation = by_slot[slot]
            # Core admission already validated the original protocol before this helper.
            original = observation["protocol"]
            oracle_ref = {"path": point.get("oracle_manifest_path"), "sha256": point.get("oracle_manifest_sha256")}
            oracle = _bound_json(root, oracle_ref, "oracle manifest", manifest=True)
            params = oracle["parameters"]
            if (oracle["stage"] != "search" or oracle["source"] != _PINNED_DISKANN_SOURCE
                    or oracle["implementation"] != {"implementation_id": "cpp_main_search_disk_index_oracle_v1", "implementation_version": "1.0.0"}
                    or params.get("search_execution_checksum") != protocol["search_execution_checksum"]
                    or params.get("protocol_checksum") != original["protocol_checksum"]
                    or params.get("protocol_id") != original["protocol_id"]
                    or oracle["run_id"] != f"{execution['execution_id']}-oracle-L{slot[0]}-r{slot[1]}"
                    or oracle["toolchain"] != build_manifest["toolchain"]
                    or oracle["resource_budget"]["thread_budget"] != execution["thread_count"]
                    or oracle["input_artifacts"] != [execution[k] for k in ("dataset_artifact", "global_graph_artifact", "pq_artifact", "disk_layout_artifact")]):
                raise SearchEvaluationError("Parity oracle manifest binding mismatch")
            argv = params.get("argv", [])
            expected_args = {"--recall_at": "100", "--search_list": str(slot[0]), "--beamwidth": "2",
                             "--num_threads": "6", "--num_nodes_to_cache": str(execution["cache"]["requested_nodes"]),
                             "--search_io_limit": str(execution["io_limit"])}
            if not isinstance(argv, list) or any(argv.count(k) != 1 or argv.index(k) + 1 >= len(argv) or argv[argv.index(k) + 1] != v for k, v in expected_args.items()):
                raise SearchEvaluationError("Parity oracle control binding mismatch")
            for name in ("stdout", "stderr"):
                ref = params.get(name, {})
                if not isinstance(ref, dict):
                    raise SearchEvaluationError(f"invalid oracle {name} reference")
                evidence_file = _auxiliary_existing_file(root, ref.get("path"), f"oracle {name}")
                if _sha256(evidence_file) != ref.get("sha256"):
                    raise SearchEvaluationError(f"oracle {name} checksum mismatch")
            output_refs = params.get("result_files")
            if not isinstance(output_refs, list):
                raise SearchEvaluationError("oracle result references invalid")
            for ref in output_refs:
                if not isinstance(ref, dict):
                    raise SearchEvaluationError("invalid oracle result reference")
                output_file = _auxiliary_existing_file(root, ref.get("path"), "oracle result")
                if _sha256(output_file) != ref.get("sha256") or output_file.stat().st_size != ref.get("byte_length"):
                    raise SearchEvaluationError("oracle result checksum/length mismatch")
            if not isinstance(point.get("result"), str) or point["result"] not in {"PASS", "FAIL"}:
                raise SearchEvaluationError("invalid Parity point result")
            # A structured process failure means the oracle never formed a comparison.
            if (point["result"] == "FAIL" and oracle["status"] == "FAILED"
                    and oracle.get("failure", {}).get("category") == "process_failed"
                    and type(oracle.get("failure", {}).get("exit_code")) is int
                    and oracle["failure"]["exit_code"] != 0
                    and "observation_sha256" not in point):
                findings[observation_path] = {"assessment": "NOT_VERIFIED", "reason": "Parity incomplete: oracle process failed before comparison"}
                continue
            if point["result"] == "FAIL" and oracle["status"] == "SUCCEEDED" and len(output_refs) != 2 and "observation_sha256" not in point:
                findings[observation_path] = {"assessment": "NOT_VERIFIED", "reason": "Parity incomplete: oracle result files not produced"}
                continue
            if "observation_sha256" not in point:
                findings[observation_path] = {"assessment": "UNRESOLVED", "reason": "Parity FAIL without a complete comparison"}
                if point["result"] == "PASS":
                    raise SearchEvaluationError("Parity PASS lacks observation binding")
                continue
            if point["observation_sha256"] != checksums[observation_path]:
                raise SearchEvaluationError("Parity observation checksum mismatch")
            _bound_json(root, {"path": point.get("observation_path"), "sha256": point["observation_sha256"]}, "Parity observation")
            checks = {"ordered_ids_equal": True, "distances_within_tolerance": True, "mean_ios_equal": True,
                      "query_stats_mapping": "total_us_and_n_ios_direct", "qps_formula": "query_count/canonical_batch_wall_clock_seconds",
                      "latency_formula": "pinned_QueryStats_total_us", "timing_boundary": execution["timing_boundary"], "persistence_outside_timing": True}
            if any(key in point and type(point[key]) is not type(expected) for key, expected in checks.items()):
                raise SearchEvaluationError("invalid Parity comparison field type")
            wrong = [key for key, expected in checks.items() if key in point and point[key] != expected]
            missing = [key for key in checks if key not in point]
            if oracle["status"] != "SUCCEEDED":
                raise SearchEvaluationError("Parity comparison references unsuccessful oracle")
            if len(output_refs) != 2:
                raise SearchEvaluationError("Parity comparison lacks oracle result files")
            if wrong:
                findings[observation_path] = {"assessment": "ERROR", "reason": "Parity detected semantic error: " + ", ".join(wrong)}
            elif missing or point["result"] != "PASS":
                findings[observation_path] = {"assessment": "UNRESOLVED", "reason": "Parity comparison unresolved; original GroundTruth Recall is not re-certified"}
            else:
                findings[observation_path] = {"assessment": "PASS", "reason": "execution/observation-bound search semantics parity; Recall applies only to original GroundTruth"}
        except (OSError, SearchEvaluationError) as error:
            if getattr(error, "_unexpected_bound_json_error", False):
                raise
            reason = f"Parity evidence unavailable: {error}"
            if observation_path is None:
                unscoped_reason = reason
            else:
                findings[observation_path] = {"assessment": "UNRESOLVED", "reason": reason}
    for slot in by_slot.keys() - seen:
        observation_path, _ = by_slot[slot]
        findings[observation_path] = {
            "assessment": "UNRESOLVED",
            "reason": f"Parity {result} has no comparison evidence for this point",
        }
    if result == "FAIL" and all(f["assessment"] == "PASS" for f in findings.values()):
        unscoped_reason = "Parity overall FAIL contradicts all point-level PASS results"
    if result == "PASS":
        for finding in findings.values():
            if finding["assessment"] == "NOT_VERIFIED":
                finding.update(assessment="UNRESOLVED", reason="Parity overall PASS contradicts incomplete comparison")
    if unscoped_reason:
        # Missing scope must withhold aggregation without undoing proven errors.
        for finding in findings.values():
            if finding["assessment"] != "ERROR":
                finding.update(assessment="UNRESOLVED", reason=unscoped_reason)
    states = {f["assessment"] for f in findings.values()}
    summary.update(assessment=next(iter(states)) if len(states) == 1 else "MIXED", reason="See per-observation parity applicability; no cross-execution PASS reuse")
    return summary, findings


def _evaluate_observation_directory(
    *, protocol_path: str | Path, observations_path: str | Path,
    workspace: str | Path, provenance: dict[str, Any],
) -> dict[str, Any]:
    """Offline-only evaluator; never invokes the C++ runner or SSD search."""
    root = Path(workspace).resolve()
    protocol_file = Path(protocol_path).resolve()
    protocol = json.loads(protocol_file.read_text(encoding="utf-8"))
    normalized = normalize_search_protocol(protocol)
    if normalized != protocol:
        raise SearchEvaluationError("frozen SearchProtocol checksum/identity mismatch")
    execution = protocol["search_execution"]
    v2 = protocol["protocol_version"] == "1.1.0"
    truth_ref = protocol["evaluation"]["ground_truth"]
    ground_truth, ground_truth_distances = _load_evaluation_ground_truth(root, protocol)
    observation_dir = Path(observations_path).resolve()
    documents: list[tuple[Path, dict[str, Any]]] = []
    file_checksums: dict[Path, str] = {}
    for path in sorted(observation_dir.glob("*.json")):
        try:
            payload = path.read_bytes()
            document = json.loads(payload)
        except (OSError, ValueError, UnicodeError) as error:
            raise SearchEvaluationError(f"cannot read observation identity: {path}: {error}") from error
        documents.append((path, document))
        file_checksums[path] = hashlib.sha256(payload).hexdigest()
    if not documents:
        raise SearchEvaluationError("no SearchObservation was supplied")
    validator = _feature_validator("search-observation.schema.json")
    admitted = _admit_observation_set(protocol, documents, validator)
    participating = [(p, d) for p, d in documents if p in admitted]
    for _, document in participating:
        if v2 or document["protocol"]["protocol_checksum"] != protocol["protocol_checksum"]:
            _original_protocol(root, document, protocol)
    invocation, aggregation_reason = (None, "legacy performance admission")
    parity_summary: dict[str, Any] = {}
    parity_findings: dict[Path, dict[str, str]] = {}
    if v2:
        invocation, aggregation_reason = _single_invocation(root, protocol, participating, file_checksums)
        parity_summary, parity_findings = _parity_findings(root, protocol, participating, file_checksums, invocation)

    results: list[dict[str, Any]] = []
    display: list[dict[str, Any]] = []
    quality_groups: dict[tuple[str, int], list[dict[str, float]]] = {}
    performance_groups: dict[tuple[str, int], list[dict[str, float]]] = {}
    environment_path = None
    environment_sha = None
    environments: dict[str, dict[str, Any]] = {}
    for path, document in documents:
        point = document["raw_search_point"]
        cache_mode = point["cache"]["mode"]
        l_search = point["L_search"]
        repetition = point["repetition_index"]
        file_checksum = file_checksums[path]
        exclusion_reason = _observation_exclusion_reason(
            document, validator, identity_compatible=path in admitted,
            execution=execution,
        )
        relative = path.relative_to(root).as_posix()
        parity = parity_findings.get(path, {"assessment": "NOT_VERIFIED", "reason": "observation excluded before parity use"})
        if v2 and exclusion_reason is None and parity["assessment"] == "ERROR":
            exclusion_reason = parity["reason"]
        if exclusion_reason is not None:
            result = audit_excluded_observation(
                observation_id=document["observation_id"],
                path=relative, checksum=file_checksum,
                status=point["status"], reason=exclusion_reason,
            )
            result.update({"cache_mode": cache_mode, "L_search": l_search, "repetition_index": repetition})
            if v2:
                result["performance_valid"] = result.pop("performance_eligible")
                result["performance_validity_reason"] = result.pop("performance_eligibility_reason")
                result.update(original_protocol=document["protocol"], parity=parity)
            results.append(result)
            continue
        environment_path = document["environment"]["fingerprint_path"]
        actual_environment, environment_sha = _verified_observation_environment(
            root, document["environment"], path,
        )
        performance_admitted = not v2 and bool(actual_environment.get("performance_admitted", False))
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
        if v2:
            result.pop("performance_eligible")
            result.pop("performance_eligibility_reason")
            result.update(
                performance_valid=True, performance_validity_reason="complete valid measurement under recorded runtime conditions",
                performance_metrics=evaluated["display_metrics"], environment=document["environment"],
                original_protocol=document["protocol"], parity=parity,
            )
            environments[relative] = actual_environment
        quality_groups.setdefault((cache_mode, l_search), []).append(
            evaluated["quality_metrics"]
        )
        displayed = {"observation_id": document["observation_id"], **evaluated["display_metrics"]}
        if v2:
            displayed["performance_valid"] = True
        else:
            displayed["classification"] = "formal" if performance_admitted else "informational_non_formal"
        display.append(displayed)
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
    if v2:
        valid = [r for r in results if r["performance_valid"]]
        limits, conflict = _environment_limits([environments[r["path"]] for r in valid])
        if conflict:
            aggregation_reason = limits[-1]
        elif invocation is not None:
            aggregation_reason = "single-invocation descriptive aggregation; see scope limitations"
            for cache_mode, l_search in sorted({(r["cache_mode"], r["L_search"]) for r in valid}):
                members = [r for r in valid if (r["cache_mode"], r["L_search"]) == (cache_mode, l_search)]
                if any(r["parity"]["assessment"] == "UNRESOLVED" for r in members):
                    aggregation_reason = "unresolved Parity evidence prevents affected performance aggregation; see per-observation reasons"
                    continue
                performance_aggregates.append({
                    "cache_mode": cache_mode, "L_search": l_search, "eligible_count": len(members), "aggregation": "median_eligible",
                    "median_performance_metrics": {k: _median([r["performance_metrics"][k] for r in members]) for k in members[0]["performance_metrics"]},
                    "scope": {**invocation, "members": [{k: r[k] for k in ("observation_id", "path", "sha256")} for r in members],
                              "environments": list({json.dumps(r["environment"], sort_keys=True): r["environment"] for r in members}.values()), "limitations": limits},
                })
    if environment_path is None or environment_sha is None:
        raise SearchEvaluationError("no complete valid observation is eligible for a report")

    parity_path = root / "reports" / "search-parity" / f"{execution['execution_id']}.json"
    if v2:
        parity = parity_summary
    elif parity_path.exists():
        parity = json.loads(parity_path.read_text(encoding="utf-8"))
    else:
        parity = {"result": "NOT_REACHED", "reason": "actual Search Runner Parity pending"}
        write_immutable_json(parity_path, parity)
    non_reexecution_path = root / "reports" / "search-evaluation" / f"{execution['execution_id']}-non-reexecution.json"
    non_reexecution = {"partition_invoked": False, "local_build_invoked": False, "merge_invoked": False, "proof": "Feature runner exposes no graph-stage operation"}
    if not non_reexecution_path.exists():
        write_immutable_json(non_reexecution_path, non_reexecution)
    report = {
        "report_version": "search-evaluation-report/v2" if v2 else "search-evaluation-report/v1",
        "report_id": f"{protocol['protocol_id']}-evaluation",
        "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "evaluator": protocol["evaluation"]["evaluator"],
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
        "search_runner_parity": parity_summary if v2 else {"evidence_path": parity_path.relative_to(root).as_posix(), "evidence_sha256": _sha256(parity_path), "result": parity["result"]},
        "non_reexecution_evidence": {**{key: non_reexecution[key] for key in ("partition_invoked", "local_build_invoked", "merge_invoked")}, "evidence_path": non_reexecution_path.relative_to(root).as_posix(), "evidence_sha256": _sha256(non_reexecution_path)},
        "observation_results": results,
        "aggregates": {"quality": quality_aggregates, "performance": performance_aggregates},
    }
    if v2:
        report.pop("environment_fingerprint")
        report.update(evaluator_provenance=provenance, performance_aggregation_reason=aggregation_reason,
                      evaluation_policy=protocol["evaluation"]["performance_eligibility_policy"])
    report["report_checksum"] = _canonical_digest(report)
    errors = list(report_validator(report).iter_errors(report))
    if errors:
        raise SearchEvaluationError(f"SearchEvaluationReport is invalid: {errors[0].message}")
    report_path = root / "reports" / "search-evaluation" / f"{report['report_id']}.json"
    write_immutable_json(report_path, report)
    return {"report_path": str(report_path), "report": report, "display": display}


def _evaluation_manifest(
    *, protocol: dict[str, Any], workspace: Path, started_at: str,
    ended_at: str, status: str, report_path: Path | None,
    observation_root: Path,
    provenance: dict[str, Any],
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
        "evaluator_provenance": provenance,
        "search_runner_build": runner_build,
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
            "implementation_id": protocol["evaluation"]["evaluator"]["implementation_id"],
            "implementation_version": protocol["evaluation"]["evaluator"]["version"],
        },
        "source": provenance["source"],
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
    provenance = _evaluator_provenance()
    manifest_path = (
        root / "manifests" / f"{protocol['protocol_id']}-evaluation.json"
    )
    try:
        result = _evaluate_observation_directory(
            protocol_path=protocol_path,
            observations_path=observations_path,
            workspace=workspace,
            provenance=provenance,
        )
        ended_at = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        manifest = _evaluation_manifest(
            protocol=protocol, workspace=root, started_at=started_at,
            ended_at=ended_at, status="SUCCEEDED",
            report_path=Path(result["report_path"]),
            observation_root=observation_root,
            provenance=provenance,
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
                    provenance=provenance,
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


def _oracle_artifact_payloads(
    *, workspace: Path, artifact: Any, expected_type: str,
    required_roles: tuple[str, ...],
) -> dict[str, Path]:
    reference = _artifact_ref(artifact, expected_type)
    normal_root = (workspace / "normal").resolve()
    metadata_path = _confined_existing_file(
        normal_root, reference["metadata_path"], f"{expected_type} metadata"
    )
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SearchEvaluationError(
            f"{expected_type} metadata is unreadable"
        ) from error
    if (
        metadata.get("artifact_type") != reference["artifact_type"]
        or metadata.get("format_version") != reference["format_version"]
        or metadata.get("aggregate_checksum") != reference["aggregate_checksum"]
    ):
        raise SearchEvaluationError(f"{expected_type} metadata identity mismatch")
    payloads = metadata.get("payloads")
    if type(payloads) is not list:
        raise SearchEvaluationError(f"{expected_type} payload declarations are invalid")
    by_role: dict[str, Any] = {}
    for payload in payloads:
        if type(payload) is not dict or type(payload.get("role")) is not str:
            raise SearchEvaluationError(
                f"{expected_type} payload declaration is invalid"
            )
        role = payload["role"]
        if role in by_role:
            raise SearchEvaluationError(f"{expected_type} payload role is duplicated")
        by_role[role] = payload
    admitted: dict[str, Path] = {}
    for role in required_roles:
        payload = by_role.get(role)
        if payload is None:
            raise SearchEvaluationError(f"{expected_type} payload role is missing: {role}")
        relative = Path(payload.get("path", ""))
        if relative.is_absolute() or not relative.parts or ".." in relative.parts:
            raise SearchEvaluationError(f"{expected_type} payload path escapes Artifact")
        source = (metadata_path.parent / relative).resolve()
        try:
            source.relative_to(metadata_path.parent.resolve())
        except ValueError as error:
            raise SearchEvaluationError(
                f"{expected_type} payload path escapes Artifact"
            ) from error
        byte_length = payload.get("byte_length")
        if (
            type(byte_length) is not int
            or byte_length < 0
            or not source.is_file()
            or source.stat().st_size != byte_length
        ):
            raise SearchEvaluationError(
                f"{expected_type} payload is missing or has wrong byte length: {role}"
            )
        admitted[role] = source
    return admitted


def _stage_oracle_native_index(
    *, workspace: Path, execution: dict[str, Any]
) -> tuple[Path, Path]:
    execution_id = execution.get("execution_id")
    if (
        type(execution_id) is not str
        or not execution_id
        or Path(execution_id).name != execution_id
    ):
        raise SearchEvaluationError("oracle staging execution identity is invalid")
    sources = {
        **_oracle_artifact_payloads(
            workspace=workspace, artifact=execution.get("pq_artifact"),
            expected_type="pq",
            required_roles=("pq_pivots", "pq_compressed_vectors"),
        ),
        **_oracle_artifact_payloads(
            workspace=workspace, artifact=execution.get("disk_layout_artifact"),
            expected_type="disk_layout", required_roles=("disk_index", "medoids"),
        ),
    }
    temporary_root = (workspace / "temporary").resolve()
    try:
        temporary_root.relative_to(workspace)
    except ValueError as error:
        raise SearchEvaluationError("oracle staging path escapes workspace") from error
    temporary_root.mkdir(parents=True, exist_ok=True)
    stage = temporary_root / f"{execution_id}-oracle-native-index"
    try:
        stage.mkdir()
    except FileExistsError as error:
        raise SearchEvaluationError("oracle staging directory already exists") from error
    prefix = stage / "index"
    aliases = {
        "disk_index": Path(f"{prefix}_disk.index"),
        "medoids": Path(f"{prefix}_disk.index_medoids.bin"),
        "pq_pivots": Path(f"{prefix}_pq_pivots.bin"),
        "pq_compressed_vectors": Path(f"{prefix}_pq_compressed.bin"),
    }
    fallback_errors = {errno.EXDEV, errno.ENOSYS, errno.ENOTSUP, errno.EOPNOTSUPP}
    try:
        for role, destination in aliases.items():
            source = sources[role]
            try:
                os.link(source, destination)
            except OSError as error:
                if error.errno not in fallback_errors or destination.exists():
                    raise
                shutil.copyfile(source, destination)
            if (
                not destination.is_file()
                or destination.stat().st_size != source.stat().st_size
            ):
                raise SearchEvaluationError(
                    f"oracle staging alias is invalid: {role}"
                )
    except Exception:
        shutil.rmtree(stage)
        raise
    return stage, prefix


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
    try:
        oracle_stage, prefix = _stage_oracle_native_index(
            workspace=workspace_path, execution=execution
        )
    except Exception as error:
        write_immutable_json(
            evidence_path,
            {
                "result": "FAIL",
                "reason": f"oracle staging failed: {error}",
                "instrumented_exit_code": runner_result.returncode,
                "points": [],
                "graph_structural_parity_gate": "NOT_REACHED",
                "framework_state": "candidate",
            },
        )
        return {"result": "FAIL", "evidence_path": str(evidence_path)}
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
    shutil.rmtree(oracle_stage)
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
    evidence_sets: list[dict[str, Any]] = []
    for declaration in declarations:
        execution = declaration.get("search_execution", {})
        evidence = execution.get("admission_evidence")
        if evidence is None:
            if not fixture_mode:
                common_pass = False
            continue
        if type(evidence) is not dict or set(evidence) != {
            "pq_artifact", "disk_layout_artifact"
        }:
            raise SearchEvaluationError("search admission evidence set is invalid")
        _validate_search_admission_evidence(
            root=root,
            declaration=evidence["pq_artifact"],
            artifact=execution.get("pq_artifact"),
            expected_type="pq",
        )
        _validate_search_admission_evidence(
            root=root,
            declaration=evidence["disk_layout_artifact"],
            artifact=execution.get("disk_layout_artifact"),
            expected_type="disk_layout",
        )
        evidence_sets.append(evidence)
    if evidence_sets and any(value != evidence_sets[0] for value in evidence_sets[1:]):
        raise SearchEvaluationError(
            "zero/fixed protocols must bind the same fresh admission evidence"
        )
    if not fixture_mode and len(evidence_sets) != len(declarations):
        common_pass = False
    if not fixture_mode:
        # A real formal-search preflight cannot substitute declaration booleans for
        # the exact input files and freshly validated PQ/Layout evidence it binds.
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
    value = dict(value)
    allowed = {
        "request_id", "run_id", "workspace", "operation", "dataset",
        "global_graph", "implementation", "parameters", "resource_budget",
        "candidate_path",
    }
    # Historical direct-native requests omitted identity and used empty/fallback
    # parameters. They remain frozen 1.0.0, never implicitly upgraded by N.
    required = allowed - {"global_graph", "implementation", "request_id", "candidate_path"}
    if not required.issubset(value) or not set(value).issubset(allowed):
        raise SearchEvaluationError("materialization request fields are invalid")
    operation = value["operation"]
    if type(operation) is not str or operation not in {"pq", "disk_layout"}:
        raise SearchEvaluationError("materialization operation is invalid")
    _artifact_ref(value["dataset"], "dataset")
    if operation == "pq" and "global_graph" in value:
        raise SearchEvaluationError("PQ request must not bind a GlobalGraph")
    if operation == "disk_layout":
        if "global_graph" not in value:
            raise SearchEvaluationError("DiskLayout request requires a GlobalGraph")
        _artifact_ref(value["global_graph"], "global_graph")
    expected_id = (
        "cpp_main_post_graph_pq_v1"
        if operation == "pq"
        else "cpp_main_post_graph_disk_layout_v1"
    )
    implicit = "implementation" not in value
    parameters = value["parameters"]
    if implicit and not (
        type(parameters) is dict
        and (
            parameters == {}
            or operation == "disk_layout" and parameters == {"centroid_policy": "medoid_vector_fallback"}
        )
    ):
        raise SearchEvaluationError("materialization implementation is required outside legacy requests")
    implementation = value.setdefault("implementation", {
        "implementation_id": expected_id,
        "implementation_version": "1.0.0",
        "reproduction_mode": "upstream_faithful",
    })
    version = implementation.get("implementation_version") if type(implementation) is dict else None
    versions = {"1.0.0", "1.1.0"} if operation == "pq" else {"1.0.0"}
    if type(version) is not str or version not in versions or implementation != {
        "implementation_id": expected_id,
        "implementation_version": version,
        "reproduction_mode": "upstream_faithful",
    }:
        raise SearchEvaluationError("materialization implementation identity is invalid")
    expected = _PQ_PARAMETERS if operation == "pq" else _LAYOUT_PARAMETERS
    if implicit:
        parameters = dict(expected)
        value["parameters"] = parameters
    if type(parameters) is not dict:
        raise SearchEvaluationError("materialization parameters must be an object")
    if operation == "pq":
        if version == "1.0.0":
            if parameters != expected or type(parameters.get("opq")) is not bool or any(
                type(parameters.get(key)) is not int for key in ("QD", "pq_chunks", "centroid_count")
            ):
                raise SearchEvaluationError("materialization parameters drifted")
        elif set(parameters) != {"B", "QD"}:
            raise SearchEvaluationError("PQ 1.1.0 requires only requested B and QD")
        b, qd = parameters.get("B"), parameters.get("QD")
        try:
            valid_b = type(b) in (int, float) and math.isfinite(b) and b > 0
        except OverflowError:
            valid_b = False
        if not valid_b or type(qd) is not int or not 0 <= qd <= 512:
            raise SearchEvaluationError("PQ B must be finite positive and QD an integer in [0,512]")
        # Dataset-dependent derivation and conversion checks belong solely to C++.
    elif (
        parameters != expected
        or type(parameters.get("pq_disk_bytes")) is not int
        or any(type(parameters.get(key)) is not bool for key in ("reorder", "labels"))
    ):
        raise SearchEvaluationError("materialization parameters drifted")
    value.setdefault("request_id", value["run_id"])
    value.setdefault("candidate_path", f"temporary/{value['run_id']}/candidate/artifact.json")
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
    return json.loads(json.dumps(value, sort_keys=True, allow_nan=False))


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
        request = normalize_materialization_request(_read_request(args.request))
        if request["operation"] != args.operation:
            raise SearchEvaluationError("CLI operation differs from materialization request")
        # Preserve the user's historical request; execute exactly the normalized
        # document rather than forwarding unchecked bytes.
        with tempfile.TemporaryDirectory(prefix="diskann-materialization-") as temporary:
            normalized_path = Path(temporary) / "request.json"
            normalized_path.write_text(
                json.dumps(request, sort_keys=True, allow_nan=False), encoding="utf-8"
            )
            return _run_argv([args.runner, "materialize", "--request", str(normalized_path)])
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
