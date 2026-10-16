"""Feature-local cpp_main Baseline preflight and structural-closure CLI."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .manifests import ImmutableWriteError, write_immutable_json
from . import native_dataset
from .workspace import WorkspaceError


PINNED_COMMIT = "78256bbab4685e1774e78d331e081a153be26823"
PINNED_REPOSITORY = "https://github.com/microsoft/DiskANN.git"
PINNED_BRANCH = "cpp_main"
DATASET_SHA256 = "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e"
PROFILE_REF = (
    "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml"
)
REPORT_REF = "specs/002-cpp-main-upstream-baseline/unified-sharded-report.json"
REQUEST_NAME = "unified-sharded-request.json"
NATIVE_DATASET_MEDIA = "application/vnd.diskann.cpp-main.float32-bin;version=1"


class BaselineHarnessError(RuntimeError):
    """Stable failure raised by the Feature-local Baseline lifecycle."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _canonical_digest(document: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            document,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace(
        "+00:00", "Z"
    )


def _repository_relative(path: Path, repository_root: Path) -> str:
    resolved = path.resolve(strict=False)
    try:
        return resolved.relative_to(repository_root).as_posix()
    except ValueError:
        return str(resolved)


def _resolve_repository_path(value: str, repository_root: Path) -> Path:
    path = Path(value)
    return path.resolve(strict=False) if path.is_absolute() else (repository_root / path).resolve()


def _write_immutable_bytes(target: Path, source: Path) -> None:
    try:
        native_dataset.write_immutable_bytes(target, source)
    except native_dataset.NativeDatasetError as error:
        raise BaselineHarnessError(str(error)) from error


def normalize_profile(profile: dict[str, Any]) -> dict[str, Any]:
    values = profile["profile"]
    derived = profile["derived"]
    return {
        "stage_parameters": {
            "partition": {
                "R": values["R"],
                "build_DRAM_budget_gib": values["build_DRAM_budget_gib"],
                "replication_factor": derived["p0_replication_factor"],
                "sample_probability": derived["p0_sample_probability"],
            },
            "local_build": {
                "L": values["L"],
                "R": values["R"],
                "alpha": derived["b0_alpha"],
                "build_pq_bytes": values["build_pq_bytes"],
                "max_occlusion_size": derived["b0_max_occlusion_size"],
                "num_threads": values["num_threads"],
                "saturation": derived["b0_saturation"],
                "use_opq": values["opq_enabled"],
            },
            "merge": {"R": values["R"], "use_filters": values["filtered"]},
        }
    }


def _load_profile(config_path: Path, repository_root: Path) -> dict[str, Any]:
    # Imported only by preflight so execute remains runnable in the pinned
    # minimal Linux image without a second YAML implementation.
    from jsonschema import Draft202012Validator

    from .config import load_yaml_file

    profile = load_yaml_file(config_path)
    schema_path = (
        repository_root
        / "contracts"
        / "baseline-v1"
        / "schemas"
        / "cpp-main-run-request.schema.json"
    )
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    errors = sorted(Draft202012Validator(schema).iter_errors(profile), key=str)
    if errors:
        raise BaselineHarnessError(f"draft Profile validation failed: {errors[0]}")
    return profile


def _git(repository: Path, *arguments: str) -> str:
    process = subprocess.run(
        ["git", "-c", f"safe.directory={repository}", "-C", str(repository), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if process.returncode != 0:
        raise BaselineHarnessError(
            f"source identity command failed: {' '.join(arguments)}"
        )
    return process.stdout.strip()


def _parse_cmake_identity(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("set(") or not line.endswith(")"):
            continue
        name, separator, quoted = line[4:-1].partition(" ")
        if not separator or len(quoted) < 2 or quoted[0] != '"' or quoted[-1] != '"':
            continue
        values[name] = quoted[1:-1]
    required = {
        "CPP_MAIN_IDENTITY_COMMIT",
        "CPP_MAIN_IDENTITY_COMPILER",
        "CPP_MAIN_IDENTITY_COMPILER_VERSION",
        "CPP_MAIN_IDENTITY_CMAKE_VERSION",
        "CPP_MAIN_IDENTITY_GENERATOR",
        "CPP_MAIN_IDENTITY_BUILD_TYPE",
        "CPP_MAIN_IDENTITY_CONFIGURE_COMMAND",
        "CPP_MAIN_IDENTITY_BUILD_COMMAND",
        "CPP_MAIN_IDENTITY_EFFECTIVE_DEFINITIONS",
        "CPP_MAIN_IDENTITY_PROJECT_COMPILE_OPTIONS",
    }
    if not required.issubset(values):
        raise BaselineHarnessError("cpp_main Build Identity is incomplete")
    return values


def _admit_source_build(repository_root: Path) -> dict[str, Any]:
    upstream = repository_root / "upstream" / "diskann-cpp-main"
    gitlink = _git(repository_root, "ls-files", "--stage", "--", "upstream/diskann-cpp-main")
    fields = gitlink.split()
    if len(fields) != 4 or fields[0] != "160000" or fields[1] != PINNED_COMMIT:
        raise BaselineHarnessError("parent gitlink is not the pinned cpp_main commit")
    head = _git(upstream, "rev-parse", "HEAD")
    branch = _git(upstream, "branch", "--show-current")
    remote = _git(upstream, "remote", "get-url", "origin")
    status = _git(upstream, "status", "--porcelain=v1", "--untracked-files=all")
    if (
        head != PINNED_COMMIT
        or branch != PINNED_BRANCH
        or remote != PINNED_REPOSITORY
        or status
    ):
        raise BaselineHarnessError("upstream source identity or clean state is invalid")

    candidates = [
        repository_root / "build" / "cpp-main-baseline" / "cpp-main-baseline-build-identity.cmake",
        repository_root / "build" / "cpp-main-baseline-linux" / "cpp-main-baseline-build-identity.cmake",
    ]
    identity_path = next((item for item in candidates if item.is_file()), None)
    if identity_path is None:
        raise BaselineHarnessError("cpp_main Build Identity document is missing")
    identity = _parse_cmake_identity(identity_path)
    if identity["CPP_MAIN_IDENTITY_COMMIT"] != PINNED_COMMIT:
        raise BaselineHarnessError("cpp_main Build Identity commit differs")
    build_root = identity_path.parent
    library = build_root / "cpp-main-upstream" / "src" / "libdiskann_s.a"
    if not library.is_file():
        raise BaselineHarnessError("linked upstream diskann_s library is missing")
    return {
        "repository": remote,
        "branch": branch,
        "commit": head,
        "gitlink": fields[1],
        "clean": True,
        "build_identity_path": _repository_relative(identity_path, repository_root),
        "build_identity": identity,
        "upstream_library_path": _repository_relative(library, repository_root),
        "upstream_library_sha256": _sha256(library),
    }


def _scan_dataset(source: Path, expected: dict[str, Any]) -> dict[str, Any]:
    try:
        facts = native_dataset.scan_dataset(source, expected, label="SIFT1M")
    except native_dataset.NativeDatasetError as error:
        raise BaselineHarnessError(str(error)) from error
    if facts["sha256"] != DATASET_SHA256:
        raise BaselineHarnessError("SIFT1M Dataset SHA-256 differs")
    return facts


def _dataset_identity_projection(
    dataset_checksum: str, structural: dict[str, Any], payload: dict[str, Any]
) -> dict[str, Any]:
    return native_dataset.dataset_identity_projection(dataset_checksum, structural, payload)


def _admit_dataset(
    repository_root: Path, workspace: Path, dataset: dict[str, Any]
) -> dict[str, Any]:
    # _load_profile retains the frozen request schema; the scanner additionally
    # requires the pinned payload checksum. Public admission cannot bypass it.
    source = _resolve_repository_path(dataset["path"], repository_root)
    facts = _scan_dataset(source, dataset)
    try:
        return native_dataset.publish_dataset(
            source, workspace, dataset["dataset_id"], facts,
            repository_root=repository_root, directory="datasets/sift1m",
            evidence_path="manifests/evidence/dataset-admission.json",
        )
    except (native_dataset.NativeDatasetError, ImmutableWriteError, WorkspaceError) as error:
        raise BaselineHarnessError(str(error)) from error


def _sharded_admission(profile: dict[str, Any]) -> dict[str, Any]:
    npts = int(profile["dataset"]["global_node_count"])
    dimension = int(profile["dataset"]["vector_dimension"])
    r = int(profile["profile"]["R"])
    abi_lock_plus_ptrdiff_bytes = 48
    estimated = 1.1 * npts * (
        ((dimension + 7) // 8) * 8 * 4
        + r * 4 * 1.2999999523162842
        + abi_lock_plus_ptrdiff_bytes
    )
    budget = float(profile["profile"]["build_DRAM_budget_gib"]) * 1024**3
    if estimated < budget:
        raise BaselineHarnessError("draft Profile does not require the sharded path")
    return {
        "estimated_full_index_ram_bytes": estimated,
        "build_dram_budget_bytes": budget,
        "abi_lock_plus_ptrdiff_bytes": abi_lock_plus_ptrdiff_bytes,
        "single_branch_predicate": False,
        "sharded_path_required": True,
    }


def preflight(
    config_path: str | Path,
    workspace_root: str | Path,
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    config = Path(config_path).resolve()
    profile = _load_profile(config, repository)
    source_build = _admit_source_build(repository)
    workspace = Path(workspace_root).resolve()
    upstream = (repository / "upstream" / "diskann-cpp-main").resolve()
    try:
        workspace.relative_to(upstream)
    except ValueError:
        pass
    else:
        raise BaselineHarnessError("workspace must remain outside upstream")
    workspace.mkdir(parents=True, exist_ok=True)
    dataset = _admit_dataset(repository, workspace, profile["dataset"])
    sharded = _sharded_admission(profile)
    profile_checksum = _sha256(config)
    normalized = normalize_profile(profile)
    run_id = "cpp-main-sift1m-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    workspace_ref = _repository_relative(workspace, repository)
    request = {
        "request_version": "1.0.0",
        "request_id": run_id,
        "operation": "execute",
        "study_class": "baseline_validation",
        "reproduction_mode": "upstream_faithful",
        "replacement_scope": "none",
        "workspace": {
            "root": workspace_ref,
            "memory_budget_bytes": int(math.ceil(sharded["build_dram_budget_bytes"])),
            "thread_budget": profile["profile"]["num_threads"],
        },
        "profile_ref": PROFILE_REF,
        "profile_checksum": profile_checksum,
        "profile_status": "draft",
        "source": source_build,
        "dataset_identity": {
            "checksum": profile["dataset"]["checksum"],
            "global_node_count": profile["dataset"]["global_node_count"],
            "vector_dimension": profile["dataset"]["vector_dimension"],
            "byte_length": profile["dataset"]["byte_length"],
        },
        "dataset": dataset["artifact_ref"],
        "dataset_validation_evidence_path": dataset["validation_evidence_path"],
        "implementations": profile["implementations"],
        "stage_parameters": normalized["stage_parameters"],
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
        "admission": {
            **sharded,
            "dataset_source_sha256": dataset["source_sha256"],
            "dataset_admitted_sha256": dataset["destination_sha256"],
            "non_finite_count": dataset["non_finite_count"],
            "upstream_clean": source_build.get("clean", True),
        },
    }
    request["source_config"] = {
        "yaml_sha256": profile_checksum,
        "normalized_json_sha256": _canonical_digest(request),
    }
    request_path = workspace / "requests" / REQUEST_NAME
    try:
        write_immutable_json(request_path, request)
    except ImmutableWriteError as error:
        raise BaselineHarnessError(str(error)) from error
    response = {
        "outcome": "SUCCEEDED",
        "request_path": str(request_path),
        "dataset_artifact": dataset["artifact_ref"],
        "dataset_validation_evidence_path": dataset["validation_evidence_path"],
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
        "sharded_path_required": True,
    }
    try:
        write_immutable_json(workspace / "preflight-result.json", response)
    except ImmutableWriteError as error:
        raise BaselineHarnessError(str(error)) from error
    return response


def _invoke_runner(runner: Path, request: Path, result_path: Path) -> dict[str, Any]:
    command = [str(runner), "execute", "--request", str(request), "--result", str(result_path)]
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        shell=False,
    )
    stdout, stderr = process.communicate()
    stdout_path = result_path.with_suffix(".stdout.log")
    stderr_path = result_path.with_suffix(".stderr.log")
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stdout_path.write_text(stdout, encoding="utf-8")
    stderr_path.write_text(stderr, encoding="utf-8")
    if not result_path.is_file():
        request_document = json.loads(request.read_text(encoding="utf-8"))
        result = {
            "result_version": "cpp-main-baseline-result/v1",
            "request_id": request_document["request_id"],
            "operation": "execute",
            "outcome": "FAILED",
            "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
            "local_partition_ids": [],
            "partition_count": 0,
            "stage_results": [],
            "artifacts": {"dataset": request_document["dataset"]},
            "build_manifest_paths": [],
            "validation_evidence_paths": [
                request_document["dataset_validation_evidence_path"]
            ],
            "upstream_clean_before": request_document["source"]["clean"],
            "upstream_clean_after": request_document["source"]["clean"],
            "failure": {
                "category": "RUNNER_FAILURE",
                "exit_code": process.returncode,
                "reason": stderr.strip() or "runner published no terminal result",
            },
        }
        try:
            write_immutable_json(result_path, result)
        except ImmutableWriteError as error:
            raise BaselineHarnessError(str(error)) from error
        return result
    result = json.loads(result_path.read_text(encoding="utf-8"))
    return result


def _load_evaluation(workspace: Path, reference: str) -> dict[str, Any]:
    path = Path(reference)
    target = path if path.is_absolute() else workspace / path
    document = json.loads(target.read_text(encoding="utf-8"))
    if document.get("evaluator") != {
        "implementation_id": "cpp_main_native_graph_integrity_evaluator",
        "version": "1.0.0",
    } or not document.get("findings", {}).get("passed", False):
        raise BaselineHarnessError("native graph integrity EvaluationReport is invalid")
    return document


def _build_report(request: dict[str, Any], result: dict[str, Any], workspace: Path) -> dict[str, Any]:
    evaluation_ref = result["evaluation_report_path"]
    _load_evaluation(workspace, evaluation_ref)
    partition_count = int(result["partition_count"])
    ids = result["local_partition_ids"]
    invocations = result["stage_invocations"]
    passed = (
        result.get("outcome") == "SUCCEEDED"
        and partition_count >= 2
        and ids == list(range(partition_count))
        and invocations == {
            "partition": 1,
            "local_build": partition_count,
            "merge": 1,
        }
        and result.get("upstream_clean_before") is True
        and result.get("upstream_clean_after") is True
    )
    return {
        "run_id": request["request_id"],
        "run_class": "unified_sharded_closure",
        "formal_graph_parity": False,
        "validated_graph_baseline_registered": False,
        "profile_ref": request["profile_ref"],
        "profile_checksum": request["profile_checksum"],
        "profile_status": "draft",
        "upstream_commit": PINNED_COMMIT,
        "dataset_checksum": request["dataset_identity"]["checksum"],
        "implementation_identities": request["implementations"],
        "partition_count": partition_count,
        "stage_results": result["stage_results"],
        "artifact_references": result["artifacts"],
        "build_manifest_refs": result["build_manifest_paths"],
        "validation_evidence_refs": result["validation_evidence_paths"],
        "evaluation_report_ref": evaluation_ref,
        "structural_validation": "PASS" if passed else "FAIL",
        "upstream_clean_before": result["upstream_clean_before"],
        "upstream_clean_after": result["upstream_clean_after"],
    }


def _build_failure_report(request: dict[str, Any], result: dict[str, Any]) -> dict[str, Any]:
    report = {
        "run_id": request["request_id"],
        "run_class": "unified_sharded_closure",
        "formal_graph_parity": False,
        "validated_graph_baseline_registered": False,
        "profile_ref": request["profile_ref"],
        "profile_checksum": request["profile_checksum"],
        "profile_status": "draft",
        "upstream_commit": PINNED_COMMIT,
        "dataset_checksum": request["dataset_identity"]["checksum"],
        "implementation_identities": request["implementations"],
        "partition_count": int(result.get("partition_count", 0)),
        "stage_results": result.get("stage_results", []),
        "stage_invocations": result.get(
            "stage_invocations", {"partition": 0, "local_build": 0, "merge": 0}
        ),
        "artifact_references": result.get(
            "artifacts", {"dataset": request["dataset"]}
        ),
        "build_manifest_refs": result.get("build_manifest_paths", []),
        "validation_evidence_refs": result.get(
            "validation_evidence_paths",
            [request["dataset_validation_evidence_path"]],
        ),
        "structural_validation": "FAIL",
        "upstream_clean_before": result.get(
            "upstream_clean_before", request["source"]["clean"]
        ),
        "upstream_clean_after": result.get(
            "upstream_clean_after", request["source"]["clean"]
        ),
        "failure": result.get(
            "failure", {"category": "RUNNER_FAILURE", "reason": "unknown failure"}
        ),
    }
    if "evaluation_report_path" in result:
        report["evaluation_report_ref"] = result["evaluation_report_path"]
    return report


def execute(
    request_path: str | Path,
    workspace_root: str | Path,
    runner: str | Path,
    *,
    repository_root: str | Path | None = None,
    report_path: str | Path | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    request_file = Path(request_path).resolve()
    request_bytes = request_file.read_bytes()
    request = json.loads(request_bytes)
    if request.get("operation") != "execute" or request.get("profile_status") != "draft":
        raise BaselineHarnessError("immutable request is not a draft execute request")
    if _sha256(repository / request["profile_ref"]) != request["profile_checksum"]:
        raise BaselineHarnessError("draft Profile checksum changed after preflight")
    workspace = Path(workspace_root).resolve()
    expected_workspace = _resolve_repository_path(request["workspace"]["root"], repository)
    if workspace != expected_workspace:
        raise BaselineHarnessError("execute workspace differs from admitted request")
    runner_result = workspace / "runner-results" / f"{request['request_id']}.json"
    result = _invoke_runner(Path(runner), request_file, runner_result)
    if request_file.read_bytes() != request_bytes:
        raise BaselineHarnessError("execute mutated the immutable request")
    succeeded = result.get("outcome") == "SUCCEEDED"
    report = (
        _build_report(request, result, workspace)
        if succeeded
        else _build_failure_report(request, result)
    )
    target = (
        Path(report_path).resolve()
        if report_path is not None
        else (repository / REPORT_REF).resolve()
    )
    try:
        write_immutable_json(target, report)
    except ImmutableWriteError as error:
        raise BaselineHarnessError(str(error)) from error
    if not succeeded:
        raise BaselineHarnessError(
            f"cpp_main runner failed: {result.get('failure', result)}"
        )
    return report


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpp_main_baseline")
    commands = parser.add_subparsers(dest="command", required=True)
    preflight_parser = commands.add_parser("preflight")
    preflight_parser.add_argument("--config", required=True)
    preflight_parser.add_argument("--workspace", required=True)
    execute_parser = commands.add_parser("execute")
    execute_parser.add_argument("--request", required=True)
    execute_parser.add_argument("--workspace", required=True)
    execute_parser.add_argument("--runner", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_argument_parser().parse_args(argv)
    try:
        if arguments.command == "preflight":
            response = preflight(arguments.config, arguments.workspace)
        else:
            response = execute(arguments.request, arguments.workspace, arguments.runner)
    except (BaselineHarnessError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(json.dumps({"outcome": "FAILED", "reason": str(error)}), file=sys.stderr)
        return 1
    print(json.dumps(response, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
