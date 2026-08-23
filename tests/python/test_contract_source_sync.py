from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[2]


def test_runtime_contract_sources_are_complete_stable_and_byte_identical() -> None:
    inventory = json.loads((ROOT / "contracts/v1/SOURCES.json").read_text(encoding="utf-8"))
    assert inventory["format_version"] == "diskann-contract-sources-v1"
    assert set(inventory) == {"format_version", "entries"}
    entries = inventory["entries"]
    targets = [entry["target"] for entry in entries]
    assert targets == sorted(targets)
    assert len(targets) == len(set(targets))

    feature_001 = ROOT / "specs/001-modular-cpp-diskann-pipeline/contracts"
    feature_005 = ROOT / "specs/005-diskann-search-evaluation/contracts"
    source_by_target = {entry["target"]: entry["source"] for entry in entries}
    assert source_by_target["contracts/v1/schemas/common.schema.json"] == (
        feature_005 / "common.schema.json"
    ).relative_to(ROOT).as_posix()
    assert source_by_target["contracts/v1/schemas/build-manifest.schema.json"] == (
        feature_005 / "build-manifest.schema.json"
    ).relative_to(ROOT).as_posix()
    assert source_by_target["contracts/v1/schemas/pq-artifact.schema.json"] == (
        feature_005 / "pq-artifact.schema.json"
    ).relative_to(ROOT).as_posix()
    assert source_by_target[
        "contracts/v1/schemas/disk-layout-artifact.schema.json"
    ] == (feature_005 / "disk-layout-artifact.schema.json").relative_to(
        ROOT
    ).as_posix()
    for target, source in source_by_target.items():
        if target in {
            "contracts/v1/schemas/common.schema.json",
            "contracts/v1/schemas/build-manifest.schema.json",
            "contracts/v1/schemas/pq-artifact.schema.json",
            "contracts/v1/schemas/disk-layout-artifact.schema.json",
        }:
            continue
        assert source.startswith(feature_001.relative_to(ROOT).as_posix() + "/")
    for entry in entries:
        assert set(entry) == {"source", "target", "sha256"}
        assert not PurePosixPath(entry["source"]).is_absolute()
        assert not PurePosixPath(entry["target"]).is_absolute()
        assert ".." not in PurePosixPath(entry["source"]).parts
        assert ".." not in PurePosixPath(entry["target"]).parts
        source = ROOT / entry["source"]
        target = ROOT / entry["target"]
        source_bytes = source.read_bytes()
        assert target.read_bytes() == source_bytes
        assert hashlib.sha256(source_bytes).hexdigest() == entry["sha256"]
