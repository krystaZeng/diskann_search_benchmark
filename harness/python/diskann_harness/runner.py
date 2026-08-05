"""CLI/JSON orchestration for the Feature 001 C++ runner."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource

from .config import load_yaml_file, normalize_run_request, write_normalized_request
from .manifests import ImmutableWriteError, write_immutable_json
from .workspace import create_workspace, normalize_workspace, resolve_confined_path


class RunnerError(RuntimeError):
    """Stable public error for runner protocol and lifecycle failures."""


EXIT_MEANINGS = {
    0: "success",
    2: "usage_or_request_invalid",
    3: "preflight_rejected",
    4: "stage_failed",
    5: "invalid_output",
    6: "cancelled",
    7: "validation_or_evaluation_failed",
    10: "internal_error",
}


def build_runner_command(
    runner: str | Path,
    operation: str,
    request: str | Path,
    result: str | Path,
) -> list[str]:
    return [
        str(runner),
        operation,
        "--request",
        str(request),
        "--result",
        str(result),
    ]


def _schema_validator(schema_name: str, repository_root: Path) -> Draft202012Validator:
    schema_root = repository_root / "contracts" / "v1" / "schemas"
    schemas = {
        path.name: json.loads(path.read_text(encoding="utf-8"))
        for path in sorted(schema_root.glob("*.json"))
    }
    registry = Registry().with_resources(
        (schema["$id"], Resource.from_contents(schema))
        for schema in schemas.values()
        if "$id" in schema
    )
    return Draft202012Validator(schemas[schema_name], registry=registry)


def run_runner(
    runner: str | Path,
    request: str | Path,
    result: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    try:
        operation = json.loads(Path(request).read_text(encoding="utf-8"))["operation"]
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise RunnerError("cannot determine runner operation from request") from error
    command = build_runner_command(runner, operation, request, result)
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            shell=False,
        )
    except OSError as error:
        raise RunnerError(f"cannot start C++ runner: {error}") from error
    try:
        stdout, stderr = process.communicate()
    except KeyboardInterrupt:
        process.terminate()
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
        raise RunnerError("runner cancelled by caller") from None

    result_path = Path(result)
    if not result_path.is_file():
        meaning = EXIT_MEANINGS.get(process.returncode, "unknown_exit")
        raise RunnerError(
            f"runner result was not published ({meaning}, exit={process.returncode}): "
            f"{stderr.strip() or stdout.strip()}"
        )
    try:
        document = json.loads(result_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RunnerError("runner result is not valid UTF-8 JSON") from error
    root = (
        Path(repository_root)
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    try:
        _schema_validator("runner-result.schema.json", root).validate(document)
    except (ValidationError, KeyError, OSError, json.JSONDecodeError) as error:
        raise RunnerError(f"runner result schema validation failed: {error}") from error
    if document.get("exit_code") != process.returncode:
        raise RunnerError("runner exit code differs from its result document")
    return document


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _evaluation_projection(report: dict[str, Any]) -> dict[str, Any]:
    findings = report["findings"]
    return {
        "graph_integrity": findings["graph_integrity"],
        "logical_node_count": findings["logical_node_count"],
        "invalid_neighbor_count": findings["invalid_neighbor_count"],
        "degree_bound": (
            findings["observed_max_degree"] <= findings["declared_max_degree"]
        ),
        "passed": findings["passed"],
    }


def _result_identity(result: dict[str, Any], roots: dict[str, str]) -> dict[str, Any]:
    artifact = result["artifact"]
    metadata_path = resolve_confined_path(roots["normal_root"], artifact["metadata_path"])
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    report_path = resolve_confined_path(roots["root"], result["evaluation_report_path"])
    report = json.loads(report_path.read_text(encoding="utf-8"))
    return {
        "aggregate_checksum": artifact["aggregate_checksum"],
        "payload_checksums": [payload["sha256"] for payload in metadata["payloads"]],
        "evaluation_projection": _evaluation_projection(report),
        "report_id": report["report_id"],
        "generated_at": report["generated_at"],
    }


def execute_configuration(
    config_path: str | Path,
    workspace_root: str | Path,
    runner: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    config_file = Path(config_path).resolve()
    configuration = load_yaml_file(config_file)
    configuration["workspace"]["root"] = str(Path(workspace_root).resolve())
    normalized = normalize_run_request(configuration, repository_root=repository)
    roots = normalized["workspace"]
    create_workspace(roots)
    count = normalized["repetition_policy"]["count"]
    if count != 2 or normalized["repetition_policy"]["aggregation"] != "retain_all":
        raise RunnerError("Feature 001 requires two retained deterministic runs")

    base_id = normalized["request_id"]
    retained: list[dict[str, Any]] = []
    identities: list[dict[str, Any]] = []
    for repetition in range(1, count + 1):
        request = copy.deepcopy(normalized)
        request["request_id"] = f"{base_id}-run-{repetition}"
        request_path = resolve_confined_path(
            roots["manifest_root"], f"requests/{request['request_id']}.json"
        )
        result_path = resolve_confined_path(
            roots["report_root"], f"runner/{request['request_id']}.json"
        )
        write_normalized_request(request_path, request)
        result = run_runner(
            runner, request_path, result_path, repository_root=repository
        )
        retained.append(result)
        if result["outcome"] == "SUCCEEDED":
            identities.append(_result_identity(result, roots))

    deterministic_equal = (
        len(identities) == 2
        and identities[0]["aggregate_checksum"]
        == identities[1]["aggregate_checksum"]
        and identities[0]["payload_checksums"] == identities[1]["payload_checksums"]
        and identities[0]["evaluation_projection"]
        == identities[1]["evaluation_projection"]
    )
    distinct_provenance = (
        len(retained) == 2
        and retained[0]["request_id"] != retained[1]["request_id"]
        and len(identities) == 2
        and identities[0]["report_id"] != identities[1]["report_id"]
        and identities[0]["generated_at"] != identities[1]["generated_at"]
    )
    summary = {
        "summary_version": "1.0.0",
        "source_config": {
            "path": config_file.as_posix(),
            "yaml_sha256": _sha256(config_file),
        },
        "aggregation": "retain_all",
        "retained_runs": retained,
        "deterministic_equal": deterministic_equal,
        "distinct_run_provenance": distinct_provenance,
    }
    summary_path = resolve_confined_path(
        roots["report_root"], f"summaries/{base_id}.json"
    )
    write_immutable_json(summary_path, summary)
    summary["summary_path"] = str(summary_path)
    return summary


def run_single_request(
    request: dict[str, Any],
    workspace_root: str | Path,
    runner: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    request = copy.deepcopy(request)
    request["workspace"] = {
        **normalize_workspace(workspace_root),
        "memory_budget_bytes": 1048576,
        "thread_budget": 1,
    }
    normalized = normalize_run_request(request, repository_root=repository)
    roots = normalized["workspace"]
    create_workspace(roots)
    request_path = resolve_confined_path(
        roots["manifest_root"], f"requests/{normalized['request_id']}.json"
    )
    result_path = resolve_confined_path(
        roots["report_root"], f"runner/{normalized['request_id']}.json"
    )
    write_normalized_request(request_path, normalized)
    return run_runner(runner, request_path, result_path, repository_root=repository)


def build_project(
    source_dir: str | Path,
    build_dir: str | Path,
    *,
    build_type: str = "Debug",
) -> Path:
    source = Path(source_dir).resolve()
    build = Path(build_dir).resolve()
    configure = [
        "cmake",
        "-S",
        str(source),
        "-B",
        str(build),
        f"-DCMAKE_BUILD_TYPE={build_type}",
    ]
    compile_command = ["cmake", "--build", str(build), "--parallel"]
    for command in (configure, compile_command):
        completed = subprocess.run(command, capture_output=True, text=True, shell=False)
        if completed.returncode != 0:
            raise RunnerError(
                f"build command failed ({completed.returncode}): {completed.stderr.strip()}"
            )
    cache = build / "CMakeCache.txt"
    provenance = {
        "source_dir": str(source),
        "build_dir": str(build),
        "build_type": build_type,
        "commands": [configure, compile_command],
        "cmake_cache_sha256": _sha256(cache),
    }
    target = build / "build-provenance.json"
    try:
        write_immutable_json(target, provenance)
    except ImmutableWriteError:
        existing = json.loads(target.read_text(encoding="utf-8"))
        if existing != provenance:
            raise
    return target
