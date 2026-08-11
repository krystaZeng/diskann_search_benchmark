from __future__ import annotations

import copy
import hashlib
import json
import tempfile
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, ValidationError

from diskann_harness import ogp_partition_adaptation


ROOT = Path(__file__).resolve().parents[2]
SOURCE_SCHEMA = (
    ROOT
    / "specs"
    / "003-ogp-partition-adaptation"
    / "contracts"
    / "ogp-parameters.schema.json"
)
RUNTIME_SCHEMA = (
    ROOT
    / "contracts"
    / "papers"
    / "gp-ann-ogp-v1"
    / "schemas"
    / "ogp-parameters.schema.json"
)
SOURCES = ROOT / "contracts" / "papers" / "gp-ann-ogp-v1" / "SOURCES.json"
SIFT1M_CONFIG = (
    ROOT
    / "experiments"
    / "configs"
    / "ogp-partition-adaptation"
    / "sift1m-structural.yaml"
)


def validator() -> Draft202012Validator:
    schema = json.loads(RUNTIME_SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def test_runtime_parameter_contract_is_byte_identical_and_traced() -> None:
    source_bytes = SOURCE_SCHEMA.read_bytes()
    assert RUNTIME_SCHEMA.read_bytes() == source_bytes
    digest = hashlib.sha256(source_bytes).hexdigest()
    sources = json.loads(SOURCES.read_text(encoding="utf-8"))
    assert sources == {
        "format_version": "diskann-paper-contract-sources-v1",
        "entries": [
            {
                "source": (
                    "specs/003-ogp-partition-adaptation/contracts/"
                    "ogp-parameters.schema.json"
                ),
                "target": (
                    "contracts/papers/gp-ann-ogp-v1/schemas/"
                    "ogp-parameters.schema.json"
                ),
                "sha256": digest,
            }
        ],
    }


def test_parameter_schema_matches_official_cli_surface() -> None:
    check = validator()
    check.validate(
        {
            "requested_num_clusters": 40,
            "requested_overlap": 0.2,
            "partitioning_mode": "default",
        }
    )
    check.validate(
        {
            "requested_num_clusters": 2,
            "requested_overlap": 0,
            "partitioning_mode": "strong",
        }
    )
    rejected = [
        {
            "requested_num_clusters": 40,
            "requested_overlap": 0.2,
            "partitioning_mode": "default",
            "epsilon": 0.05,
        },
        {"requested_overlap": 0.2, "partitioning_mode": "default"},
        {
            "requested_num_clusters": 0,
            "requested_overlap": 0.2,
            "partitioning_mode": "default",
        },
        {
            "requested_num_clusters": 40,
            "requested_overlap": -0.1,
            "partitioning_mode": "default",
        },
        {
            "requested_num_clusters": 40,
            "requested_overlap": 0.2,
            "partitioning_mode": "callable",
        },
    ]
    for value in rejected:
        with pytest.raises(ValidationError):
            check.validate(value)


def test_real_runner_small_composed_lifecycle(tmp_path: Path) -> None:
    runner = ogp_partition_adaptation.find_built_runner(ROOT)
    result = ogp_partition_adaptation.run_small_composed_fixture(
        runner=runner,
        workspace_parent=(
            ROOT
            / "build"
            / "ogp-partition-adaptation"
            / "linux"
            / "python-tests"
        ),
        repository_root=ROOT,
    )
    assert result["outcome"] == "SUCCEEDED"
    assert result["stage_invocations"]["partition"] == 1
    assert result["stage_invocations"]["local_build"] >= 2
    assert result["stage_invocations"]["merge"] == 1
    assert result["artifact"]["artifact_type"] == "global_graph"
    assert result["evaluation_report_path"]


def test_frozen_sift1m_configuration_is_exact_and_bounded() -> None:
    config = ogp_partition_adaptation.load_frozen_configuration(
        SIFT1M_CONFIG, repository_root=ROOT
    )
    assert config["status"] == "frozen"
    assert config["dataset"] == {
        "dataset_id": "sift1m-base-float32-v1",
        "path": "sift1M_dataset/base_vector.bin",
        "checksum": "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
        "global_node_count": 1_000_000,
        "vector_dimension": 128,
        "byte_length": 512_000_008,
        "scalar_type": "float32",
        "metric": "l2",
    }
    assert config["stage_parameters"]["partition"] == {
        "requested_num_clusters": 40,
        "requested_overlap": 0.2,
        "partitioning_mode": "default",
    }
    assert config["effective_constants"] == {
        "epsilon": 0.05,
        "epsilon_source": "upstream_cli_constant",
    }
    assert config["execution"]["parameter_sweep"] is False
    assert config["execution"]["retention"] == "retain_all"

    larger = copy.deepcopy(config)
    larger["dataset"]["global_node_count"] = 1_000_001
    with pytest.raises(ogp_partition_adaptation.OgpHarnessError, match="SIFT1M"):
        ogp_partition_adaptation.validate_frozen_configuration(larger)


@pytest.mark.parametrize(
    ("mutation", "message"),
    [
        (
            lambda value: value["stage_parameters"]["partition"].update(
                {"requested_overlap": 0.3}
            ),
            "frozen",
        ),
        (
            lambda value: value["execution"].update({"parameter_sweep": True}),
            "frozen",
        ),
        (
            lambda value: value["effective_constants"].update({"epsilon": 0.1}),
            "frozen",
        ),
    ],
)
def test_frozen_configuration_rejects_experimental_drift(
    mutation: object, message: str
) -> None:
    config = ogp_partition_adaptation.load_frozen_configuration(
        SIFT1M_CONFIG, repository_root=ROOT
    )
    changed = copy.deepcopy(config)
    mutation(changed)  # type: ignore[operator]
    with pytest.raises(ogp_partition_adaptation.OgpHarnessError, match=message):
        ogp_partition_adaptation.validate_frozen_configuration(changed)


def test_preflight_is_immutable_and_never_invokes_runner(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    workspace = Path(
        tempfile.mkdtemp(
            prefix="preflight-unit-",
            dir=ROOT / "build" / "ogp-partition-adaptation" / "linux" / "python-tests",
        )
    )
    monkeypatch.setattr(
        ogp_partition_adaptation,
        "_admit_formal_sources",
        lambda *_args, **_kwargs: {
            "partition": {"source": ogp_partition_adaptation.GPANN_SOURCE},
            "local_build": {"source": ogp_partition_adaptation.DISKANN_SOURCE},
            "merge": {"source": ogp_partition_adaptation.DISKANN_SOURCE},
        },
    )
    monkeypatch.setattr(
        ogp_partition_adaptation,
        "_admit_sift1m_dataset",
        lambda *_args, **_kwargs: {
            "artifact_ref": {
                "metadata_path": "datasets/sift1m/artifact.json",
                "artifact_type": "dataset",
                "format_version": "diskann-float32-dataset/v1",
                "aggregate_checksum": "a" * 64,
            },
            "validation_evidence_path": "manifests/evidence/dataset-admission.json",
        },
    )
    monkeypatch.setattr(
        ogp_partition_adaptation.subprocess,
        "run",
        lambda *_args, **_kwargs: pytest.fail("preflight must not start a process"),
    )
    response = ogp_partition_adaptation.preflight(
        SIFT1M_CONFIG, workspace, repository_root=ROOT
    )
    request_path = Path(response["request_path"])
    before = request_path.read_bytes()
    request = json.loads(before)
    assert request["stage_invocations"] == {
        "partition": 0,
        "local_build": 0,
        "merge": 0,
    }
    assert response["stage_invocations"] == request["stage_invocations"]
    assert request_path.read_bytes() == before


def test_retry_keeps_frozen_identity_and_retains_distinct_attempts(
    tmp_path: Path,
) -> None:
    workspace = Path(
        tempfile.mkdtemp(
            prefix="retry-unit-",
            dir=ROOT / "build" / "ogp-partition-adaptation" / "linux" / "python-tests",
        )
    )
    preflight_request = workspace / "ogp-request.json"
    config = ogp_partition_adaptation.load_frozen_configuration(
        SIFT1M_CONFIG, repository_root=ROOT
    )
    base = {
        "request_version": "1.0.0",
        "operation": "preflight",
        "configuration_ref": SIFT1M_CONFIG.relative_to(ROOT).as_posix(),
        "configuration_checksum": hashlib.sha256(SIFT1M_CONFIG.read_bytes()).hexdigest(),
        "normalized_configuration_sha256": ogp_partition_adaptation._canonical_digest(
            config
        ),
        "dataset": {"artifact_type": "dataset"},
        "stage_sources": {
            "partition": ogp_partition_adaptation.GPANN_SOURCE,
            "local_build": ogp_partition_adaptation.DISKANN_SOURCE,
            "merge": ogp_partition_adaptation.DISKANN_SOURCE,
        },
    }
    preflight_request.write_text(json.dumps(base), encoding="utf-8")
    first = ogp_partition_adaptation.prepare_attempt_request(
        preflight_request,
        workspace / "attempt-001",
        "sift1m-structural-attempt-001",
        repository_root=ROOT,
        materialize_dataset=False,
    )
    retry = ogp_partition_adaptation.prepare_attempt_request(
        preflight_request,
        workspace / "attempt-002",
        "sift1m-structural-attempt-002",
        retry_of="sift1m-structural-attempt-001",
        retry_reason="external_execution_failure",
        repository_root=ROOT,
        materialize_dataset=False,
    )
    first_document = json.loads(Path(first["request_path"]).read_text(encoding="utf-8"))
    retry_document = json.loads(Path(retry["request_path"]).read_text(encoding="utf-8"))
    assert first_document["source_config"] == retry_document["source_config"]
    assert first_document["request_id"] != retry_document["request_id"]
    assert Path(first["request_path"]).is_file()
    assert Path(retry["request_path"]).is_file()

    with pytest.raises(ogp_partition_adaptation.OgpHarnessError, match="retry reason"):
        ogp_partition_adaptation.prepare_attempt_request(
            preflight_request,
            workspace / "attempt-003",
            "sift1m-structural-attempt-003",
            retry_of="sift1m-structural-attempt-001",
            retry_reason="changed_parameters",
            repository_root=ROOT,
            materialize_dataset=False,
        )
