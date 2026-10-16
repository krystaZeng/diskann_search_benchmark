"""User-facing Feature 001 Harness CLI."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Sequence
from pathlib import Path

from .config import ConfigurationError
from .manifests import ImmutableWriteError
from .native_dataset import CAPABILITY, FORMAT, NativeDatasetError, admit_dataset
from .runner import RunnerError, build_project, execute_configuration, run_single_request
from .workspace import WorkspaceError


CANONICAL_ARTIFACT_CONTRACTS: dict[str, dict[str, object]] = {
    "dataset": {
        "format_version": "fake-dataset-json/v1",
        "id_space": "none",
        "capabilities": ["dataset.vector_access/v1"],
    },
    "partition": {
        "format_version": "partition-artifact/v1",
        "id_space": "local",
        "capabilities": [
            "partition.complete_local_to_global_map/v1",
            "partition.overlap/v1",
        ],
    },
    "local_graph": {
        "format_version": "local-graph-artifact/v1",
        "id_space": "local",
        "capabilities": ["graph.local_adjacency/v1"],
    },
    "global_graph": {
        "format_version": "global-graph-artifact/v1",
        "id_space": "global",
        "capabilities": ["graph.global_adjacency/v1"],
    },
}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="diskann-harness")
    parser.add_argument(
        "operation",
        choices=("build", "execute", "validate", "evaluate", "admit-dataset"),
        nargs="?",
        help="Lifecycle to execute.",
    )
    parser.add_argument("--config")
    parser.add_argument("--artifact-ref")
    parser.add_argument("--workspace")
    parser.add_argument("--runner")
    parser.add_argument("--build-dir")
    parser.add_argument("--build-type", default="Debug")
    parser.add_argument("--source-dir", default=str(Path(__file__).resolve().parents[3]))
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.operation is None:
        parser.print_help()
        return 0
    try:
        if args.operation == "build":
            if not args.build_dir:
                raise RunnerError("build requires --build-dir")
            output = build_project(
                args.source_dir,
                args.build_dir,
                build_type=args.build_type,
                config_path=args.config,
            )
            print(output)
            return 0
        if args.operation == "admit-dataset":
            if not args.config or not args.workspace:
                raise RunnerError("admit-dataset requires --config and --workspace")
            declaration = json.loads(Path(args.config).read_text(encoding="utf-8"))
            result = admit_dataset(
                declaration, args.workspace,
                repository_root=Path(__file__).resolve().parents[3], source_dir=args.source_dir,
            )
            print(json.dumps(result, sort_keys=True))
            return 0
        if not args.workspace or not args.runner:
            raise RunnerError(f"{args.operation} requires --workspace and --runner")
        if args.operation == "execute":
            if not args.config:
                raise RunnerError("execute requires --config")
            summary = execute_configuration(args.config, args.workspace, args.runner)
            print(summary["summary_path"])
            return 0 if all(
                run["exit_code"] == 0 for run in summary["retained_runs"]
            ) else max(run["exit_code"] for run in summary["retained_runs"])

        if not args.artifact_ref:
            raise RunnerError(f"{args.operation} requires --artifact-ref")
        artifact = json.loads(Path(args.artifact_ref).read_text(encoding="utf-8"))
        if not isinstance(artifact, dict):
            raise RunnerError("ArtifactRef must be an object")
        artifact_type = artifact.get("artifact_type")
        if not isinstance(artifact_type, str) or artifact_type not in CANONICAL_ARTIFACT_CONTRACTS:
            raise RunnerError("unsupported Artifact type for Feature 001 lifecycle")
        contract = CANONICAL_ARTIFACT_CONTRACTS[artifact_type]
        if artifact_type == "dataset":
            if artifact.get("format_version") == FORMAT:
                contract = {"format_version": FORMAT, "id_space": "none", "capabilities": [CAPABILITY]}
            elif artifact.get("format_version") != contract["format_version"]:
                raise RunnerError("unsupported Dataset format/version")
        if not isinstance(artifact.get("aggregate_checksum"), str):
            raise RunnerError("ArtifactRef requires aggregate_checksum")
        request: dict[str, object] = {
            "request_version": "1.0.0",
            "request_id": (
                f"harness-{args.operation}-{artifact_type}-"
                f"{artifact['aggregate_checksum'][:12]}"
            ),
            "operation": args.operation,
            "study_class": "infrastructure_validation",
            "workspace": {"root": str(Path(args.workspace).resolve())},
            "target_artifact": artifact,
        }
        if args.operation == "validate":
            request["expected_contract"] = {
                "artifact_type": artifact_type,
                "accepted_format_versions": [contract["format_version"]],
                "id_space": contract["id_space"],
                "capabilities": contract["capabilities"],
            }
        else:
            request["evaluator"] = {
                "implementation_id": "fake_graph_integrity_evaluator",
                "parameters": {},
            }
        result = run_single_request(request, args.workspace, args.runner)
        print(json.dumps(result, sort_keys=True))
        return int(result["exit_code"])
    except (ConfigurationError, RunnerError, WorkspaceError, NativeDatasetError,
            ImmutableWriteError, OSError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
