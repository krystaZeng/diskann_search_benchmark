"""Thin Feature-local lifecycle for the admitted OGP experiment runner."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from .checksum import aggregate_checksum
from .manifests import ImmutableWriteError, write_immutable_json
from .runner import RunnerError, _schema_validator


GPANN_SOURCE = {
    "repository": "https://github.com/larsgottesbueren/gp-ann.git",
    "branch": "main",
    "commit": "df5efb4a6b303011c795666ed267f29d994b558c",
    "dirty_worktree": False,
}
DISKANN_SOURCE = {
    "repository": "https://github.com/microsoft/DiskANN.git",
    "branch": "cpp_main",
    "commit": "78256bbab4685e1774e78d331e081a153be26823",
    "dirty_worktree": False,
}
SIFT1M_CHECKSUM = "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e"
SIFT1M_CONFIG_REF = "experiments/configs/ogp-partition-adaptation/sift1m-structural.yaml"
SIFT1M_REQUEST_NAME = "ogp-request.json"
_STABLE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/+-]*$")


class OgpHarnessError(RuntimeError):
    """Stable failure for the Feature-local frozen SIFT1M lifecycle."""


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


def _repository_relative(path: Path, repository_root: Path) -> str:
    resolved = path.resolve(strict=False)
    try:
        return resolved.relative_to(repository_root.resolve()).as_posix()
    except ValueError as error:
        raise OgpHarnessError("path is outside the repository") from error


def _resolve_repository_path(value: str, repository_root: Path) -> Path:
    path = Path(value)
    return path.resolve() if path.is_absolute() else (repository_root / path).resolve()


def _expected_frozen_values() -> dict[str, Any]:
    return {
        "governance": {
            "study_class": "single_paper_reproduction",
            "reproduction_mode": "faithful_adaptation",
            "replacement_scope": "partition",
            "status": "frozen",
        },
        "dataset": {
            "dataset_id": "sift1m-base-float32-v1",
            "path": "sift1M_dataset/base_vector.bin",
            "checksum": SIFT1M_CHECKSUM,
            "global_node_count": 1_000_000,
            "vector_dimension": 128,
            "byte_length": 512_000_008,
            "scalar_type": "float32",
            "metric": "l2",
        },
        "implementations": {
            "partition": "gp_ann_ogp_partition_v1",
            "local_build": "cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1",
            "merge": "cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1",
        },
        "stage_parameters": {
            "partition": {
                "requested_num_clusters": 40,
                "requested_overlap": 0.2,
                "partitioning_mode": "default",
            },
            "local_build": {
                "L": 32,
                "R": 16,
                "alpha": 1.2,
                "build_pq_bytes": 0,
                "max_occlusion_size": 750,
                "num_threads": 6,
                "saturation": False,
                "use_opq": False,
            },
            "merge": {"R": 16, "use_filters": False},
        },
        "effective_constants": {
            "epsilon": 0.05,
            "epsilon_source": "upstream_cli_constant",
        },
        "resources": {
            "memory_budget_bytes": 644_245_095,
            "thread_budget": 6,
            "configured_environment": {"PARLAY_NUM_THREADS": "6"},
        },
        "execution": {
            "parameter_sweep": False,
            "retention": "retain_all",
            "max_dataset_nodes": 1_000_000,
            "allowed_retry_reasons": [
                "infrastructure_failure",
                "external_execution_failure",
            ],
            "workspace": "build/ogp-partition-adaptation/runs/sift1m-structural",
            "runner": "build/ogp-partition-adaptation/linux/src/papers/gp_ann/ogp/ogp_experiment_runner",
            "container_image": "diskann-native-probe:jammy",
            "container_image_id": "sha256:7b2782df15ca76874f902fd6336c66cab7b077f8eeb788adf148165c65f9dfcb",
        },
        "outputs": {
            "request": "requests/ogp-request.json",
            "attempt_result": "reports/runner/result.json",
            "runner_stdout": "reports/runner/stdout.log",
            "runner_stderr": "reports/runner/stderr.log",
        },
    }


def validate_frozen_configuration(config: dict[str, Any]) -> None:
    expected = _expected_frozen_values()
    governance = {name: config.get(name) for name in expected["governance"]}
    if governance != expected["governance"]:
        raise OgpHarnessError("frozen OGP governance differs")
    if config.get("dataset") != expected["dataset"]:
        raise OgpHarnessError("frozen SIFT1M Dataset identity differs")
    if int(config["dataset"]["global_node_count"]) > 1_000_000:
        raise OgpHarnessError("Dataset larger than SIFT1M is prohibited")
    for field in (
        "implementations",
        "stage_parameters",
        "effective_constants",
        "resources",
        "execution",
        "outputs",
    ):
        if config.get(field) != expected[field]:
            raise OgpHarnessError(f"frozen {field} differs")
    if set(config) != {
        *expected["governance"],
        "dataset",
        "sources",
        "implementations",
        "stage_parameters",
        "effective_constants",
        "resources",
        "execution",
        "outputs",
    }:
        raise OgpHarnessError("frozen configuration fields differ")
    sources = config.get("sources")
    if not isinstance(sources, dict) or set(sources) != {
        "partition",
        "local_build",
        "merge",
    }:
        raise OgpHarnessError("frozen stage source declarations differ")
    if sources["partition"].get("repository") != GPANN_SOURCE["repository"] or sources[
        "partition"
    ].get("branch") != GPANN_SOURCE["branch"] or sources["partition"].get(
        "commit"
    ) != GPANN_SOURCE[
        "commit"
    ]:
        raise OgpHarnessError("frozen GP-ANN source identity differs")
    for stage in ("local_build", "merge"):
        if any(sources[stage].get(key) != DISKANN_SOURCE[key] for key in ("repository", "branch", "commit")):
            raise OgpHarnessError("frozen DiskANN source identity differs")


def load_frozen_configuration(
    config_path: str | Path, *, repository_root: str | Path | None = None
) -> dict[str, Any]:
    from .config import load_yaml_file

    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    path = Path(config_path).resolve()
    if _repository_relative(path, repository) != SIFT1M_CONFIG_REF:
        raise OgpHarnessError("only the frozen SIFT1M configuration is admitted")
    config = load_yaml_file(path)
    validate_frozen_configuration(config)
    return config


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _git(repository: Path, *arguments: str) -> str:
    completed = subprocess.run(
        [
            "git",
            "-c",
            f"safe.directory={repository}",
            "-C",
            str(repository),
            *arguments,
        ],
        check=False,
        capture_output=True,
        text=True,
        shell=False,
    )
    if completed.returncode != 0:
        raise OgpHarnessError(
            f"source identity command failed: {' '.join(arguments)}"
        )
    return completed.stdout.strip()


def _parse_cmake_identity(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise OgpHarnessError(f"build identity is missing: {path}")
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("set(") or not line.endswith(")"):
            continue
        name, separator, quoted = line[4:-1].partition(" ")
        if separator and len(quoted) >= 2 and quoted[0] == '"' and quoted[-1] == '"':
            values[name] = quoted[1:-1]
    return values


def _admit_formal_sources(
    config: dict[str, Any], repository_root: Path
) -> dict[str, Any]:
    sources = config["sources"]
    gpann_config = sources["partition"]
    gpann_root = _resolve_repository_path(gpann_config["source_path"], repository_root)
    if (
        _git(gpann_root, "remote", "get-url", "origin") != GPANN_SOURCE["repository"]
        or _git(gpann_root, "branch", "--show-current") != GPANN_SOURCE["branch"]
        or _git(gpann_root, "rev-parse", "HEAD") != GPANN_SOURCE["commit"]
        or _git(gpann_root, "status", "--porcelain=v1", "--untracked-files=all")
    ):
        raise OgpHarnessError("formal GP-ANN source identity or clean state is invalid")
    dependency_paths = {
        "parlaylib": "external/parlaylib",
        "kaminpar": "external/KaMinPar",
        "hnswlib": "external/hnswlib",
        "message_queue": "external/message-queue",
    }
    for name, relative in dependency_paths.items():
        dependency = gpann_root / relative
        if (
            _git(dependency, "rev-parse", "HEAD")
            != gpann_config["dependencies"][name]
            or _git(dependency, "status", "--porcelain=v1", "--untracked-files=all")
        ):
            raise OgpHarnessError(f"formal GP-ANN dependency is invalid: {name}")
    recursive = _git(gpann_root, "submodule", "status", "--recursive")
    if any(line and line[0] in "-+U" for line in recursive.splitlines()):
        raise OgpHarnessError("recursive GP-ANN dependency identity is incomplete")

    gpann_identity_path = _resolve_repository_path(
        gpann_config["build_identity"], repository_root
    )
    gpann_identity = _parse_cmake_identity(gpann_identity_path)
    expected_gpann_identity = {
        "GPANN_IDENTITY_REPOSITORY": GPANN_SOURCE["repository"],
        "GPANN_IDENTITY_BRANCH": GPANN_SOURCE["branch"],
        "GPANN_IDENTITY_COMMIT": GPANN_SOURCE["commit"],
        "GPANN_IDENTITY_PARLAYLIB_COMMIT": gpann_config["dependencies"]["parlaylib"],
        "GPANN_IDENTITY_KAMINPAR_COMMIT": gpann_config["dependencies"]["kaminpar"],
        "GPANN_IDENTITY_HNSWLIB_COMMIT": gpann_config["dependencies"]["hnswlib"],
        "GPANN_IDENTITY_MESSAGE_QUEUE_COMMIT": gpann_config["dependencies"]["message_queue"],
        "GPANN_IDENTITY_PARTITION_SHA256": gpann_config["executable_sha256"],
    }
    if any(gpann_identity.get(key) != value for key, value in expected_gpann_identity.items()):
        raise OgpHarnessError("admitted GP-ANN build identity differs")
    executable = _resolve_repository_path(gpann_config["executable"], repository_root)
    if not executable.is_file() or _sha256(executable) != gpann_config["executable_sha256"]:
        raise OgpHarnessError("admitted GP-ANN Partition executable identity differs")

    diskann_config = sources["local_build"]
    diskann_root = _resolve_repository_path(
        diskann_config["source_path"], repository_root
    )
    gitlink = _git(
        repository_root,
        "ls-files",
        "--stage",
        "--",
        diskann_config["source_path"],
    ).split()
    if (
        len(gitlink) != 4
        or gitlink[0] != "160000"
        or gitlink[1] != DISKANN_SOURCE["commit"]
        or _git(diskann_root, "remote", "get-url", "origin")
        != DISKANN_SOURCE["repository"]
        or _git(diskann_root, "branch", "--show-current") != DISKANN_SOURCE["branch"]
        or _git(diskann_root, "rev-parse", "HEAD") != DISKANN_SOURCE["commit"]
        or _git(diskann_root, "status", "--porcelain=v1", "--untracked-files=all")
    ):
        raise OgpHarnessError("formal DiskANN source identity or clean state is invalid")
    diskann_identity_path = _resolve_repository_path(
        diskann_config["build_identity"], repository_root
    )
    diskann_identity = _parse_cmake_identity(diskann_identity_path)
    if diskann_identity.get("CPP_MAIN_IDENTITY_COMMIT") != DISKANN_SOURCE["commit"]:
        raise OgpHarnessError("admitted DiskANN build identity differs")
    library = diskann_identity_path.parent / "cpp-main-upstream" / "src" / "libdiskann_s.a"
    if not library.is_file():
        raise OgpHarnessError("admitted DiskANN library is missing")

    gpann_evidence = {
        "source": GPANN_SOURCE,
        "build_identity_path": _repository_relative(
            gpann_identity_path, repository_root
        ),
        "build_identity_sha256": _sha256(gpann_identity_path),
        "executable_path": _repository_relative(executable, repository_root),
        "executable_sha256": _sha256(executable),
        "dependencies": gpann_config["dependencies"],
    }
    diskann_evidence = {
        "source": DISKANN_SOURCE,
        "build_identity_path": _repository_relative(
            diskann_identity_path, repository_root
        ),
        "build_identity_sha256": _sha256(diskann_identity_path),
        "library_path": _repository_relative(library, repository_root),
        "library_sha256": _sha256(library),
    }
    return {
        "partition": gpann_evidence,
        "local_build": diskann_evidence,
        "merge": diskann_evidence,
    }


def _admit_sift1m_dataset(
    config: dict[str, Any], workspace: Path, repository_root: Path
) -> dict[str, Any]:
    from .cpp_main_baseline import _admit_dataset

    return _admit_dataset(repository_root, workspace, config["dataset"])


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
    config_path_resolved = Path(config_path).resolve()
    config = load_frozen_configuration(
        config_path_resolved, repository_root=repository
    )
    workspace = Path(workspace_root).resolve()
    expected_workspace = _resolve_repository_path(
        config["execution"]["workspace"], repository
    )
    if workspace != expected_workspace and repository_root is None:
        raise OgpHarnessError("preflight workspace differs from frozen configuration")
    for protected in (
        repository / "upstream" / "diskann-cpp-main",
        _resolve_repository_path(
            config["sources"]["partition"]["source_path"], repository
        ),
    ):
        try:
            workspace.relative_to(protected.resolve())
        except ValueError:
            pass
        else:
            raise OgpHarnessError("preflight workspace is inside an admitted source tree")
    workspace.mkdir(parents=True, exist_ok=True)
    for name in ("normal", "temporary", "manifests", "reports", "quarantine", "requests"):
        (workspace / name).mkdir(exist_ok=True)
    sources = _admit_formal_sources(config, repository)
    dataset = _admit_sift1m_dataset(config, workspace, repository)
    configuration_checksum = _sha256(config_path_resolved)
    normalized_configuration_sha256 = _canonical_digest(config)
    request = {
        "request_version": "1.0.0",
        "request_id": "ogp-sift1m-preflight",
        "operation": "preflight",
        "study_class": "single_paper_reproduction",
        "reproduction_mode": "faithful_adaptation",
        "replacement_scope": "partition",
        "configuration_ref": SIFT1M_CONFIG_REF,
        "configuration_checksum": configuration_checksum,
        "normalized_configuration_sha256": normalized_configuration_sha256,
        "workspace": {
            "root": _repository_relative(workspace, repository),
            "memory_budget_bytes": config["resources"]["memory_budget_bytes"],
            "thread_budget": config["resources"]["thread_budget"],
        },
        "dataset": dataset["artifact_ref"],
        "dataset_validation_evidence_path": dataset["validation_evidence_path"],
        "implementations": config["implementations"],
        "stage_parameters": config["stage_parameters"],
        "source": DISKANN_SOURCE,
        "stage_sources": {
            "partition": GPANN_SOURCE,
            "local_build": DISKANN_SOURCE,
            "merge": DISKANN_SOURCE,
        },
        "source_config": {
            "yaml_sha256": configuration_checksum,
            "normalized_json_sha256": normalized_configuration_sha256,
        },
        "source_build_admission": sources,
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
        "execution_policy": {
            "parameter_sweep": False,
            "retention": "retain_all",
            "allowed_retry_reasons": config["execution"]["allowed_retry_reasons"],
        },
    }
    request_path = workspace / config["outputs"]["request"]
    response = {
        "outcome": "SUCCEEDED",
        "request_path": str(request_path),
        "configuration_checksum": configuration_checksum,
        "dataset_artifact": dataset["artifact_ref"],
        "dataset_validation_evidence_path": dataset["validation_evidence_path"],
        "stage_invocations": {"partition": 0, "local_build": 0, "merge": 0},
    }
    try:
        write_immutable_json(request_path, request)
        write_immutable_json(workspace / "preflight-result.json", response)
    except ImmutableWriteError as error:
        raise OgpHarnessError(str(error)) from error
    return response


def _write_small_fbin(path: Path, rows: int = 96, dimension: int = 4) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as output:
        output.write(struct.pack("<II", rows, dimension))
        for row in range(rows):
            for column in range(dimension):
                output.write(struct.pack("<f", ((row * 7 + column * 13) % 101) / 17.0))


def _materialize_dataset(workspace: Path) -> dict[str, Any]:
    directory = workspace / "normal" / "dataset"
    vectors = directory / "vectors.bin"
    _write_small_fbin(vectors)
    checksum = _sha256(vectors)
    structural = {
        "global_node_count": 96,
        "vector_dimension": 4,
        "scalar_type": "float32",
        "distance_metric": "l2",
    }
    payload = {
        "role": "vectors",
        "path": "vectors.bin",
        "media_type": "application/vnd.diskann.cpp-main.float32-bin;version=1",
        "byte_length": vectors.stat().st_size,
        "sha256": checksum,
    }
    projection = {
        "schema_version": "1.0.0",
        "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": "dataset",
        "format_version": "diskann-float32-dataset/v1",
        "dataset_checksum": checksum,
        "id_space": "none",
        "capabilities": ["dataset.diskann_float32_bin/v1"],
        "payloads": [
            {
                "role": payload["role"],
                "byte_length": payload["byte_length"],
                "sha256": payload["sha256"],
            }
        ],
        "source_artifacts": [],
        "structural": structural,
    }
    aggregate = aggregate_checksum(projection)
    metadata = {
        "schema_version": "1.0.0",
        "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": "dataset",
        "format_version": "diskann-float32-dataset/v1",
        "dataset": {
            "dataset_id": "ogp-python-small-96x4-v1",
            "dataset_checksum": checksum,
            **structural,
        },
        "id_space": "none",
        "structural": structural,
        "capabilities": ["dataset.diskann_float32_bin/v1"],
        "source_artifacts": [],
        "payloads": [payload],
        "aggregate_checksum": aggregate,
    }
    (directory / "artifact.json").write_text(
        json.dumps(metadata, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    return {
        "metadata_path": "dataset/artifact.json",
        "artifact_type": "dataset",
        "format_version": "diskann-float32-dataset/v1",
        "aggregate_checksum": aggregate,
    }


def find_built_runner(repository_root: Path) -> Path:
    candidates = [
        repository_root
        / "build"
        / "ogp-partition-adaptation"
        / "linux"
        / "src"
        / "papers"
        / "gp_ann"
        / "ogp"
        / "ogp_experiment_runner",
        repository_root
        / "build"
        / "ogp-partition-adaptation"
        / "src"
        / "papers"
        / "gp_ann"
        / "ogp"
        / "ogp_experiment_runner",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RunnerError("built ogp_experiment_runner was not found")


def _container_path(path: Path, repository_root: Path) -> str:
    try:
        relative = path.resolve().relative_to(repository_root.resolve())
    except ValueError as error:
        raise RunnerError("OGP container path is outside the repository") from error
    parts = list(relative.parts)
    if parts[:2] == ["build", "ogp-partition-adaptation"] and len(parts) > 2 and parts[2] == "linux":
        parts.pop(2)
    return str(Path("/workspace", *parts))


def run_small_composed_fixture(
    *, runner: Path, workspace_parent: Path, repository_root: Path
) -> dict[str, Any]:
    workspace_parent.mkdir(parents=True, exist_ok=True)
    workspace = Path(tempfile.mkdtemp(prefix="small-real-", dir=workspace_parent))
    dataset = _materialize_dataset(workspace)
    run_id = workspace.name.replace("_", "-")
    container_workspace = _container_path(workspace, repository_root)
    request = {
        "request_version": "1.0.0",
        "request_id": run_id,
        "operation": "execute",
        "study_class": "single_paper_reproduction",
        "reproduction_mode": "faithful_adaptation",
        "replacement_scope": "partition",
        "workspace": {
            "root": container_workspace,
            "memory_budget_bytes": 1024 * 1024 * 1024,
            "thread_budget": 6,
        },
        "dataset": dataset,
        "implementations": {
            "partition": "gp_ann_ogp_partition_v1",
            "local_build": "cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1",
            "merge": "cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1",
        },
        "stage_parameters": {
            "partition": {
                "requested_num_clusters": 4,
                "requested_overlap": 0.2,
                "partitioning_mode": "default",
            },
            "local_build": {
                "L": 32,
                "R": 16,
                "alpha": 1.2,
                "build_pq_bytes": 0,
                "max_occlusion_size": 750,
                "num_threads": 6,
                "saturation": False,
                "use_opq": False,
            },
            "merge": {"R": 16, "use_filters": False},
        },
        "stage_sources": {
            "partition": GPANN_SOURCE,
            "local_build": DISKANN_SOURCE,
            "merge": DISKANN_SOURCE,
        },
    }
    normalized_bytes = json.dumps(
        request, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode("utf-8")
    request["source_config"] = {
        "yaml_sha256": hashlib.sha256(b"ogp-small-real-config-v1").hexdigest(),
        "normalized_json_sha256": hashlib.sha256(normalized_bytes).hexdigest(),
    }
    request_path = workspace / "requests" / "request.json"
    result_path = workspace / "reports" / "runner" / "result.json"
    request_path.parent.mkdir(parents=True, exist_ok=True)
    request_path.write_text(
        json.dumps(request, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )

    container = os.environ.get("OGP_TEST_CONTAINER", "gpann-ogp-builder-session")
    command = [
        "docker",
        "exec",
        container,
        _container_path(runner, repository_root),
        "execute",
        "--request",
        _container_path(request_path, repository_root),
        "--result",
        _container_path(result_path, repository_root),
    ]
    process = subprocess.run(command, capture_output=True, text=True, check=False)
    if not result_path.is_file():
        raise RunnerError(
            "real OGP runner published no result: "
            + (process.stderr.strip() or process.stdout.strip())
        )
    result = json.loads(result_path.read_text(encoding="utf-8"))
    _schema_validator("runner-result.schema.json", repository_root).validate(result)
    if result.get("exit_code") != process.returncode:
        raise RunnerError("OGP runner exit status differs from terminal result")
    return result


def _copy_immutable(source: Path, target: Path) -> None:
    if target.exists():
        raise OgpHarnessError(f"immutable target already exists: {target}")
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, target)
    except OSError:
        shutil.copyfile(source, target)
    target.chmod(0o444)


def _materialize_attempt_dataset(
    preflight_request: dict[str, Any],
    attempt_workspace: Path,
    repository_root: Path,
) -> None:
    preflight_workspace = _resolve_repository_path(
        preflight_request["workspace"]["root"], repository_root
    )
    artifact_ref = preflight_request["dataset"]
    source_metadata = preflight_workspace / "normal" / artifact_ref["metadata_path"]
    metadata = json.loads(source_metadata.read_text(encoding="utf-8"))
    target_metadata = attempt_workspace / "normal" / artifact_ref["metadata_path"]
    for payload in metadata["payloads"]:
        source_payload = source_metadata.parent / payload["path"]
        target_payload = target_metadata.parent / payload["path"]
        _copy_immutable(source_payload, target_payload)
        if _sha256(target_payload) != payload["sha256"]:
            raise OgpHarnessError("attempt Dataset payload checksum differs")
    _copy_immutable(source_metadata, target_metadata)
    evidence_relative = preflight_request["dataset_validation_evidence_path"]
    _copy_immutable(
        preflight_workspace / evidence_relative,
        attempt_workspace / evidence_relative,
    )


def prepare_attempt_request(
    preflight_request_path: str | Path,
    attempt_workspace_root: str | Path,
    run_id: str,
    *,
    retry_of: str | None = None,
    retry_reason: str | None = None,
    repository_root: str | Path | None = None,
    materialize_dataset: bool = True,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    if not _STABLE_ID.fullmatch(run_id):
        raise OgpHarnessError("run ID is invalid")
    preflight_path = Path(preflight_request_path).resolve()
    preflight_request = json.loads(preflight_path.read_text(encoding="utf-8"))
    if preflight_request.get("operation") != "preflight":
        raise OgpHarnessError("attempt input is not an immutable preflight request")
    config_path = repository / preflight_request["configuration_ref"]
    config = load_frozen_configuration(config_path, repository_root=repository)
    configuration_checksum = _sha256(config_path)
    normalized_configuration_sha256 = _canonical_digest(config)
    if (
        preflight_request.get("configuration_checksum") != configuration_checksum
        or preflight_request.get("normalized_configuration_sha256")
        != normalized_configuration_sha256
    ):
        raise OgpHarnessError("frozen configuration identity changed after preflight")
    allowed_reasons = set(config["execution"]["allowed_retry_reasons"])
    if (retry_of is None) != (retry_reason is None):
        raise OgpHarnessError("retry identity and retry reason must be supplied together")
    if retry_reason is not None and retry_reason not in allowed_reasons:
        raise OgpHarnessError("retry reason is not an approved infrastructure/external reason")
    if retry_of == run_id:
        raise OgpHarnessError("retry must use a distinct run ID")

    attempt_workspace = Path(attempt_workspace_root).resolve()
    configured_root = _resolve_repository_path(config["execution"]["workspace"], repository)
    if repository_root is None:
        try:
            attempt_workspace.relative_to(configured_root)
        except ValueError as error:
            raise OgpHarnessError("attempt workspace escapes the frozen run root") from error
    for name in ("normal", "temporary", "manifests", "reports", "quarantine", "requests"):
        (attempt_workspace / name).mkdir(parents=True, exist_ok=True)
    request_path = attempt_workspace / "requests" / f"{run_id}.json"
    if request_path.exists():
        raise OgpHarnessError("attempt request already exists")
    if materialize_dataset:
        _materialize_attempt_dataset(preflight_request, attempt_workspace, repository)

    attempt = {"run_id": run_id}
    if retry_of is not None:
        attempt["retry_of"] = retry_of
        attempt["retry_reason"] = retry_reason
    request = {
        "request_version": "1.0.0",
        "request_id": run_id,
        "operation": "execute",
        "study_class": "single_paper_reproduction",
        "reproduction_mode": "faithful_adaptation",
        "replacement_scope": "partition",
        "workspace": {
            "root": _container_path(attempt_workspace, repository),
            "memory_budget_bytes": config["resources"]["memory_budget_bytes"],
            "thread_budget": config["resources"]["thread_budget"],
        },
        "dataset": preflight_request["dataset"],
        "implementations": config["implementations"],
        "stage_parameters": config["stage_parameters"],
        "source": DISKANN_SOURCE,
        "stage_sources": {
            "partition": GPANN_SOURCE,
            "local_build": DISKANN_SOURCE,
            "merge": DISKANN_SOURCE,
        },
        "source_config": {
            "yaml_sha256": configuration_checksum,
            "normalized_json_sha256": normalized_configuration_sha256,
        },
        "frozen_configuration": {
            "path": SIFT1M_CONFIG_REF,
            "sha256": configuration_checksum,
            "normalized_sha256": normalized_configuration_sha256,
        },
        "attempt": attempt,
    }
    try:
        write_immutable_json(request_path, request)
    except ImmutableWriteError as error:
        raise OgpHarnessError(str(error)) from error
    return {
        "request_path": str(request_path),
        "workspace": str(attempt_workspace),
        "run_id": run_id,
        "configuration_checksum": configuration_checksum,
    }


def _resolve_workspace_reference(workspace: Path, reference: str) -> Path:
    path = Path(reference)
    candidate = path if path.is_absolute() else workspace / path
    resolved = candidate.resolve()
    try:
        resolved.relative_to(workspace.resolve())
    except ValueError as error:
        raise OgpHarnessError("runner evidence path escapes attempt workspace") from error
    return resolved


def _validate_successful_attempt(
    result: dict[str, Any], workspace: Path, repository_root: Path
) -> None:
    _schema_validator("runner-result.schema.json", repository_root).validate(result)
    if result.get("outcome") != "SUCCEEDED" or result.get("exit_code") != 0:
        raise OgpHarnessError("SIFT1M attempt did not succeed")
    invocations = result["stage_invocations"]
    if (
        invocations.get("partition") != 1
        or invocations.get("merge") != 1
        or invocations.get("local_build", 0) < 2
    ):
        raise OgpHarnessError("SIFT1M stage invocation counts are incomplete")
    manifest_paths = sorted((workspace / "manifests").glob("*/stages/*.json"))
    if len(manifest_paths) != invocations["local_build"] + 2:
        raise OgpHarnessError("SIFT1M stage BuildManifest set is incomplete")
    artifacts: list[dict[str, Any]] = []
    evidence_paths: set[Path] = set()
    for manifest_path in manifest_paths:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        _schema_validator("build-manifest.schema.json", repository_root).validate(
            manifest
        )
        if manifest["status"] != "SUCCEEDED":
            raise OgpHarnessError("a SIFT1M Stage BuildManifest is not successful")
        artifacts.extend(manifest["output_artifacts"])
        evidence_paths.add(
            _resolve_workspace_reference(
                workspace, manifest["validation_evidence_path"]
            )
        )
    if len(artifacts) != invocations["local_build"] + 2:
        raise OgpHarnessError("SIFT1M Artifact reference set is incomplete")
    schema_for = {
        "partition": "partition-artifact.schema.json",
        "local_graph": "local-graph-artifact.schema.json",
        "global_graph": "global-graph-artifact.schema.json",
    }
    counts = {"partition": 0, "local_graph": 0, "global_graph": 0}
    for reference in artifacts:
        metadata_path = workspace / "normal" / reference["metadata_path"]
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        artifact_type = reference["artifact_type"]
        _schema_validator(schema_for[artifact_type], repository_root).validate(metadata)
        if metadata["aggregate_checksum"] != reference["aggregate_checksum"]:
            raise OgpHarnessError("published Artifact reference checksum differs")
        counts[artifact_type] += 1
    if counts != {
        "partition": 1,
        "local_graph": invocations["local_build"],
        "global_graph": 1,
    }:
        raise OgpHarnessError("published SIFT1M Artifact types are incomplete")
    for evidence_path in evidence_paths:
        evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        _schema_validator("validation-evidence.schema.json", repository_root).validate(
            evidence
        )
        if evidence["result"] != "VALID":
            raise OgpHarnessError("SIFT1M ValidationEvidence is not VALID")
    evaluation_path = _resolve_workspace_reference(
        workspace, result["evaluation_report_path"]
    )
    evaluation = json.loads(evaluation_path.read_text(encoding="utf-8"))
    _schema_validator("evaluation-report.schema.json", repository_root).validate(
        evaluation
    )
    if evaluation.get("evaluator") != {
        "implementation_id": "cpp_main_native_graph_integrity_evaluator",
        "version": "1.0.0",
    } or not evaluation.get("findings", {}).get("passed", False):
        raise OgpHarnessError("native GlobalGraph EvaluationReport is invalid")


def execute_attempt(
    preflight_request_path: str | Path,
    workspace_root: str | Path,
    run_id: str,
    runner: str | Path,
    *,
    retry_of: str | None = None,
    retry_reason: str | None = None,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    repository = (
        Path(repository_root).resolve()
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    prepared = prepare_attempt_request(
        preflight_request_path,
        workspace_root,
        run_id,
        retry_of=retry_of,
        retry_reason=retry_reason,
        repository_root=repository if repository_root is not None else None,
    )
    workspace = Path(prepared["workspace"])
    config = load_frozen_configuration(
        repository / SIFT1M_CONFIG_REF, repository_root=repository
    )
    runner_path = Path(runner).resolve()
    if _repository_relative(runner_path, repository) != config["execution"]["runner"]:
        raise OgpHarnessError("execute runner differs from frozen configuration")
    result_path = workspace / config["outputs"]["attempt_result"]
    stdout_path = workspace / config["outputs"]["runner_stdout"]
    stderr_path = workspace / config["outputs"]["runner_stderr"]
    result_path.parent.mkdir(parents=True, exist_ok=True)
    tmpdir = workspace / "temporary" / "docker-tmp"
    tmpdir.mkdir(parents=True, exist_ok=True)
    gpann_source = _resolve_repository_path(
        config["sources"]["partition"]["source_path"], repository
    )
    linux_build = repository / "build" / "ogp-partition-adaptation" / "linux"
    runtime_git = linux_build / "runtime-tools" / "git"
    if not runtime_git.is_file():
        raise OgpHarnessError("confined Linux Git admission tool is missing")
    runtime_tbb = linux_build / "runtime-tools" / "libtbb.so.12.5"
    if not runtime_tbb.is_file():
        raise OgpHarnessError("confined GP-ANN TBB runtime is missing")
    run_root = repository / "build" / "ogp-partition-adaptation"
    try:
        mountpoint_relative = workspace.relative_to(run_root)
    except ValueError as error:
        raise OgpHarnessError("attempt workspace is outside the frozen run root") from error
    # Docker cannot create a nested bind target below the read-only admitted
    # Linux build mount. Materialize only the empty target directory in that
    # build tree before mounting the retained host attempt over it.
    (linux_build / mountpoint_relative).mkdir(parents=True, exist_ok=True)
    command = [
        "docker",
        "run",
        "--rm",
        "--network",
        "none",
        "--read-only",
        "--pull=never",
        "--mount",
        f"type=bind,src={repository},dst=/workspace,readonly",
        "--mount",
        f"type=bind,src={linux_build},dst=/workspace/build/ogp-partition-adaptation,readonly",
        "--mount",
        f"type=bind,src={gpann_source},dst=/gpann-source,readonly",
        "--mount",
        f"type=bind,src={runtime_git},dst=/usr/local/bin/git,readonly",
        "--mount",
        f"type=bind,src={runtime_tbb},dst=/lib/x86_64-linux-gnu/libtbb.so.12,readonly",
        "--mount",
        f"type=bind,src={workspace},dst={_container_path(workspace, repository)}",
        "--env",
        f"TMPDIR={_container_path(tmpdir, repository)}",
        config["execution"]["container_image"],
        _container_path(runner_path, repository),
        "execute",
        "--request",
        _container_path(Path(prepared["request_path"]), repository),
        "--result",
        _container_path(result_path, repository),
    ]
    completed = subprocess.run(
        command, capture_output=True, text=True, check=False, shell=False
    )
    stdout_path.write_text(completed.stdout, encoding="utf-8")
    stderr_path.write_text(completed.stderr, encoding="utf-8")
    if not result_path.is_file():
        raise OgpHarnessError(
            "OGP runner published no terminal result: "
            + (completed.stderr.strip() or completed.stdout.strip())
        )
    result = json.loads(result_path.read_text(encoding="utf-8"))
    _schema_validator("runner-result.schema.json", repository).validate(result)
    if result.get("exit_code") != completed.returncode:
        raise OgpHarnessError("runner process and terminal result exit status differ")
    _validate_successful_attempt(result, workspace, repository)
    return result


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="ogp_partition_adaptation")
    commands = parser.add_subparsers(dest="command", required=True)
    preflight_parser = commands.add_parser("preflight")
    preflight_parser.add_argument("--config", required=True)
    preflight_parser.add_argument("--workspace", required=True)
    execute_parser = commands.add_parser("execute")
    execute_parser.add_argument("--request", required=True)
    execute_parser.add_argument("--workspace", required=True)
    execute_parser.add_argument("--run-id", required=True)
    execute_parser.add_argument("--runner", required=True)
    execute_parser.add_argument("--retry-of")
    execute_parser.add_argument("--retry-reason")
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_argument_parser().parse_args(argv)
    try:
        if arguments.command == "preflight":
            result = preflight(arguments.config, arguments.workspace)
        else:
            result = execute_attempt(
                arguments.request,
                arguments.workspace,
                arguments.run_id,
                arguments.runner,
                retry_of=arguments.retry_of,
                retry_reason=arguments.retry_reason,
            )
    except (OgpHarnessError, RunnerError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"ogp_partition_adaptation: {error}", file=os.sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
