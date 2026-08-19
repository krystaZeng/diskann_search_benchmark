"""Feature 002's fixed Structural/Contract Parity gate.

This module is deliberately feature-local.  It derives a report from a frozen
policy and already-retained evidence projections; it is not a public report,
registry, certification, or graph-validation framework.
"""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from jsonschema import Draft202012Validator


POLICY_RELATIVE_PATH = Path(
    "specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.json"
)
POLICY_SCHEMA_RELATIVE_PATH = Path(
    "specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.schema.json"
)
REPORT_SCHEMA_RELATIVE_PATH = Path(
    "specs/002-cpp-main-upstream-baseline/contracts/structural-parity-report.schema.json"
)


class StructuralParityError(ValueError):
    """Raised when the frozen policy or a derived report is malformed."""


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StructuralParityError(f"cannot read JSON document {path}: {error}") from error
    if not isinstance(value, dict):
        raise StructuralParityError(f"JSON document must be an object: {path}")
    return value


def _validator(schema_path: Path) -> Draft202012Validator:
    schema = _read_json(schema_path)
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def load_policy(repository_root: str | Path) -> dict[str, Any]:
    root = Path(repository_root)
    policy = _read_json(root / POLICY_RELATIVE_PATH)
    errors = sorted(
        _validator(root / POLICY_SCHEMA_RELATIVE_PATH).iter_errors(policy),
        key=lambda error: list(error.absolute_path),
    )
    if errors:
        raise StructuralParityError(f"invalid frozen policy: {errors[0].message}")
    return policy


def policy_checksum(repository_root: str | Path) -> str:
    return hashlib.sha256(
        (Path(repository_root) / POLICY_RELATIVE_PATH).read_bytes()
    ).hexdigest()


def _lookup(value: Mapping[str, Any], *path: str) -> Any:
    current: Any = value
    for component in path:
        if not isinstance(current, Mapping) or component not in current:
            return None
        current = current[component]
    return current


def _truth(value: Any) -> str:
    if value is None:
        return "MISSING"
    return "PASS" if value is True else "FAIL"


def _all_equal(actual: tuple[Any, ...], expected: tuple[Any, ...]) -> str:
    if any(value is None for value in actual):
        return "MISSING"
    return "PASS" if actual == expected else "FAIL"


def _source_status(evidence: Mapping[str, Any], policy: Mapping[str, Any]) -> str:
    actual = (
        _lookup(evidence, "source", "repository"),
        _lookup(evidence, "source", "branch"),
        _lookup(evidence, "source", "commit"),
    )
    expected = (
        _lookup(policy, "source", "repository"),
        _lookup(policy, "source", "branch"),
        _lookup(policy, "source", "commit"),
    )
    return _all_equal(actual, expected)


def _graph_status(evidence: Mapping[str, Any], policy: Mapping[str, Any]) -> str:
    graph = _lookup(evidence, "global_graph")
    if not isinstance(graph, Mapping):
        return "MISSING"
    required = (
        graph.get("validation_status"),
        graph.get("id_space"),
        graph.get("node_count"),
        graph.get("invalid_neighbor_count"),
        graph.get("self_edge_count"),
        graph.get("observed_max_degree"),
        graph.get("medoids_valid"),
        graph.get("dataset_lineage_checksum"),
    )
    if any(value is None for value in required):
        return "MISSING"
    fixed_expected = (
        "PASS",
        policy["controls"]["global_id_space"],
        policy["dataset"]["node_count"],
        0,
        0,
    )
    if required[:5] != fixed_expected:
        return "FAIL"
    observed_degree = required[5]
    if not isinstance(observed_degree, int) or not (
        0 <= observed_degree <= policy["controls"]["maximum_degree"]
    ):
        return "FAIL"
    return (
        "PASS"
        if required[6:]
        == (
            policy["controls"]["medoids_required"],
            policy["dataset"]["sha256"],
        )
        else "FAIL"
    )


def _derive_checks(
    policy: Mapping[str, Any],
    reference: Mapping[str, Any],
    framework: Mapping[str, Any],
) -> list[dict[str, str]]:
    expected_reference_id = policy["reference_baseline"]["baseline_id"]
    expected_framework_id = policy["framework_baseline"]["baseline_id"]
    source = (_source_status(reference, policy), _source_status(framework, policy))
    dataset = (
        reference.get("dataset_checksum"),
        framework.get("dataset_checksum"),
    )
    profile = (
        reference.get("profile_checksum"),
        framework.get("profile_checksum"),
    )
    branch = (reference.get("branch_mode"), framework.get("branch_mode"))
    partitions = (reference.get("partition_count"), framework.get("partition_count"))

    statuses: dict[str, str] = {
        "source_identity": (
            "MISSING" if "MISSING" in source else "FAIL" if "FAIL" in source else "PASS"
        ),
        "dataset_identity": _all_equal(
            dataset,
            (policy["dataset"]["sha256"], policy["dataset"]["sha256"]),
        ),
        "profile_controls": _all_equal(
            profile,
            (policy["profile"]["sha256"], policy["profile"]["sha256"]),
        ),
        "sharded_branch": _all_equal(
            branch,
            (policy["controls"]["branch_mode"], policy["controls"]["branch_mode"]),
        ),
        "upstream_clean": _truth(
            all(
                _lookup(item, "source", field) is True
                for item in (reference, framework)
                for field in ("clean_before", "clean_after")
            )
            if all(
                _lookup(item, "source", field) is not None
                for item in (reference, framework)
                for field in ("clean_before", "clean_after")
            )
            else None
        ),
        "native_stage_sequence": _truth(reference.get("native_stage_sequence_observed")),
        "framework_stage_sequence": _truth(
            framework.get("stage_invocations")
            == {
                "partition": 1,
                "local_build": framework.get("partition_count"),
                "merge": 1,
            }
            if framework.get("stage_invocations") is not None
            and framework.get("partition_count") is not None
            else None
        ),
        "framework_artifact_handoffs": _truth(framework.get("artifact_handoffs_valid")),
        "cross_role_controls": _truth(
            reference.get("baseline_id") == expected_reference_id
            and framework.get("baseline_id") == expected_framework_id
            and reference.get("dataset_checksum") == framework.get("dataset_checksum")
            and reference.get("profile_checksum") == framework.get("profile_checksum")
            if all(
                value is not None
                for value in (
                    reference.get("baseline_id"),
                    framework.get("baseline_id"),
                    reference.get("dataset_checksum"),
                    framework.get("dataset_checksum"),
                    reference.get("profile_checksum"),
                    framework.get("profile_checksum"),
                )
            )
            else None
        ),
    }

    if any(value is None for value in partitions):
        statuses["partition_count"] = "MISSING"
    elif all(
        isinstance(value, int) and value >= policy["controls"]["minimum_partition_count"]
        for value in partitions
    ):
        statuses["partition_count"] = "PASS"
    else:
        statuses["partition_count"] = "FAIL"

    graph_statuses = (_graph_status(reference, policy), _graph_status(framework, policy))
    statuses["global_graph_contract"] = (
        "MISSING"
        if "MISSING" in graph_statuses
        else "FAIL"
        if "FAIL" in graph_statuses
        else "PASS"
    )

    return [{"id": check_id, "status": statuses[check_id]} for check_id in policy["required_checks"]]


def derive_report(
    policy: Mapping[str, Any],
    reference_evidence: Mapping[str, Any],
    framework_evidence: Mapping[str, Any],
    *,
    report_id: str,
    generated_at: str,
    repository_root: str | Path,
) -> dict[str, Any]:
    """Derive one immutable-value report without mutating supplied evidence."""

    reference = copy.deepcopy(dict(reference_evidence))
    framework = copy.deepcopy(dict(framework_evidence))
    unavailable = any(
        item.get("execution_status") != "SUCCEEDED"
        or _lookup(item, "global_graph", "available") is not True
        for item in (reference, framework)
    )
    checks = _derive_checks(policy, reference, framework)
    if unavailable:
        gate = {
            "status": "NOT_REACHED",
            "result": "NOT_REACHED",
            "reason": "required Reference or Framework execution/final-graph evidence is unavailable",
        }
    elif any(check["status"] == "FAIL" for check in checks):
        gate = {
            "status": "REACHED",
            "result": "FAIL",
            "reason": "one or more frozen structural-and-contract checks failed",
        }
    elif any(check["status"] == "MISSING" for check in checks):
        gate = {
            "status": "REACHED",
            "result": "INCONCLUSIVE",
            "reason": "required structural-and-contract evidence is incomplete or ambiguous",
        }
    else:
        gate = {
            "status": "REACHED",
            "result": "PASS",
            "reason": "all frozen structural-and-contract checks passed",
        }

    report = {
        "schema_version": "1.0.0",
        "report_id": report_id,
        "generated_at": generated_at,
        "scope": policy["scope"],
        "policy": {
            "policy_id": policy["policy_id"],
            "path": POLICY_RELATIVE_PATH.as_posix(),
            "sha256": policy_checksum(repository_root),
        },
        "reference_baseline": {
            "baseline_id": policy["reference_baseline"]["baseline_id"],
            "state": "reference",
            "execution_status": reference.get("execution_status", "NOT_REACHED"),
            "evidence_refs": list(reference.get("evidence_refs", [])),
        },
        "framework_baseline": {
            "baseline_id": policy["framework_baseline"]["baseline_id"],
            "state": "candidate",
            "execution_status": framework.get("execution_status", "NOT_REACHED"),
            "evidence_refs": list(framework.get("evidence_refs", [])),
        },
        "checks": checks,
        "gate": gate,
        "claims": {
            "structural_and_contract_parity": gate["result"] == "PASS",
            "search_semantics_parity": False,
            "quality_parity": False,
            "recall_parity": False,
            "complete_native_framework_parity": False,
            "final_framework_baseline_certified": False,
        },
        "allowed_claim": policy["allowed_claim"],
        "forbidden_claims": list(policy["forbidden_claims"]),
    }
    validate_report(report, repository_root)
    return report


def validate_report(report: Mapping[str, Any], repository_root: str | Path) -> None:
    root = Path(repository_root)
    errors = sorted(
        _validator(root / REPORT_SCHEMA_RELATIVE_PATH).iter_errors(dict(report)),
        key=lambda error: list(error.absolute_path),
    )
    if errors:
        raise StructuralParityError(f"invalid Structural/Contract Parity Report: {errors[0].message}")
    if _lookup(report, "policy", "sha256") != policy_checksum(root):
        raise StructuralParityError("report policy checksum does not match the frozen policy")
