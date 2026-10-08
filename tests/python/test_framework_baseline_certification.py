from __future__ import annotations

import copy
import json
import hashlib
import struct
import subprocess
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from diskann_harness import framework_baseline_certification as fbc
from diskann_harness import diskann_search_evaluation as search
from diskann_harness.checksum import aggregate_checksum
from diskann_harness.framework_baseline_certification import (
    FeatureCertificationError,
    SCHEMA_FILENAMES,
    build_argument_parser,
    canonical_document_checksum,
    load_feature_schema,
    resolve_feature_output,
    verify_document_checksum,
    write_verified_document,
)


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

FIXTURE_DATASET_IDENTITY = {
    "dataset_id": "reference-build-fixture-v1",
    "dataset_checksum": "1" * 64,
    "global_node_count": 6,
    "vector_dimension": 2,
    "scalar_type": "float32",
    "distance_metric": "l2",
}

FIXTURE_CMAKE_DEFINITIONS = [
    "BUILD_TESTING=ON",
    "CMAKE_BUILD_TYPE=Release",
    "CMAKE_EXPORT_COMPILE_COMMANDS=ON",
    "DISKANN_CPP_MAIN_BASELINE=ON",
    "DISKANN_SEARCH_EVALUATION=ON",
]


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _file_identity(path: Path) -> dict[str, object]:
    return {
        "path": str(path.resolve()),
        "byte_length": path.stat().st_size,
        "sha256": _sha256(path),
    }


def _write_build_manifest_fixture(
    workspace: Path,
    *,
    name: str,
    target: str,
    source_role: str,
    implementation_id: str,
    source: dict[str, object],
    binary: Path,
) -> Path:
    build_root = workspace / "clean-build"
    cache = build_root / "CMakeCache.txt"
    compile_commands = build_root / "compile_commands.json"
    link_relative = (
        "baseline/cpp_main/CMakeFiles/native_reference_capture_runner.dir/link.txt"
        if target == "native_reference_capture_runner"
        else "cpp-main-upstream/apps/CMakeFiles/build_disk_index.dir/link.txt"
    )
    link_command = build_root / link_relative
    cache.parent.mkdir(parents=True, exist_ok=True)
    if not cache.exists():
        cache.write_text(
            "\n".join(
                [
                    "CMAKE_BUILD_TYPE:STRING=Release",
                    "CMAKE_COMMAND:INTERNAL=/usr/bin/cmake",
                    "CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3",
                    "CMAKE_CACHE_MINOR_VERSION:INTERNAL=22",
                    "CMAKE_CACHE_PATCH_VERSION:INTERNAL=1",
                    "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++",
                    "CMAKE_GENERATOR:INTERNAL=Unix Makefiles",
                    "BUILD_TESTING:BOOL=ON",
                    "DISKANN_CPP_MAIN_BASELINE:BOOL=ON",
                    "DISKANN_SEARCH_EVALUATION:BOOL=ON",
                    "CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        owner_source = (
            REPOSITORY_ROOT
            / "src/baseline/cpp_main/native_reference_capture_runner.cpp"
        )
        native_source = (
            REPOSITORY_ROOT / "upstream/diskann-cpp-main/apps/build_disk_index.cpp"
        )
        compile_commands.write_text(
            json.dumps(
                [
                    {
                        "directory": str(build_root),
                        "command": (
                            f"/usr/bin/c++ -O3 -DNDEBUG -std=c++17 "
                            f"-c {owner_source}"
                        ),
                        "file": str(owner_source),
                    },
                    {
                        "directory": str(build_root),
                        "command": (
                            f"/usr/bin/c++ -O3 -DNDEBUG -mavx2 -fopenmp "
                            f"-std=gnu++17 -c {native_source}"
                        ),
                        "file": str(native_source),
                    },
                ]
            ),
            encoding="utf-8",
        )
    compile_command = (
        f"/usr/bin/c++ -O3 -DNDEBUG -std=c++17 -c "
        f"{REPOSITORY_ROOT / 'src/baseline/cpp_main/native_reference_capture_runner.cpp'}"
        if target == "native_reference_capture_runner"
        else f"/usr/bin/c++ -O3 -DNDEBUG -mavx2 -fopenmp -std=gnu++17 -c "
        f"{REPOSITORY_ROOT / 'upstream/diskann-cpp-main/apps/build_disk_index.cpp'}"
    )
    link_command.parent.mkdir(parents=True, exist_ok=True)
    link_command.write_text(
        f"/usr/bin/c++ -O3 -DNDEBUG -fopenmp {target}.o -o {binary}\n",
        encoding="utf-8",
    )
    manifest = {
        "manifest_version": "1.0.0",
        "run_id": f"{name}-build",
        "stage": "build",
        "implementation": {
            "implementation_id": implementation_id,
            "implementation_version": "1.0.0",
        },
        "source": source,
        "toolchain": {
            "os": "Linux-x86_64",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": "11.4.0",
            "cmake_version": "3.22.1",
            "generator": "Unix Makefiles",
            "build_type": "Release",
            "cmake_definitions": FIXTURE_CMAKE_DEFINITIONS,
            "compile_commands": [compile_command],
            "link_commands": [
                f"/usr/bin/c++ -O3 -DNDEBUG -fopenmp {target}.o -o {binary}"
            ],
        },
        "dataset": FIXTURE_DATASET_IDENTITY,
        "parameters": {
            "build_closure": {
                "source_role": source_role,
                "target": target,
                "configure_command": (
                    f"cmake -S {REPOSITORY_ROOT} -B {build_root}"
                ),
                "build_command": (
                    f"cmake --build {build_root} --target {target} --parallel 6"
                ),
                "configure_started_at": "2026-08-26T00:00:00Z",
                "configure_ended_at": "2026-08-26T00:00:01Z",
                "binary": _file_identity(binary),
                "evidence": {
                    "cmake_cache": _file_identity(cache),
                    "compile_commands": _file_identity(compile_commands),
                    "link_command": _file_identity(link_command),
                },
                "applicable_flags": (
                    ["-O3", "-DNDEBUG", "-std=c++17", "-fopenmp"]
                    if target == "native_reference_capture_runner"
                    else [
                        "-O3",
                        "-DNDEBUG",
                        "-std=gnu++17",
                        "-mavx2",
                        "-fopenmp",
                    ]
                ),
            }
        },
        "resource_budget": {
            "memory_budget_bytes": 1024,
            "thread_budget": 6,
        },
        "input_artifacts": [],
        "output_artifacts": [],
        "status": "SUCCEEDED",
        "started_at": "2026-08-26T00:00:01Z",
        "ended_at": "2026-08-26T00:00:02Z",
    }
    path = workspace / "manifests" / "build" / f"{name}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(manifest, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    return path


def _reference_request_fixture(tmp_path: Path) -> tuple[Path, dict[str, object]]:
    workspace = tmp_path / "workspace"
    workspace.mkdir()
    owner = workspace / "clean-build/src/baseline/cpp_main/native_reference_capture_runner"
    native = workspace / "clean-build/cpp-main-upstream/apps/build_disk_index"
    owner.parent.mkdir(parents=True)
    native.parent.mkdir(parents=True)
    owner.write_bytes(b"owner-fixture")
    native.write_bytes(b"native-fixture")
    owner.chmod(0o755)
    native.chmod(0o755)
    project_source = {
        "repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
        "branch": "feature/framework-baseline-certification",
        "commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
        "dirty_worktree": False,
    }
    upstream_source = {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": "78256bbab4685e1774e78d331e081a153be26823",
        "dirty_worktree": False,
    }
    owner_manifest = _write_build_manifest_fixture(
        workspace,
        name="owner",
        target="native_reference_capture_runner",
        source_role="project_capture_owner",
        implementation_id="framework_baseline_native_reference_capture_v1",
        source=project_source,
        binary=owner,
    )
    native_manifest = _write_build_manifest_fixture(
        workspace,
        name="native",
        target="build_disk_index",
        source_role="fixed_upstream_build_disk_index",
        implementation_id="cpp_main_build_disk_index_native_v1",
        source=upstream_source,
        binary=native,
    )
    evidence = workspace / "manifests" / "evidence" / "dataset.json"
    evidence.parent.mkdir(parents=True)
    evidence.write_text("{}", encoding="utf-8")
    profile = (
        REPOSITORY_ROOT
        / "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml"
    )
    request: dict[str, object] = {
        "request_version": "native-reference-build-request/v1",
        "run_id": "native-reference-fixture",
        "study_class": "baseline_validation",
        "reference_role": "cpp_main_native_sharded_reference_v1",
        "reproduction_mode": "upstream_faithful",
        "execution_authorized": False,
        "workspace_root": str(workspace.resolve()),
        "source": {
            "repository": "https://github.com/microsoft/DiskANN.git",
            "branch": "cpp_main",
            "commit": "78256bbab4685e1774e78d331e081a153be26823",
            "source_path": "upstream/diskann-cpp-main",
            "dirty_worktree": False,
        },
        "project_operation_owner": {
            "implementation_id": "framework_baseline_native_reference_capture_v1",
            "implementation_version": "1.0.0",
            **project_source,
            "binary": {
                "implementation_id": "framework_baseline_native_reference_capture_v1",
                "path": str(owner.resolve()),
                "byte_length": owner.stat().st_size,
                "sha256": _sha256(owner),
                "build_manifest": {
                    "path": owner_manifest.relative_to(workspace).as_posix(),
                    "byte_length": owner_manifest.stat().st_size,
                    "sha256": _sha256(owner_manifest),
                },
            },
        },
        "native_binary": {
            "implementation_id": "cpp_main_build_disk_index_native_v1",
            "path": str(native.resolve()),
            "byte_length": native.stat().st_size,
            "sha256": _sha256(native),
            "build_manifest": {
                "path": native_manifest.relative_to(workspace).as_posix(),
                "byte_length": native_manifest.stat().st_size,
                "sha256": _sha256(native_manifest),
            },
        },
        "dataset_artifact": {
            "metadata_path": "datasets/fixture/artifact.json",
            "artifact_type": "dataset",
            "format_version": "dataset-artifact/v1",
            "aggregate_checksum": "1" * 64,
        },
        "dataset_validation_evidence": {
            "path": evidence.relative_to(workspace).as_posix(),
            "byte_length": evidence.stat().st_size,
            "sha256": _sha256(evidence),
        },
        "profile": {
            "path": "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml",
            "sha256": _sha256(profile),
            "status": "draft",
        },
        "controls": {
            "data_type": "float",
            "distance_metric": "l2",
            "branch_mode": "sharded",
            "R": 16,
            "L": 32,
            "B": 0.03,
            "build_DRAM_budget_gib": 0.6,
            "num_threads": 6,
            "QD": 0,
            "disk_PQ_bytes": 0,
            "build_PQ_bytes": 0,
            "opq": False,
            "labels": False,
        },
        "capture": {
            "mechanism": "prearmed_inotify_same_filesystem_hardlink",
            "source_graph_name": "fixture_mem.index",
            "source_medoids_name": "fixture_disk.index_medoids.bin",
            "captured_graph_path": "normal/reference/global.index",
            "captured_medoids_path": "normal/reference/medoids.bin",
            "same_filesystem_required": True,
            "copy_fallback_allowed": False,
            "exclusive_create": True,
        },
        "resource_budget": {
            "memory_budget_bytes": 1024,
            "thread_budget": 6,
            "disk_budget_bytes": 2048,
        },
        "outputs": {
            "artifact_metadata_path": "normal/reference/artifact.json",
            "build_manifest_path": "manifests/reference.json",
            "validation_evidence_path": "manifests/evidence/reference.json",
            "stdout_path": "reports/reference.stdout",
            "stderr_path": "reports/reference.stderr",
            "observer_evidence_path": "reports/reference-observer.json",
            "structural_projection_path": "reports/reference-structural.json",
        },
    }
    return workspace, request


def _write_dataset_admission_fixture(
    workspace: Path,
    request: dict[str, object],
    *,
    format_version: str,
    dataset_checksum: str | None = None,
) -> tuple[dict[str, object], Path, Path]:
    metadata_relative = "datasets/fixture/artifact.json"
    metadata_path = workspace / "normal" / metadata_relative
    metadata_path.parent.mkdir(parents=True, exist_ok=True)

    if format_version == "fake-dataset-json/v1":
        payload_bytes = b'{"vectors":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]}'
        payload_name = "vectors.json"
        payload_sha = _sha256_bytes(payload_bytes)
        dataset = {
            "dataset_id": "fake-six-node-v1",
            "dataset_checksum": payload_sha,
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        }
        structural = {
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        }
        capabilities = ["dataset.vector_access/v1"]
        media_type = "application/vnd.diskann.fake-vectors+json;version=1"
        aggregate = "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d"
    else:
        payload_bytes = struct.pack("<II2f", 1, 2, 1.0, 2.0)
        payload_name = "vectors.bin"
        payload_sha = _sha256_bytes(payload_bytes)
        dataset = {
            "dataset_id": "native-dataset-fixture-v1",
            "dataset_checksum": dataset_checksum or payload_sha,
            "global_node_count": 1,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        }
        structural = {
            "global_node_count": 1,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        }
        capabilities = ["dataset.diskann_float32_bin/v1"]
        media_type = "application/vnd.diskann.cpp-main.float32-bin;version=1"
        aggregate = "a" * 64

    payload_path = metadata_path.parent / payload_name
    payload_path.write_bytes(payload_bytes)
    metadata: dict[str, object] = {
        "schema_version": "1.0.0",
        "canonicalization": "diskann-artifact-c14n-v1",
        "artifact_type": "dataset",
        "format_version": format_version,
        "dataset": dataset,
        "id_space": "none",
        "structural": structural,
        "capabilities": capabilities,
        "source_artifacts": [],
        "payloads": [
            {
                "role": "vectors",
                "path": payload_name,
                "media_type": media_type,
                "byte_length": len(payload_bytes),
                "sha256": payload_sha,
            }
        ],
        "aggregate_checksum": aggregate,
    }
    metadata_path.write_text(
        json.dumps(metadata, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    request["dataset_artifact"] = {
        "metadata_path": metadata_relative,
        "artifact_type": "dataset",
        "format_version": format_version,
        "aggregate_checksum": aggregate,
    }

    evidence_path = workspace / "manifests/evidence/dataset.json"
    evidence_path.parent.mkdir(parents=True, exist_ok=True)
    evidence = {
        "evidence_version": "1.0.0",
        "validator_version": "1.0.0",
        "artifact_candidate": {
            "artifact_type": "dataset",
            "format_version": format_version,
            "metadata_path": metadata_relative,
            "claimed_aggregate_checksum": aggregate,
        },
        "checks": [
            {
                "check_id": "dataset_fixture",
                "passed": True,
                "message": "small Dataset fixture is valid",
            }
        ],
        "result": "VALID",
    }
    evidence_path.write_text(
        json.dumps(evidence, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    request["dataset_validation_evidence"] = {
        "path": evidence_path.relative_to(workspace).as_posix(),
        "byte_length": evidence_path.stat().st_size,
        "sha256": _sha256(evidence_path),
    }
    return metadata, payload_path, evidence_path


def _refresh_dataset_evidence_reference(
    workspace: Path, request: dict[str, object], evidence_path: Path
) -> None:
    request["dataset_validation_evidence"] = {
        "path": evidence_path.relative_to(workspace).as_posix(),
        "byte_length": evidence_path.stat().st_size,
        "sha256": _sha256(evidence_path),
    }


def _bind_build_manifest_dataset(
    workspace: Path, request: dict[str, object], dataset: dict[str, object]
) -> None:
    for binary_key in ("project_operation_owner", "native_binary"):
        binary = request[binary_key]
        if binary_key == "project_operation_owner":
            binary = binary["binary"]
        reference = binary["build_manifest"]
        path = workspace / reference["path"]
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["dataset"] = dataset
        path.write_text(
            json.dumps(manifest, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        binary["build_manifest"] = {
            "path": path.relative_to(workspace).as_posix(),
            "byte_length": path.stat().st_size,
            "sha256": _sha256(path),
        }


_COMPLETE_NATIVE_STAGE_STDOUT = (
    "Time for partitioning data : 0.100000 seconds\n"
    "Time for building indices on shards: 0.200000 seconds\n"
    "Time for merging indices: 0.300000 seconds\n"
)


def _artifact_aggregate(metadata: dict[str, object]) -> str:
    return aggregate_checksum(
        {
            "schema_version": metadata["schema_version"],
            "canonicalization": metadata["canonicalization"],
            "artifact_type": metadata["artifact_type"],
            "format_version": metadata["format_version"],
            "dataset_checksum": metadata["dataset"]["dataset_checksum"],
            "id_space": metadata["id_space"],
            "capabilities": metadata["capabilities"],
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


def _write_cpp_native_closure_fixture(
    workspace: Path,
    request: dict[str, object],
    request_path: Path,
    *,
    status: str = "SUCCEEDED",
    stdout: str = _COMPLETE_NATIVE_STAGE_STDOUT,
    stderr: str = "native-child-stderr\n",
    exit_code: int = 0,
) -> None:
    outputs = request["outputs"]
    capture = request["capture"]
    stdout_path = workspace / outputs["stdout_path"]
    stderr_path = workspace / outputs["stderr_path"]
    observer_path = workspace / outputs["observer_evidence_path"]
    manifest_path = workspace / outputs["build_manifest_path"]
    for path in (stdout_path, stderr_path, observer_path, manifest_path):
        path.parent.mkdir(parents=True, exist_ok=True)
    stdout_path.write_bytes(stdout.encode("utf-8"))
    stderr_path.write_bytes(stderr.encode("utf-8"))
    observer = {
        "run_id": request["run_id"],
        "observer_mechanism": capture["mechanism"],
        "observer_armed": True,
        "observer_ready_at": "2026-08-27T00:00:01Z",
        "child_launched_at": "2026-08-27T00:00:02Z",
        "native_child_argv": [request["native_binary"]["path"], "--fixture"],
        "source_graph_name": capture["source_graph_name"],
        "captured_graph_path": capture["captured_graph_path"],
        "child_exit_code": exit_code,
        "exact_graph_event_observed": status == "SUCCEEDED",
        "graph_hard_link_created": status == "SUCCEEDED",
        "capture_started_at": "2026-08-27T00:00:00Z",
        "capture_ended_at": "2026-08-27T00:00:03Z",
        "terminal_result": status,
    }
    if status != "SUCCEEDED":
        observer["failure_reason"] = "fixture native child failed"
    observer_path.write_text(
        json.dumps(observer, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )

    def relative_identity(path: Path, *, stream_role: str | None = None) -> dict[str, object]:
        value: dict[str, object] = {
            "path": path.relative_to(workspace).as_posix(),
            "byte_length": path.stat().st_size,
            "sha256": _sha256(path),
        }
        if stream_role is not None:
            value["stream_role"] = stream_role
        return value

    native_manifest_path = workspace / request["native_binary"]["build_manifest"]["path"]
    native_manifest = json.loads(native_manifest_path.read_text(encoding="utf-8"))
    admitted = {
        "source": {
            key: request["source"][key]
            for key in ("repository", "branch", "commit", "dirty_worktree")
        },
        "gitlink_commit": request["source"]["commit"],
        "upstream_source_dir": str(REPOSITORY_ROOT / "upstream/diskann-cpp-main"),
        "upstream_binary_dir": str(workspace / "clean-build/cpp-main-upstream"),
        "upstream_library": {
            "path": str(workspace / "clean-build/cpp-main-upstream/src/libdiskann_s.a"),
            "sha256": "9" * 64,
        },
        "build": native_manifest["toolchain"],
    }
    parameters = {
        "reference_role": request["reference_role"],
        "request": {
            "path": str(request_path.resolve()),
            "byte_length": request_path.stat().st_size,
            "sha256": _sha256(request_path),
        },
        "project_operation_owner": request["project_operation_owner"],
        "native_binary": request["native_binary"],
        "profile": request["profile"],
        "controls": request["controls"],
        "capture": request["capture"],
        "native_child_argv": observer["native_child_argv"],
        "upstream_admission": {
            "pre": admitted,
            "post": copy.deepcopy(admitted),
            "consistent": True,
        },
        "native_child_logs": {
            "stdout": relative_identity(stdout_path, stream_role="stdout"),
            "stderr": relative_identity(stderr_path, stream_role="stderr"),
        },
        "observer_evidence": relative_identity(observer_path),
    }
    manifest: dict[str, object] = {
        "manifest_version": "1.0.0",
        "run_id": request["run_id"],
        "stage": "build",
        "implementation": {
            "implementation_id": "framework_baseline_native_reference_capture_v1",
            "implementation_version": "1.0.0",
        },
        "source": admitted["source"],
        "toolchain": native_manifest["toolchain"],
        "dataset": FIXTURE_DATASET_IDENTITY,
        "parameters": parameters,
        "resource_budget": {
            "memory_budget_bytes": request["resource_budget"]["memory_budget_bytes"],
            "thread_budget": request["resource_budget"]["thread_budget"],
        },
        "input_artifacts": [request["dataset_artifact"]],
        "output_artifacts": [],
        "status": status,
        "started_at": "2026-08-27T00:00:00Z",
        "ended_at": "2026-08-27T00:00:04Z",
    }
    if status == "SUCCEEDED":
        graph = workspace / capture["captured_graph_path"]
        medoids = workspace / capture["captured_medoids_path"]
        graph.parent.mkdir(parents=True, exist_ok=True)
        graph.write_bytes(b"fixture-native-global-graph")
        medoids.write_bytes(b"fixture-native-medoids")
        artifact_path = workspace / outputs["artifact_metadata_path"]
        artifact_path.parent.mkdir(parents=True, exist_ok=True)
        source_artifact = dict(request["dataset_artifact"])
        source_artifact.pop("metadata_path")
        metadata: dict[str, object] = {
            "schema_version": "1.0.0",
            "canonicalization": "diskann-artifact-c14n-v1",
            "artifact_type": "global_graph",
            "format_version": "global-graph-artifact/v2",
            "dataset": FIXTURE_DATASET_IDENTITY,
            "id_space": "global",
            "structural": {
                "lineage_mode": "direct_native_final",
                "global_node_count": FIXTURE_DATASET_IDENTITY["global_node_count"],
                "declared_max_degree": 16,
                "observed_max_degree": 2,
                "partition_count": 2,
                "medoid_count": 2,
            },
            "capabilities": ["graph.global_adjacency/v1", "graph.medoids/v1"],
            "source_artifacts": [source_artifact],
            "payloads": [
                {
                    "role": "global_graph",
                    "path": graph.relative_to(artifact_path.parent).as_posix(),
                    "media_type": "application/vnd.diskann.cpp-main.global-graph;version=1",
                    "byte_length": graph.stat().st_size,
                    "sha256": _sha256(graph),
                },
                {
                    "role": "medoids",
                    "path": medoids.relative_to(artifact_path.parent).as_posix(),
                    "media_type": "application/vnd.diskann.cpp-main.uint32-medoids;version=1",
                    "byte_length": medoids.stat().st_size,
                    "sha256": _sha256(medoids),
                },
            ],
        }
        metadata["aggregate_checksum"] = _artifact_aggregate(metadata)
        artifact_path.write_text(
            json.dumps(metadata, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        artifact_ref = {
            "metadata_path": artifact_path.relative_to(workspace / "normal").as_posix(),
            "artifact_type": "global_graph",
            "format_version": "global-graph-artifact/v2",
            "aggregate_checksum": metadata["aggregate_checksum"],
        }
        evidence_path = workspace / outputs["validation_evidence_path"]
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(
            json.dumps(
                {
                    "evidence_version": "1.0.0",
                    "validator_version": "artifact-contract-validator/1.1.0",
                    "artifact_candidate": {
                        "artifact_type": "global_graph",
                        "format_version": "global-graph-artifact/v2",
                        "metadata_path": artifact_ref["metadata_path"],
                        "claimed_aggregate_checksum": metadata["aggregate_checksum"],
                    },
                    "checks": [
                        {
                            "check_id": "artifact.contract/v2",
                            "passed": True,
                            "message": "fixture direct native final graph is valid",
                        }
                    ],
                    "result": "VALID",
                },
                sort_keys=True,
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
        manifest["output_artifacts"] = [artifact_ref]
        manifest["validation_evidence_path"] = outputs["validation_evidence_path"]
    else:
        manifest["failure"] = {
            "category": "native_child_failed",
            "reason": "fixture native child failed",
            "exit_code": exit_code,
        }
    manifest_path.write_text(
        json.dumps(manifest, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )


def _write_fake_cmake_evidence(repository: Path, build: Path) -> None:
    build.mkdir(parents=True, exist_ok=True)
    (build / "CMakeCache.txt").write_text(
        "\n".join(
            [
                "CMAKE_BUILD_TYPE:STRING=Release",
                "CMAKE_COMMAND:INTERNAL=/usr/bin/cmake",
                "CMAKE_CACHE_MAJOR_VERSION:INTERNAL=3",
                "CMAKE_CACHE_MINOR_VERSION:INTERNAL=22",
                "CMAKE_CACHE_PATCH_VERSION:INTERNAL=1",
                "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++",
                "CMAKE_GENERATOR:INTERNAL=Unix Makefiles",
                "BUILD_TESTING:BOOL=ON",
                "DISKANN_CPP_MAIN_BASELINE:BOOL=ON",
                "DISKANN_SEARCH_EVALUATION:BOOL=ON",
                "CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON",
            ]
        )
        + "\n",
        encoding="utf-8",
    )
    owner_source = repository / "src/baseline/cpp_main/native_reference_capture_runner.cpp"
    native_source = repository / "upstream/diskann-cpp-main/apps/build_disk_index.cpp"
    (build / "compile_commands.json").write_text(
        json.dumps(
            [
                {
                    "directory": str(build),
                    "command": f"/usr/bin/c++ -O3 -DNDEBUG -std=c++17 -c {owner_source}",
                    "file": str(owner_source),
                },
                {
                    "directory": str(build),
                    "command": (
                        f"/usr/bin/c++ -O3 -DNDEBUG -mavx2 -fopenmp "
                        f"-std=gnu++17 -c {native_source}"
                    ),
                    "file": str(native_source),
                },
            ]
        ),
        encoding="utf-8",
    )
    links = {
        "baseline/cpp_main/CMakeFiles/native_reference_capture_runner.dir/link.txt": (
            "/usr/bin/c++ -O3 -DNDEBUG -fopenmp owner.o -o "
            "../../src/baseline/cpp_main/native_reference_capture_runner\n"
        ),
        "cpp-main-upstream/apps/CMakeFiles/build_disk_index.dir/link.txt": (
            "/usr/bin/c++ -O3 -DNDEBUG -mavx2 -fopenmp native.o -o build_disk_index\n"
        ),
    }
    for relative, command in links.items():
        path = build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(command, encoding="utf-8")


def _fake_source_identity(repository: Path, project: Path) -> dict[str, object]:
    if repository == project:
        return {
            "repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
            "branch": "feature/framework-baseline-certification",
            "commit": "a" * 40,
            "dirty_worktree": False,
        }
    return {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": "78256bbab4685e1774e78d331e081a153be26823",
        "dirty_worktree": False,
    }


def test_feature_local_schema_loader_is_bounded_and_strict() -> None:
    assert SCHEMA_FILENAMES == {
        "global-graph-artifact-v2.schema.json",
        "native-reference-build-request.schema.json",
        "quality-parity-policy.schema.json",
        "quality-parity-evidence.schema.json",
        "framework-baseline-certification-decision.schema.json",
    }
    for name in SCHEMA_FILENAMES:
        schema, validator = load_feature_schema(REPOSITORY_ROOT, name)
        assert schema["additionalProperties"] is False
        assert validator.schema is schema

    with pytest.raises(FeatureCertificationError, match="not admitted"):
        load_feature_schema(REPOSITORY_ROOT, "search-protocol.schema.json")


def test_canonical_checksum_verification_ignores_only_its_own_field() -> None:
    document = {"identity": "fixture", "items": [2, 1]}
    checksum = canonical_document_checksum(document, "document_checksum")
    sealed = {**document, "document_checksum": checksum}
    verify_document_checksum(sealed, "document_checksum")

    with pytest.raises(FeatureCertificationError, match="checksum"):
        verify_document_checksum({**sealed, "identity": "changed"}, "document_checksum")


def test_confined_exclusive_write_rereads_and_rejects_existing_output(
    tmp_path: Path,
) -> None:
    target = resolve_feature_output(tmp_path, "reports/result.json")
    document = {"identity": "fixture", "document_checksum": ""}
    document["document_checksum"] = canonical_document_checksum(
        document, "document_checksum"
    )
    written = write_verified_document(
        tmp_path,
        "reports/result.json",
        document,
        checksum_field="document_checksum",
    )
    assert written == target
    assert json.loads(target.read_text(encoding="utf-8")) == document

    with pytest.raises(FeatureCertificationError, match="already exists"):
        write_verified_document(
            tmp_path,
            "reports/result.json",
            document,
            checksum_field="document_checksum",
        )
    with pytest.raises(FeatureCertificationError, match="workspace"):
        resolve_feature_output(tmp_path, "../escape.json")
    with pytest.raises(FeatureCertificationError, match="workspace"):
        resolve_feature_output(tmp_path, target)


def test_cli_exposes_exactly_the_six_feature_operations() -> None:
    parser = build_argument_parser()
    subparsers = next(
        action for action in parser._actions if getattr(action, "choices", None)
    )
    assert set(subparsers.choices) == {
        "reference-preflight",
        "reference-execute",
        "derive-structural",
        "freeze-quality-policy",
        "compare-quality",
        "decide",
    }
    assert {"validate", "certify", "protocol"}.isdisjoint(subparsers.choices)


def test_reference_lifecycle_has_one_preflight_and_execution_owner() -> None:
    assert callable(fbc.reference_preflight)
    assert callable(fbc.reference_execute)


def test_reference_binary_build_closure_writes_two_independent_terminal_manifests(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    repository = REPOSITORY_ROOT
    nlohmann = tmp_path / "nlohmann"
    nlohmann.mkdir()
    build = tmp_path / "clean-build"
    workspace = tmp_path / "manifest-workspace"
    workspace.mkdir()
    owner_manifest = workspace / "manifests/build/owner.json"
    native_manifest = workspace / "manifests/build/native.json"
    observed: list[list[str]] = []

    def fake_run(
        argv: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        observed.append(argv)
        assert kwargs["shell"] is False
        if argv[:3] == ["cmake", "-S", str(repository)]:
            assert not owner_manifest.exists()
            assert not native_manifest.exists()
            _write_fake_cmake_evidence(repository, build)
            return subprocess.CompletedProcess(argv, 0, "configured", "")
        if argv == ["/usr/bin/c++", "--version"]:
            return subprocess.CompletedProcess(argv, 0, "g++ 11.4.0\n", "")
        if argv == ["/usr/bin/cmake", "--version"]:
            return subprocess.CompletedProcess(argv, 0, "cmake version 3.22.1\n", "")
        if argv[:3] == ["cmake", "--build", str(build)]:
            target = argv[argv.index("--target") + 1]
            if target == "native_reference_capture_runner":
                assert not owner_manifest.exists()
                binary = build / "src/baseline/cpp_main/native_reference_capture_runner"
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b"clean-owner-binary")
                binary.chmod(0o755)
            elif target == "build_disk_index":
                assert owner_manifest.is_file()
                assert not native_manifest.exists()
                binary = build / "cpp-main-upstream/apps/build_disk_index"
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b"clean-native-binary")
                binary.chmod(0o755)
            else:  # pragma: no cover - fixed target contract
                raise AssertionError(f"unexpected target: {target}")
            return subprocess.CompletedProcess(argv, 0, f"built {target}", "")
        raise AssertionError(f"unexpected process invocation: {argv}")

    timestamps = iter(
        [
            "2026-08-26T00:00:00Z",
            "2026-08-26T00:00:01Z",
            "2026-08-26T00:00:02Z",
            "2026-08-26T00:00:03Z",
            "2026-08-26T00:00:04Z",
            "2026-08-26T00:00:05Z",
        ]
    )
    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    monkeypatch.setattr(
        fbc,
        "_source_identity",
        lambda path, **_: _fake_source_identity(Path(path), repository),
        raising=False,
    )
    monkeypatch.setattr(fbc, "_utc_now", lambda: next(timestamps), raising=False)

    result = fbc._build_reference_binary_closure(
        repository_root=repository,
        build_dir=build,
        manifest_workspace=workspace,
        owner_manifest_path="manifests/build/owner.json",
        native_manifest_path="manifests/build/native.json",
        dataset=FIXTURE_DATASET_IDENTITY,
        nlohmann_source=nlohmann,
        memory_budget_bytes=4096,
        parallel=6,
    )

    assert set(result) == {"project_capture_owner", "fixed_upstream_build_disk_index"}
    validator = fbc._runtime_validator(REPOSITORY_ROOT, "build-manifest.schema.json")
    owner = json.loads(owner_manifest.read_text(encoding="utf-8"))
    native = json.loads(native_manifest.read_text(encoding="utf-8"))
    validator.validate(owner)
    validator.validate(native)
    assert owner["status"] == native["status"] == "SUCCEEDED"
    assert owner["source"]["repository"].endswith("diskann_search_benchmark.git")
    assert native["source"] == {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": "78256bbab4685e1774e78d331e081a153be26823",
        "dirty_worktree": False,
    }
    assert owner["started_at"] == "2026-08-26T00:00:02Z"
    assert owner["ended_at"] == "2026-08-26T00:00:03Z"
    assert native["started_at"] == "2026-08-26T00:00:04Z"
    assert native["ended_at"] == "2026-08-26T00:00:05Z"
    for manifest, target in (
        (owner, "native_reference_capture_runner"),
        (native, "build_disk_index"),
    ):
        closure = manifest["parameters"]["build_closure"]
        assert closure["target"] == target
        assert closure["binary"]["sha256"] == _sha256(Path(closure["binary"]["path"]))
        assert manifest["toolchain"]["build_type"] == "Release"
        assert manifest["toolchain"]["compile_commands"]
        assert manifest["toolchain"]["link_commands"]
        assert {"-O3", "-DNDEBUG"}.issubset(closure["applicable_flags"])
        for evidence in closure["evidence"].values():
            evidence_path = Path(evidence["path"])
            assert evidence["byte_length"] == evidence_path.stat().st_size
            assert evidence["sha256"] == _sha256(evidence_path)
    built_targets = [
        call[call.index("--target") + 1]
        for call in observed
        if "--target" in call
    ]
    assert built_targets == ["native_reference_capture_runner", "build_disk_index"]


def test_reference_binary_build_closure_retains_failed_terminal_manifest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    repository = REPOSITORY_ROOT
    nlohmann = tmp_path / "nlohmann"
    nlohmann.mkdir()
    build = tmp_path / "clean-build"
    workspace = tmp_path / "manifest-workspace"
    workspace.mkdir()
    owner_manifest = workspace / "manifests/build/owner.json"
    native_manifest = workspace / "manifests/build/native.json"

    def fake_run(
        argv: list[str], **kwargs: object
    ) -> subprocess.CompletedProcess[str]:
        assert kwargs["shell"] is False
        if argv[:3] == ["cmake", "-S", str(repository)]:
            _write_fake_cmake_evidence(repository, build)
            return subprocess.CompletedProcess(argv, 0, "configured", "")
        if argv == ["/usr/bin/c++", "--version"]:
            return subprocess.CompletedProcess(argv, 0, "g++ 11.4.0\n", "")
        if argv == ["/usr/bin/cmake", "--version"]:
            return subprocess.CompletedProcess(argv, 0, "cmake version 3.22.1\n", "")
        if argv[:3] == ["cmake", "--build", str(build)]:
            target = argv[argv.index("--target") + 1]
            if target == "native_reference_capture_runner":
                binary = build / "src/baseline/cpp_main/native_reference_capture_runner"
                binary.parent.mkdir(parents=True, exist_ok=True)
                binary.write_bytes(b"clean-owner-binary")
                binary.chmod(0o755)
                return subprocess.CompletedProcess(argv, 0, "built owner", "")
            assert not native_manifest.exists()
            return subprocess.CompletedProcess(argv, 17, "", "fixture target failed")
        raise AssertionError(f"unexpected process invocation: {argv}")

    timestamps = iter(
        [
            "2026-08-26T00:00:00Z",
            "2026-08-26T00:00:01Z",
            "2026-08-26T00:00:02Z",
            "2026-08-26T00:00:03Z",
            "2026-08-26T00:00:04Z",
            "2026-08-26T00:00:05Z",
        ]
    )
    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    monkeypatch.setattr(
        fbc,
        "_source_identity",
        lambda path, **_: _fake_source_identity(Path(path), repository),
        raising=False,
    )
    monkeypatch.setattr(fbc, "_utc_now", lambda: next(timestamps), raising=False)

    with pytest.raises(FeatureCertificationError, match="target build closure failed"):
        fbc._build_reference_binary_closure(
            repository_root=repository,
            build_dir=build,
            manifest_workspace=workspace,
            owner_manifest_path="manifests/build/owner.json",
            native_manifest_path="manifests/build/native.json",
            dataset=FIXTURE_DATASET_IDENTITY,
            nlohmann_source=nlohmann,
            memory_budget_bytes=4096,
            parallel=6,
        )

    owner = json.loads(owner_manifest.read_text(encoding="utf-8"))
    native = json.loads(native_manifest.read_text(encoding="utf-8"))
    validator = fbc._runtime_validator(REPOSITORY_ROOT, "build-manifest.schema.json")
    validator.validate(owner)
    validator.validate(native)
    assert owner["status"] == "SUCCEEDED"
    assert native["status"] == "FAILED"
    assert native["failure"] == {
        "category": "target_build_failed",
        "reason": "fixture target failed",
        "exit_code": 17,
    }
    assert native["started_at"] == "2026-08-26T00:00:04Z"
    assert native["ended_at"] == "2026-08-26T00:00:05Z"
    assert not (build / "cpp-main-upstream/apps/build_disk_index").exists()


def test_reference_preflight_is_immutable_nonexecuting_and_execute_is_shell_free(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    config = tmp_path / "reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    identity_checks = 0

    def admit_identities(*args: object) -> None:
        nonlocal identity_checks
        identity_checks += 1

    monkeypatch.setattr(fbc, "_assert_source_identities", admit_identities)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )

    def no_launch(*args: object, **kwargs: object) -> object:
        raise AssertionError("preflight launched a process")

    monkeypatch.setattr(fbc.subprocess, "run", no_launch)
    result = fbc.reference_preflight(
        config,
        workspace / "requests" / "reference.json",
        repository_root=REPOSITORY_ROOT,
    )
    request_path = Path(result["request_path"])
    assert result["execution_authorized"] is False
    assert result["byte_length"] == request_path.stat().st_size
    assert result["sha256"] == _sha256(request_path)
    assert json.loads(request_path.read_text(encoding="utf-8")) == request
    with pytest.raises(FeatureCertificationError, match="already exists"):
        fbc.reference_preflight(
            config, request_path, repository_root=REPOSITORY_ROOT
        )

    observed: list[list[str]] = []

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        observed.append(argv)
        assert kwargs["shell"] is False
        _write_cpp_native_closure_fixture(workspace, request, request_path)
        return subprocess.CompletedProcess(
            argv, 0, "capture-owner-control-json\n", "capture-owner-diagnostic\n"
        )

    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    execution = fbc.reference_execute(
        request_path, repository_root=REPOSITORY_ROOT
    )
    assert execution["returncode"] == 0
    assert execution["status"] == "complete"
    assert identity_checks == 3  # preflight plus execution pre/post admission
    assert observed == [
        [
            request["project_operation_owner"]["binary"]["path"],
            "--request",
            str(request_path.resolve()),
        ]
    ]
    assert (
        workspace / request["outputs"]["stdout_path"]
    ).read_text() == _COMPLETE_NATIVE_STAGE_STDOUT
    assert "capture-owner-control-json" not in (
        workspace / request["outputs"]["stdout_path"]
    ).read_text()
    assert "capture-owner-diagnostic" not in (
        workspace / request["outputs"]["stderr_path"]
    ).read_text()
    assert execution["runner_diagnostic"] == {
        "stdout": "capture-owner-control-json\n",
        "stderr": "capture-owner-diagnostic\n",
    }
    for relative in request["outputs"].values():
        assert (workspace / relative).is_file()
    projection = json.loads(
        (workspace / request["outputs"]["structural_projection_path"]).read_text(
            encoding="utf-8"
        )
    )
    assert projection["native_stage_sequence_observed"] is True
    assert projection["baseline_id"] == request["reference_role"]


@pytest.mark.parametrize(
    "format_version",
    ["fake-dataset-json/v1", "diskann-float32-dataset/v1"],
)
def test_fresh_validate_dataset_selects_the_matching_schema(
    tmp_path: Path, format_version: str
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    metadata, _, _ = _write_dataset_admission_fixture(
        workspace, request, format_version=format_version
    )

    assert fbc._fresh_validate_dataset(request, REPOSITORY_ROOT) == metadata["dataset"]


def test_fresh_validate_dataset_rejects_unsupported_format_version(
    tmp_path: Path,
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    _write_dataset_admission_fixture(
        workspace, request, format_version="unsupported-dataset/v9"
    )

    with pytest.raises(
        FeatureCertificationError, match="unsupported Dataset format version"
    ):
        fbc._fresh_validate_dataset(request, REPOSITORY_ROOT)


def test_fresh_validate_native_dataset_rejects_payload_sha_mismatch(
    tmp_path: Path,
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    _, payload_path, _ = _write_dataset_admission_fixture(
        workspace, request, format_version="diskann-float32-dataset/v1"
    )
    changed = bytearray(payload_path.read_bytes())
    changed[-1] ^= 1
    payload_path.write_bytes(changed)

    with pytest.raises(FeatureCertificationError, match="payload identity mismatch"):
        fbc._fresh_validate_dataset(request, REPOSITORY_ROOT)


def test_fresh_validate_native_dataset_rejects_cross_field_checksum_mismatch(
    tmp_path: Path,
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    _write_dataset_admission_fixture(
        workspace,
        request,
        format_version="diskann-float32-dataset/v1",
        dataset_checksum="f" * 64,
    )

    with pytest.raises(
        FeatureCertificationError,
        match="Dataset checksum and vectors payload checksum mismatch",
    ):
        fbc._fresh_validate_dataset(request, REPOSITORY_ROOT)


@pytest.mark.parametrize("failure_case", ["invalid", "artifact_ref_mismatch"])
def test_fresh_validate_native_dataset_rejects_invalid_evidence(
    tmp_path: Path, failure_case: str
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    _, _, evidence_path = _write_dataset_admission_fixture(
        workspace, request, format_version="diskann-float32-dataset/v1"
    )
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    if failure_case == "invalid":
        evidence["result"] = "INVALID"
        evidence["failure_category"] = "fixture_invalid"
        evidence["checks"][0]["passed"] = False
    else:
        evidence["artifact_candidate"]["claimed_aggregate_checksum"] = "f" * 64
    evidence_path.write_text(
        json.dumps(evidence, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    _refresh_dataset_evidence_reference(workspace, request, evidence_path)

    with pytest.raises(
        FeatureCertificationError, match="ValidationEvidence identity mismatch"
    ):
        fbc._fresh_validate_dataset(request, REPOSITORY_ROOT)


def test_reference_preflight_admits_native_dataset_without_execution(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    metadata, _, _ = _write_dataset_admission_fixture(
        workspace, request, format_version="diskann-float32-dataset/v1"
    )
    _bind_build_manifest_dataset(workspace, request, metadata["dataset"])
    config = tmp_path / "native-reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    process_invocations = 0

    def no_process(*args: object, **kwargs: object) -> object:
        nonlocal process_invocations
        process_invocations += 1
        raise AssertionError("reference preflight launched a child process")

    monkeypatch.setattr(fbc.subprocess, "run", no_process)
    result = fbc.reference_preflight(
        config,
        workspace / "requests/native-reference.json",
        repository_root=REPOSITORY_ROOT,
    )

    request_path = Path(result["request_path"])
    assert result["execution_authorized"] is False
    assert result["sha256"] == _sha256(request_path)
    assert json.loads(request_path.read_text(encoding="utf-8")) == request
    assert process_invocations == 0


@pytest.mark.parametrize(
    ("stdout", "expected"),
    [
        (_COMPLETE_NATIVE_STAGE_STDOUT, True),
        ("", False),
        ("Time for partitioning data : 0.100000 seconds\n", False),
        (
            "Time for partitioning data : 0.100000 seconds\n"
            "Time for building indices on shards: 0.200000 seconds\n"
            "Time for building indices on shards: 0.250000 seconds\n"
            "Time for merging indices: 0.300000 seconds\n",
            False,
        ),
    ],
)
def test_native_stage_sequence_requires_complete_ordered_exact_evidence(
    stdout: str, expected: bool
) -> None:
    assert fbc._native_stage_sequence_observed(stdout) is expected


def test_native_stage_markers_are_fixed_source_mapped_evidence() -> None:
    source = (
        REPOSITORY_ROOT / "upstream/diskann-cpp-main/src/disk_utils.cpp"
    ).read_text(encoding="utf-8")
    timer = (
        REPOSITORY_ROOT / "upstream/diskann-cpp-main/include/timer.h"
    ).read_text(encoding="utf-8")
    mapping = (
        REPOSITORY_ROOT
        / "specs/002-cpp-main-upstream-baseline/source-mapping.md"
    ).read_text(encoding="utf-8")
    assert 'elapsed_seconds_for_step("partitioning data ")' in source
    assert 'elapsed_seconds_for_step("building indices on shards")' in source
    assert 'elapsed_seconds_for_step("merging indices")' in source
    assert 'std::string("Time for ")' in timer
    assert "SM-013" in mapping and "SM-019" in mapping and "SM-024" in mapping


@pytest.mark.parametrize(
    "mutation",
    [
        "manifest_log_sha",
        "log_bytes",
        "observer_result",
        "artifact_payload_size",
        "evidence_result",
    ],
)
def test_reference_execute_fails_closed_on_cpp_closure_mismatch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mutation: str
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    request_path = workspace / "requests/reference.json"
    request_path.parent.mkdir(parents=True)
    request_path.write_text(json.dumps(request), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        _write_cpp_native_closure_fixture(workspace, request, request_path)
        manifest_path = workspace / request["outputs"]["build_manifest_path"]
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if mutation == "manifest_log_sha":
            manifest["parameters"]["native_child_logs"]["stdout"]["sha256"] = "0" * 64
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        elif mutation == "log_bytes":
            (workspace / request["outputs"]["stdout_path"]).write_text("changed")
        elif mutation == "observer_result":
            observer_path = workspace / request["outputs"]["observer_evidence_path"]
            observer = json.loads(observer_path.read_text(encoding="utf-8"))
            observer["terminal_result"] = "FAILED"
            observer_path.write_text(json.dumps(observer), encoding="utf-8")
        elif mutation == "artifact_payload_size":
            artifact_path = workspace / request["outputs"]["artifact_metadata_path"]
            artifact = json.loads(artifact_path.read_text(encoding="utf-8"))
            artifact["payloads"][0]["byte_length"] += 1
            artifact_path.write_text(json.dumps(artifact), encoding="utf-8")
        else:
            evidence_path = workspace / request["outputs"]["validation_evidence_path"]
            evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
            evidence["result"] = "INVALID"
            evidence["failure_category"] = "fixture_mismatch"
            evidence_path.write_text(json.dumps(evidence), encoding="utf-8")
        return subprocess.CompletedProcess(argv, 0, "runner\n", "")

    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    result = fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert result["status"] == "incomplete"
    assert result["returncode"] != 0
    assert not (
        workspace / request["outputs"]["structural_projection_path"]
    ).exists()


def test_reference_execute_preserves_terminal_failure_and_bootstrap_boundaries(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    request_path = workspace / "requests/reference.json"
    request_path.parent.mkdir(parents=True)
    request_path.write_text(json.dumps(request), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )
    launches = 0

    def failed_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        nonlocal launches
        launches += 1
        _write_cpp_native_closure_fixture(
            workspace, request, request_path, status="FAILED", exit_code=9
        )
        return subprocess.CompletedProcess(argv, 1, "", "owner failed\n")

    monkeypatch.setattr(fbc.subprocess, "run", failed_run)
    result = fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert result["status"] == "failed"
    assert result["failure_class"] == "lifecycle-established"
    assert launches == 1
    manifest = json.loads(
        (workspace / request["outputs"]["build_manifest_path"]).read_text()
    )
    assert manifest["failure"]["reason"] == "fixture native child failed"
    assert manifest["output_artifacts"] == []
    assert not (workspace / request["outputs"]["artifact_metadata_path"]).exists()
    assert not (workspace / request["outputs"]["validation_evidence_path"]).exists()
    assert not (workspace / request["outputs"]["structural_projection_path"]).exists()

    bootstrap_root = tmp_path / "b"
    bootstrap_root.mkdir()
    bootstrap_workspace, bootstrap_request = _reference_request_fixture(bootstrap_root)
    bootstrap_path = bootstrap_workspace / "requests/reference.json"
    bootstrap_path.parent.mkdir(parents=True)
    bootstrap_path.write_text(json.dumps(bootstrap_request), encoding="utf-8")
    monkeypatch.setattr(
        fbc.subprocess,
        "run",
        lambda argv, **kwargs: subprocess.CompletedProcess(argv, 2, "", "bootstrap\n"),
    )
    bootstrap = fbc.reference_execute(
        bootstrap_path, repository_root=REPOSITORY_ROOT
    )
    assert bootstrap["status"] == "failed"
    assert bootstrap["failure_class"] == "pre-lifecycle/bootstrap"
    assert not (
        bootstrap_workspace
        / bootstrap_request["outputs"]["build_manifest_path"]
    ).exists()


def test_projection_failure_keeps_valid_cpp_closure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    request_path = workspace / "requests/reference.json"
    request_path.parent.mkdir(parents=True)
    request_path.write_text(json.dumps(request), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        _write_cpp_native_closure_fixture(workspace, request, request_path)
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    monkeypatch.setattr(
        fbc,
        "_write_native_structural_projection",
        lambda *args, **kwargs: (_ for _ in ()).throw(
            FeatureCertificationError("projection fixture failure")
        ),
        raising=False,
    )
    result = fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert result["status"] == "incomplete"
    assert result["returncode"] != 0
    for key in (
        "artifact_metadata_path",
        "build_manifest_path",
        "validation_evidence_path",
        "stdout_path",
        "stderr_path",
        "observer_evidence_path",
    ):
        assert (workspace / request["outputs"][key]).is_file()
    assert not (workspace / request["outputs"]["structural_projection_path"]).exists()


def test_reference_execute_post_identity_mismatch_preserves_cpp_evidence(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    request_path = workspace / "requests/reference.json"
    request_path.parent.mkdir(parents=True)
    request_path.write_text(json.dumps(request), encoding="utf-8")
    checks = 0

    def identity_admission(*args: object) -> None:
        nonlocal checks
        checks += 1
        if checks == 2:
            raise FeatureCertificationError("post-run Project identity mismatch")

    monkeypatch.setattr(fbc, "_assert_source_identities", identity_admission)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )

    def fake_run(argv: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
        _write_cpp_native_closure_fixture(workspace, request, request_path)
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(fbc.subprocess, "run", fake_run)
    with pytest.raises(FeatureCertificationError, match="post-run Project"):
        fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert checks == 2
    for key in (
        "artifact_metadata_path",
        "build_manifest_path",
        "validation_evidence_path",
        "stdout_path",
        "stderr_path",
        "observer_evidence_path",
    ):
        assert (workspace / request["outputs"][key]).is_file()
    assert not (workspace / request["outputs"]["structural_projection_path"]).exists()


def test_reference_preflight_and_execute_fail_closed_before_launch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    config = tmp_path / "reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )
    request_path = workspace / "requests" / "reference.json"
    fbc.reference_preflight(config, request_path, repository_root=REPOSITORY_ROOT)

    calls = 0

    def forbidden(*args: object, **kwargs: object) -> object:
        nonlocal calls
        calls += 1
        raise AssertionError("invalid request launched capture")

    monkeypatch.setattr(fbc.subprocess, "run", forbidden)
    owner = Path(request["project_operation_owner"]["binary"]["path"])
    owner.write_bytes(b"changed")
    with pytest.raises(FeatureCertificationError, match="binary identity"):
        fbc.reference_execute(request_path, repository_root=REPOSITORY_ROOT)
    assert calls == 0


@pytest.mark.parametrize(
    "failure_case",
    [
        "missing",
        "manifest_sha",
        "cross_bound",
        "stale_target",
        "source_repository",
        "source_branch",
        "source_commit",
        "source_dirty",
        "source_role",
        "binary_path",
        "binary_size",
        "binary_sha",
        "toolchain_compiler",
        "toolchain_cmake",
        "toolchain_release",
        "compile_evidence",
        "link_evidence",
        "build_flags",
        "failed",
    ],
)
def test_reference_preflight_rejects_target_build_manifest_mismatch(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    failure_case: str,
) -> None:
    workspace, request = _reference_request_fixture(tmp_path)
    owner_identity = request["project_operation_owner"]["binary"]
    native_identity = request["native_binary"]
    owner_manifest = workspace / owner_identity["build_manifest"]["path"]
    native_manifest = workspace / native_identity["build_manifest"]["path"]

    if failure_case == "missing":
        owner_manifest.unlink()
    elif failure_case == "manifest_sha":
        owner_manifest.write_bytes(owner_manifest.read_bytes() + b"\n")
    elif failure_case == "cross_bound":
        owner_identity["build_manifest"] = copy.deepcopy(native_identity["build_manifest"])
    else:
        manifest = json.loads(owner_manifest.read_text(encoding="utf-8"))
        closure = manifest["parameters"]["build_closure"]
        if failure_case == "stale_target":
            closure["target"] = "build_disk_index"
        elif failure_case == "source_repository":
            manifest["source"]["repository"] = "https://example.invalid/project.git"
        elif failure_case == "source_branch":
            manifest["source"]["branch"] = "wrong-branch"
        elif failure_case == "source_commit":
            manifest["source"]["commit"] = "b" * 40
        elif failure_case == "source_dirty":
            manifest["source"]["dirty_worktree"] = True
            manifest["source"]["dirty_source_checksum"] = "d" * 64
        elif failure_case == "source_role":
            closure["source_role"] = "fixed_upstream_build_disk_index"
        elif failure_case == "binary_path":
            closure["binary"]["path"] = native_identity["path"]
        elif failure_case == "binary_size":
            closure["binary"]["byte_length"] += 1
        elif failure_case == "binary_sha":
            closure["binary"]["sha256"] = "f" * 64
        elif failure_case == "toolchain_compiler":
            manifest["toolchain"]["compiler_path"] = "/usr/bin/clang++"
        elif failure_case == "toolchain_cmake":
            manifest["toolchain"]["cmake_version"] = "9.9.9"
        elif failure_case == "toolchain_release":
            manifest["toolchain"]["build_type"] = "Debug"
        elif failure_case == "compile_evidence":
            manifest["toolchain"]["compile_commands"] = [
                "/usr/bin/c++ -O3 -DNDEBUG -std=c++17 -c reconstructed.cpp"
            ]
        elif failure_case == "link_evidence":
            manifest["toolchain"]["link_commands"] = [
                "/usr/bin/c++ -O3 -DNDEBUG reconstructed.o -o reconstructed"
            ]
        elif failure_case == "build_flags":
            closure["applicable_flags"] = ["-O3", "-std=c++17"]
        elif failure_case == "failed":
            manifest["status"] = "FAILED"
            manifest["failure"] = {
                "category": "target_build_failed",
                "reason": "fixture failure",
                "exit_code": 9,
            }
        owner_manifest.write_text(
            json.dumps(manifest, sort_keys=True, separators=(",", ":")),
            encoding="utf-8",
        )
        owner_identity["build_manifest"] = {
            "path": owner_manifest.relative_to(workspace).as_posix(),
            "byte_length": owner_manifest.stat().st_size,
            "sha256": _sha256(owner_manifest),
        }

    config = tmp_path / "reference-config.json"
    config.write_text(json.dumps({"request": request}), encoding="utf-8")
    monkeypatch.setattr(fbc, "_assert_source_identities", lambda *args: None)
    monkeypatch.setattr(
        fbc, "_fresh_validate_dataset", lambda *args: FIXTURE_DATASET_IDENTITY
    )
    with pytest.raises(FeatureCertificationError):
        fbc.reference_preflight(
            config,
            workspace / "requests" / "reference.json",
            repository_root=REPOSITORY_ROOT,
        )
    assert not (workspace / "requests/reference.json").exists()


def _structural_projection(*, reference: bool) -> dict[str, object]:
    return {
        "baseline_id": (
            "cpp_main_native_sharded_reference_v1"
            if reference
            else "cpp_main_p0_b0_m0_framework_candidate_v1"
        ),
        "execution_status": "SUCCEEDED",
        "source": {
            "repository": "https://github.com/microsoft/DiskANN.git",
            "branch": "cpp_main",
            "commit": "78256bbab4685e1774e78d331e081a153be26823",
            "clean_before": True,
            "clean_after": True,
        },
        "dataset_checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
        "profile_checksum": "6dc8009b9654b01ae61fb32ab3af6cff3c9465a5a4e0e821cfdea06793d74262",
        "branch_mode": "sharded",
        "partition_count": 3,
        **(
            {"native_stage_sequence_observed": True}
            if reference
            else {
                "stage_invocations": {"partition": 1, "local_build": 3, "merge": 1},
                "artifact_handoffs_valid": True,
            }
        ),
        "global_graph": {
            "available": True,
            "validation_status": "PASS",
            "id_space": "global",
            "node_count": 1_000_000,
            "invalid_neighbor_count": 0,
            "self_edge_count": 0,
            "observed_max_degree": 16,
            "medoids_valid": True,
            "dataset_lineage_checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
        },
        "evidence_refs": ["fixture/evidence.json"],
    }


@pytest.mark.parametrize(
    ("mutation", "expected"),
    [
        ("pass", "PASS"),
        ("fail", "FAIL"),
        ("inconclusive", "INCONCLUSIVE"),
        ("not_reached", "NOT_REACHED"),
    ],
)
def test_derive_structural_reuses_frozen_feature002_four_state_helper(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    mutation: str,
    expected: str,
) -> None:
    reference = _structural_projection(reference=True)
    framework = _structural_projection(reference=False)
    if mutation == "fail":
        framework["global_graph"]["observed_max_degree"] = 17
    elif mutation == "inconclusive":
        del reference["global_graph"]["medoids_valid"]
    elif mutation == "not_reached":
        reference["global_graph"] = {"available": False}
    input_document = {
        "workspace_root": str(tmp_path),
        "report_id": f"fixture-{mutation}",
        "generated_at": "2026-08-26T00:00:00Z",
        "reference": {"projection": reference},
        "framework": {"projection": framework},
    }
    input_path = tmp_path / "input.json"
    input_path.write_text(json.dumps(input_document), encoding="utf-8")
    monkeypatch.setattr(
        fbc,
        "_validate_structural_role_admission",
        lambda role, *args, **kwargs: role["projection"],
    )
    output = tmp_path / "reports" / f"{mutation}.json"
    result = fbc.derive_structural(
        input_path, output, repository_root=REPOSITORY_ROOT
    )
    report = json.loads(output.read_text(encoding="utf-8"))
    assert result["gate"] == expected
    assert report["gate"]["result"] == expected
    assert report["claims"]["structural_and_contract_parity"] is (
        expected == "PASS"
    )
    assert report["claims"]["search_semantics_parity"] is False
    assert report["claims"]["quality_parity"] is False
    assert report["claims"]["final_framework_baseline_certified"] is False


FROZEN_RUNNER_IDENTITY = {
    "project_commit": "8cd169255c483999be0628e53045f2c3248b5b5e",
    "runner_sha256": "4c711fdb2f68249f4b8051744f9f5c6278eae1c0f2862d0a8292244e5aec1b7d",
    "implementation_id": "cpp_main_pqflash_search_instrumented_v1",
    "diskann_commit": "78256bbab4685e1774e78d331e081a153be26823",
}


def test_exact_current_runner_parity_admission_surface_is_frozen() -> None:
    assert fbc.FROZEN_RUNNER_PARITY_IDENTITY == FROZEN_RUNNER_IDENTITY
    assert fbc.FROZEN_RUNNER_PARITY_PACKAGE["fixed"]["protocol_checksum"] == (
        "86eb2702abfd25461552e8d7db1749b5506012b508fd346d5af36b9e272389f5"
    )
    assert callable(fbc._admit_search_runner_parity)


def test_final_decision_schema_accepts_only_current_frozen_runner_identity() -> None:
    schema = json.loads(
        (
            REPOSITORY_ROOT
            / "specs/006-framework-baseline-certification/contracts/"
            "framework-baseline-certification-decision.schema.json"
        ).read_text(encoding="utf-8")
    )
    identity_schema = schema["properties"]["search_runner_parity"]["properties"][
        "identity"
    ]
    validator = Draft202012Validator(identity_schema)
    assert not list(validator.iter_errors(FROZEN_RUNNER_IDENTITY))
    old_identity = {
        **FROZEN_RUNNER_IDENTITY,
        "project_commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
        "runner_sha256": "97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856",
    }
    assert list(validator.iter_errors(old_identity))


def _artifact_ref(
    artifact_type: str, format_version: str, metadata_path: str, marker: str
) -> dict[str, object]:
    return {
        "metadata_path": metadata_path,
        "artifact_type": artifact_type,
        "format_version": format_version,
        "aggregate_checksum": marker * 64,
    }


def _canonical_fixture_document(path: Path, document: dict[str, object]) -> dict[str, object]:
    value = copy.deepcopy(document)
    value["canonical_checksum"] = search._canonical_digest(value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, sort_keys=True, separators=(",", ":")), encoding="utf-8"
    )
    return value


def _document_ref(
    workspace: Path, path: Path, *, canonical_checksum: str | None = None
) -> dict[str, object]:
    reference: dict[str, object] = {
        "path": path.relative_to(workspace).as_posix(),
        "byte_length": path.stat().st_size,
        "sha256": _sha256(path),
    }
    if canonical_checksum is not None:
        reference["canonical_checksum"] = canonical_checksum
    return reference


def _protocol_declaration(
    *,
    role: str,
    cache_mode: str,
    runner_sha: str,
    query_set: dict[str, object],
    ground_truth: dict[str, object],
) -> dict[str, object]:
    dataset = _artifact_ref(
        "dataset", "diskann-float32-dataset/v1", "dataset/artifact.json", "d"
    )
    shared_pq = _artifact_ref("pq", "pq-artifact/v1", "pq/artifact.json", "b")
    if role == "reference":
        graph = _artifact_ref(
            "global_graph",
            "global-graph-artifact/v2",
            "reference/graph/artifact.json",
            "c",
        )
        layout = _artifact_ref(
            "disk_layout",
            "disk-layout-artifact/v1",
            "reference/layout/artifact.json",
            "e",
        )
    else:
        graph = _artifact_ref(
            "global_graph",
            "global-graph-artifact/v1",
            "framework/graph/artifact.json",
            "f",
        )
        layout = _artifact_ref(
            "disk_layout",
            "disk-layout-artifact/v1",
            "framework/layout/artifact.json",
            "a",
        )
    cache: dict[str, object] = {"mode": "zero", "requested_nodes": 0}
    if cache_mode == "fixed_bfs":
        cache = {
            "mode": "fixed_bfs",
            "requested_nodes": 10000,
            "construction": "medoid_bfs",
            "shuffle": False,
        }
    execution = {
        "execution_id": f"{role}-{cache_mode}",
        "dataset_artifact": dataset,
        "pq_artifact": shared_pq,
        "disk_layout_artifact": layout,
        "global_graph_artifact": graph,
        "query_set": query_set,
        "distance_metric": "l2",
        "K": 100,
        "L_search": [100, 200],
        "beamwidth": 2,
        "thread_count": 6,
        "cache": cache,
        "io_limit": 4294967295,
        "warmup_policy": "none",
        "query_order": "query_row_index_ascending",
        "timing_boundary": "cpp_main_search_disk_index_query_batch_v1",
        "storage_placement": "small-fixture",
        "repetition_count": 3,
        "schedule": "repetition_major_then_L_order",
        "entry_policy": "end_to_end",
        "environment": {
            "os": "linux",
            "storage": "local_ssd",
            "direct_io": "required_and_probed",
            "linux_aio": "required_and_probed",
            "remote_storage_allowed": False,
        },
        "implementation": {
            "implementation_id": FROZEN_RUNNER_IDENTITY["implementation_id"],
            "version": "1.0.0",
            "reproduction_mode": "instrumented",
        },
        "runner_build": {
            "project_repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
            "project_branch": "feature/search-evaluation",
            "project_commit": FROZEN_RUNNER_IDENTITY["project_commit"],
            "project_dirty": False,
            "binary_path": "bin/runner",
            "binary_sha256": runner_sha,
            "build_manifest_path": "manifests/runner-build.json",
            "build_manifest_sha256": "b" * 64,
        },
        "diskann_source_commit": FROZEN_RUNNER_IDENTITY["diskann_commit"],
    }
    return {
        "protocol_version": "1.0.0",
        "protocol_id": f"{role}-{cache_mode}",
        "status": "frozen",
        "search_execution": execution,
        "search_execution_checksum": "0" * 64,
        "evaluation": {
            "ground_truth": ground_truth,
            "evaluator": {
                "implementation_id": "cpp_main_search_metrics_evaluator_v1",
                "version": "1.0.0",
                "reproduction_mode": "instrumented",
            },
            "metrics": [
                "recall_at_10",
                "recall_at_100",
                "qps_k100",
                "mean_latency_us_k100",
                "p99_9_latency_us_k100",
                "mean_ios_k100",
            ],
            "quality_eligibility_policy": "complete_valid_ground_truth_identity_search_semantics_v1",
            "performance_eligibility_policy": "complete_valid_normative_environment_timing_parity_v1",
            "aggregation_policy": "median_eligible",
            "replacement_repetitions": False,
        },
        "canonicalization": "diskann-search-protocol-c14n-v1",
        "protocol_checksum": "0" * 64,
    }


def _write_feature005_observations(
    *,
    workspace: Path,
    protocol_path: Path,
    missed_rows: int,
    degrade_recall100: bool,
    invalidate: tuple[int, int] | None = None,
) -> Path:
    protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
    execution = protocol["search_execution"]
    cache = execution["cache"]
    observation_root = workspace / "reports" / "observations" / protocol["protocol_id"]
    observation_root.mkdir(parents=True)
    environment = workspace / "reports" / "environment.json"
    if not environment.exists():
        environment.parent.mkdir(parents=True, exist_ok=True)
        environment.write_text('{"performance_admitted":false}', encoding="utf-8")
    full_results = list(range(100))
    recall100_threshold_results = list(range(99)) + [100]
    shifted_results = list(range(1, 101))
    for repetition in range(3):
        for l_search in execution["L_search"]:
            point_id = f"{protocol['protocol_id']}-L{l_search}-r{repetition}"
            document = {
                "format_version": "search-observation/v1",
                "observation_id": point_id,
                "run_id": f"{protocol['protocol_id']}-fixture",
                "generated_at": "2026-08-26T01:00:00Z",
                "implementation": execution["implementation"],
                "source_build": {
                    "diskann_repository": "https://github.com/microsoft/DiskANN.git",
                    "diskann_branch": "cpp_main",
                    "diskann_commit": FROZEN_RUNNER_IDENTITY["diskann_commit"],
                    "binary_path": execution["runner_build"]["binary_path"],
                    "binary_sha256": execution["runner_build"]["binary_sha256"],
                    "build_manifest_path": execution["runner_build"]["build_manifest_path"],
                    "build_manifest_sha256": execution["runner_build"]["build_manifest_sha256"],
                },
                "protocol": {
                    "path": protocol_path.relative_to(workspace).as_posix(),
                    "protocol_id": protocol["protocol_id"],
                    "protocol_checksum": protocol["protocol_checksum"],
                },
                "search_execution": {
                    "execution_id": execution["execution_id"],
                    "canonical_checksum": protocol["search_execution_checksum"],
                },
                "dataset_artifact": execution["dataset_artifact"],
                "global_graph_artifact": execution["global_graph_artifact"],
                "pq_artifact": execution["pq_artifact"],
                "disk_layout_artifact": execution["disk_layout_artifact"],
                "query_set_checksum": execution["query_set"]["canonical_checksum"],
                "raw_search_point": {
                    "point_id": point_id,
                    "L_search": l_search,
                    "K": 100,
                    "beamwidth": 2,
                    "cache": {
                        "mode": cache["mode"],
                        "requested_nodes": cache["requested_nodes"],
                        "effective_nodes": cache["requested_nodes"],
                        "primary_payload_bytes": 0,
                    },
                    "repetition_index": repetition,
                    "query_count": 10,
                    "completed_query_count": 10,
                    "batch_wall_clock_us": 10000,
                    "status": "SUCCEEDED",
                },
                "environment": {
                    "fingerprint_path": environment.relative_to(workspace).as_posix(),
                    "fingerprint_sha256": _sha256(environment),
                },
                "per_query": [
                    {
                        "query_row_index": row,
                        "source_query_id": f"opaque-{row}",
                        "result_ids": (
                            shifted_results
                            if row < missed_rows
                            else recall100_threshold_results
                            if degrade_recall100
                            else full_results
                        ),
                        "result_distances": [float(index) for index in range(100)],
                        "total_us": 10.0 + row,
                        "n_ios": 2 + row,
                    }
                    for row in range(10)
                ],
            }
            if invalidate == (l_search, repetition):
                document["per_query"][0]["total_us"] = -1
            document["observation_checksum"] = search._canonical_digest(document)
            (observation_root / f"{point_id}.json").write_text(
                json.dumps(document, sort_keys=True, separators=(",", ":")),
                encoding="utf-8",
            )
    return observation_root


def _feature005_two_graph_fixture(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    *,
    framework_missed_rows: int = 1,
    invalidate_framework: tuple[int, int] | None = None,
) -> dict[str, object]:
    workspace = tmp_path / "quality-workspace"
    workspace.mkdir()
    runner = workspace / "bin" / "runner"
    runner.parent.mkdir(parents=True)
    runner.write_bytes(b"feature-005-two-graph-fixture-runner")
    actual_sha = search._sha256

    def fixture_sha(path: Path) -> str:
        if Path(path).resolve() == runner.resolve():
            return FROZEN_RUNNER_IDENTITY["runner_sha256"]
        return actual_sha(Path(path))

    monkeypatch.setattr(search, "_sha256", fixture_sha)
    dataset_metadata = workspace / "normal" / "dataset" / "artifact.json"
    dataset_metadata.parent.mkdir(parents=True)
    dataset_metadata.write_text(
        json.dumps(
            {
                "dataset": {
                    "dataset_id": "small-certification-dataset",
                    "dataset_checksum": "e" * 64,
                    "global_node_count": 256,
                    "vector_dimension": 4,
                    "scalar_type": "float32",
                    "distance_metric": "l2",
                }
            }
        ),
        encoding="utf-8",
    )
    query_reference_path = workspace / "inputs" / "query-reference.json"
    query_reference = _canonical_fixture_document(
        query_reference_path,
        {
            "reference_version": "1.0.0",
            "query_set_id": "shared-query",
            "logical_source": "tests/fixtures/search/selected_query_small.csv",
            "source": {
                "path": "inputs/query.csv",
                "format": "csv",
                "byte_length": 1,
                "sha256": "1" * 64,
            },
            "query_count": 10,
            "dimension": 4,
            "scalar_type": "float32",
            "query_identity": "query_row_index",
            "source_query_id_semantics": "opaque",
            "self_match_policy": "no_id_based_exclusion",
            "native_payload": {
                "path": "inputs/query.bin",
                "format": "diskann-float32-bin",
                "byte_length": 168,
                "sha256": "2" * 64,
            },
            "row_mapping": {
                "path": "inputs/query-map.json",
                "format": "json",
                "byte_length": 1,
                "sha256": "3" * 64,
            },
            "conversion_manifest_path": "manifests/query.json",
            "validation_evidence_path": "manifests/evidence/query.json",
        },
    )
    truth_path = workspace / "inputs" / "truth.bin"
    truth_ids = list(range(101)) * 10
    truth_distances = [float(index) for index in range(101)] * 10
    truth_path.write_bytes(
        struct.pack("<II", 10, 101)
        + struct.pack(f"<{len(truth_ids)}I", *truth_ids)
        + struct.pack(f"<{len(truth_distances)}f", *truth_distances)
    )
    ground_truth_reference_path = workspace / "inputs" / "truth-reference.json"
    ground_truth = _canonical_fixture_document(
        ground_truth_reference_path,
        {
            "reference_version": "1.0.0",
            "ground_truth_id": "shared-truth",
            "payload": {
                "path": truth_path.relative_to(workspace).as_posix(),
                "format": "diskann-truthset",
                "byte_length": truth_path.stat().st_size,
                "sha256": actual_sha(truth_path),
            },
            "query_count": 10,
            "width": 101,
            "id_space": "global",
            "distance_metric": "l2_squared_float32",
            "dataset_artifact": _artifact_ref(
                "dataset", "diskann-float32-dataset/v1", "dataset/artifact.json", "d"
            ),
            "query_set_checksum": query_reference["canonical_checksum"],
            "self_match_policy": "no_id_based_exclusion",
            "rank_100_tie_group_closed": True,
            "generation_manifest_path": "manifests/truth.json",
            "validation_evidence_path": "manifests/evidence/truth.json",
        },
    )
    protocol_paths: dict[str, dict[str, Path]] = {}
    report_paths: dict[str, dict[str, Path]] = {}
    manifest_paths: dict[str, dict[str, Path]] = {}
    for role in ("reference", "framework"):
        declarations = [
            _protocol_declaration(
                role=role,
                cache_mode=cache,
                runner_sha=FROZEN_RUNNER_IDENTITY["runner_sha256"],
                query_set=query_reference,
                ground_truth=ground_truth,
            )
            for cache in ("zero", "fixed_bfs")
        ]
        config = {
            "mode": "formal-search",
            "fixture_mode": True,
            "protocols": declarations,
            "common_admission": {
                "artifacts_valid": True,
                "inputs_compatible": True,
                "runner_path": str(runner),
                "runner_sha256": FROZEN_RUNNER_IDENTITY["runner_sha256"],
                "abi_dependencies_usable": True,
                "load_probe_pass": True,
                "paths_confined": True,
                "resources_sufficient": True,
            },
            "performance_admission": {
                "native_linux": False,
                "local_ssd": False,
                "direct_io": False,
                "linux_aio": False,
                "parity_pass": False,
            },
        }
        config_path = workspace / f"{role}-formal-search.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        preflight = search.preflight(
            mode="formal-search", config_path=config_path, workspace=workspace
        )
        assert preflight["common_search_admission"] is True
        assert preflight["operations_started"]["search"] == 0
        role_protocols: dict[str, Path] = {}
        role_reports: dict[str, Path] = {}
        role_manifests: dict[str, Path] = {}
        for raw_path in preflight["protocol_paths"]:
            protocol_path = Path(raw_path)
            protocol = json.loads(protocol_path.read_text(encoding="utf-8"))
            cache_mode = protocol["search_execution"]["cache"]["mode"]
            cache_key = "zero" if cache_mode == "zero" else "fixed"
            observations = _write_feature005_observations(
                workspace=workspace,
                protocol_path=protocol_path,
                missed_rows=0 if role == "reference" else framework_missed_rows,
                degrade_recall100=role == "framework",
                invalidate=invalidate_framework if role == "framework" else None,
            )
            evaluated = search.evaluate_observation_directory(
                protocol_path=protocol_path,
                observations_path=observations,
                workspace=workspace,
            )
            role_protocols[cache_key] = protocol_path
            role_reports[cache_key] = Path(evaluated["report_path"])
            role_manifests[cache_key] = Path(evaluated["build_manifest_path"])
        protocol_paths[role] = role_protocols
        report_paths[role] = role_reports
        manifest_paths[role] = role_manifests
    return {
        "workspace": workspace,
        "query_reference_path": query_reference_path,
        "query_reference": query_reference,
        "ground_truth_reference_path": ground_truth_reference_path,
        "ground_truth": ground_truth,
        "protocol_paths": protocol_paths,
        "report_paths": report_paths,
        "manifest_paths": manifest_paths,
    }


def _freeze_input(fixture: dict[str, object]) -> dict[str, object]:
    workspace = fixture["workspace"]
    protocol_paths = fixture["protocol_paths"]
    reference_zero = json.loads(protocol_paths["reference"]["zero"].read_text())
    framework_zero = json.loads(protocol_paths["framework"]["zero"].read_text())
    return {
        "workspace_root": str(workspace),
        "created_at": "2026-08-26T02:00:00Z",
        "artifacts": {
            "dataset": reference_zero["search_execution"]["dataset_artifact"],
            "shared_pq": reference_zero["search_execution"]["pq_artifact"],
            "reference_graph": reference_zero["search_execution"]["global_graph_artifact"],
            "reference_disk_layout": reference_zero["search_execution"]["disk_layout_artifact"],
            "framework_graph": framework_zero["search_execution"]["global_graph_artifact"],
            "framework_disk_layout": framework_zero["search_execution"]["disk_layout_artifact"],
        },
        "shared_inputs": {
            "query_set": _document_ref(
                workspace,
                fixture["query_reference_path"],
                canonical_checksum=fixture["query_reference"]["canonical_checksum"],
            ),
            "ground_truth": _document_ref(
                workspace,
                fixture["ground_truth_reference_path"],
                canonical_checksum=fixture["ground_truth"]["canonical_checksum"],
            ),
        },
        "protocols": {
            role: {
                cache: {"path": path.relative_to(workspace).as_posix()}
                for cache, path in protocol_paths[role].items()
            }
            for role in ("reference", "framework")
        },
    }


def _comparison_input(fixture: dict[str, object]) -> dict[str, object]:
    workspace = fixture["workspace"]
    return {
        "workspace_root": str(workspace),
        "generated_at": "2026-08-26T03:00:00Z",
        "evaluation_reports": {
            role: [
                paths[cache].relative_to(workspace).as_posix()
                for cache in ("zero", "fixed")
            ]
            for role, paths in fixture["report_paths"].items()
        },
    }


def test_two_graph_fixture_uses_feature005_preflight_and_evaluator_for_all_four_reports(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    workspace = fixture["workspace"]
    policy_path = workspace / "evidence" / "quality-policy.json"
    freeze_path = workspace / "freeze-input.json"
    freeze_path.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    frozen = fbc.freeze_quality_policy(
        freeze_path, policy_path, repository_root=REPOSITORY_ROOT
    )
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    assert frozen["protocol_count"] == 4
    assert len({
        policy["protocols"][role][cache]["protocol_id"]
        for role in ("reference", "framework")
        for cache in ("zero", "fixed")
    }) == 4
    assert policy["artifacts"]["reference_graph"]["format_version"] == "global-graph-artifact/v2"
    assert policy["artifacts"]["framework_graph"]["format_version"] == "global-graph-artifact/v1"
    assert policy["artifacts"]["shared_pq"] == _freeze_input(fixture)["artifacts"]["shared_pq"]

    compare_input = workspace / "compare-input.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    evidence_path = workspace / "evidence" / "quality-evidence.json"
    result = fbc.compare_quality(
        policy_path, compare_input, evidence_path, repository_root=REPOSITORY_ROOT
    )
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    assert result["result"] == "PASS"
    assert len(evidence["evaluation_reports"]["reference"]) == 2
    assert len(evidence["evaluation_reports"]["framework"]) == 2
    assert len(evidence["groups"]) == 4
    assert all(group["result"] == "PASS" for group in evidence["groups"])
    assert all(
        finding["shortfall_fraction"] == pytest.approx(0.01)
        for group in evidence["groups"]
        for finding in group["metric_findings"]
    )
    assert "performance" not in json.dumps(evidence)


@pytest.mark.parametrize(
    ("missed_rows", "expected"), [(0, "PASS"), (1, "PASS"), (2, "FAIL")]
)
def test_quality_threshold_is_pointwise_without_compensation(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    missed_rows: int,
    expected: str,
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, framework_missed_rows=missed_rows
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    output = workspace / "quality.json"
    result = fbc.compare_quality(
        policy, compare_input, output, repository_root=REPOSITORY_ROOT
    )
    assert result["result"] == expected


def test_quality_missing_group_and_identity_mismatch_are_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_document = _comparison_input(fixture)
    # Independent legacy-report fixtures exercise Certification's own missing
    # group defense; the current evaluator must only generate complete sets.
    legacy_paths = []
    for cache, original in fixture["report_paths"]["framework"].items():
        legacy = json.loads(original.read_text(encoding="utf-8"))
        legacy["report_id"] = f"legacy-incomplete-{cache}"
        legacy["observation_results"] = [item for item in legacy["observation_results"] if item["L_search"] != 200]
        for family in ("quality", "performance"):
            legacy["aggregates"][family] = [item for item in legacy["aggregates"][family] if item["L_search"] != 200]
        legacy.pop("report_checksum")
        legacy["report_checksum"] = search._canonical_digest(legacy)
        search._feature_validator("search-evaluation-report.schema.json").validate(legacy)
        legacy_path = workspace / f"legacy-incomplete-{cache}.json"
        legacy_path.write_text(json.dumps(legacy), encoding="utf-8")
        legacy_paths.append(legacy_path.relative_to(workspace).as_posix())
    compare_document["evaluation_reports"]["framework"] = legacy_paths
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(compare_document), encoding="utf-8")
    missing_output = workspace / "missing-quality.json"
    assert fbc.compare_quality(
        policy, compare_input, missing_output, repository_root=REPOSITORY_ROOT
    )["result"] == "INCONCLUSIVE"
    missing_evidence = json.loads(missing_output.read_text(encoding="utf-8"))
    assert len(missing_evidence["evaluation_reports"]["framework"]) == 2
    assert all(group["result"] == "INCONCLUSIVE"
               and group["framework"]["eligible_count"] == 0
               and "metrics" not in group["framework"]
               and group["reason"] == "missing, incomplete, or unevaluable Feature 005 aggregate"
               for group in missing_evidence["groups"] if group["L_search"] == 200)
    assert not any(item["L_search"] == 200 for item in missing_evidence["observation_attempts"]["framework"])

    mismatched = _comparison_input(fixture)
    mismatched["evaluation_reports"]["framework"][0] = (
        fixture["report_paths"]["reference"]["zero"]
        .relative_to(workspace)
        .as_posix()
    )
    mismatch_input = workspace / "mismatch.json"
    mismatch_input.write_text(json.dumps(mismatched), encoding="utf-8")
    mismatch_output = workspace / "mismatch-quality.json"
    assert fbc.compare_quality(
        policy, mismatch_input, mismatch_output, repository_root=REPOSITORY_ROOT
    )["result"] == "INCONCLUSIVE"


def test_quality_retains_exclusion_without_replacement_repetition(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, invalidate_framework=(200, 2)
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    output = workspace / "quality.json"
    assert fbc.compare_quality(
        policy, compare_input, output, repository_root=REPOSITORY_ROOT
    )["result"] == "PASS"
    evidence = json.loads(output.read_text(encoding="utf-8"))
    group = next(
        item
        for item in evidence["groups"]
        if item["cache_mode"] == "fixed_bfs" and item["L_search"] == 200
    )
    assert group["framework"]["attempted_count"] == 3
    assert group["framework"]["eligible_count"] == 2
    assert any(item["terminal_status"] == "EXCLUDED" for item in evidence["audit_exclusions"])


def _write_json_ref(workspace: Path, relative: str, value: dict[str, object]) -> dict[str, object]:
    path = workspace / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")
    return _document_ref(workspace, path)


def _parity_build_manifest(
    *,
    implementation_id: str,
    source: dict[str, object],
    parameters: dict[str, object],
    stage: str,
) -> dict[str, object]:
    return {
        "manifest_version": "1.0.0",
        "run_id": f"synthetic-{implementation_id}",
        "stage": stage,
        "implementation": {
            "implementation_id": implementation_id,
            "implementation_version": "1.0.0",
        },
        "source": source,
        "toolchain": {
            "os": "Linux",
            "compiler_path": "/usr/bin/c++",
            "compiler_version": "11.4.0",
            "cmake_version": "3.22.1",
            "generator": "Unix Makefiles",
            "build_type": "Release",
            "cmake_definitions": ["CMAKE_BUILD_TYPE=Release"],
            "compile_commands": [
                "/usr/bin/c++ -O3 -DNDEBUG -std=c++17 -mavx2 -mfma -fopenmp"
            ],
            "link_commands": ["/usr/bin/c++ -fopenmp synthetic.o -o synthetic"],
        },
        "dataset": FIXTURE_DATASET_IDENTITY,
        "parameters": parameters,
        "resource_budget": {"memory_budget_bytes": 4096, "thread_budget": 6},
        "input_artifacts": [],
        "output_artifacts": [],
        "status": "SUCCEEDED",
        "started_at": "2026-08-27T00:00:00Z",
        "ended_at": "2026-08-27T00:00:01Z",
    }


def _write_compact_json(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )


def _synthetic_exact_parity_package(
    fixture: dict[str, object], monkeypatch: pytest.MonkeyPatch
) -> tuple[dict[str, object], dict[str, object]]:
    workspace = fixture["workspace"]
    project_source = {
        "repository": "https://github.com/krystaZeng/diskann_search_benchmark.git",
        "branch": "feature/framework-baseline-certification",
        "commit": FROZEN_RUNNER_IDENTITY["project_commit"],
        "dirty_worktree": False,
    }
    upstream_source = {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": FROZEN_RUNNER_IDENTITY["diskann_commit"],
        "dirty_worktree": False,
    }
    runner_path = workspace / "parity/runner-build.json"
    _write_compact_json(
        runner_path,
        _parity_build_manifest(
            implementation_id="cpp_main_post_graph_disk_layout_v1",
            source=project_source,
            parameters={
                "runner_binary": {
                    "path": "/synthetic/runner",
                    "sha256": FROZEN_RUNNER_IDENTITY["runner_sha256"],
                },
                "diskann_source": upstream_source,
            },
            stage="build",
        ),
    )


    package: dict[str, object] = {"oracle_sha256": "9" * 64}
    evidence_refs: dict[str, object] = {}
    paths: dict[str, object] = {"runner": runner_path, "evidence": {}}
    for cache_key, cache_mode in (("zero", "zero"), ("fixed", "fixed_bfs")):
        declaration = _protocol_declaration(
            role="reference",
            cache_mode=cache_mode,
            runner_sha=FROZEN_RUNNER_IDENTITY["runner_sha256"],
            query_set=fixture["query_reference"],
            ground_truth=fixture["ground_truth"],
        )
        declaration["protocol_id"] = f"synthetic-parity-{cache_mode}"
        declaration["search_execution"]["execution_id"] = declaration["protocol_id"]
        declaration["search_execution"]["L_search"] = [100, 200, 400]
        declaration["search_execution"]["runner_build"]["build_manifest_path"] = (
            runner_path.relative_to(workspace).as_posix()
        )
        declaration["search_execution"]["runner_build"]["build_manifest_sha256"] = (
            _sha256(runner_path)
        )
        protocol = search.normalize_search_protocol(declaration)
        protocol_path = workspace / f"parity/{cache_mode}-protocol.json"
        _write_compact_json(protocol_path, protocol)

        points: list[dict[str, object]] = []
        point_paths: list[tuple[Path, Path]] = []
        for l_search in (100, 200, 400):
            for repetition in (0, 1, 2):
                point_id = f"{cache_mode}-L{l_search}-r{repetition}"
                observation_path = workspace / f"parity/observations/{point_id}.json"
                observation = {
                    "observation_id": point_id,
                    "implementation": {
                        "implementation_id": FROZEN_RUNNER_IDENTITY["implementation_id"]
                    },
                    "source_build": {
                        "diskann_repository": upstream_source["repository"],
                        "diskann_branch": upstream_source["branch"],
                        "diskann_commit": upstream_source["commit"],
                        "binary_sha256": FROZEN_RUNNER_IDENTITY["runner_sha256"],
                    },
                    "protocol": {
                        "path": protocol_path.relative_to(workspace).as_posix(),
                        "protocol_id": protocol["protocol_id"],
                        "protocol_checksum": protocol["protocol_checksum"],
                    },
                    "search_execution": {
                        "execution_id": protocol["search_execution"]["execution_id"],
                        "canonical_checksum": protocol["search_execution_checksum"],
                    },
                }
                _write_compact_json(observation_path, observation)

                oracle_path = workspace / f"parity/oracle/{point_id}.json"
                oracle = _parity_build_manifest(
                    implementation_id="cpp_main_search_disk_index_oracle_v1",
                    source=upstream_source,
                    parameters={
                        "argv": ["search_disk_index", "--search_list", str(l_search)],
                        "oracle_binary": {
                            "path": "/synthetic/search_disk_index",
                            "sha256": package["oracle_sha256"],
                        },
                        "protocol_checksum": protocol["protocol_checksum"],
                        "search_execution_checksum": protocol["search_execution_checksum"],
                    },
                    stage="search",
                )
                _write_compact_json(oracle_path, oracle)
                points.append(
                    {
                        "L_search": l_search,
                        "repetition_index": repetition,
                        "result": "PASS",
                        "ordered_ids_equal": True,
                        "distances_within_tolerance": True,
                        "mean_ios_equal": True,
                        "observation_path": observation_path.relative_to(workspace).as_posix(),
                        "observation_sha256": _sha256(observation_path),
                        "oracle_manifest_path": oracle_path.relative_to(workspace).as_posix(),
                        "oracle_manifest_sha256": _sha256(oracle_path),
                    }
                )
                point_paths.append((observation_path, oracle_path))
        evidence_path = workspace / f"parity/{cache_mode}.json"
        evidence = {
            "result": "PASS",
            "instrumented_exit_code": 0,
            "controls": {
                "cache": {"mode": cache_mode},
                "search_execution_checksum": protocol["search_execution_checksum"],
            },
            "points": points,
        }
        _write_compact_json(evidence_path, evidence)
        package[cache_key] = {
            "evidence_sha256": _sha256(evidence_path),
            "protocol_file_sha256": _sha256(protocol_path),
            "protocol_checksum": protocol["protocol_checksum"],
            "search_execution_checksum": protocol["search_execution_checksum"],
        }
        evidence_refs[cache_key] = _document_ref(workspace, evidence_path)
        paths["evidence"][cache_key] = evidence_path
        paths[f"{cache_key}_protocol"] = protocol_path
        paths[f"{cache_key}_points"] = point_paths

    monkeypatch.setattr(fbc, "FROZEN_RUNNER_PARITY_PACKAGE", package)
    return (
        {
            "result": "PASS",
            "identity": FROZEN_RUNNER_IDENTITY,
            "runner_build_manifest": _document_ref(workspace, runner_path),
            "evidence": evidence_refs,
            "reason": "synthetic exact PASS package",
        },
        paths,
    )


def _admit_synthetic_parity(
    fixture: dict[str, object], parity_input: dict[str, object]
) -> dict[str, str]:
    return fbc._admit_search_runner_parity(
        fixture["workspace"],
        parity_input,
        repository_root=REPOSITORY_ROOT,
    )


def test_exact_synthetic_search_runner_parity_package_is_admitted(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    parity_input, _ = _synthetic_exact_parity_package(fixture, monkeypatch)
    assert _admit_synthetic_parity(fixture, parity_input)["result"] == "PASS"


def test_old_search_runner_parity_identity_is_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    parity_input, _ = _synthetic_exact_parity_package(fixture, monkeypatch)
    parity_input["identity"] = {
        **FROZEN_RUNNER_IDENTITY,
        "project_commit": "10f8520d8617fdaf8de3abf8a87842346ae4c96f",
        "runner_sha256": "97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856",
    }
    assert _admit_synthetic_parity(fixture, parity_input)["result"] == "INCONCLUSIVE"


@pytest.mark.parametrize("mutation", ["source", "runner_sha", "diskann", "status"])
def test_runner_provenance_mismatch_is_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mutation: str
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    parity_input, paths = _synthetic_exact_parity_package(fixture, monkeypatch)
    runner_path = paths["runner"]
    runner = json.loads(runner_path.read_text(encoding="utf-8"))
    if mutation == "source":
        runner["source"]["repository"] = "https://example.invalid/not-project.git"
    elif mutation == "runner_sha":
        runner["parameters"]["runner_binary"]["sha256"] = "0" * 64
    elif mutation == "diskann":
        runner["parameters"]["diskann_source"]["commit"] = "0" * 40
    else:
        runner["status"] = "CANCELLED"
    _write_compact_json(runner_path, runner)
    parity_input["runner_build_manifest"] = _document_ref(
        fixture["workspace"], runner_path
    )
    assert _admit_synthetic_parity(fixture, parity_input)["result"] == "INCONCLUSIVE"


@pytest.mark.parametrize(
    "mutation", ["evidence_sha", "execution_checksum", "missing", "duplicate", "point_fail"]
)
def test_parity_evidence_or_matrix_mismatch_is_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mutation: str
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    parity_input, paths = _synthetic_exact_parity_package(fixture, monkeypatch)
    evidence_path = paths["evidence"]["zero"]
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    if mutation == "evidence_sha":
        fbc.FROZEN_RUNNER_PARITY_PACKAGE["zero"]["evidence_sha256"] = "0" * 64
    else:
        if mutation == "execution_checksum":
            evidence["controls"]["search_execution_checksum"] = "0" * 64
        elif mutation == "missing":
            evidence["points"].pop()
        elif mutation == "duplicate":
            evidence["points"][-1] = copy.deepcopy(evidence["points"][0])
        else:
            evidence["points"][0]["result"] = "FAIL"
        _write_compact_json(evidence_path, evidence)
        parity_input["evidence"]["zero"] = _document_ref(
            fixture["workspace"], evidence_path
        )
        fbc.FROZEN_RUNNER_PARITY_PACKAGE["zero"]["evidence_sha256"] = _sha256(
            evidence_path
        )
    assert _admit_synthetic_parity(fixture, parity_input)["result"] == "INCONCLUSIVE"


@pytest.mark.parametrize("mutation", ["observation", "protocol", "oracle"])
def test_parity_linked_document_identity_mismatch_is_inconclusive(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mutation: str
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    parity_input, paths = _synthetic_exact_parity_package(fixture, monkeypatch)
    if mutation == "protocol":
        path = paths["zero_protocol"]
    else:
        observation_path, oracle_path = paths["zero_points"][0]
        path = observation_path if mutation == "observation" else oracle_path
    path.write_bytes(path.read_bytes() + b"\n")
    assert _admit_synthetic_parity(fixture, parity_input)["result"] == "INCONCLUSIVE"


def _decision_input(
    fixture: dict[str, object],
    quality_path: Path,
    *,
    parity_input: dict[str, object],
    structural: str = "PASS",
    mandatory: str = "PASS",
    quality: str = "PASS",
) -> dict[str, object]:
    workspace = fixture["workspace"]
    policy_path = workspace / "policy.json"
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    structural_policy = _write_json_ref(
        workspace, "evidence/structural-policy.json", {"policy": "frozen-feature-002"}
    )
    structural_report = _write_json_ref(
        workspace, "evidence/structural-report.json", {"gate": {"result": structural}}
    )
    admission_ref = _write_json_ref(
        workspace, "evidence/admission.json", {"result": mandatory}
    )
    validation_ref = _write_json_ref(
        workspace, "evidence/input-validation.json", {"result": "VALID"}
    )
    protocol_refs = {
        role: [
            {
                key: value
                for key, value in policy["protocols"][role][cache].items()
                if key in {"path", "byte_length", "sha256"}
            }
            for cache in ("zero", "fixed")
        ]
        for role in ("reference", "framework")
    }
    quality_evidence = json.loads(quality_path.read_text(encoding="utf-8"))
    report_refs = {
        role: [
            {key: value for key, value in item.items() if key in {"path", "byte_length", "sha256"}}
            for item in quality_evidence["evaluation_reports"][role]
        ]
        for role in ("reference", "framework")
    }
    observation_refs = {
        role: [
            {key: item[key] for key in ("path", "byte_length", "sha256")}
            for item in quality_evidence["observation_attempts"][role]
        ]
        for role in ("reference", "framework")
    }
    manifests = [
        _document_ref(workspace, path)
        for paths in fixture["manifest_paths"].values()
        for path in paths.values()
    ]
    return {
        "workspace_root": str(workspace),
        "generated_at": "2026-08-26T04:00:00Z",
        "structural_gate": {
            "policy": structural_policy,
            "report": structural_report,
            "result": structural,
        },
        "search_runner_parity": parity_input,
        "mandatory_admission": {
            "result": mandatory,
            "checks": [
                {
                    "check_id": "controlled-inputs",
                    "status": "PASS" if mandatory == "PASS" else ("FAIL" if mandatory == "FAIL" else "MISSING"),
                    "reason": "fixture admission",
                    "evidence": [admission_ref],
                }
            ],
        },
        "artifacts": policy["artifacts"],
        "shared_inputs": {
            key: {
                name: value
                for name, value in reference.items()
                if name in {"path", "byte_length", "sha256"}
            }
            for key, reference in policy["shared_inputs"].items()
        },
        "protocols": protocol_refs,
        "observations": observation_refs,
        "evaluation_reports": report_refs,
        "quality_parity": {
            "result": quality,
            "policy": _document_ref(workspace, policy_path),
            "evidence": _document_ref(workspace, quality_path),
            "reason": "fixture quality evidence",
        },
        "build_manifests": manifests,
        "validation_evidence": [validation_ref],
        "audit_exclusions": quality_evidence["audit_exclusions"],
    }


@pytest.mark.parametrize(
    ("structural", "parity", "mandatory", "quality", "expected"),
    [
        ("PASS", "PASS", "PASS", "PASS", "CERTIFIED"),
        ("FAIL", "PASS", "PASS", "PASS", "NOT_CERTIFIED"),
        ("NOT_REACHED", "FAIL", "FAIL", "FAIL", "INCONCLUSIVE"),
        ("PASS", "FAIL", "PASS", "PASS", "NOT_CERTIFIED"),
        ("PASS", "PASS", "FAIL", "PASS", "NOT_CERTIFIED"),
        ("PASS", "PASS", "PASS", "FAIL", "NOT_CERTIFIED"),
        ("PASS", "INCONCLUSIVE", "PASS", "PASS", "INCONCLUSIVE"),
    ],
)
def test_final_decision_precedence_and_exact_runner_parity_identity(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    structural: str,
    parity: str,
    mandatory: str,
    quality: str,
    expected: str,
) -> None:
    fixture = _feature005_two_graph_fixture(
        tmp_path, monkeypatch, framework_missed_rows=2 if quality == "FAIL" else 1
    )
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy_path = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy_path, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    quality_path = workspace / "quality.json"
    fbc.compare_quality(
        policy_path, compare_input, quality_path, repository_root=REPOSITORY_ROOT
    )
    parity_input, _ = _synthetic_exact_parity_package(fixture, monkeypatch)
    if parity != "PASS":
        monkeypatch.setattr(
            fbc,
            "_admit_search_runner_parity",
            lambda *args, **kwargs: {
                "result": parity,
                "reason": f"synthetic decision-precedence {parity}",
            },
        )
    decision_input = workspace / "decision-input.json"
    decision_input.write_text(
        json.dumps(
            _decision_input(
                fixture,
                quality_path,
                parity_input=parity_input,
                structural=structural,
                mandatory=mandatory,
                quality=quality,
            )
        ),
        encoding="utf-8",
    )
    decision_path = workspace / "decision.json"
    assert fbc.decide(
        decision_input, decision_path, repository_root=REPOSITORY_ROOT
    )["decision"] == expected


def test_stale_runner_identity_is_inconclusive_not_reusable(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fixture = _feature005_two_graph_fixture(tmp_path, monkeypatch)
    workspace = fixture["workspace"]
    freeze_input = workspace / "freeze.json"
    freeze_input.write_text(json.dumps(_freeze_input(fixture)), encoding="utf-8")
    policy_path = workspace / "policy.json"
    fbc.freeze_quality_policy(freeze_input, policy_path, repository_root=REPOSITORY_ROOT)
    compare_input = workspace / "compare.json"
    compare_input.write_text(json.dumps(_comparison_input(fixture)), encoding="utf-8")
    quality_path = workspace / "quality.json"
    fbc.compare_quality(
        policy_path, compare_input, quality_path, repository_root=REPOSITORY_ROOT
    )
    parity_input, _ = _synthetic_exact_parity_package(fixture, monkeypatch)
    stale = {**FROZEN_RUNNER_IDENTITY, "runner_sha256": "0" * 64}
    parity_input["identity"] = stale
    decision_input = workspace / "decision-input.json"
    decision_input.write_text(
        json.dumps(
            _decision_input(
                fixture,
                quality_path,
                parity_input=parity_input,
            )
        ),
        encoding="utf-8",
    )
    output = workspace / "decision.json"
    result = fbc.decide(decision_input, output, repository_root=REPOSITORY_ROOT)
    assert result["decision"] == "INCONCLUSIVE"
    assert json.loads(output.read_text(encoding="utf-8"))["search_runner_parity"]["result"] == "INCONCLUSIVE"
