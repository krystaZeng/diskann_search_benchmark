from __future__ import annotations

from pathlib import Path

import pytest

from diskann_harness.config import (
    load_yaml_file,
    normalize_run_request,
    write_normalized_request,
)
from diskann_harness.manifests import ImmutableWriteError
from diskann_harness.workspace import WorkspaceError, resolve_confined_path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
EXAMPLE = (
    REPOSITORY_ROOT
    / "specs"
    / "001-modular-cpp-diskann-pipeline"
    / "contracts"
    / "examples"
    / "fake-experiment.yaml"
)


def test_example_normalizes_workspace_and_validates_schema(tmp_path: Path) -> None:
    configuration = load_yaml_file(EXAMPLE)
    configuration["workspace"]["root"] = str(tmp_path / "workspace")
    normalized = normalize_run_request(
        configuration, repository_root=REPOSITORY_ROOT
    )
    workspace = normalized["workspace"]
    assert Path(workspace["root"]).is_absolute()
    assert workspace["normal_root"] != workspace["quarantine_root"]
    assert workspace["manifest_root"] != workspace["report_root"]
    assert normalized["study_class"] == "infrastructure_validation"
    assert normalized["repetition_policy"] == {
        "count": 2,
        "aggregation": "retain_all",
    }


def test_immutable_normalized_request_and_path_escape_rejection(tmp_path: Path) -> None:
    target = tmp_path / "requests" / "request.json"
    write_normalized_request(target, {"request": "phase1"})
    with pytest.raises(ImmutableWriteError):
        write_normalized_request(target, {"request": "changed"})
    with pytest.raises(WorkspaceError):
        resolve_confined_path(tmp_path, "../escape.json")

