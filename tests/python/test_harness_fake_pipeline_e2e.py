from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
from pathlib import Path

import pytest

from diskann_harness.cli import main
from diskann_harness.config import (
    ConfigurationError,
    load_yaml_file,
    normalize_run_request,
)
from diskann_harness.runner import (
    _canonical_json_sha256,
    _schema_validator,
    build_project,
    execute_configuration,
    run_single_request,
)


ROOT = Path(__file__).resolve().parents[2]
FAKE_RUNNER = ROOT / "build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner"


@pytest.fixture(scope="module")
def artifact_ref_runners() -> tuple[Path, Path]:
    # Allow an explicitly rebuilt out-of-tree test build, without falling back
    # to stale binaries when that build is missing a target.
    build_dir = Path(os.environ.get("DISKANN_TEST_BUILD_DIR", ROOT / "build/feature-001"))
    runners = (
        build_dir / "tests/cpp/fakes/diskann_pipeline_fake_runner",
        build_dir / "src/cli/diskann_pipeline_runner",
    )
    for runner in runners:
        assert runner.is_file(), f"Build the real C++ runner first: {runner}"
    return runners


def _invoke_identity_runner(
    runner: Path, request: dict, output_dir: Path
) -> tuple[int, dict]:
    output_dir.mkdir()
    request_path = output_dir / "request.json"
    result_path = output_dir / "result.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    completed = subprocess.run(
        [str(runner), request["operation"], "--request", str(request_path),
         "--result", str(result_path)],
        capture_output=True, text=True, check=False,
    )
    assert result_path.is_file(), completed.stderr
    return completed.returncode, json.loads(result_path.read_text(encoding="utf-8"))


def _file_checksums(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in root.rglob("*") if path.is_file()
    }


@pytest.mark.parametrize("catalogued", [True, False], ids=["catalog", "fallback"])
@pytest.mark.parametrize(
    "operation,target_kind", [("validate", "dataset"), ("validate", "global_graph"),
                              ("evaluate", "global_graph")],
)
@pytest.mark.parametrize(
    "field,category",
    [(None, None), ("aggregate_checksum", "checksum_mismatch"),
     ("artifact_type", "schema_invalid"), ("format_version", "unsupported_version")],
    ids=["valid", "checksum", "type", "version"],
)
def test_artifact_ref_identity_real_runner(
    tmp_path: Path, artifact_ref_runners: tuple[Path, Path], catalogued: bool,
    operation: str, target_kind: str, field: str | None, category: str | None,
) -> None:
    fake_runner, production_runner = artifact_ref_runners
    config = load_yaml_file(ROOT / "tests/fixtures/fake_pipeline/experiment.yaml")
    workspace = tmp_path / "workspace"
    config["workspace"]["root"] = str(workspace)
    setup = normalize_run_request(config, repository_root=ROOT)
    setup_code, built = _invoke_identity_runner(fake_runner, setup, tmp_path / "setup")
    assert setup_code == 0 and built["outcome"] == "SUCCEEDED", built
    original = dict(setup["dataset"] if target_kind == "dataset" else built["artifact"])
    metadata_path = workspace / "normal" / original["metadata_path"]
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if target_kind == "global_graph":
        assert len(metadata["source_artifacts"]) == 3  # Partition + two LocalGraphs
    if not catalogued:
        replacement = metadata_path.with_name("uncatalogued.json")
        metadata_path.rename(replacement)
        original["metadata_path"] = str(replacement.relative_to(workspace / "normal"))

    normal_before = _file_checksums(workspace / "normal")
    reports_before = _file_checksums(workspace / "reports")
    target = dict(original)
    if field is not None:
        target[field] = {
            "aggregate_checksum": "0" * 64,
            "artifact_type": "partition",
            "format_version": ("global-graph-artifact/v2" if target_kind == "global_graph"
                               else "fake-dataset-json/v2"),
        }[field]
        assert {key for key in target if target[key] != original[key]} == {field}
    request = {
        "request_version": "1.0.0", "request_id": "artifact-ref-identity",
        "operation": operation, "study_class": "infrastructure_validation",
        "workspace": setup["workspace"], "target_artifact": target,
    }
    if operation == "validate":
        # Keep the consumer contract correct: only the caller's ArtifactRef is bad.
        request["expected_contract"] = {
            "artifact_type": metadata["artifact_type"],
            "accepted_format_versions": [metadata["format_version"]],
            "id_space": metadata["id_space"], "capabilities": metadata["capabilities"],
        }
    else:
        request["evaluator"] = {
            "implementation_id": "fake_graph_integrity_evaluator", "parameters": {},
        }
    code, result = _invoke_identity_runner(production_runner, request, tmp_path / "call")
    assert _file_checksums(workspace / "normal") == normal_before
    expected_code = 0 if field is None else (6 if operation == "validate" else 7)
    assert code == expected_code, result
    assert result["exit_code"] == expected_code
    if field is None:
        assert result["outcome"] == "SUCCEEDED"
        assert result["artifact"] == original
        if operation == "evaluate":
            report = workspace / result["evaluation_report_path"]
            assert report.is_file()
            assert str(report.relative_to(workspace / "reports")) not in reports_before
    else:
        assert result["outcome"] == "FAILED"
        assert result["failure"]["category"] == category
        assert "artifact" not in result
        assert "evaluation_report_path" not in result
        assert _file_checksums(workspace / "reports") == reports_before
    if operation == "validate":
        evidence = json.loads(
            (workspace / result["validation_evidence_path"]).read_text(encoding="utf-8")
        )
        assert evidence["artifact_candidate"] == {
            "metadata_path": target["metadata_path"], "artifact_type": target["artifact_type"],
            "format_version": target["format_version"],
            "claimed_aggregate_checksum": target["aggregate_checksum"],
        }
        assert evidence["result"] == ("VALID" if field is None else "INVALID")


def test_build_execute_validate_and_evaluate_lifecycles(tmp_path: Path) -> None:
    build_dir = tmp_path / "feature-build"
    config_path = ROOT / "tests/fixtures/fake_pipeline/experiment.yaml"
    assert not build_dir.exists()
    provenance = build_project(
        ROOT,
        build_dir,
        repository_root=ROOT,
        config_path=config_path,
    )
    assert provenance == build_dir / "build-manifest.json"
    assert provenance.is_file()
    cache_path = build_dir / "CMakeCache.txt"
    assert cache_path.is_file()
    build_manifest = json.loads(provenance.read_text(encoding="utf-8"))
    _schema_validator("build-manifest.schema.json", ROOT).validate(build_manifest)
    assert build_manifest["stage"] == "build"
    assert build_manifest["status"] == "SUCCEEDED"

    git_root = Path(
        subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    ).resolve()
    expected_branch = subprocess.run(
        ["git", "-C", str(git_root), "branch", "--show-current"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip() or "detached-head"
    expected_commit = subprocess.run(
        ["git", "-C", str(git_root), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    expected_dirty = bool(
        subprocess.run(
            [
                "git",
                "-C",
                str(git_root),
                "status",
                "--porcelain=v1",
                "--untracked-files=all",
            ],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.rstrip()
    )
    assert build_manifest["source"]["repository"] == str(git_root)
    assert build_manifest["source"]["branch"] == expected_branch
    assert build_manifest["source"]["branch"] != "not-git"
    assert build_manifest["source"]["commit"] == expected_commit
    assert build_manifest["source"]["commit"] != "0" * 40
    assert build_manifest["source"]["dirty_worktree"] is expected_dirty
    if expected_dirty:
        assert len(build_manifest["source"]["dirty_source_checksum"]) == 64

    cache_entries: dict[str, str] = {}
    for line in cache_path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        key_type, value = line.split("=", 1)
        if ":" in key_type:
            key, _ = key_type.split(":", 1)
            cache_entries[key] = value
    toolchain = build_manifest["toolchain"]
    assert toolchain["compiler_path"] == cache_entries["CMAKE_CXX_COMPILER"]
    assert toolchain["compiler_path"] != "unavailable"
    assert toolchain["compiler_version"] != "unavailable"
    assert toolchain["cmake_version"] == subprocess.run(
        ["cmake", "--version"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()[0]
    assert toolchain["generator"] == cache_entries["CMAKE_GENERATOR"]
    assert toolchain["generator"] != "unknown"
    assert toolchain["build_type"] == cache_entries["CMAKE_BUILD_TYPE"]
    assert all("=" in definition for definition in toolchain["cmake_definitions"])
    configure_command, build_command = map(
        shlex.split, toolchain["compile_commands"]
    )
    assert configure_command == [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Debug",
    ]
    assert build_command == [
        "cmake",
        "--build",
        str(build_dir),
        "--parallel",
    ]
    assert toolchain["link_commands"] == []
    assert any(
        item.startswith("PROJECT_COMPILE_OPTIONS=")
        and bool(item.partition("=")[2])
        for item in toolchain["cmake_definitions"]
    )

    immutable = build_manifest["parameters"]["immutable_build_configuration"]
    assert immutable == {
        "source_dir": str(ROOT),
        "build_dir": str(build_dir),
        "build_type": toolchain["build_type"],
        "cmake_cache_sha256": hashlib.sha256(cache_path.read_bytes()).hexdigest(),
    }
    source_config = build_manifest["parameters"]["source_config"]
    expected_configuration = normalize_run_request(
        load_yaml_file(config_path), repository_root=ROOT
    )
    assert source_config == {
        "path": str(config_path.resolve()),
        "yaml_sha256": hashlib.sha256(config_path.read_bytes()).hexdigest(),
        "normalized_json_sha256": _canonical_json_sha256(expected_configuration),
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
    assert summary["build_manifest"]["sha256"] == hashlib.sha256(
        provenance.read_bytes()
    ).hexdigest()
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
