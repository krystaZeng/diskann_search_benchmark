from __future__ import annotations

import json
from pathlib import Path

import pytest

import diskann_harness.runner as runner_module
from diskann_harness.runner import RunnerError, execute_configuration


ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize(
    "mismatch", ["aggregate", "payload", "projection", "provenance"]
)
def test_two_successful_runs_fail_closed_when_identity_or_provenance_differs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mismatch: str
) -> None:
    def fake_run_runner(*args: object, **kwargs: object) -> dict[str, object]:
        request_path = Path(args[1])
        request = json.loads(request_path.read_text(encoding="utf-8"))
        return {"request_id": request["request_id"], "outcome": "SUCCEEDED"}

    def mismatched_identity(
        result: dict[str, object], roots: dict[str, str]
    ) -> dict[str, object]:
        run_id = str(result["request_id"])
        repetition = run_id[-1]
        return {
            "aggregate_checksum": (
                repetition if mismatch == "aggregate" else "same"
            ),
            "payload_checksums": [
                repetition if mismatch == "payload" else "same"
            ],
            "evaluation_projection": {
                "graph_integrity": "VALID",
                "logical_node_count": 6,
                "invalid_neighbor_count": 0,
                "declared_max_degree": 3,
                "observed_max_degree": (
                    int(repetition) if mismatch == "projection" else 3
                ),
                "degree_bound": True,
                "passed": True,
            },
            "report_id": (
                "same-report" if mismatch == "provenance" else f"report-{run_id}"
            ),
            "generated_at": (
                "2026-08-05T00:00:00Z"
                if mismatch == "provenance"
                else f"2026-08-05T00:00:0{repetition}Z"
            ),
        }

    monkeypatch.setattr(runner_module, "run_runner", fake_run_runner)
    monkeypatch.setattr(runner_module, "_result_identity", mismatched_identity)
    monkeypatch.setattr(
        runner_module,
        "_require_terminal_build_manifest",
        lambda *args, **kwargs: (Path("/build/build-manifest.json"), {}, "0" * 64),
    )
    workspace = tmp_path / "workspace"
    with pytest.raises(RunnerError, match="both runs were retained"):
        execute_configuration(
            ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
            workspace,
            ROOT / "unused-runner",
            repository_root=ROOT,
        )
    summaries = list((workspace / "reports" / "summaries").glob("*.json"))
    assert len(summaries) == 1
    retained = json.loads(summaries[0].read_text(encoding="utf-8"))
    assert len(retained["retained_runs"]) == 2
    assert (
        retained["distinct_run_provenance"] is False
        if mismatch == "provenance"
        else retained["deterministic_equal"] is False
    )


def test_execute_rejects_missing_terminal_build_manifest_before_workspace_mutation(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = tmp_path / "incomplete-build"
    runner = build / "tests/cpp/fakes/diskann_pipeline_fake_runner"
    runner.parent.mkdir(parents=True)
    runner.write_text("not executed", encoding="utf-8")
    (build / "CMakeCache.txt").write_text(
        "CMAKE_GENERATOR:INTERNAL=Ninja\n", encoding="utf-8"
    )
    monkeypatch.setattr(
        runner_module,
        "run_runner",
        lambda *args, **kwargs: pytest.fail("runner must not be started"),
    )
    workspace = tmp_path / "workspace"
    with pytest.raises(RunnerError, match="terminal BuildManifest"):
        execute_configuration(
            ROOT / "tests/fixtures/fake_pipeline/experiment.yaml",
            workspace,
            runner,
            repository_root=ROOT,
        )
    assert not workspace.exists()
