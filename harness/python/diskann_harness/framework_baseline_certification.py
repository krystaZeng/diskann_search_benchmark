"""Bounded Feature 006 baseline-certification orchestration.

This module owns only the six operations specified by Feature 006.  It is not
a public validation, graph, search, or certification framework.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shlex
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator
from referencing import Registry, Resource

from . import cpp_main_structural_parity as structural_parity
from . import diskann_search_evaluation as search_evaluation
from .checksum import aggregate_checksum
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

_REFERENCE_CMAKE_DEFINITIONS = (
    "CMAKE_BUILD_TYPE=Release",
    "BUILD_TESTING=ON",
    "DISKANN_CPP_MAIN_BASELINE=ON",
    "DISKANN_SEARCH_EVALUATION=ON",
    "CMAKE_EXPORT_COMPILE_COMMANDS=ON",
)

_REFERENCE_BUILD_TARGETS = (
    {
        "result_key": "project_capture_owner",
        "target": "native_reference_capture_runner",
        "source_role": "project_capture_owner",
        "implementation_id": "framework_baseline_native_reference_capture_v1",
        "binary": "src/baseline/cpp_main/native_reference_capture_runner",
        "compile_source": "src/baseline/cpp_main/native_reference_capture_runner.cpp",
        "link_evidence": (
            "baseline/cpp_main/CMakeFiles/"
            "native_reference_capture_runner.dir/link.txt"
        ),
    },
    {
        "result_key": "fixed_upstream_build_disk_index",
        "target": "build_disk_index",
        "source_role": "fixed_upstream_build_disk_index",
        "implementation_id": "cpp_main_build_disk_index_native_v1",
        "binary": "cpp-main-upstream/apps/build_disk_index",
        "compile_source": "upstream/diskann-cpp-main/apps/build_disk_index.cpp",
        "link_evidence": (
            "cpp-main-upstream/apps/CMakeFiles/build_disk_index.dir/link.txt"
        ),
    },
)


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


def _dataset_artifact_validator(
    repository_root: Path, format_version: Any
) -> Draft202012Validator:
    if format_version == "fake-dataset-json/v1":
        return _runtime_validator(repository_root, "dataset-artifact.schema.json")
    if format_version == "diskann-float32-dataset/v1":
        schema = _read_object(
            repository_root
            / "contracts/baseline-v1/schemas/cpp-main-dataset-artifact.schema.json"
        )
        Draft202012Validator.check_schema(schema)
        return Draft202012Validator(schema)
    raise FeatureCertificationError(
        f"unsupported Dataset format version: {format_version!r}"
    )


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


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _source_identity(
    repository: Path, *, include_untracked: bool = False
) -> dict[str, Any]:
    status = _run_git(
        repository,
        "status",
        "--porcelain=v1",
        "--untracked-files=all" if include_untracked else "--untracked-files=no",
    )
    return {
        "repository": _run_git(repository, "remote", "get-url", "origin"),
        "branch": _run_git(repository, "branch", "--show-current"),
        "commit": _run_git(repository, "rev-parse", "HEAD"),
        "dirty_worktree": bool(status),
    }


def _read_cmake_cache(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
    except (OSError, UnicodeError) as error:
        raise FeatureCertificationError(f"cannot read CMake cache {path}: {error}") from error
    values: dict[str, str] = {}
    for line in lines:
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_type, value = line.split("=", 1)
        if ":" not in key_type:
            continue
        key, _ = key_type.split(":", 1)
        values[key] = value
    return values


def _file_identity(path: Path) -> dict[str, Any]:
    resolved = path.resolve()
    if not resolved.is_file():
        raise FeatureCertificationError(f"build evidence is missing: {resolved}")
    return {
        "path": str(resolved),
        "byte_length": resolved.stat().st_size,
        "sha256": _sha256_file(resolved),
    }


def _target_compile_commands(
    compile_commands_path: Path, source_path: Path
) -> list[str]:
    try:
        entries = json.loads(compile_commands_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeError) as error:
        raise FeatureCertificationError(
            f"cannot read compile command evidence: {compile_commands_path}"
        ) from error
    if type(entries) is not list:
        raise FeatureCertificationError("compile_commands.json must contain an array")
    expected = source_path.resolve()
    commands: list[str] = []
    for entry in entries:
        if type(entry) is not dict or type(entry.get("file")) is not str:
            continue
        candidate = Path(entry["file"])
        if not candidate.is_absolute():
            directory = Path(str(entry.get("directory", compile_commands_path.parent)))
            candidate = directory / candidate
        if candidate.resolve() != expected:
            continue
        command = entry.get("command")
        if type(command) is str and command.strip():
            commands.append(command.strip())
            continue
        arguments = entry.get("arguments")
        if type(arguments) is list and arguments and all(type(item) is str for item in arguments):
            commands.append(shlex.join(arguments))
    if not commands:
        raise FeatureCertificationError(
            f"target compile command evidence is missing: {source_path}"
        )
    return commands


def _read_command_evidence(path: Path) -> str:
    try:
        command = path.read_text(encoding="utf-8", errors="strict").strip()
    except (OSError, UnicodeError) as error:
        raise FeatureCertificationError(f"cannot read link command evidence: {path}") from error
    if not command:
        raise FeatureCertificationError(f"link command evidence is empty: {path}")
    return command


def _command_version(command: list[str], label: str) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            shell=False,
        )
    except OSError as error:
        raise FeatureCertificationError(f"cannot inspect {label} version") from error
    first_line = completed.stdout.splitlines()[0] if completed.stdout.splitlines() else ""
    if completed.returncode != 0 or not first_line:
        raise FeatureCertificationError(f"cannot inspect {label} version")
    return first_line


def _applicable_build_flags(commands: list[str]) -> list[str]:
    admitted = (
        "-O3",
        "-DNDEBUG",
        "-std=c++17",
        "-std=gnu++17",
        "-mavx",
        "-mavx2",
        "-mfma",
        "-msse2",
        "-fopenmp",
        "-fopenmp-simd",
    )
    tokens = set()
    for command in commands:
        try:
            tokens.update(shlex.split(command))
        except ValueError as error:
            raise FeatureCertificationError("build command evidence cannot be parsed") from error
    return [flag for flag in admitted if flag in tokens]


def _publish_build_manifest(
    target: Path,
    manifest: dict[str, Any],
    repository_root: Path,
) -> dict[str, Any]:
    _validate_with(
        _runtime_validator(repository_root, "build-manifest.schema.json"),
        manifest,
        "target BuildManifest",
    )
    try:
        write_immutable_json(target, manifest)
    except ImmutableWriteError as error:
        raise FeatureCertificationError(str(error)) from error
    reread = _read_object(target)
    if reread != manifest:
        raise FeatureCertificationError("target BuildManifest changed after publication")
    _validate_with(
        _runtime_validator(repository_root, "build-manifest.schema.json"),
        reread,
        "published target BuildManifest",
    )
    return {
        "path": str(target),
        "byte_length": target.stat().st_size,
        "sha256": _sha256_file(target),
        "status": manifest["status"],
    }


def _build_reference_binary_closure(
    *,
    repository_root: str | Path,
    build_dir: str | Path,
    manifest_workspace: str | Path,
    owner_manifest_path: str | Path,
    native_manifest_path: str | Path,
    dataset: dict[str, Any],
    nlohmann_source: str | Path,
    memory_budget_bytes: int,
    parallel: int = 6,
) -> dict[str, Any]:
    """Privately build and evidence the two fixed Feature 006 binaries.

    This helper is intentionally not exposed through the six-operation CLI.  It owns one new
    configure and two individually timed target builds; a target Manifest is published only after
    that target reaches a terminal status.
    """

    repository = Path(repository_root).resolve()
    upstream = repository / "upstream" / "diskann-cpp-main"
    build = Path(build_dir).resolve()
    workspace = Path(manifest_workspace).resolve()
    nlohmann = Path(nlohmann_source).resolve()
    if build.exists():
        raise FeatureCertificationError(f"clean build directory already exists: {build}")
    if not repository.is_dir() or not upstream.is_dir() or not nlohmann.is_dir():
        raise FeatureCertificationError("reference build-closure input path is missing")
    if (
        type(parallel) is not int
        or parallel < 1
        or type(memory_budget_bytes) is not int
        or memory_budget_bytes < 1
    ):
        raise FeatureCertificationError("reference build-closure resource budget is invalid")

    manifest_paths = {
        "project_capture_owner": resolve_feature_output(workspace, owner_manifest_path),
        "fixed_upstream_build_disk_index": resolve_feature_output(
            workspace, native_manifest_path
        ),
    }
    if len(set(manifest_paths.values())) != 2 or any(
        path.exists() for path in manifest_paths.values()
    ):
        raise FeatureCertificationError("target BuildManifest path already exists or conflicts")

    project_source = _source_identity(repository, include_untracked=False)
    upstream_source = _source_identity(upstream, include_untracked=True)
    if project_source["dirty_worktree"] is not False:
        raise FeatureCertificationError("Project source is not tracked-clean")
    if upstream_source != {**PINNED_DISKANN, "dirty_worktree": False}:
        raise FeatureCertificationError("fixed DiskANN source identity is invalid")

    definitions = [
        *_REFERENCE_CMAKE_DEFINITIONS,
        f"FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON={nlohmann}",
    ]
    configure_command = [
        "cmake",
        "-S",
        str(repository),
        "-B",
        str(build),
        "-G",
        "Unix Makefiles",
        *(f"-D{definition}" for definition in definitions),
    ]
    configure_started_at = _utc_now()
    try:
        configured = subprocess.run(
            configure_command,
            check=False,
            capture_output=True,
            text=True,
            shell=False,
        )
    except OSError as error:
        raise FeatureCertificationError("reference clean configure could not launch") from error
    configure_ended_at = _utc_now()
    if configured.returncode != 0:
        reason = configured.stderr.strip() or configured.stdout.strip() or "configure failed"
        raise FeatureCertificationError(f"reference clean configure failed: {reason}")

    cache_path = build / "CMakeCache.txt"
    compile_commands_path = build / "compile_commands.json"
    cache = _read_cmake_cache(cache_path)
    if (
        cache.get("CMAKE_BUILD_TYPE") != "Release"
        or cache.get("CMAKE_GENERATOR") != "Unix Makefiles"
    ):
        raise FeatureCertificationError("configured build identity is not Release/Unix Makefiles")
    compiler_path = cache.get("CMAKE_CXX_COMPILER")
    cmake_path = cache.get("CMAKE_COMMAND", "cmake")
    if not compiler_path:
        raise FeatureCertificationError("configured compiler path is missing")
    compiler_version = _command_version([compiler_path, "--version"], "compiler")
    cmake_version = _command_version([cmake_path, "--version"], "CMake")
    common_evidence = {
        "cmake_cache": _file_identity(cache_path),
        "compile_commands": _file_identity(compile_commands_path),
    }

    sources = {
        "project_capture_owner": project_source,
        "fixed_upstream_build_disk_index": upstream_source,
    }
    results: dict[str, Any] = {}
    failures: list[str] = []
    for target_spec in _REFERENCE_BUILD_TARGETS:
        result_key = target_spec["result_key"]
        target = target_spec["target"]
        build_command = [
            "cmake",
            "--build",
            str(build),
            "--target",
            target,
            "--parallel",
            str(parallel),
        ]
        started_at = _utc_now()
        try:
            completed = subprocess.run(
                build_command,
                check=False,
                capture_output=True,
                text=True,
                shell=False,
            )
        except OSError as error:
            completed = subprocess.CompletedProcess(
                build_command, 127, "", f"target build could not launch: {error}"
            )
        ended_at = _utc_now()

        binary_path = build / target_spec["binary"]
        link_path = build / target_spec["link_evidence"]
        compile_source = repository / target_spec["compile_source"]
        evidence_error: FeatureCertificationError | None = None
        compile_commands: list[str] = []
        link_command = ""
        try:
            compile_commands = _target_compile_commands(
                compile_commands_path, compile_source
            )
            link_command = _read_command_evidence(link_path)
            target_evidence = {
                **common_evidence,
                "link_command": _file_identity(link_path),
            }
        except FeatureCertificationError as error:
            evidence_error = error
            target_evidence = dict(common_evidence)

        binary_identity: dict[str, Any] = {"path": str(binary_path.resolve())}
        if binary_path.is_file():
            binary_identity.update(
                {
                    "byte_length": binary_path.stat().st_size,
                    "sha256": _sha256_file(binary_path),
                }
            )
        else:
            binary_identity["present"] = False
        success = (
            completed.returncode == 0
            and binary_path.is_file()
            and evidence_error is None
        )
        status = "SUCCEEDED" if success else "FAILED"
        toolchain_commands = [*compile_commands, *([link_command] if link_command else [])]
        manifest: dict[str, Any] = {
            "manifest_version": "1.0.0",
            "run_id": f"feature-006-{target}-build",
            "stage": "build",
            "implementation": {
                "implementation_id": target_spec["implementation_id"],
                "implementation_version": "1.0.0",
            },
            "source": sources[result_key],
            "toolchain": {
                "os": f"{platform.system()}-{platform.machine()}",
                "compiler_path": compiler_path,
                "compiler_version": compiler_version,
                "cmake_version": cmake_version,
                "generator": cache["CMAKE_GENERATOR"],
                "build_type": cache["CMAKE_BUILD_TYPE"],
                "cmake_definitions": definitions,
                "compile_commands": compile_commands,
                "link_commands": [link_command] if link_command else [],
            },
            "dataset": dataset,
            "parameters": {
                "build_closure": {
                    "source_role": target_spec["source_role"],
                    "target": target,
                    "configure_command": shlex.join(configure_command),
                    "build_command": shlex.join(build_command),
                    "configure_started_at": configure_started_at,
                    "configure_ended_at": configure_ended_at,
                    "binary": binary_identity,
                    "evidence": target_evidence,
                    "applicable_flags": _applicable_build_flags(toolchain_commands),
                }
            },
            "resource_budget": {
                "memory_budget_bytes": memory_budget_bytes,
                "thread_budget": parallel,
            },
            "input_artifacts": [],
            "output_artifacts": [],
            "status": status,
            "started_at": started_at,
            "ended_at": ended_at,
        }
        if not success:
            if completed.returncode != 0:
                category = "target_build_failed"
                reason = completed.stderr.strip() or completed.stdout.strip() or "target build failed"
            elif not binary_path.is_file():
                category = "missing_build_output"
                reason = f"target did not publish binary: {binary_path}"
            else:
                category = "incomplete_build_evidence"
                reason = str(evidence_error)
            manifest["failure"] = {"category": category, "reason": reason}
            if completed.returncode != 0:
                manifest["failure"]["exit_code"] = completed.returncode
            failures.append(f"{target}: {reason}")

        results[result_key] = _publish_build_manifest(
            manifest_paths[result_key], manifest, repository
        )

    if failures:
        raise FeatureCertificationError(
            "target build closure failed: " + "; ".join(failures)
        )
    return results


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
        _run_git(repository_root, "remote", "get-url", "origin")
        != owner["repository"]
        or
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


def _verify_file_identity(identity: dict[str, Any], label: str) -> Path:
    path = Path(identity["path"])
    if (
        not path.is_absolute()
        or not path.is_file()
        or path.stat().st_size != identity["byte_length"]
        or _sha256_file(path) != identity["sha256"]
    ):
        raise FeatureCertificationError(f"{label} identity mismatch: {path}")
    return path


def _parse_terminal_time(value: Any, label: str) -> datetime:
    if type(value) is not str:
        raise FeatureCertificationError(f"{label} timestamp is missing")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise FeatureCertificationError(f"{label} timestamp is invalid") from error
    if parsed.tzinfo is None:
        raise FeatureCertificationError(f"{label} timestamp lacks a timezone")
    return parsed


def _verify_target_build_manifest(
    workspace: Path,
    identity: dict[str, Any],
    *,
    repository_root: Path,
    expected_source: dict[str, Any],
    expected_source_role: str,
    expected_target: str,
    expected_dataset: dict[str, Any],
) -> Path:
    binary_path = _verify_file_identity(identity, "binary")
    if not os.access(binary_path, os.X_OK):
        raise FeatureCertificationError(f"{expected_target} binary is not executable")
    target_spec = next(
        (
            item
            for item in _REFERENCE_BUILD_TARGETS
            if item["target"] == expected_target
        ),
        None,
    )
    if target_spec is None:  # pragma: no cover - fixed private target table
        raise FeatureCertificationError("unsupported reference build target")
    binary_suffix = Path(target_spec["binary"])
    build_root = binary_path
    for _ in binary_suffix.parts:
        build_root = build_root.parent
    if (build_root / binary_suffix).resolve() != binary_path:
        raise FeatureCertificationError(f"{expected_target} binary build path mismatch")
    manifest_path = _verify_document_reference(workspace, identity["build_manifest"])
    manifest = _read_object(manifest_path)
    _validate_with(
        _runtime_validator(repository_root, "build-manifest.schema.json"),
        manifest,
        f"{expected_target} BuildManifest",
    )
    if manifest["status"] != "SUCCEEDED" or manifest["stage"] != "build":
        raise FeatureCertificationError(
            f"{expected_target} BuildManifest is not terminal SUCCEEDED build evidence"
        )
    if manifest["implementation"]["implementation_id"] != identity["implementation_id"]:
        raise FeatureCertificationError(
            f"{expected_target} implementation identity mismatch"
        )
    if manifest["source"] != expected_source:
        raise FeatureCertificationError(f"{expected_target} source identity mismatch")
    if manifest["dataset"] != expected_dataset:
        raise FeatureCertificationError(f"{expected_target} dataset identity mismatch")

    started_at = _parse_terminal_time(manifest["started_at"], expected_target)
    ended_at = _parse_terminal_time(manifest["ended_at"], expected_target)
    if ended_at < started_at:
        raise FeatureCertificationError(f"{expected_target} terminal timestamps are reversed")

    toolchain = manifest["toolchain"]
    if (
        toolchain["build_type"] != "Release"
        or toolchain["generator"] != "Unix Makefiles"
        or not toolchain["compiler_path"]
        or not toolchain["compiler_version"]
        or not toolchain["cmake_version"]
        or not toolchain["compile_commands"]
        or not toolchain["link_commands"]
        or not set(_REFERENCE_CMAKE_DEFINITIONS).issubset(
            set(toolchain["cmake_definitions"])
        )
    ):
        raise FeatureCertificationError(f"{expected_target} toolchain identity mismatch")

    closure = manifest["parameters"].get("build_closure")
    if type(closure) is not dict:
        raise FeatureCertificationError(f"{expected_target} build evidence is missing")
    expected_binary = {
        "path": str(binary_path),
        "byte_length": identity["byte_length"],
        "sha256": identity["sha256"],
    }
    if (
        closure.get("source_role") != expected_source_role
        or closure.get("target") != expected_target
        or closure.get("binary") != expected_binary
        or type(closure.get("configure_command")) is not str
        or not closure["configure_command"].strip()
        or type(closure.get("build_command")) is not str
        or expected_target not in closure["build_command"]
    ):
        raise FeatureCertificationError(f"{expected_target} build binding mismatch")
    configure_started_at = _parse_terminal_time(
        closure.get("configure_started_at"), f"{expected_target} configure start"
    )
    configure_ended_at = _parse_terminal_time(
        closure.get("configure_ended_at"), f"{expected_target} configure end"
    )
    if configure_ended_at < configure_started_at or started_at < configure_ended_at:
        raise FeatureCertificationError(
            f"{expected_target} configure/build timestamps are inconsistent"
        )

    evidence = closure.get("evidence")
    if type(evidence) is not dict or set(evidence) != {
        "cmake_cache",
        "compile_commands",
        "link_command",
    }:
        raise FeatureCertificationError(f"{expected_target} build evidence is incomplete")
    expected_evidence_paths = {
        "cmake_cache": (build_root / "CMakeCache.txt").resolve(),
        "compile_commands": (build_root / "compile_commands.json").resolve(),
        "link_command": (build_root / target_spec["link_evidence"]).resolve(),
    }
    for label, reference in evidence.items():
        if type(reference) is not dict:
            raise FeatureCertificationError(
                f"{expected_target} {label} evidence is malformed"
            )
        evidence_path = _verify_file_identity(
            reference, f"{expected_target} {label} evidence"
        )
        if evidence_path != expected_evidence_paths[label]:
            raise FeatureCertificationError(
                f"{expected_target} {label} build-tree binding mismatch"
            )

    cache = _read_cmake_cache(expected_evidence_paths["cmake_cache"])
    cache_cmake_version = ".".join(
        cache.get(key, "")
        for key in (
            "CMAKE_CACHE_MAJOR_VERSION",
            "CMAKE_CACHE_MINOR_VERSION",
            "CMAKE_CACHE_PATCH_VERSION",
        )
    )
    definition_values = dict(
        definition.split("=", 1)
        for definition in _REFERENCE_CMAKE_DEFINITIONS
    )
    if (
        cache.get("CMAKE_CXX_COMPILER") != toolchain["compiler_path"]
        or cache.get("CMAKE_BUILD_TYPE") != toolchain["build_type"]
        or cache.get("CMAKE_GENERATOR") != toolchain["generator"]
        or not cache_cmake_version.strip(".")
        or not toolchain["cmake_version"].endswith(cache_cmake_version)
        or any(cache.get(key) != value for key, value in definition_values.items())
        or str(repository_root) not in closure["configure_command"]
        or str(build_root) not in closure["configure_command"]
        or str(build_root) not in closure["build_command"]
    ):
        raise FeatureCertificationError(f"{expected_target} CMake evidence mismatch")

    command_evidence = [*toolchain["compile_commands"], *toolchain["link_commands"]]
    actual_flags = _applicable_build_flags(command_evidence)
    recorded_flags = closure.get("applicable_flags")
    if (
        type(recorded_flags) is not list
        or recorded_flags != actual_flags
        or "-O3" not in actual_flags
        or "-DNDEBUG" not in actual_flags
        or not {"-std=c++17", "-std=gnu++17"}.intersection(actual_flags)
    ):
        raise FeatureCertificationError(f"{expected_target} build flags mismatch")

    compile_evidence_path = expected_evidence_paths["compile_commands"]
    try:
        compile_evidence = json.loads(compile_evidence_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeError) as error:
        raise FeatureCertificationError(
            f"cannot read compile command evidence: {compile_evidence_path}"
        ) from error
    if type(compile_evidence) is not list:
        raise FeatureCertificationError(
            f"{expected_target} compile command evidence is malformed"
        )
    target_compile_commands = _target_compile_commands(
        compile_evidence_path,
        repository_root / target_spec["compile_source"],
    )
    if toolchain["compile_commands"] != target_compile_commands:
        raise FeatureCertificationError(
            f"{expected_target} target compile command mismatch"
        )
    recorded_compile_commands = {
        entry.get("command")
        if type(entry) is dict and type(entry.get("command")) is str
        else shlex.join(entry.get("arguments", []))
        if type(entry) is dict
        and type(entry.get("arguments")) is list
        and all(type(item) is str for item in entry["arguments"])
        else None
        for entry in compile_evidence
    }
    if not set(toolchain["compile_commands"]).issubset(recorded_compile_commands):
        raise FeatureCertificationError(
            f"{expected_target} compile command binding mismatch"
        )
    recorded_link_command = _read_command_evidence(
        expected_evidence_paths["link_command"]
    )
    if toolchain["link_commands"] != [recorded_link_command]:
        raise FeatureCertificationError(f"{expected_target} link command binding mismatch")
    return binary_path


def _verify_reference_binary_manifests(
    request: dict[str, Any],
    repository_root: Path,
    dataset: dict[str, Any],
) -> Path:
    workspace = Path(request["workspace_root"]).resolve()
    owner = request["project_operation_owner"]
    owner_source = {
        key: owner[key]
        for key in ("repository", "branch", "commit", "dirty_worktree")
    }
    native_source = {
        key: request["source"][key]
        for key in ("repository", "branch", "commit", "dirty_worktree")
    }
    runner = _verify_target_build_manifest(
        workspace,
        owner["binary"],
        repository_root=repository_root,
        expected_source=owner_source,
        expected_source_role="project_capture_owner",
        expected_target="native_reference_capture_runner",
        expected_dataset=dataset,
    )
    _verify_target_build_manifest(
        workspace,
        request["native_binary"],
        repository_root=repository_root,
        expected_source=native_source,
        expected_source_role="fixed_upstream_build_disk_index",
        expected_target="build_disk_index",
        expected_dataset=dataset,
    )
    return runner


def _fresh_validate_dataset(
    request: dict[str, Any], repository_root: Path
) -> dict[str, Any]:
    workspace = Path(request["workspace_root"]).resolve()
    reference = request["dataset_artifact"]
    metadata_path = resolve_feature_output(workspace / "normal", reference["metadata_path"])
    metadata = _read_object(metadata_path)
    _validate_with(
        _dataset_artifact_validator(repository_root, metadata.get("format_version")),
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
    payloads = metadata["payloads"]
    if len(payloads) != 1 or payloads[0].get("role") != "vectors":
        raise FeatureCertificationError(
            "DatasetArtifact must contain exactly one vectors payload"
        )
    for record in payloads:
        payload = resolve_feature_output(metadata_path.parent, record["path"])
        if (
            not payload.is_file()
            or payload.stat().st_size != record["byte_length"]
            or _sha256_file(payload) != record["sha256"]
        ):
            raise FeatureCertificationError("DatasetArtifact payload identity mismatch")
    if payloads[0]["sha256"] != metadata["dataset"]["dataset_checksum"]:
        raise FeatureCertificationError(
            "Dataset checksum and vectors payload checksum mismatch"
        )
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
    return metadata["dataset"]


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
    dataset = _fresh_validate_dataset(request, repository)
    _verify_reference_binary_manifests(request, repository, dataset)
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


def _aggregate_artifact_metadata(metadata: dict[str, Any]) -> str:
    return aggregate_checksum(
        {
            "schema_version": metadata["schema_version"],
            "canonicalization": metadata["canonicalization"],
            "artifact_type": metadata["artifact_type"],
            "format_version": metadata["format_version"],
            "dataset_checksum": metadata["dataset"]["dataset_checksum"],
            "id_space": metadata["id_space"],
            "capabilities": sorted(metadata["capabilities"]),
            "payloads": [
                {
                    "role": item["role"],
                    "byte_length": item["byte_length"],
                    "sha256": item["sha256"],
                }
                for item in metadata["payloads"]
            ],
            "source_artifacts": metadata["source_artifacts"],
            "structural": metadata["structural"],
        }
    )


def _relative_file_identity(
    workspace: Path, path: Path, *, stream_role: str | None = None
) -> dict[str, Any]:
    try:
        relative = path.resolve().relative_to(workspace)
    except ValueError as error:
        raise FeatureCertificationError("native closure file escapes workspace") from error
    identity: dict[str, Any] = {
        "path": relative.as_posix(),
        "byte_length": path.stat().st_size,
        "sha256": _sha256_file(path),
    }
    if stream_role is not None:
        identity["stream_role"] = stream_role
    return identity


def _require_manifest_file_identity(
    workspace: Path,
    reference: Any,
    expected_relative: str,
    label: str,
    *,
    stream_role: str | None = None,
) -> Path:
    if type(reference) is not dict:
        raise FeatureCertificationError(f"{label} identity is missing")
    path = resolve_feature_output(workspace, expected_relative)
    if reference != _relative_file_identity(
        workspace, path, stream_role=stream_role
    ):
        raise FeatureCertificationError(f"{label} identity mismatch")
    return path


def _expected_upstream_source(request: dict[str, Any]) -> dict[str, Any]:
    return {
        key: request["source"][key]
        for key in ("repository", "branch", "commit", "dirty_worktree")
    }


def _verify_manifest_common(
    manifest: dict[str, Any],
    request: dict[str, Any],
    request_path: Path,
    dataset: dict[str, Any],
    repository: Path,
    *,
    require_consistent_admission: bool,
) -> None:
    _validate_with(
        _runtime_validator(repository, "build-manifest.schema.json"),
        manifest,
        "native Reference terminal BuildManifest",
    )
    if (
        manifest["run_id"] != request["run_id"]
        or manifest["stage"] != "build"
        or manifest["implementation"]
        != {
            "implementation_id": "framework_baseline_native_reference_capture_v1",
            "implementation_version": "1.0.0",
        }
        or manifest["source"] != _expected_upstream_source(request)
        or manifest["dataset"] != dataset
        or manifest["input_artifacts"] != [request["dataset_artifact"]]
        or manifest["resource_budget"]
        != {
            "memory_budget_bytes": request["resource_budget"]["memory_budget_bytes"],
            "thread_budget": request["resource_budget"]["thread_budget"],
        }
    ):
        raise FeatureCertificationError("native Reference Manifest binding mismatch")
    native_manifest_path = _verify_document_reference(
        Path(request["workspace_root"]), request["native_binary"]["build_manifest"]
    )
    native_manifest = _read_object(native_manifest_path)
    if manifest["toolchain"] != native_manifest["toolchain"]:
        raise FeatureCertificationError("native Reference Manifest toolchain mismatch")
    started = _parse_terminal_time(manifest["started_at"], "native Reference start")
    ended = _parse_terminal_time(manifest["ended_at"], "native Reference end")
    if ended < started:
        raise FeatureCertificationError("native Reference terminal timestamps are reversed")
    parameters = manifest["parameters"]
    request_binding = parameters.get("request")
    if request_binding != {
        "path": str(request_path.resolve()),
        "byte_length": request_path.stat().st_size,
        "sha256": _sha256_file(request_path),
    }:
        raise FeatureCertificationError("immutable native Reference request binding mismatch")
    if (
        parameters.get("project_operation_owner")
        != request["project_operation_owner"]
        or parameters.get("native_binary") != request["native_binary"]
        or parameters.get("profile") != request["profile"]
        or parameters.get("controls") != request["controls"]
        or parameters.get("capture") != request["capture"]
    ):
        raise FeatureCertificationError("native Reference execution parameter mismatch")
    admission = parameters.get("upstream_admission")
    if require_consistent_admission:
        if (
            type(admission) is not dict
            or admission.get("consistent") is not True
            or admission.get("pre") != admission.get("post")
            or type(admission.get("pre")) is not dict
            or admission["pre"].get("source") != _expected_upstream_source(request)
            or admission["pre"].get("gitlink_commit") != request["source"]["commit"]
        ):
            raise FeatureCertificationError("pre/post upstream admission binding mismatch")
    elif admission is not None:
        if type(admission) is not dict:
            raise FeatureCertificationError("failure upstream admission is malformed")
        for phase in ("pre", "post"):
            evidence = admission.get(phase)
            if evidence is not None and (
                type(evidence) is not dict
                or evidence.get("source") != _expected_upstream_source(request)
                or evidence.get("gitlink_commit") != request["source"]["commit"]
            ):
                raise FeatureCertificationError(
                    f"failure {phase}-run upstream admission mismatch"
                )
        if admission.get("consistent") is True and admission.get("pre") != admission.get("post"):
            raise FeatureCertificationError("failure admission consistency is false")


def _verify_native_observer(
    observer: dict[str, Any], request: dict[str, Any], *, succeeded: bool
) -> None:
    required = {
        "run_id",
        "observer_mechanism",
        "observer_armed",
        "observer_ready_at",
        "child_launched_at",
        "native_child_argv",
        "source_graph_name",
        "captured_graph_path",
        "child_exit_code",
        "exact_graph_event_observed",
        "graph_hard_link_created",
        "capture_started_at",
        "capture_ended_at",
        "terminal_result",
    }
    if not required.issubset(observer):
        raise FeatureCertificationError("native observer evidence is incomplete")
    for label in (
        "observer_ready_at",
        "child_launched_at",
        "capture_started_at",
        "capture_ended_at",
    ):
        _parse_terminal_time(observer[label], f"observer {label}")
    if (
        observer["run_id"] != request["run_id"]
        or observer["observer_mechanism"] != request["capture"]["mechanism"]
        or observer["observer_armed"] is not True
        or observer["source_graph_name"] != request["capture"]["source_graph_name"]
        or observer["captured_graph_path"]
        != request["capture"]["captured_graph_path"]
        or type(observer["native_child_argv"]) is not list
        or not observer["native_child_argv"]
    ):
        raise FeatureCertificationError("native observer evidence binding mismatch")
    if succeeded and (
        observer["terminal_result"] != "SUCCEEDED"
        or observer["child_exit_code"] != 0
        or observer["exact_graph_event_observed"] is not True
        or observer["graph_hard_link_created"] is not True
    ):
        raise FeatureCertificationError("native observer success evidence mismatch")


def _verify_cpp_success_closure(
    request: dict[str, Any],
    request_path: Path,
    dataset: dict[str, Any],
    repository: Path,
) -> dict[str, Any]:
    workspace = Path(request["workspace_root"]).resolve()
    outputs = request["outputs"]
    manifest_path = resolve_feature_output(workspace, outputs["build_manifest_path"])
    manifest = _read_object(manifest_path)
    _verify_manifest_common(
        manifest,
        request,
        request_path,
        dataset,
        repository,
        require_consistent_admission=True,
    )
    if manifest["status"] != "SUCCEEDED" or "failure" in manifest:
        raise FeatureCertificationError("native Reference Manifest is not SUCCEEDED")

    parameters = manifest["parameters"]
    logs = parameters.get("native_child_logs")
    if type(logs) is not dict or set(logs) != {"stdout", "stderr"}:
        raise FeatureCertificationError("native child log evidence is incomplete")
    stdout_path = _require_manifest_file_identity(
        workspace,
        logs["stdout"],
        outputs["stdout_path"],
        "native child stdout",
        stream_role="stdout",
    )
    stderr_path = _require_manifest_file_identity(
        workspace,
        logs["stderr"],
        outputs["stderr_path"],
        "native child stderr",
        stream_role="stderr",
    )
    observer_path = _require_manifest_file_identity(
        workspace,
        parameters.get("observer_evidence"),
        outputs["observer_evidence_path"],
        "native observer evidence",
    )
    observer = _read_object(observer_path)
    _verify_native_observer(observer, request, succeeded=True)
    if parameters.get("native_child_argv") != observer["native_child_argv"]:
        raise FeatureCertificationError("native child argv evidence mismatch")

    artifact_path = resolve_feature_output(
        workspace, outputs["artifact_metadata_path"]
    )
    artifact = _read_object(artifact_path)
    _validate_with(
        _runtime_validator(repository, "global-graph-artifact-v2.schema.json"),
        artifact,
        "native Reference GlobalGraphArtifact/v2",
    )
    expected_source = dict(request["dataset_artifact"])
    expected_source.pop("metadata_path")
    if (
        artifact["dataset"] != dataset
        or artifact["source_artifacts"] != [expected_source]
        or artifact["structural"]["lineage_mode"] != "direct_native_final"
        or artifact["aggregate_checksum"] != _aggregate_artifact_metadata(artifact)
    ):
        raise FeatureCertificationError("native Reference Artifact identity mismatch")
    payloads = {item["role"]: item for item in artifact["payloads"]}
    for role, capture_key in (
        ("global_graph", "captured_graph_path"),
        ("medoids", "captured_medoids_path"),
    ):
        payload = payloads[role]
        payload_path = resolve_feature_output(artifact_path.parent, payload["path"])
        expected_path = resolve_feature_output(workspace, request["capture"][capture_key])
        if (
            payload_path != expected_path
            or not payload_path.is_file()
            or payload_path.stat().st_size != payload["byte_length"]
            or _sha256_file(payload_path) != payload["sha256"]
        ):
            raise FeatureCertificationError(f"native Reference {role} payload mismatch")
    artifact_ref = {
        "metadata_path": artifact_path.relative_to(workspace / "normal").as_posix(),
        "artifact_type": "global_graph",
        "format_version": "global-graph-artifact/v2",
        "aggregate_checksum": artifact["aggregate_checksum"],
    }
    if manifest["output_artifacts"] != [artifact_ref]:
        raise FeatureCertificationError("native Reference Manifest output mismatch")

    evidence_path = resolve_feature_output(
        workspace, outputs["validation_evidence_path"]
    )
    evidence = _read_object(evidence_path)
    _validate_with(
        _runtime_validator(repository, "validation-evidence.schema.json"),
        evidence,
        "native Reference ValidationEvidence",
    )
    candidate = evidence["artifact_candidate"]
    if (
        evidence["result"] != "VALID"
        or candidate
        != {
            "artifact_type": artifact_ref["artifact_type"],
            "format_version": artifact_ref["format_version"],
            "metadata_path": artifact_ref["metadata_path"],
            "claimed_aggregate_checksum": artifact_ref["aggregate_checksum"],
        }
        or manifest.get("validation_evidence_path")
        != outputs["validation_evidence_path"]
    ):
        raise FeatureCertificationError("native Reference ValidationEvidence mismatch")
    return {
        "manifest": manifest,
        "artifact": artifact,
        "artifact_ref": artifact_ref,
        "validation_evidence": evidence,
        "observer": observer,
        "stdout_path": stdout_path,
        "stderr_path": stderr_path,
        "observer_path": observer_path,
        "manifest_path": manifest_path,
        "artifact_path": artifact_path,
        "evidence_path": evidence_path,
    }


def _verify_cpp_failure_closure(
    request: dict[str, Any],
    request_path: Path,
    dataset: dict[str, Any],
    repository: Path,
) -> dict[str, Any]:
    workspace = Path(request["workspace_root"]).resolve()
    outputs = request["outputs"]
    manifest = _read_object(
        resolve_feature_output(workspace, outputs["build_manifest_path"])
    )
    _verify_manifest_common(
        manifest,
        request,
        request_path,
        dataset,
        repository,
        require_consistent_admission=False,
    )
    if (
        manifest["status"] not in {"FAILED", "CANCELLED", "INVALID_OUTPUT"}
        or type(manifest.get("failure")) is not dict
        or not manifest["failure"].get("reason")
        or manifest["output_artifacts"] != []
    ):
        raise FeatureCertificationError("native Reference failure Manifest is invalid")
    for key in (
        "artifact_metadata_path",
        "validation_evidence_path",
        "structural_projection_path",
    ):
        if resolve_feature_output(workspace, outputs[key]).exists():
            raise FeatureCertificationError("failed native operation published normal output")
    parameters = manifest["parameters"]
    logs = parameters.get("native_child_logs")
    if logs is not None:
        if type(logs) is not dict or not logs or not set(logs).issubset({"stdout", "stderr"}):
            raise FeatureCertificationError("failed native child log evidence is malformed")
        for stream in ("stdout", "stderr"):
            if stream in logs:
                _require_manifest_file_identity(
                    workspace,
                    logs[stream],
                    outputs[f"{stream}_path"],
                    f"failed native child {stream}",
                    stream_role=stream,
                )
    observer_ref = parameters.get("observer_evidence")
    if observer_ref is not None:
        observer = _read_object(
            _require_manifest_file_identity(
                workspace,
                observer_ref,
                outputs["observer_evidence_path"],
                "failed native observer evidence",
            )
        )
        _verify_native_observer(observer, request, succeeded=False)
    return manifest


_NATIVE_STAGE_MARKERS = (
    re.compile(r"^Time for partitioning data : [0-9]+\.[0-9]{6} seconds$"),
    re.compile(r"^Time for building indices on shards: [0-9]+\.[0-9]{6} seconds$"),
    re.compile(r"^Time for merging indices: [0-9]+\.[0-9]{6} seconds$"),
)


def _native_stage_sequence_observed(stdout: str) -> bool:
    positions: list[int] = []
    lines = stdout.splitlines()
    for marker in _NATIVE_STAGE_MARKERS:
        matches = [index for index, line in enumerate(lines) if marker.fullmatch(line)]
        if len(matches) != 1:
            return False
        positions.append(matches[0])
    return positions == sorted(positions) and len(set(positions)) == len(positions)


def _write_native_structural_projection(
    request: dict[str, Any],
    request_path: Path,
    closure: dict[str, Any],
    repository: Path,
) -> Path:
    workspace = Path(request["workspace_root"]).resolve()
    artifact = closure["artifact"]
    stdout = closure["stdout_path"].read_text(encoding="utf-8", errors="strict")
    source_mapping = (
        repository / "specs/002-cpp-main-upstream-baseline/source-mapping.md"
    )
    if not source_mapping.is_file():
        raise FeatureCertificationError("fixed upstream source mapping is missing")
    projection: dict[str, Any] = {
        "baseline_id": request["reference_role"],
        "execution_status": "SUCCEEDED",
        "source": {
            "repository": request["source"]["repository"],
            "branch": request["source"]["branch"],
            "commit": request["source"]["commit"],
            "clean_before": True,
            "clean_after": True,
        },
        "dataset_checksum": artifact["dataset"]["dataset_checksum"],
        "profile_checksum": request["profile"]["sha256"],
        "branch_mode": request["controls"]["branch_mode"],
        "partition_count": artifact["structural"]["partition_count"],
        "global_graph": {
            "available": True,
            "validation_status": "PASS",
            "id_space": artifact["id_space"],
            "node_count": artifact["structural"]["global_node_count"],
            "invalid_neighbor_count": 0,
            "self_edge_count": 0,
            "observed_max_degree": artifact["structural"]["observed_max_degree"],
            "medoids_valid": True,
            "dataset_lineage_checksum": artifact["dataset"]["dataset_checksum"],
        },
        "evidence_refs": [
            request_path.relative_to(workspace).as_posix(),
            closure["artifact_path"].relative_to(workspace).as_posix(),
            closure["evidence_path"].relative_to(workspace).as_posix(),
            closure["observer_path"].relative_to(workspace).as_posix(),
            closure["manifest_path"].relative_to(workspace).as_posix(),
            closure["stdout_path"].relative_to(workspace).as_posix(),
            closure["stderr_path"].relative_to(workspace).as_posix(),
            source_mapping.relative_to(repository).as_posix(),
        ],
        "evidence_identity": {
            "request_sha256": _sha256_file(request_path),
            "artifact_aggregate_checksum": artifact["aggregate_checksum"],
            "validation_evidence_sha256": _sha256_file(closure["evidence_path"]),
            "observer_evidence_sha256": _sha256_file(closure["observer_path"]),
            "terminal_manifest_sha256": _sha256_file(closure["manifest_path"]),
            "native_stdout_sha256": _sha256_file(closure["stdout_path"]),
            "native_stderr_sha256": _sha256_file(closure["stderr_path"]),
            "source_mapping_sha256": _sha256_file(source_mapping),
        },
    }
    if _native_stage_sequence_observed(stdout):
        projection["native_stage_sequence_observed"] = True
    output = resolve_feature_output(
        workspace, request["outputs"]["structural_projection_path"]
    )
    published = write_verified_document(
        workspace, output.relative_to(workspace), projection
    )
    if _read_object(published) != projection:
        raise FeatureCertificationError("structural projection reread mismatch")
    return published


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
    dataset = _fresh_validate_dataset(request, repository)
    runner = _verify_reference_binary_manifests(request, repository, dataset)
    _require_outputs_absent(request)
    completed = subprocess.run(
        [str(runner), "--request", str(immutable_request)],
        check=False,
        capture_output=True,
        text=True,
        shell=False,
    )
    if immutable_request.read_bytes() != request_bytes:
        raise FeatureCertificationError("capture owner mutated immutable request")
    _assert_source_identities(request, repository)
    post_runner = _verify_reference_binary_manifests(request, repository, dataset)
    if post_runner != runner:
        raise FeatureCertificationError("post-run capture owner identity mismatch")
    result = {
        "returncode": completed.returncode,
        "request_path": str(immutable_request),
        "request_byte_length": len(request_bytes),
        "request_sha256": hashlib.sha256(request_bytes).hexdigest(),
        "runner_diagnostic": {
            "stdout": completed.stdout,
            "stderr": completed.stderr,
        },
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
    }
    terminal_manifest = resolve_feature_output(
        workspace, request["outputs"]["build_manifest_path"]
    )
    if completed.returncode != 0:
        if terminal_manifest.is_file():
            try:
                manifest = _verify_cpp_failure_closure(
                    request, immutable_request, dataset, repository
                )
            except FeatureCertificationError as error:
                return {
                    **result,
                    "status": "failed",
                    "failure_class": "lifecycle-established-invalid",
                    "terminal_status": None,
                    "closure_error": str(error),
                }
            return {
                **result,
                "status": "failed",
                "failure_class": "lifecycle-established",
                "terminal_status": manifest["status"],
            }
        return {
            **result,
            "status": "failed",
            "failure_class": "pre-lifecycle/bootstrap",
            "terminal_status": None,
        }

    try:
        closure = _verify_cpp_success_closure(
            request, immutable_request, dataset, repository
        )
    except FeatureCertificationError as error:
        return {
            **result,
            "returncode": 1,
            "status": "incomplete",
            "terminal_status": None,
            "incomplete_reason": str(error),
        }
    try:
        projection = _write_native_structural_projection(
            request, immutable_request, closure, repository
        )
    except Exception as error:
        return {
            **result,
            "returncode": 1,
            "status": "incomplete",
            "terminal_status": "SUCCEEDED",
            "incomplete_reason": str(error),
        }
    missing = [
        relative
        for relative in request["outputs"].values()
        if not resolve_feature_output(workspace, relative).is_file()
    ]
    if missing:
        return {
            **result,
            "returncode": 1,
            "status": "incomplete",
            "terminal_status": "SUCCEEDED",
            "incomplete_reason": "request outputs are missing: " + ", ".join(missing),
        }
    return {
        **result,
        "status": "complete",
        "terminal_status": "SUCCEEDED",
        "artifact_path": str(closure["artifact_path"]),
        "manifest_path": str(closure["manifest_path"]),
        "validation_evidence_path": str(closure["evidence_path"]),
        "stdout_path": str(closure["stdout_path"]),
        "stderr_path": str(closure["stderr_path"]),
        "observer_evidence_path": str(closure["observer_path"]),
        "structural_projection_path": str(projection),
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
