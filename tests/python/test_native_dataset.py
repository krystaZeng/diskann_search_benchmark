from __future__ import annotations

import collections
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from diskann_harness import native_dataset as native
from diskann_harness.cli import main
from diskann_harness.workspace import WorkspaceError


ROOT = Path(__file__).resolve().parents[2]
PINNED = "78256bbab4685e1774e78d331e081a153be26823"


def declaration(tmp_path: Path, rows: int = 12, dimension: int = 4) -> dict:
    # Same small vector construction as NativeDatasetFixture; real native bytes.
    values = [(row * 7 + column * 13) % 101 / 17.0
              for row in range(rows) for column in range(dimension)]
    source = tmp_path / "input.bin"
    source.write_bytes(struct.pack("<II", rows, dimension) + struct.pack(f"<{len(values)}f", *values))
    return {"path": str(source), "dataset_id": "native-small", "global_node_count": rows,
            "vector_dimension": dimension, "scalar_type": "float32", "distance_metric": "l2"}


def admit(tmp_path: Path, document: dict) -> dict:
    return native.admit_dataset(document, tmp_path / "workspace", repository_root=ROOT)


@pytest.mark.parametrize("with_expected_checksum", [False, True])
def test_native_admission_publishes_existing_contract(tmp_path: Path, with_expected_checksum: bool) -> None:
    document = declaration(tmp_path)
    source = Path(document["path"])
    before = source.read_bytes()
    if with_expected_checksum:
        document.update(checksum=hashlib.sha256(before).hexdigest(), byte_length=len(before))
    result = admit(tmp_path, document)
    workspace = tmp_path / "workspace"
    metadata_path = workspace / "normal" / result["artifact_ref"]["metadata_path"]
    metadata = json.loads(metadata_path.read_text())
    schema = json.loads((ROOT / "contracts/baseline-v1/schemas/cpp-main-dataset-artifact.schema.json").read_text())
    Draft202012Validator(schema).validate(metadata)
    assert metadata["dataset"] == {
        "dataset_id": "native-small", "dataset_checksum": hashlib.sha256(before).hexdigest(),
        "global_node_count": 12, "vector_dimension": 4, "scalar_type": "float32", "distance_metric": "l2",
    }
    assert (metadata_path.parent / "vectors.bin").read_bytes() == before == source.read_bytes()
    assert json.loads((workspace / result["artifact_ref_path"]).read_text()) == result["artifact_ref"]
    assert json.loads((workspace / result["admission_record_path"]).read_text()) == result
    assert result["source_sha256"] == result["destination_sha256"] == metadata["dataset"]["dataset_checksum"]
    assert result["declared_dataset"] == document
    assert result["admission_source"]["sha256"] == native.sha256_file(Path(native.__file__))
    evidence = json.loads((workspace / result["validation_evidence_path"]).read_text())
    assert evidence["result"] == "VALID" and all(check["passed"] for check in evidence["checks"])
    retained = {p: p.read_bytes() for p in workspace.rglob("*") if p.is_file()}
    with pytest.raises(native.NativeDatasetError, match="immutable"):
        admit(tmp_path, document)
    assert all(p.read_bytes() == content for p, content in retained.items())


@pytest.mark.parametrize("case,reason", [
    ("count", "header identity"), ("dimension", "header identity"), ("checksum", "SHA-256 differs"),
    ("length", "byte length"), ("truncated", "byte length"), ("trailing", "byte length"),
    ("nan", "finiteness"), ("inf", "finiteness"), ("format", "float32/L2"),
    ("metric", "float32/L2"), ("unknown", "unknown fields"), ("missing", "missing"),
])
def test_native_admission_rejects_invalid_input(tmp_path: Path, case: str, reason: str) -> None:
    document = declaration(tmp_path)
    source = Path(document["path"])
    if case in {"count", "dimension"}:
        document["global_node_count" if case == "count" else "vector_dimension"] += 1
    elif case == "checksum":
        document["checksum"] = "0" * 64
    elif case == "length":
        document["byte_length"] = source.stat().st_size + 4
    elif case == "truncated":
        source.write_bytes(source.read_bytes()[:-4])
    elif case == "trailing":
        source.write_bytes(source.read_bytes() + b"\x00" * 4)
    elif case in {"nan", "inf"}:
        source.write_bytes(source.read_bytes()[:-4] + struct.pack("<f", float(case)))
    elif case == "format":
        document["scalar_type"] = "uint8"
    elif case == "metric":
        document["distance_metric"] = "cosine"
    elif case == "unknown":
        document["convert"] = True
    elif case == "missing":
        source.unlink()
    before = source.read_bytes() if source.exists() else None
    with pytest.raises(native.NativeDatasetError, match=reason):
        admit(tmp_path, document)
    assert not (tmp_path / "workspace").exists()
    assert (source.read_bytes() if source.exists() else None) == before


@pytest.mark.parametrize("case", ["relative_escape", "normal_symlink", "manifest_symlink", "upstream"])
def test_native_admission_confines_writes(tmp_path: Path, case: str) -> None:
    document = declaration(tmp_path)
    workspace = tmp_path / "workspace"
    repository = tmp_path / "repository"
    repository.mkdir()
    if case == "relative_escape":
        document["path"] = "../input.bin"
    elif case == "upstream":
        workspace = repository / "upstream/diskann-cpp-main/admission-must-not-exist"
    else:
        workspace.mkdir()
        outside = tmp_path / "outside"
        outside.mkdir()
        (workspace / ("normal" if case == "normal_symlink" else "manifests")).symlink_to(outside, target_is_directory=True)
    with pytest.raises(WorkspaceError):
        native.admit_dataset(document, workspace, repository_root=repository)
    if case.endswith("symlink"):
        assert list(outside.iterdir()) == []
    if case == "upstream":
        assert not workspace.exists()


def test_native_admission_copy_must_match_scan(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    document = declaration(tmp_path)
    original = native.write_immutable_bytes
    def changed_copy(target: Path, source: Path) -> None:
        source.write_bytes(source.read_bytes()[:-4] + struct.pack("<f", 123.0))
        original(target, source)
    monkeypatch.setattr(native, "write_immutable_bytes", changed_copy)
    with pytest.raises(native.NativeDatasetError, match="admitted Dataset checksum"):
        admit(tmp_path, document)
    assert not list((tmp_path / "workspace").rglob("artifact.json"))


def test_native_admission_cli(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    document = declaration(tmp_path)
    config = tmp_path / "declaration.json"
    config.write_text(json.dumps(document))
    args = ["admit-dataset", "--config", str(config), "--workspace", str(tmp_path / "workspace")]
    assert main(args) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["facts"]["npts"] == 12
    assert main(args) == 2
    assert "immutable" in capsys.readouterr().err
    config.write_text("[]")
    assert main(args) == 2
    assert "declaration" in capsys.readouterr().err


@pytest.mark.parametrize("case", ["default_protected", "external_protected", "external_workspace"])
def test_native_cli_source_dir_preserves_upstream_protection(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], case: str,
) -> None:
    from diskann_harness import cli

    repository = tmp_path / "repository"
    external = tmp_path / "external-data"
    repository.mkdir()
    external.mkdir()
    default_document = declaration(repository)
    external_document = declaration(external, rows=13)
    document = default_document if case == "default_protected" else external_document
    input_bytes = Path(document["path"]).read_bytes()
    document["path"] = "input.bin"
    config = tmp_path / "declaration.json"
    config.write_text(json.dumps(document))
    # Replace only the existing repository-location dependency, not path protection.
    monkeypatch.setattr(cli, "__file__", str(repository / "harness/python/diskann_harness/cli.py"))
    upstream = repository / "upstream/diskann-cpp-main"
    upstream.mkdir(parents=True)
    sentinel = upstream / "retained.txt"
    sentinel.write_bytes(b"fixed source must remain unchanged")
    protected = case.endswith("protected")
    workspace = upstream / "admission" if protected else tmp_path / "workspace"
    args = ["admit-dataset", "--config", str(config), "--workspace", str(workspace)]
    if case != "default_protected":
        args.extend(["--source-dir", str(external)])
    exit_code = main(args)
    captured = capsys.readouterr()
    assert exit_code == (2 if protected else 0), captured.err
    assert list(upstream.iterdir()) == [sentinel]
    assert sentinel.read_bytes() == b"fixed source must remain unchanged"
    if protected:
        assert "upstream/diskann-cpp-main" in captured.err and not captured.out
        assert not workspace.exists()
    else:
        result = json.loads(captured.out)
        assert result["source_path"] == str(external / "input.bin")
        assert result["source_sha256"] == hashlib.sha256(input_bytes).hexdigest()
        metadata_file = workspace / "normal" / result["artifact_ref"]["metadata_path"]
        metadata = json.loads(metadata_file.read_text())
        assert metadata["dataset"]["global_node_count"] == 13
        assert (metadata_file.parent / metadata["payloads"][0]["path"]).read_bytes() == input_bytes
        assert input_bytes != (repository / "input.bin").read_bytes()


@pytest.mark.parametrize("filename", [
    "vectors.bin", "artifact.json", "validation.json", "artifact-ref.json", "admission.json",
])
def test_native_admission_rejects_dangling_output(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], filename: str,
) -> None:
    document = declaration(tmp_path)
    input_bytes = Path(document["path"]).read_bytes()
    key = hashlib.sha256((document["dataset_id"] + "\x00" + hashlib.sha256(input_bytes).hexdigest()).encode()).hexdigest()
    workspace = tmp_path / "workspace"
    output_dir = (workspace / "normal/datasets" / key if filename in {"vectors.bin", "artifact.json"}
                  else workspace / "manifests/dataset-admission" / key)
    output_dir.mkdir(parents=True)
    target = workspace / "normal/redirected" / filename
    target.parent.mkdir(parents=True)
    sentinel = target.parent / "retained.txt"
    sentinel.write_bytes(b"retained")
    link = output_dir / filename
    link.symlink_to(target)
    assert link.is_symlink() and not link.exists() and not target.exists()
    entries = set(workspace.rglob("*"))
    config = tmp_path / "declaration.json"
    config.write_text(json.dumps(document))

    assert main(["admit-dataset", "--config", str(config), "--workspace", str(workspace)]) == 2
    captured = capsys.readouterr()
    assert "immutable" in captured.err and not captured.out
    assert link.is_symlink() and link.readlink() == target
    assert not target.exists() and set(workspace.rglob("*")) == entries
    assert sentinel.read_bytes() == b"retained"
    assert Path(document["path"]).read_bytes() == input_bytes


@pytest.fixture
def native_runners() -> tuple[str, str]:
    composition = os.environ.get("DISKANN_NATIVE_COMPOSITION_RUNNER")
    validator = os.environ.get("DISKANN_NATIVE_VALIDATOR")
    if not composition or not validator:
        pytest.skip("set DISKANN_NATIVE_COMPOSITION_RUNNER and DISKANN_NATIVE_VALIDATOR to rebuilt Linux runners")
    assert Path(composition).is_file() and Path(validator).is_file()
    return composition, validator


def test_native_validator_accepts_admitted_dataset(
    tmp_path: Path, native_runners: tuple[str, str], capsys: pytest.CaptureFixture[str],
) -> None:
    _, validator = native_runners
    admission = admit(tmp_path, declaration(tmp_path))
    workspace = tmp_path / "workspace"
    assert main(["validate", "--artifact-ref", str(workspace / admission["artifact_ref_path"]),
                 "--workspace", str(workspace), "--runner", validator]) == 0
    result = json.loads(capsys.readouterr().out)
    assert result["exit_code"] == 0 and result["outcome"] == "SUCCEEDED"
    assert result["artifact"] == admission["artifact_ref"] and result["operation"] == "validate"
    assert result["stage_invocations"] == {"partition": 0, "local_build": 0, "merge": 0}


@pytest.mark.parametrize("corruption", ["payload", "header_identity"])
def test_native_validator_rejects_corruption(
    tmp_path: Path, native_runners: tuple[str, str], capsys: pytest.CaptureFixture[str], corruption: str,
) -> None:
    _, validator = native_runners
    result = admit(tmp_path, declaration(tmp_path))
    workspace = tmp_path / "workspace"
    metadata_file = workspace / "normal" / result["artifact_ref"]["metadata_path"]
    if corruption == "payload":
        payload = metadata_file.parent / "vectors.bin"
        payload.chmod(0o644)
        payload.write_bytes(payload.read_bytes() + b"\x00" * 4)
    else:
        from diskann_harness.checksum import aggregate_checksum
        metadata = json.loads(metadata_file.read_text())
        metadata["dataset"]["global_node_count"] = 13
        metadata["structural"]["global_node_count"] = 13
        metadata["aggregate_checksum"] = aggregate_checksum(native.dataset_identity_projection(
            metadata["dataset"]["dataset_checksum"], metadata["structural"], metadata["payloads"][0]))
        metadata_file.write_text(json.dumps(metadata))
        # Deliberately malformed test input, not a successful admission record.
        ref = {**result["artifact_ref"], "aggregate_checksum": metadata["aggregate_checksum"]}
        (workspace / result["artifact_ref_path"]).write_text(json.dumps(ref))
    assert main(["validate", "--artifact-ref", str(workspace / result["artifact_ref_path"]),
                 "--workspace", str(workspace), "--runner", validator]) == 6
    assert json.loads(capsys.readouterr().out)["outcome"] != "SUCCEEDED"


@pytest.mark.parametrize("partition", ["p0", "kmeans", "ogp"])
def test_native_admission_complete_composition(
    tmp_path: Path, native_runners: tuple[str, str], capsys: pytest.CaptureFixture[str], partition: str,
) -> None:
    composition, validator = native_runners
    document = declaration(tmp_path, rows=96)
    input_path = tmp_path / "dataset.json"
    input_path.write_text(json.dumps(document))
    workspace = tmp_path / "workspace"
    assert main(["admit-dataset", "--config", str(input_path), "--workspace", str(workspace)]) == 0
    admission = json.loads(capsys.readouterr().out)
    retained = {p: p.read_bytes() for p in workspace.rglob("*") if p.is_file()}
    assert main(["validate", "--artifact-ref", str(workspace / admission["artifact_ref_path"]),
                 "--workspace", str(workspace), "--runner", validator]) == 0
    assert json.loads(capsys.readouterr().out)["outcome"] == "SUCCEEDED"
    config = json.loads((ROOT / "experiments/configs/module-composition/p0-b0-m0.json").read_text())
    config.update(run_id=f"admitted-{partition}", dataset=admission["artifact_ref"])
    config["workspace"]["root"] = str(workspace)
    if partition == "kmeans":
        config["partition"] = {"implementation_id": f"diskann_{PINNED}_kmeans_configurable_k_v1",
                               "implementation_version": "1.0.0", "parameters": {"K": 3}}
    elif partition == "ogp":
        config["partition"] = {"implementation_id": "gp_ann_ogp_partition_v1", "implementation_version": "1.0.0",
                               "parameters": {"requested_num_clusters": 4, "requested_overlap": 0.2, "partitioning_mode": "default"}}
    request = tmp_path / "composition.json"
    request.write_text(json.dumps(config))
    output = workspace / "reports/composition.json"
    completed = subprocess.run([composition, "execute", "--request", str(request), "--result", str(output)],
                               capture_output=True, text=True, timeout=240)
    assert completed.returncode == 0, completed.stderr[-8000:]
    result = json.loads(output.read_text())
    assert result["outcome"] == "SUCCEEDED"
    def metadata(ref: dict) -> tuple[Path, dict]:
        path = workspace / "normal" / ref["metadata_path"]
        data = json.loads(path.read_text())
        assert data["aggregate_checksum"] == ref["aggregate_checksum"]
        assert data["dataset"]["global_node_count"] == 96
        assert data["dataset"]["dataset_checksum"] == admission["source_sha256"]
        for payload in data["payloads"]:
            content = (path.parent / payload["path"]).read_bytes()
            assert len(content) == payload["byte_length"]
            assert hashlib.sha256(content).hexdigest() == payload["sha256"]
        return path, data
    path, partitions = metadata(result["partition"])
    count = partitions["structural"]["partition_count"]
    membership: collections.Counter[int] = collections.Counter()
    by_role = {p["role"]: p for p in partitions["payloads"]}
    for record in partitions["structural"]["partitions"]:
        payload = by_role[record["local_to_global_map_role"]]
        content = (path.parent / payload["path"]).read_bytes()
        rows, dim = struct.unpack("<II", content[:8])
        ids = list(struct.unpack(f"<{rows}I", content[8:]))
        assert dim == 1 and rows == record["local_node_count"] and rows > 0
        assert ids == sorted(set(ids)) and all(0 <= i < 96 for i in ids)
        membership.update(ids)
    assert set(membership) == set(range(96))
    if partition == "p0":
        assert all(n == 2 for n in membership.values())
        assert config["partition"]["parameters"]["sample_probability"] == 1.0
    elif partition == "kmeans":
        assert count == 3 and all(n == 1 for n in membership.values())
    else:
        assert count == 5 != config["partition"]["parameters"]["requested_num_clusters"]
        assert 96 <= sum(membership.values()) <= 96 + int(96 * 0.2)
    assert result["stage_invocations"] == {"partition": 1, "local_build": count, "merge": 1}
    assert len(result["local_graphs"]) == count
    for ref in [*result["local_graphs"], result["global_graph"]]:
        metadata(ref)
    for record in result["build_manifests"]:
        file = workspace / record["path"]
        assert native.sha256_file(file) == record["sha256"]
        manifest = json.loads(file.read_text())
        selected = config[manifest["stage"]]
        assert manifest["implementation"] == {k: selected[k] for k in ("implementation_id", "implementation_version")}
        assert manifest["status"] == "SUCCEEDED" and manifest["dataset"]["global_node_count"] == 96
        assert manifest["dataset"]["dataset_checksum"] == admission["source_sha256"]
        assert manifest["source"]["commit"] == ("df5efb4a6b303011c795666ed267f29d994b558c" if partition == "ogp" and manifest["stage"] == "partition" else PINNED)
    for record in result["validation_evidence"]:
        file = workspace / record["path"]
        assert native.sha256_file(file) == record["sha256"]
        assert json.loads(file.read_text())["result"] == "VALID"
    entry = result["entry_build_manifest"]
    assert native.sha256_file(workspace / entry["path"]) == entry["sha256"]
    assert (workspace / result["evaluation_report_path"]).is_file()
    assert all(p.read_bytes() == content for p, content in retained.items())
    # A second invocation must not overwrite the successful output or artifacts.
    saved = output.read_bytes()
    repeated = subprocess.run([composition, "execute", "--request", str(request), "--result", str(output)], capture_output=True, timeout=30)
    assert repeated.returncode == 2 and output.read_bytes() == saved
