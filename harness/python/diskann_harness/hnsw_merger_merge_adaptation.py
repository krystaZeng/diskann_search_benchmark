"""Thin lifecycle for the HNSW-Merger same-input Merge comparison."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, ValidationError

from .config import load_yaml_file
from .manifests import ImmutableWriteError, write_immutable_json
from .runner import RunnerError, _schema_validator, run_runner
from .workspace import normalize_workspace, resolve_confined_path


PAPER_SOURCE = {
    "repository": "https://github.com/Kimchuls/HNSWMerger.git",
    "branch": "main",
    "commit": "88f6b7179f651cf5f284bd4e52249a4b1323b5af",
    "dirty_worktree": False,
    "doi": "10.1145/3786645",
    "pdf_sha256": "c5293d7c92b7ab5466788ddbc69dda7dad6e680e51a0373fee0b3d1374a2c8d0",
    "integration_mode": "source_mapping_only",
    "official_binary_linked": False,
}
DISKANN_SOURCE = {
    "repository": "https://github.com/microsoft/DiskANN.git",
    "branch": "cpp_main",
    "commit": "78256bbab4685e1774e78d331e081a153be26823",
    "dirty_worktree": False,
}
M0_ID = "cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1"
M1_ID = "hnsw_merger_vamana_merge_v1"
REQUEST_VERSION = "hnsw-merger-comparison-request/v1"
SIFT1M_COUNT = 1_000_000
FORMAL_REQUEST_RELATIVE = Path("requests/merge-comparison-request.json")
FORMAL_PROFILE_RELATIVE = Path(
    "experiments/configs/hnsw-merger-diskann-merge-adaptation/"
    "sift1m-structural.yaml"
)
_STABLE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/+-]*$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")


class HnswMergerHarnessError(RuntimeError):
    """Stable error for the Feature-private comparison lifecycle."""


def _canonical_digest(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _exact_object(value: Any, fields: set[str], label: str) -> dict[str, Any]:
    if type(value) is not dict or set(value) != fields:
        raise HnswMergerHarnessError(f"{label} has invalid or unknown fields")
    return value


def _artifact_ref(value: Any, label: str) -> dict[str, Any]:
    ref = _exact_object(
        value,
        {"artifact_type", "format_version", "aggregate_checksum", "metadata_path"},
        label,
    )
    if not all(type(ref[name]) is str and ref[name] for name in ref):
        raise HnswMergerHarnessError(f"{label} contains an invalid value")
    if not _SHA256.fullmatch(ref["aggregate_checksum"]):
        raise HnswMergerHarnessError(f"{label} checksum is invalid")
    path = Path(ref["metadata_path"])
    if path.is_absolute() or ".." in path.parts:
        raise HnswMergerHarnessError(f"{label} metadata path escapes workspace")
    return ref


def validate_normalized_request(value: Any) -> dict[str, Any]:
    request = _exact_object(
        value,
        {
            "request_version",
            "operation",
            "request_id",
            "study_class",
            "reproduction_mode",
            "replacement_scope",
            "profile_ref",
            "profile_checksum",
            "source_config",
            "workspace",
            "source_admission",
            "shared_inputs",
            "merges",
        },
        "comparison request",
    )
    constants = {
        "request_version": REQUEST_VERSION,
        "operation": "execute",
        "study_class": "single_paper_reproduction",
        "reproduction_mode": "faithful_adaptation",
        "replacement_scope": "merge",
    }
    for field, expected in constants.items():
        if request[field] != expected:
            raise HnswMergerHarnessError(f"{field} is not fixed to {expected}")
    if type(request["request_id"]) is not str or not _STABLE_ID.fullmatch(
        request["request_id"]
    ):
        raise HnswMergerHarnessError("request_id is invalid")
    if not _SHA256.fullmatch(request["profile_checksum"]):
        raise HnswMergerHarnessError("profile_checksum is invalid")
    source_config = _exact_object(
        request["source_config"],
        {"yaml_sha256", "normalized_json_sha256"},
        "source_config",
    )
    if not all(_SHA256.fullmatch(source_config[name]) for name in source_config):
        raise HnswMergerHarnessError("source_config digest is invalid")
    workspace = _exact_object(
        request["workspace"],
        {"root", "memory_budget_bytes", "thread_budget"},
        "workspace",
    )
    if (
        type(workspace["root"]) is not str
        or not workspace["root"]
        or type(workspace["memory_budget_bytes"]) is not int
        or workspace["memory_budget_bytes"] <= 0
        or type(workspace["thread_budget"]) is not int
        or workspace["thread_budget"] <= 0
    ):
        raise HnswMergerHarnessError("workspace budget is invalid")
    shared = _exact_object(
        request["shared_inputs"],
        {"dataset", "partition", "local_graphs"},
        "shared_inputs",
    )
    _artifact_ref(shared["dataset"], "shared DatasetArtifactRef")
    _artifact_ref(shared["partition"], "shared PartitionArtifactRef")
    if type(shared["local_graphs"]) is not list or not shared["local_graphs"]:
        raise HnswMergerHarnessError("ordered LocalGraphArtifactRef list is empty")
    for index, ref in enumerate(shared["local_graphs"]):
        _artifact_ref(ref, f"shared LocalGraphArtifactRef[{index}]")
    merges = _exact_object(request["merges"], {"m0", "m1"}, "merges")
    for arm, implementation in (("m0", M0_ID), ("m1", M1_ID)):
        selection = _exact_object(
            merges[arm],
            {"implementation_id", "implementation_version", "parameters"},
            arm,
        )
        if (
            selection["implementation_id"] != implementation
            or selection["implementation_version"] != "1.0.0"
            or type(selection["parameters"]) is not dict
        ):
            raise HnswMergerHarnessError(f"{arm} identity or parameters are invalid")
    if merges["m0"]["parameters"] != {"R": 16, "use_filters": False}:
        raise HnswMergerHarnessError("M0 parameters drifted")
    if merges["m1"]["parameters"] != {
        "R": 16,
        "alpha": 1.2,
        "max_candidate_size": 750,
        "metric": "l2",
        "use_filters": False,
        "multi_shard_strategy": "large_first_pairwise_v1",
        "lambda_policy": "paper_adaptive_log_v1",
    }:
        raise HnswMergerHarnessError("M1 parameters drifted")
    _exact_object(
        request["source_admission"],
        {"project_adaptation", "paper", "diskann", "toolchain"},
        "source_admission",
    )
    return request


def _comparison_validator(repository: Path) -> Draft202012Validator:
    schema = json.loads(
        (
            repository
            / "specs/004-hnsw-merger-diskann-merge-adaptation/contracts/"
            "same-input-structural-comparison.schema.json"
        ).read_text(encoding="utf-8")
    )
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def _container_path(path: Path, repository: Path) -> str:
    try:
        return "/workspace/" + path.resolve().relative_to(repository).as_posix()
    except ValueError as error:
        raise HnswMergerHarnessError("Docker test path escapes repository") from error


def execute(
    request_path: str | Path,
    runner: str | Path,
    *,
    repository_root: str | Path | None = None,
    container_image: str | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    request_file = Path(request_path).resolve()
    request_bytes = request_file.read_bytes()
    request = validate_normalized_request(json.loads(request_bytes))
    declared_workspace = Path(request["workspace"]["root"])
    if container_image is not None and declared_workspace.is_absolute():
        try:
            workspace = repository / declared_workspace.relative_to("/workspace")
        except ValueError as error:
            raise HnswMergerHarnessError(
                "Docker request workspace is not under /workspace"
            ) from error
    else:
        workspace = declared_workspace.resolve()
    result_path = resolve_confined_path(
        workspace, Path("reports/runner") / f"{request['request_id']}.json"
    )
    if container_image is None:
        result = run_runner(
            Path(runner), request_file, result_path, repository_root=repository
        )
    else:
        command = [
            "docker",
            "run",
            "--rm",
            "--network",
            "none",
            "--pull=never",
            "--user",
            "0:0",
            "-v",
            f"{repository}:/workspace",
            "-w",
            "/workspace",
            container_image,
            _container_path(Path(runner), repository),
            "execute",
            "--request",
            _container_path(request_file, repository),
            "--result",
            _container_path(result_path, repository),
        ]
        process = subprocess.run(command, capture_output=True, text=True, check=False)
        if not result_path.is_file():
            raise HnswMergerHarnessError(
                "Feature runner published no result: "
                + (process.stderr.strip() or process.stdout.strip())
            )
        result = json.loads(result_path.read_text(encoding="utf-8"))
        _schema_validator("runner-result.schema.json", repository).validate(result)
        if result["exit_code"] != process.returncode:
            raise HnswMergerHarnessError("runner result exit code differs")
    if request_file.read_bytes() != request_bytes:
        raise HnswMergerHarnessError("runner mutated immutable request")
    comparison_path = (
        workspace
        / "reports"
        / "hnsw-merger"
        / request["request_id"]
        / "same-input-structural-comparison.json"
    )
    comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
    _comparison_validator(repository).validate(comparison)
    if result["outcome"] != "SUCCEEDED":
        raise HnswMergerHarnessError(f"same-input comparison failed: {result}")
    return {"result": result, "comparison": comparison}


def _is_canonical_repository_url(value: str) -> bool:
    return bool(
        re.match(r"^(?:https://|ssh://|git@[^/:\s]+:).+", value)
    )


def _project_source_admission(
    repository: Path,
    *,
    git_reader: Any | None = None,
) -> dict[str, Any]:
    def read_git(*arguments: str) -> str:
        process = subprocess.run(
            ["git", "-C", str(repository), *arguments],
            capture_output=True,
            text=True,
            check=False,
        )
        if process.returncode != 0:
            raise HnswMergerHarnessError("project source identity is unverifiable")
        return process.stdout.strip()

    git = git_reader if git_reader is not None else read_git
    remotes = [line for line in git("remote").splitlines() if line]
    repositories = {
        git("remote", "get-url", remote).strip() for remote in remotes
    }
    if (
        len(repositories) != 1
        or not _is_canonical_repository_url(next(iter(repositories), ""))
    ):
        raise HnswMergerHarnessError(
            "canonical project Git remote is missing or ambiguous"
        )
    status = git("status", "--porcelain=v1", "--untracked-files=all")
    if status:
        raise HnswMergerHarnessError("project adaptation worktree is not clean")
    commit = git("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise HnswMergerHarnessError("project commit is not a full Git identity")
    branch = git("branch", "--show-current")
    if not branch:
        raise HnswMergerHarnessError("project branch is not identifiable")
    return {
        "repository": next(iter(repositories)),
        "branch": branch,
        "commit": commit,
        "dirty_worktree": False,
    }


_VALIDATION_CONTRACTS = {
    "dataset": {
        "artifact_type": "dataset",
        "accepted_format_versions": ["diskann-float32-dataset/v1"],
        "id_space": "none",
        "capabilities": ["dataset.diskann_float32_bin/v1"],
    },
    "partition": {
        "artifact_type": "partition",
        "accepted_format_versions": ["partition-artifact/v1"],
        "id_space": "local",
        "capabilities": ["partition.complete_local_to_global_map/v1"],
    },
    "local_graph": {
        "artifact_type": "local_graph",
        "accepted_format_versions": ["local-graph-artifact/v1"],
        "id_space": "local",
        "capabilities": ["graph.local_adjacency/v1"],
    },
}


def _validation_request_workspace(
    workspace_root: Path, memory_budget_bytes: int, thread_budget: int
) -> dict[str, Any]:
    return {
        **normalize_workspace(workspace_root),
        "memory_budget_bytes": memory_budget_bytes,
        "thread_budget": thread_budget,
    }


def _run_validation_request(
    runner: Path,
    request_path: Path,
    result_path: Path,
    *,
    repository: Path,
    container_image: str | None,
) -> dict[str, Any]:
    if container_image is None:
        return run_runner(
            runner, request_path, result_path, repository_root=repository
        )
    command = [
        "docker",
        "run",
        "--rm",
        "--network",
        "none",
        "--pull=never",
        "--user",
        "0:0",
        "-v",
        f"{repository}:/workspace",
        "-w",
        "/workspace",
        container_image,
        _container_path(runner, repository),
        "validate",
        "--request",
        _container_path(request_path, repository),
        "--result",
        _container_path(result_path, repository),
    ]
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    if not result_path.is_file():
        raise HnswMergerHarnessError(
            "validation runner published no result: "
            + (process.stderr.strip() or process.stdout.strip())
        )
    result = json.loads(result_path.read_text(encoding="utf-8"))
    _schema_validator("runner-result.schema.json", repository).validate(result)
    if result["exit_code"] != process.returncode:
        raise HnswMergerHarnessError("validation runner result exit code differs")
    return result


def _fresh_validate_shared_inputs(
    shared_inputs: dict[str, Any],
    workspace_root: Path,
    *,
    request_id: str,
    memory_budget_bytes: int,
    thread_budget: int,
    validation_runner: Path,
    repository: Path,
    container_image: str | None,
) -> list[dict[str, str]]:
    workspace = _validation_request_workspace(
        workspace_root, memory_budget_bytes, thread_budget
    )
    if container_image is not None:
        for key in (
            "root",
            "normal_root",
            "temporary_root",
            "manifest_root",
            "report_root",
            "quarantine_root",
        ):
            workspace[key] = _container_path(Path(workspace[key]), repository)
    sequence: list[tuple[str, str, dict[str, Any]]] = [
        ("dataset", "dataset", shared_inputs["dataset"]),
        ("partition", "partition", shared_inputs["partition"]),
    ]
    sequence.extend(
        (f"local_graph:{index}", "local_graph", reference)
        for index, reference in enumerate(shared_inputs["local_graphs"])
    )
    retained: list[dict[str, str]] = []
    for index, (role, contract_name, reference) in enumerate(sequence):
        validation_id = f"{request_id}-fresh-{index:04d}"
        validation_request = {
            "request_version": "1.0.0",
            "request_id": validation_id,
            "operation": "validate",
            "study_class": "infrastructure_validation",
            "workspace": workspace,
            "target_artifact": reference,
            "expected_contract": _VALIDATION_CONTRACTS[contract_name],
        }
        request_path = resolve_confined_path(
            workspace_root,
            Path("requests/preflight-validation") / f"{validation_id}.json",
        )
        result_path = resolve_confined_path(
            workspace_root,
            Path("reports/preflight-validation") / f"{validation_id}.json",
        )
        try:
            write_immutable_json(request_path, validation_request)
        except ImmutableWriteError as error:
            raise HnswMergerHarnessError(str(error)) from error
        result = _run_validation_request(
            validation_runner,
            request_path,
            result_path,
            repository=repository,
            container_image=container_image,
        )
        if result["outcome"] != "SUCCEEDED":
            raise HnswMergerHarnessError(
                f"{role} fresh validation failed: {result.get('failure', result)}"
            )
        evidence_path = result.get("validation_evidence_path")
        if type(evidence_path) is not str:
            raise HnswMergerHarnessError(
                f"{role} fresh validation produced no ValidationEvidence"
            )
        evidence_file = resolve_confined_path(workspace_root, evidence_path)
        evidence = json.loads(evidence_file.read_text(encoding="utf-8"))
        candidate = evidence.get("artifact_candidate", {})
        expected_candidate = {
            "artifact_type": reference["artifact_type"],
            "format_version": reference["format_version"],
            "claimed_aggregate_checksum": reference["aggregate_checksum"],
            "metadata_path": reference["metadata_path"],
        }
        if candidate != expected_candidate:
            raise HnswMergerHarnessError(
                f"{role} fresh validation evidence does not bind the requested ArtifactRef"
            )
        retained.append({"role": role, "path": evidence_path})

    normal_root = workspace_root / "normal"
    partition_metadata = json.loads(
        resolve_confined_path(
            normal_root, shared_inputs["partition"]["metadata_path"]
        ).read_text(encoding="utf-8")
    )
    expected_partition_ids = sorted(
        item["partition_id"]
        for item in partition_metadata["structural"]["partitions"]
    )
    observed_partition_ids = []
    for reference in shared_inputs["local_graphs"]:
        metadata = json.loads(
            resolve_confined_path(normal_root, reference["metadata_path"]).read_text(
                encoding="utf-8"
            )
        )
        observed_partition_ids.append(metadata["structural"]["partition_id"])
    if observed_partition_ids != expected_partition_ids:
        raise HnswMergerHarnessError(
            "ordered LocalGraphArtifact collection does not match Partition owners"
        )
    return retained


def _validate_preflight_profile(config: dict[str, Any]) -> None:
    if config.get("status") != "frozen":
        raise HnswMergerHarnessError("SIFT1M configuration is not frozen")
    if config.get("dataset", {}).get("global_node_count", 0) > SIFT1M_COUNT:
        raise HnswMergerHarnessError("dataset exceeds the approved SIFT1M bound")
    execution = config.get("execution", {})
    if execution.get("parameter_sweep") is not False:
        raise HnswMergerHarnessError("parameter sweep is prohibited")
    if execution.get("automatic_tuning", False) is not False:
        raise HnswMergerHarnessError("automatic tuning is prohibited")
    if execution.get("best_result_selection", False) is not False:
        raise HnswMergerHarnessError("best-result selection is prohibited")
    if type(config.get("shared_inputs")) is not dict:
        raise HnswMergerHarnessError("shared input set is missing")
    if type(config.get("merges")) is not dict:
        raise HnswMergerHarnessError("Merge selections are missing")


def preflight(
    config_path: str | Path,
    workspace_root: str | Path,
    *,
    repository_root: str | Path | None = None,
    validation_runner: str | Path | None = None,
    container_image: str | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    # Formal admission deliberately precedes any workspace mutation or Merge.
    project = _project_source_admission(repository)
    config_file = Path(config_path).resolve()
    config = load_yaml_file(config_file)
    _validate_preflight_profile(config)
    roots = normalize_workspace(workspace_root, repository_root=repository)
    workspace_path = Path(roots["root"])
    runner_path = (
        Path(validation_runner).resolve()
        if validation_runner is not None
        else (
            repository
            / "build/hnsw-merger-merge-adaptation/src/cli/"
            "diskann_pipeline_runner"
        )
    )
    fresh_evidence = _fresh_validate_shared_inputs(
        config["shared_inputs"],
        workspace_path,
        request_id=config["request_id"],
        memory_budget_bytes=config["execution"]["memory_budget_bytes"],
        thread_budget=config["execution"]["thread_budget"],
        validation_runner=runner_path,
        repository=repository,
        container_image=container_image,
    )
    request = {
        "request_version": REQUEST_VERSION,
        "operation": "execute",
        "request_id": config["request_id"],
        "study_class": "single_paper_reproduction",
        "reproduction_mode": "faithful_adaptation",
        "replacement_scope": "merge",
        "profile_ref": config_file.relative_to(repository).as_posix(),
        "profile_checksum": _sha256(config_file),
        "source_config": {
            "yaml_sha256": _sha256(config_file),
            "normalized_json_sha256": _canonical_digest(config),
        },
        "workspace": {
            "root": roots["root"],
            "memory_budget_bytes": config["execution"]["memory_budget_bytes"],
            "thread_budget": config["execution"]["thread_budget"],
        },
        "source_admission": {
            "project_adaptation": project,
            "paper": PAPER_SOURCE,
            "diskann": DISKANN_SOURCE,
            "toolchain": config["source_admission"]["toolchain"],
        },
        "shared_inputs": config["shared_inputs"],
        "merges": config["merges"],
    }
    validate_normalized_request(request)
    target = resolve_confined_path(workspace_path, FORMAL_REQUEST_RELATIVE)
    try:
        write_immutable_json(target, request)
    except ImmutableWriteError as error:
        raise HnswMergerHarnessError(str(error)) from error
    return {
        "outcome": "SUCCEEDED",
        "request": str(target),
        "fresh_validation_evidence": fresh_evidence,
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
    }


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="hnsw_merger_merge_adaptation")
    commands = parser.add_subparsers(dest="command", required=True)
    preflight_parser = commands.add_parser("preflight")
    preflight_parser.add_argument("--config", required=True)
    preflight_parser.add_argument("--workspace", required=True)
    execute_parser = commands.add_parser("execute")
    execute_parser.add_argument("--request", required=True)
    execute_parser.add_argument("--runner", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_argument_parser().parse_args(argv)
    try:
        if arguments.command == "preflight":
            result = preflight(arguments.config, arguments.workspace)
        else:
            result = execute(arguments.request, arguments.runner)
    except (
        HnswMergerHarnessError,
        RunnerError,
        ValidationError,
        OSError,
        ValueError,
        KeyError,
        json.JSONDecodeError,
    ) as error:
        print(json.dumps({"outcome": "FAILED", "reason": str(error)}), file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
