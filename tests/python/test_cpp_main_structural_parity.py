from __future__ import annotations

import copy
import json
from pathlib import Path

from jsonschema import Draft202012Validator

from diskann_harness.cpp_main_structural_parity import (
    derive_report,
    load_policy,
    policy_checksum,
    validate_report,
)


ROOT = Path(__file__).resolve().parents[2]
FEATURE = ROOT / "specs" / "002-cpp-main-upstream-baseline"
POLICY = FEATURE / "contracts" / "structural-parity-policy.json"
POLICY_SCHEMA = FEATURE / "contracts" / "structural-parity-policy.schema.json"
REPORT = FEATURE / "structural-parity-report.json"
REPORT_SCHEMA = FEATURE / "contracts" / "structural-parity-report.schema.json"
COMMIT = "78256bbab4685e1774e78d331e081a153be26823"
DATASET = "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e"
PROFILE = "6dc8009b9654b01ae61fb32ab3af6cff3c9465a5a4e0e821cfdea06793d74262"
REFERENCE_ID = "cpp_main_native_sharded_reference_v1"
FRAMEWORK_ID = "cpp_main_p0_b0_m0_framework_candidate_v1"


def _source() -> dict[str, object]:
    return {
        "repository": "https://github.com/microsoft/DiskANN.git",
        "branch": "cpp_main",
        "commit": COMMIT,
        "clean_before": True,
        "clean_after": True,
    }


def _graph() -> dict[str, object]:
    return {
        "available": True,
        "validation_status": "PASS",
        "id_space": "global",
        "node_count": 1_000_000,
        "invalid_neighbor_count": 0,
        "self_edge_count": 0,
        "observed_max_degree": 16,
        "medoids_valid": True,
        "dataset_lineage_checksum": DATASET,
    }


def _reference() -> dict[str, object]:
    return {
        "baseline_id": REFERENCE_ID,
        "execution_status": "SUCCEEDED",
        "source": _source(),
        "dataset_checksum": DATASET,
        "profile_checksum": PROFILE,
        "branch_mode": "sharded",
        "partition_count": 3,
        "native_stage_sequence_observed": True,
        "global_graph": _graph(),
        "evidence_refs": ["build/native-probe/runs/sift1m-sharded/probe-report.md"],
    }


def _framework() -> dict[str, object]:
    return {
        "baseline_id": FRAMEWORK_ID,
        "execution_status": "SUCCEEDED",
        "source": _source(),
        "dataset_checksum": DATASET,
        "profile_checksum": PROFILE,
        "branch_mode": "sharded",
        "partition_count": 3,
        "stage_invocations": {"partition": 1, "local_build": 3, "merge": 1},
        "artifact_handoffs_valid": True,
        "global_graph": _graph(),
        "evidence_refs": [
            "specs/002-cpp-main-upstream-baseline/unified-sharded-report.json"
        ],
    }


def _derive(reference: dict[str, object], framework: dict[str, object]) -> dict[str, object]:
    policy = load_policy(ROOT)
    return derive_report(
        policy,
        reference,
        framework,
        report_id="test-structural-contract-parity",
        generated_at="2026-08-19T00:00:00Z",
        repository_root=ROOT,
    )


def test_policy_and_current_report_are_feature_local_and_schema_valid() -> None:
    policy_schema = json.loads(POLICY_SCHEMA.read_text(encoding="utf-8"))
    report_schema = json.loads(REPORT_SCHEMA.read_text(encoding="utf-8"))
    policy = json.loads(POLICY.read_text(encoding="utf-8"))
    report = json.loads(REPORT.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(policy_schema)
    Draft202012Validator.check_schema(report_schema)
    Draft202012Validator(policy_schema).validate(policy)
    Draft202012Validator(report_schema).validate(report)
    validate_report(report, ROOT)
    assert report["policy"]["sha256"] == policy_checksum(ROOT)
    assert report["gate"]["result"] == "NOT_REACHED"
    assert report["framework_baseline"]["state"] == "candidate"


def test_complete_positive_evidence_passes_only_the_limited_gate() -> None:
    reference = _reference()
    reference["global_graph"]["observed_max_degree"] = 15
    report = _derive(reference, _framework())
    assert report["gate"] == {
        "status": "REACHED",
        "result": "PASS",
        "reason": "all frozen structural-and-contract checks passed",
    }
    assert all(check["status"] == "PASS" for check in report["checks"])
    assert report["framework_baseline"]["state"] == "candidate"
    assert report["claims"] == {
        "structural_and_contract_parity": True,
        "search_semantics_parity": False,
        "quality_parity": False,
        "recall_parity": False,
        "complete_native_framework_parity": False,
        "final_framework_baseline_certified": False,
    }


def test_complete_mandatory_violation_fails() -> None:
    framework = _framework()
    framework["global_graph"]["observed_max_degree"] = 17
    report = _derive(_reference(), framework)
    assert report["gate"]["result"] == "FAIL"
    assert next(
        item for item in report["checks"] if item["id"] == "global_graph_contract"
    )["status"] == "FAIL"


def test_reached_but_incomplete_evidence_is_inconclusive() -> None:
    reference = _reference()
    del reference["global_graph"]["medoids_valid"]
    report = _derive(reference, _framework())
    assert report["gate"]["result"] == "INCONCLUSIVE"


def test_unavailable_required_reference_graph_is_not_reached() -> None:
    reference = _reference()
    reference["global_graph"] = {"available": False}
    report = _derive(reference, _framework())
    assert report["gate"] == {
        "status": "NOT_REACHED",
        "result": "NOT_REACHED",
        "reason": "required Reference or Framework execution/final-graph evidence is unavailable",
    }


def test_identity_or_control_drift_fails_closed_without_topology_statistics() -> None:
    reference = _reference()
    reference["source"]["commit"] = "0" * 40
    report = _derive(reference, _framework())
    assert report["gate"]["result"] == "FAIL"
    policy = load_policy(ROOT)
    serialized = json.dumps(policy, sort_keys=True)
    for forbidden in (
        "adjacency_equality",
        "topology_statistics",
        "repeated_run_statistics",
        "recall",
        "qps",
        "latency",
    ):
        assert forbidden in policy["excluded_comparisons"]
    assert "certification_id" not in serialized
    assert "claim_eligibility" not in serialized


def test_evidence_input_is_not_mutated() -> None:
    reference = _reference()
    framework = _framework()
    expected_reference = copy.deepcopy(reference)
    expected_framework = copy.deepcopy(framework)
    _derive(reference, framework)
    assert reference == expected_reference
    assert framework == expected_framework
