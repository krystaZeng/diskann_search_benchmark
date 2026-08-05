from __future__ import annotations

import json
from pathlib import Path

import pytest

from diskann_harness.cli import main
from diskann_harness.config import ConfigurationError
from diskann_harness.runner import (
    _schema_validator,
    build_project,
    execute_configuration,
    run_single_request,
)


ROOT = Path(__file__).resolve().parents[2]
FAKE_RUNNER = ROOT / "build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner"


def test_build_execute_validate_and_evaluate_lifecycles(tmp_path: Path) -> None:
    build_dir = tmp_path / "feature-build"
    provenance = build_project(
        ROOT,
        build_dir,
        repository_root=ROOT,
        config_path=ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
    )
    assert provenance.is_file()
    build_manifest = json.loads(provenance.read_text(encoding="utf-8"))
    assert build_manifest["status"] == "SUCCEEDED"
    assert len(build_manifest["source"]["commit"]) == 40
    assert build_manifest["toolchain"]["compiler_path"]
    assert len(build_manifest["toolchain"]["compile_commands"]) == 2
    assert build_manifest["toolchain"]["link_commands"] == []
    assert any(
        item.startswith("PROJECT_COMPILE_OPTIONS=")
        for item in build_manifest["toolchain"]["cmake_definitions"]
    )
    assert set(build_manifest["parameters"]["source_config"]) >= {
        "yaml_sha256",
        "normalized_json_sha256",
    }
    clean_build_runner = (
        build_dir / "tests/cpp/fakes/diskann_pipeline_fake_runner"
    )

    workspace = tmp_path / "workspace"
    summary = execute_configuration(
        ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
        workspace,
        clean_build_runner,
        repository_root=ROOT,
    )
    assert summary["build_manifest"]["path"] == provenance.as_posix()
    assert len(summary["build_manifest"]["sha256"]) == 64
    assert summary["aggregation"] == "retain_all"
    assert len(summary["retained_runs"]) == 2
    assert summary["deterministic_equal"] is True
    assert summary["distinct_run_provenance"] is True
    assert {item["outcome"] for item in summary["retained_runs"]} == {"SUCCEEDED"}
    terminal_manifest = json.loads(
        (
            workspace
            / summary["retained_runs"][0]["build_manifest_path"]
        ).read_text(encoding="utf-8")
    )
    assert set(terminal_manifest["parameters"]["source_config"]) == {
        "yaml_sha256",
        "normalized_json_sha256",
    }
    assert terminal_manifest["source"] == build_manifest["source"]
    assert terminal_manifest["toolchain"] == build_manifest["toolchain"]
    assert terminal_manifest["started_at"] <= terminal_manifest["ended_at"]

    first_run = summary["retained_runs"][0]
    artifact = first_run["artifact"]
    run_id = first_run["request_id"]
    refs = []
    for relative in (
        "dataset/artifact.json",
        f"{run_id}/partition/artifact.json",
        f"{run_id}/local-0/artifact.json",
        f"{run_id}/merge/artifact.json",
    ):
        metadata = json.loads(
            (workspace / "normal" / relative).read_text(encoding="utf-8")
        )
        refs.append(
            {
                "metadata_path": relative,
                "artifact_type": metadata["artifact_type"],
                "format_version": metadata["format_version"],
                "aggregate_checksum": metadata["aggregate_checksum"],
            }
        )
    for index, ref in enumerate(refs):
        artifact_ref = tmp_path / f"artifact-ref-{index}.json"
        artifact_ref.write_text(json.dumps(ref), encoding="utf-8")
        assert main(
            [
                "validate",
                "--artifact-ref",
                str(artifact_ref),
                "--workspace",
                str(workspace),
                "--runner",
                str(clean_build_runner),
            ]
        ) == 0

    artifact_ref = tmp_path / "artifact-ref-global.json"
    artifact_ref.write_text(json.dumps(artifact), encoding="utf-8")
    mismatch = run_single_request(
        {
            "request_version": "1.0.0",
            "request_id": "validate-contract-mismatch",
            "operation": "validate",
            "study_class": "infrastructure_validation",
            "workspace": {"root": str(workspace)},
            "target_artifact": artifact,
            "expected_contract": {
                "artifact_type": "local_graph",
                "accepted_format_versions": ["local-graph-artifact/v1"],
                "id_space": "local",
                "capabilities": ["graph.local_adjacency/v1"],
            },
        },
        workspace,
        clean_build_runner,
        repository_root=ROOT,
    )
    assert mismatch["exit_code"] == 6
    assert mismatch["outcome"] == "FAILED"

    unvalidated_root = workspace / "normal" / "unvalidated"
    unvalidated_root.mkdir()
    global_metadata = json.loads(
        (
            workspace / "normal" / artifact["metadata_path"]
        ).read_text(encoding="utf-8")
    )
    (unvalidated_root / "artifact.json").write_text(
        json.dumps(global_metadata), encoding="utf-8"
    )
    unvalidated = run_single_request(
        {
            "request_version": "1.0.0",
            "request_id": "validate-unvalidated",
            "operation": "validate",
            "study_class": "infrastructure_validation",
            "workspace": {"root": str(workspace)},
            "target_artifact": {
                **artifact,
                "metadata_path": "unvalidated/artifact.json",
            },
            "expected_contract": {
                "artifact_type": "global_graph",
                "accepted_format_versions": ["global-graph-artifact/v1"],
                "id_space": "global",
                "capabilities": ["graph.global_adjacency/v1"],
            },
        },
        workspace,
        clean_build_runner,
        repository_root=ROOT,
    )
    assert unvalidated["exit_code"] == 6
    assert unvalidated["outcome"] == "FAILED"

    with pytest.raises(ConfigurationError):
        run_single_request(
            {
                "request_version": "1.0.0",
                "request_id": "validate-quarantine-path",
                "operation": "validate",
                "study_class": "infrastructure_validation",
                "workspace": {"root": str(workspace)},
                "target_artifact": {
                    **artifact,
                    "metadata_path": "../quarantine/invalid/candidate/artifact.json",
                },
                "expected_contract": {
                    "artifact_type": "global_graph",
                    "accepted_format_versions": ["global-graph-artifact/v1"],
                    "id_space": "global",
                    "capabilities": ["graph.global_adjacency/v1"],
                },
            },
            workspace,
            clean_build_runner,
            repository_root=ROOT,
        )

    for path in (workspace / "manifests").glob("*/stages/*.json"):
        _schema_validator("build-manifest.schema.json", ROOT).validate(
            json.loads(path.read_text(encoding="utf-8"))
        )
    for path in (workspace / "manifests" / "evidence").glob("*.json"):
        _schema_validator("validation-evidence.schema.json", ROOT).validate(
            json.loads(path.read_text(encoding="utf-8"))
        )
    for path in (workspace / "reports").glob("*-evaluation.json"):
        _schema_validator("evaluation-report.schema.json", ROOT).validate(
            json.loads(path.read_text(encoding="utf-8"))
        )
    artifact_schemas = {
        "dataset": "dataset-artifact.schema.json",
        "partition": "partition-artifact.schema.json",
        "local_graph": "local-graph-artifact.schema.json",
        "global_graph": "global-graph-artifact.schema.json",
    }
    for path in (workspace / "normal").glob("**/artifact.json"):
        document = json.loads(path.read_text(encoding="utf-8"))
        _schema_validator(artifact_schemas[document["artifact_type"]], ROOT).validate(
            document
        )
    assert main(
        [
            "evaluate",
            "--artifact-ref",
            str(artifact_ref),
            "--workspace",
            str(workspace),
            "--runner",
            str(clean_build_runner),
        ]
    ) == 0

    for directory in ("normal", "temporary", "manifests", "reports", "quarantine"):
        assert (workspace / directory).is_dir()
    run = summary["retained_runs"][0]
    assert run["artifact"]["metadata_path"].endswith("artifact.json")
    assert run["build_manifest_path"] != run["evaluation_report_path"]
    assert run["validation_evidence_path"] != run["evaluation_report_path"]
