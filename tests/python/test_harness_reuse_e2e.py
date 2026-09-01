from __future__ import annotations

import hashlib
import json
from pathlib import Path

from diskann_harness.runner import execute_configuration


ROOT = Path(__file__).resolve().parents[2]
FAKE_RUNNER = ROOT / "build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner"
BUILD_MANIFEST = ROOT / "build/feature-001/build-manifest.json"


def test_valid_reuse_skips_partition_and_records_lineage(tmp_path: Path) -> None:
    workspace = tmp_path / "reuse"
    summary = execute_configuration(
        ROOT / "tests/fixtures/fake_pipeline/reuse-partition.yaml",
        workspace,
        FAKE_RUNNER,
        repository_root=ROOT,
    )
    assert summary["build_manifest"] == {
        "path": BUILD_MANIFEST.as_posix(),
        "sha256": hashlib.sha256(BUILD_MANIFEST.read_bytes()).hexdigest(),
    }
    assert summary["deterministic_equal"] is True
    for run in summary["retained_runs"]:
        assert run["outcome"] == "SUCCEEDED"
        assert run["stage_invocations"] == {
            "partition": 0,
            "local_build": 2,
            "merge": 1,
        }
        manifest = json.loads(
            (
                workspace
                / "manifests"
                / run["request_id"]
                / "stages/partition-reused.json"
            ).read_text(encoding="utf-8")
        )
        assert manifest["status"] == "REUSED"
        assert manifest["reuse"]["reused"] is True
        assert manifest["reuse"]["source_artifact"]["aggregate_checksum"] == (
            "a640037c2d07b2dcdff98cd22b82b5c4f5da7e6323fc8da2a61538c1cbbb2984"
        )
        assert manifest["validation_evidence_path"].startswith(
            "manifests/evidence/"
        )
        assert (workspace / manifest["validation_evidence_path"]).is_file()


def test_mismatched_reuse_is_immutable_and_never_consumed(tmp_path: Path) -> None:
    workspace = tmp_path / "mismatch"
    summary = execute_configuration(
        ROOT / "tests/fixtures/fake_pipeline/reuse-mismatch.yaml",
        workspace,
        FAKE_RUNNER,
        repository_root=ROOT,
    )
    for run in summary["retained_runs"]:
        assert run["outcome"] == "FAILED"
        assert run["stage_invocations"] == {
            "partition": 0,
            "local_build": 0,
            "merge": 0,
        }
        assert run["failure"]["category"] in {
            "unsupported_version",
            "schema_invalid",
            "reuse_mismatch",
        }
        assert (workspace / run["validation_evidence_path"]).is_file()
    metadata = workspace / "normal/reused/partition/artifact.json"
    assert metadata.is_file()
    assert json.loads(metadata.read_text(encoding="utf-8"))["format_version"] == (
        "partition-artifact/v1"
    )
