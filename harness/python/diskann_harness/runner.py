"""CLI/JSON orchestration for the Feature 001 C++ runner."""

from __future__ import annotations

import copy
import hashlib
import json
import os
import platform
import shlex
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource

from .config import load_yaml_file, normalize_run_request, write_normalized_request
from .manifests import ImmutableWriteError, write_immutable_json
from .workspace import (
    create_workspace,
    normalize_workspace,
    reject_upstream_target,
    resolve_confined_path,
)


class RunnerError(RuntimeError):
    """Stable public error for runner protocol and lifecycle failures."""


EXIT_MEANINGS = {
    0: "success",
    2: "usage_or_request_invalid",
    3: "preflight_rejected",
    4: "stage_failed_or_cancelled",
    5: "invalid_output",
    6: "validation_rejected",
    7: "evaluation_failed",
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
        request_document = json.loads(Path(request).read_text(encoding="utf-8"))
        operation = request_document["operation"]
        request_id = request_document.get("request_id", "invalid-request")
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise RunnerError("cannot determine runner operation from request") from error
    command = build_runner_command(runner, operation, request, result)
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    reject_upstream_target(result, repository)
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
        forced = False
        try:
            _, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            forced = True
            process.kill()
            _, stderr = process.communicate()
        result_path = Path(result)
        if result_path.exists():
            raise RunnerError(
                "runner published a conflicting terminal result during Harness cancellation"
            ) from None
        cancellation = {
            "result_version": "1.0.0",
            "request_id": request_id,
            "operation": operation,
            "outcome": "CANCELLED",
            "exit_code": 4,
            "stage_invocations": {
                "partition": 0,
                "local_build": 0,
                "merge": 0,
            },
            "failure": {
                "category": "cancelled",
                "reason": (
                    "Harness interruption requested subprocess termination; "
                    f"forced={str(forced).lower()}"
                ),
                "exit_code": int(process.returncode if process.returncode is not None else -1),
            },
        }
        write_immutable_json(result_path, cancellation)
        root = (
            Path(repository_root).resolve()
            if repository_root is not None
            else Path(__file__).resolve().parents[3]
        )
        _schema_validator("runner-result.schema.json", root).validate(cancellation)
        return cancellation

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
    try:
        _schema_validator("runner-result.schema.json", repository).validate(document)
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


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _canonical_json_sha256(document: Any) -> str:
    return _sha256_bytes(
        json.dumps(
            document,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    )


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace(
        "+00:00", "Z"
    )


def _evaluation_projection(report: dict[str, Any]) -> dict[str, Any]:
    findings = report["findings"]
    return {
        "graph_integrity": findings["graph_integrity"],
        "logical_node_count": findings["logical_node_count"],
        "invalid_neighbor_count": findings["invalid_neighbor_count"],
        "declared_max_degree": findings["declared_max_degree"],
        "observed_max_degree": findings["observed_max_degree"],
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
    normalized_digest = _canonical_json_sha256(normalized)
    normalized["source_config"] = {
        "yaml_sha256": _sha256(config_file),
        "normalized_json_sha256": normalized_digest,
    }
    roots = normalized["workspace"]
    create_workspace(roots, repository_root=repository)
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
    if len(identities) == 2 and (
        not deterministic_equal or not distinct_provenance
    ):
        raise RunnerError(
            "two-run determinism/provenance contract failed; both runs were retained"
        )
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
        **normalize_workspace(workspace_root, repository_root=repository),
        "memory_budget_bytes": 1048576,
        "thread_budget": 1,
    }
    normalized = normalize_run_request(request, repository_root=repository)
    roots = normalized["workspace"]
    create_workspace(roots, repository_root=repository)
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
    repository_root: str | Path | None = None,
    config_path: str | Path | None = None,
) -> Path:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    source = reject_upstream_target(source_dir, repository)
    build = reject_upstream_target(build_dir, repository)
    configure_started_at = _utc_now()
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
    build_ended_at = _utc_now()
    cache = build / "CMakeCache.txt"
    cache_entries: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="strict").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_type, value = line.split("=", 1)
        if ":" not in key_type:
            continue
        key, entry_type = key_type.split(":", 1)
        if entry_type != "STATIC":
            cache_entries[key] = value

    compiler_path = cache_entries.get("CMAKE_CXX_COMPILER", "unavailable")
    try:
        compiler_version = subprocess.run(
            [compiler_path, "--version"],
            capture_output=True,
            text=True,
            shell=False,
            check=True,
        ).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        compiler_version = cache_entries.get(
            "CMAKE_CXX_COMPILER_VERSION", "unavailable"
        )
    cmake_version = subprocess.run(
        ["cmake", "--version"],
        capture_output=True,
        text=True,
        shell=False,
        check=True,
    ).stdout.splitlines()[0]

    try:
        git_root = Path(
            subprocess.run(
                ["git", "-C", str(source), "rev-parse", "--show-toplevel"],
                capture_output=True,
                text=True,
                shell=False,
                check=True,
            ).stdout.strip()
        ).resolve()
        branch = subprocess.run(
            ["git", "-C", str(git_root), "branch", "--show-current"],
            capture_output=True,
            text=True,
            shell=False,
            check=True,
        ).stdout.strip() or "detached-head"
        commit = subprocess.run(
            ["git", "-C", str(git_root), "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            shell=False,
            check=True,
        ).stdout.strip()
        status = subprocess.run(
            ["git", "-C", str(git_root), "status", "--porcelain=v1", "--untracked-files=all"],
            capture_output=True,
            shell=False,
            check=True,
        ).stdout
        dirty = bool(status)
        dirty_material = bytearray(status)
        if dirty:
            dirty_material.extend(
                subprocess.run(
                    ["git", "-C", str(git_root), "diff", "--binary", "HEAD"],
                    capture_output=True,
                    shell=False,
                    check=True,
                ).stdout
            )
            untracked = subprocess.run(
                ["git", "-C", str(git_root), "ls-files", "--others", "--exclude-standard"],
                capture_output=True,
                text=True,
                shell=False,
                check=True,
            ).stdout.splitlines()
            for relative in sorted(untracked):
                path = git_root / relative
                if path.is_file():
                    dirty_material.extend(
                        f"\nuntracked:{relative}:{_sha256(path)}".encode("utf-8")
                    )
        source_identity = {
            "repository": str(git_root),
            "branch": branch,
            "commit": commit,
            "dirty_worktree": dirty,
        }
        if dirty:
            source_identity["dirty_source_checksum"] = _sha256_bytes(
                bytes(dirty_material)
            )
    except (OSError, subprocess.CalledProcessError):
        source_material = b"".join(
            path.relative_to(source).as_posix().encode("utf-8") + b":" + path.read_bytes()
            for path in sorted(source.rglob("*"))
            if path.is_file()
        )
        source_identity = {
            "repository": str(source),
            "branch": "not-git",
            "commit": "0" * 40,
            "dirty_worktree": True,
            "dirty_source_checksum": _sha256_bytes(source_material),
        }

    effective_definitions = [
        f"{key}={value}"
        for key, value in sorted(cache_entries.items())
        if key.startswith(("CMAKE_", "DISKANN_PIPELINE_", "BUILD_"))
    ]
    project_compile_options = (
        "/W4;/permissive-"
        if os.name == "nt"
        else "-Wall;-Wextra;-Wpedantic"
    )
    effective_definitions.append(
        f"PROJECT_COMPILE_OPTIONS={project_compile_options}"
    )
    source_config: dict[str, object]
    if config_path is not None:
        config_file = Path(config_path).resolve()
        configuration = load_yaml_file(config_file)
        normalized_configuration = normalize_run_request(
            configuration, repository_root=repository
        )
        source_config = {
            "path": str(config_file),
            "yaml_sha256": _sha256(config_file),
            "normalized_json_sha256": _canonical_json_sha256(
                normalized_configuration
            ),
        }
    else:
        source_config = {
            "applicable": False,
            "reason": "build lifecycle has no experiment YAML",
        }

    manifest = {
        "manifest_version": "1.0.0",
        "run_id": "harness-build",
        "stage": "build",
        "implementation": {
            "implementation_id": "diskann_harness_build",
            "implementation_version": "1.0.0",
        },
        "source": source_identity,
        "toolchain": {
            "os": platform.platform(),
            "compiler_path": compiler_path,
            "compiler_version": compiler_version,
            "cmake_version": cmake_version,
            "generator": cache_entries.get("CMAKE_GENERATOR", "unknown"),
            "build_type": cache_entries.get("CMAKE_BUILD_TYPE", build_type),
            "cmake_definitions": effective_definitions,
            "compile_commands": [shlex.join(configure), shlex.join(compile_command)],
            "link_commands": [],
        },
        "dataset": {
            "dataset_id": "fake-six-node-v1",
            "dataset_checksum": "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b",
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
        "parameters": {
            "immutable_build_configuration": {
                "source_dir": str(source),
                "build_dir": str(build),
                "build_type": build_type,
                "cmake_cache_sha256": _sha256(cache),
            },
            "source_config": source_config,
        },
        "resource_budget": {"memory_budget_bytes": 1, "thread_budget": 1},
        "input_artifacts": [],
        "output_artifacts": [],
        "status": "SUCCEEDED",
        "started_at": configure_started_at,
        "ended_at": build_ended_at,
    }
    _schema_validator("build-manifest.schema.json", repository).validate(manifest)
    target = build / "build-manifest.json"
    try:
        write_immutable_json(target, manifest)
    except ImmutableWriteError:
        existing = json.loads(target.read_text(encoding="utf-8"))
        if existing != manifest:
            raise
    return target
