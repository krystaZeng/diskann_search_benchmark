from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_fake_fixture_inventory_freezes_exact_bytes_and_checksums() -> None:
    inventory = json.loads(
        (ROOT / "tests/fixtures/fake_pipeline/golden.json").read_text(encoding="utf-8")
    )
    assert inventory["dataset"]["dataset_id"] == "fake-six-node-v1"
    assert inventory["dataset"]["global_node_count"] == 6
    assert inventory["payload_order"]["partition"] == [
        "partition/0/vectors",
        "partition/0/local_to_global_map",
        "partition/1/vectors",
        "partition/1/local_to_global_map",
    ]
    for payload in inventory["payloads"]:
        raw = payload["content"].encode("utf-8")
        assert len(raw) == payload["byte_length"]
        assert hashlib.sha256(raw).hexdigest() == payload["sha256"]
    assert inventory["aggregate_checksums"]["global"] == (
        "443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe"
    )
