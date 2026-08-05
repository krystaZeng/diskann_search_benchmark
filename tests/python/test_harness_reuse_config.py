from __future__ import annotations

from pathlib import Path

from diskann_harness.config import load_yaml_file, normalize_run_request


ROOT = Path(__file__).resolve().parents[2]


def test_reuse_configuration_retains_identity_and_parameters(tmp_path: Path) -> None:
    configuration = load_yaml_file(
        ROOT / "tests/fixtures/fake_pipeline/reuse-partition.yaml"
    )
    configuration["workspace"]["root"] = str(tmp_path / "reuse")
    normalized = normalize_run_request(configuration, repository_root=ROOT)
    assert normalized["reuse_artifact"]["artifact_type"] == "partition"
    assert normalized["implementations"]["partition"] == "fake_partition_v1"
    assert normalized["stage_parameters"]["merge"]["declared_max_degree"] == 3


def test_reuse_mismatch_is_retained_for_runtime_refusal(tmp_path: Path) -> None:
    configuration = load_yaml_file(
        ROOT / "tests/fixtures/fake_pipeline/reuse-mismatch.yaml"
    )
    configuration["workspace"]["root"] = str(tmp_path / "reuse-mismatch")
    normalized = normalize_run_request(configuration, repository_root=ROOT)
    assert normalized["reuse_artifact"]["format_version"] == "partition-artifact/v2"
    assert normalized["reuse_artifact"]["format_version"] != "partition-artifact/v1"
