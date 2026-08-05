from __future__ import annotations

import pytest

from diskann_harness.checksum import (
    CanonicalizationError,
    aggregate_checksum,
    canonicalize_restricted_json,
    sha256_bytes,
)


DATASET_CHECKSUM = "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b"


def test_payload_and_dataset_aggregate_golden() -> None:
    payload = b'{"vectors":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]}'
    assert len(payload) == 49
    assert sha256_bytes(payload) == DATASET_CHECKSUM
    projection = {
        "artifact_type": "dataset",
        "canonicalization": "diskann-artifact-c14n-v1",
        "capabilities": ["dataset.vector_access/v1"],
        "dataset_checksum": DATASET_CHECKSUM,
        "format_version": "fake-dataset-json/v1",
        "id_space": "none",
        "payloads": [
            {"byte_length": 49, "role": "vectors", "sha256": DATASET_CHECKSUM}
        ],
        "schema_version": "1.0.0",
        "source_artifacts": [],
        "structural": {
            "global_node_count": 6,
            "vector_dimension": 2,
            "scalar_type": "float32",
            "distance_metric": "l2",
        },
    }
    canonical = canonicalize_restricted_json(projection)
    assert canonical == (
        b'{"artifact_type":"dataset","canonicalization":"diskann-artifact-c14n-v1",'
        b'"capabilities":["dataset.vector_access/v1"],"dataset_checksum":"'
        + DATASET_CHECKSUM.encode("ascii")
        + b'","format_version":"fake-dataset-json/v1","id_space":"none",'
        b'"payloads":[{"byte_length":49,"role":"vectors","sha256":"'
        + DATASET_CHECKSUM.encode("ascii")
        + b'"}],"schema_version":"1.0.0","source_artifacts":[],"structural":'
        b'{"distance_metric":"l2","global_node_count":6,"scalar_type":"float32",'
        b'"vector_dimension":2}}'
    )
    assert aggregate_checksum(projection) == (
        "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d"
    )


@pytest.mark.parametrize(
    "value",
    [
        {"forbidden": None},
        {"forbidden": 1.5},
        b'\xff',
        '{"duplicate":1,"duplicate":2}',
    ],
)
def test_restricted_projection_rejects_invalid_values(value: object) -> None:
    with pytest.raises(CanonicalizationError):
        canonicalize_restricted_json(value)


def test_non_identity_documents_may_retain_schema_permitted_float_and_null() -> None:
    document = {"metrics": {"finite": 1.5, "optional": None}}
    assert document["metrics"]["finite"] == 1.5
    assert document["metrics"]["optional"] is None
