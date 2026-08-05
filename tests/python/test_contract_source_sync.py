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

    design = ROOT / "specs/001-modular-cpp-diskann-pipeline/contracts"
    expected_sources = {
        (design / "checksum-canonicalization.md").relative_to(ROOT).as_posix(),
        (design / "adapter-policy.md").relative_to(ROOT).as_posix(),
        *{
            path.relative_to(ROOT).as_posix()
            for path in (design / "schemas").glob("*.json")
        },
    }
    assert {entry["source"] for entry in entries} == expected_sources
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

