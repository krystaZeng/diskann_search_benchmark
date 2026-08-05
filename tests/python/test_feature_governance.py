from __future__ import annotations

import copy
import subprocess
from pathlib import Path

import pytest

from diskann_harness.config import ConfigurationError, load_yaml_file, normalize_run_request


ROOT = Path(__file__).resolve().parents[2]


def test_feature_configuration_is_frozen_infrastructure_validation(tmp_path: Path) -> None:
    configuration = load_yaml_file(ROOT / "tests/fixtures/fake_pipeline/experiment.yaml")
    configuration["workspace"]["root"] = str(tmp_path / "workspace")
    normalized = normalize_run_request(configuration, repository_root=ROOT)
    assert normalized["study_class"] == "infrastructure_validation"
    assert normalized["repetition_policy"] == {"count": 2, "aggregation": "retain_all"}
    assert normalized["dataset"]["aggregate_checksum"] == (
        "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d"
    )
    assert normalized["stage_parameters"]["merge"]["declared_max_degree"] == 3
    assert subprocess.check_output(
        ["git", "-C", str(ROOT), "branch", "--show-current"], text=True
    ).strip() == "master"
    assert len(
        subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True
        ).strip()
    ) == 40


def test_non_fake_dataset_and_parameter_search_are_rejected(tmp_path: Path) -> None:
    configuration = load_yaml_file(ROOT / "tests/fixtures/fake_pipeline/experiment.yaml")
    configuration["workspace"]["root"] = str(tmp_path / "workspace")
    non_fake = copy.deepcopy(configuration)
    non_fake["dataset"]["format_version"] = "real-dataset/v1"
    with pytest.raises(ConfigurationError, match="canonical Fake dataset"):
        normalize_run_request(non_fake, repository_root=ROOT)
    parameter_search = copy.deepcopy(configuration)
    parameter_search["parameter_search"] = {"merge_degree": [2, 3]}
    with pytest.raises(ConfigurationError):
        normalize_run_request(parameter_search, repository_root=ROOT)

