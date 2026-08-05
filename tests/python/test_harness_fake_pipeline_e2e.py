from __future__ import annotations

import json
from pathlib import Path

from diskann_harness.cli import main
from diskann_harness.runner import build_project, execute_configuration


ROOT = Path(__file__).resolve().parents[2]
FAKE_RUNNER = ROOT / "build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner"


def test_build_execute_validate_and_evaluate_lifecycles(tmp_path: Path) -> None:
    tiny_source = tmp_path / "tiny-source"
    tiny_source.mkdir()
    (tiny_source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(harness_smoke LANGUAGES NONE)\n",
        encoding="utf-8",
    )
    provenance = build_project(tiny_source, tmp_path / "tiny-build")
    assert provenance.is_file()

    workspace = tmp_path / "workspace"
    summary = execute_configuration(
        ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
        workspace,
        FAKE_RUNNER,
        repository_root=ROOT,
    )
    assert summary["aggregation"] == "retain_all"
    assert len(summary["retained_runs"]) == 2
    assert summary["deterministic_equal"] is True
    assert summary["distinct_run_provenance"] is True
    assert {item["outcome"] for item in summary["retained_runs"]} == {"SUCCEEDED"}

    artifact = summary["retained_runs"][0]["artifact"]
    artifact_ref = tmp_path / "artifact-ref.json"
    artifact_ref.write_text(json.dumps(artifact), encoding="utf-8")
    assert main(
        [
            "validate",
            "--artifact-ref",
            str(artifact_ref),
            "--workspace",
            str(workspace),
            "--runner",
            str(FAKE_RUNNER),
        ]
    ) == 0
    assert main(
        [
            "evaluate",
            "--artifact-ref",
            str(artifact_ref),
            "--workspace",
            str(workspace),
            "--runner",
            str(FAKE_RUNNER),
        ]
    ) == 0

    for directory in ("normal", "temporary", "manifests", "reports", "quarantine"):
        assert (workspace / directory).is_dir()
    run = summary["retained_runs"][0]
    assert run["artifact"]["metadata_path"].endswith("artifact.json")
    assert run["build_manifest_path"] != run["evaluation_report_path"]
    assert run["validation_evidence_path"] != run["evaluation_report_path"]

