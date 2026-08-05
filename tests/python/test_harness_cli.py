from __future__ import annotations

import json
from pathlib import Path

import pytest

from diskann_harness.cli import main
from diskann_harness.cli import build_parser
from diskann_harness.runner import EXIT_MEANINGS, RunnerError, build_runner_command, run_runner
from diskann_harness.workspace import WorkspaceError, normalize_workspace, resolve_confined_path


def test_runner_command_is_an_argument_array(tmp_path: Path) -> None:
    command = build_runner_command(
        tmp_path / "runner",
        "execute",
        tmp_path / "request.json",
        tmp_path / "result.json",
    )
    assert command == [
        str(tmp_path / "runner"),
        "execute",
        "--request",
        str(tmp_path / "request.json"),
        "--result",
        str(tmp_path / "result.json"),
    ]


def test_workspace_normalization_and_confinement(tmp_path: Path) -> None:
    roots = normalize_workspace(tmp_path / "workspace")
    assert roots["normal_root"] != roots["quarantine_root"]
    assert roots["report_root"] != roots["normal_root"]
    assert resolve_confined_path(roots["normal_root"], "a/b.json").is_absolute()
    with pytest.raises(WorkspaceError):
        resolve_confined_path(roots["normal_root"], "../escape")


def test_all_harness_lifecycles_have_stable_argument_shapes() -> None:
    parser = build_parser()
    for operation in ("build", "execute", "validate", "evaluate"):
        assert parser.parse_args([operation]).operation == operation
    assert EXIT_MEANINGS == {
        0: "success",
        2: "usage_or_request_invalid",
        3: "preflight_rejected",
        4: "stage_failed",
        5: "invalid_output",
        6: "cancelled",
        7: "validation_or_evaluation_failed",
        10: "internal_error",
    }


def test_invalid_runner_json_has_stable_error(tmp_path: Path) -> None:
    runner = tmp_path / "runner"
    runner.write_text("#!/bin/sh\nprintf 'not-json' > \"$5\"\nexit 0\n", encoding="utf-8")
    runner.chmod(0o755)
    request = tmp_path / "request.json"
    request.write_text('{"operation":"execute"}', encoding="utf-8")
    with pytest.raises(RunnerError, match="runner result"):
        run_runner(runner, request, tmp_path / "result.json")


def test_cli_propagates_configuration_error(tmp_path: Path) -> None:
    config = tmp_path / "bad.yaml"
    config.write_text("workspace: &w\n  root: .\nworkspace: *w\n", encoding="utf-8")
    assert main(["execute", "--config", str(config), "--workspace", str(tmp_path),
                 "--runner", str(tmp_path / "missing")]) == 2
