from __future__ import annotations

import copy
import json
import math
from pathlib import Path
import struct

import pytest
from jsonschema import ValidationError

from diskann_harness import partition_compactness as pc
from diskann_harness.checksum import sha256_file


def matrix(path, rows, code="f", dimension=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    dimension = dimension if dimension is not None else len(rows[0])
    path.write_bytes(struct.pack("<II", len(rows), dimension) + b"".join(
        struct.pack("<" + code * dimension, *row) for row in rows))


def payload(path, role, media):
    return {"path": path.name, "role": role, "media_type": media,
            "byte_length": path.stat().st_size, "sha256": sha256_file(path)}


def publish_metadata(path, metadata):
    metadata["aggregate_checksum"] = pc._aggregate_artifact_metadata(metadata)
    path.write_text(json.dumps(metadata))
    return {"metadata_path": str(path.relative_to(path.parents[1])),
            **{key: metadata[key] for key in ("artifact_type", "format_version", "aggregate_checksum")}}


def fixture(root, vectors=None, maps=None, centroids=True, batch_rows=2):
    # Unequal sizes: C0=1, C1=8/3, weighted=2 (simple mean=11/6).
    vectors = vectors if vectors is not None else [[0, 0], [2, 0], [0, 2], [0, 4], [0, 6]]
    maps = maps if maps is not None else [[0, 1], [2, 3, 4]]
    workspace = root / "workspace"
    data_dir, part_dir = workspace / "normal/data", workspace / "normal/partition"
    vector_path = data_dir / "vectors.bin"
    matrix(vector_path, vectors)
    identity = {"dataset_id": "compactness-fixture", "dataset_checksum": sha256_file(vector_path),
                "global_node_count": len(vectors), "vector_dimension": len(vectors[0]),
                "scalar_type": "float32", "distance_metric": "l2"}
    common = {"schema_version": "1.0.0", "canonicalization": "diskann-artifact-c14n-v1", "dataset": identity}
    data = {**common, "artifact_type": "dataset", "format_version": pc.DATASET_FORMAT,
            "id_space": "none", "capabilities": ["dataset.diskann_float32_bin/v1"],
            "source_artifacts": [], "structural": {key: identity[key] for key in
             ("global_node_count", "vector_dimension", "scalar_type", "distance_metric")},
            "payloads": [payload(vector_path, "vectors", pc.FLOAT_MEDIA)]}
    data_ref = publish_metadata(data_dir / "artifact.json", data)
    part = {**common, "artifact_type": "partition", "format_version": "partition-artifact/v1",
            "id_space": "local", "capabilities": ["partition.complete_local_to_global_map/v1",
                                                     "partition.local_to_global_ascending/v1"],
            "source_artifacts": [{key: data_ref[key] for key in
                                 ("artifact_type", "format_version", "aggregate_checksum")}],
            "structural": {"partition_count": len(maps), "partitions": []}, "payloads": []}
    for index, ids in enumerate(maps):
        path = part_dir / f"map-{index}.bin"
        matrix(path, [(value,) for value in ids], "I", 1)
        role = f"partition/{index}/local_to_global_map"
        part["payloads"].append(payload(path, role, pc.MAP_MEDIA))
        part["structural"]["partitions"].append({"partition_id": index, "local_node_count": len(ids),
                                                "local_to_global_map_role": role})
    if centroids:
        path = part_dir / "centroids.bin"
        # Deliberately unrelated to either full-partition mean.
        matrix(path, [[999.0] * len(vectors[0]) for _ in maps])
        part["payloads"].append(payload(path, "centroids", pc.CENTROID_MEDIA))
        part["capabilities"].insert(0, "partition.centroids/v1")
    part_ref = publish_metadata(part_dir / "artifact.json", part)
    request = {"request_version": "partition-compactness-request/v1", "workspace": str(workspace),
               "dataset": data_ref, "partition": part_ref, "batch_rows": batch_rows,
               "report_path": str(root / "reports/quality.json")}
    path = root / "request.json"
    path.write_text(json.dumps(request))
    return path


def change_metadata(request_path, kind, change):
    request = json.loads(request_path.read_text())
    path = Path(request["workspace"]) / "normal" / request[kind]["metadata_path"]
    metadata = json.loads(path.read_text())
    change(metadata, path.parent)
    request[kind] = publish_metadata(path, metadata)
    request_path.write_text(json.dumps(request))


def rejected(request, match=None, error=(pc.CompactnessError, ValidationError)):
    with pytest.raises(error, match=match):
        pc.evaluate_request(request)
    assert not Path(json.loads(request.read_text())["report_path"]).exists()


def change_capabilities(request_path, kind, capabilities):
    # Rebind identities without normalizing the deliberately invalid declaration.
    change_metadata(request_path, kind, lambda metadata, _: metadata.update(capabilities=capabilities))
    if kind == "dataset":
        current = json.loads(request_path.read_text())
        source = {key: current["dataset"][key] for key in
                  ("artifact_type", "format_version", "aggregate_checksum")}
        change_metadata(request_path, "partition", lambda metadata, _: metadata.update(source_artifacts=[source]))


def assert_cli_input_failure(request, capsys, reason):
    assert pc.main(["--request", str(request)]) == 2
    captured = capsys.readouterr()
    assert captured.out == ""
    failure = json.loads(captured.err)
    assert set(failure) == {"outcome", "failure"}
    assert failure["outcome"] == "FAILED"
    assert failure["failure"] == {"category": "evaluation_failed", "reason": reason}
    assert not Path(json.loads(request.read_text())["report_path"]).exists()


@pytest.mark.parametrize("kind,case", [
    ("dataset", "unordered"), ("partition", "unordered"),
    ("dataset", "duplicate"), ("dataset", "empty"), ("dataset", "non_string"),
])
def test_capabilities_admission_cli_failure(tmp_path, capsys, kind, case):
    request = fixture(tmp_path)
    current = json.loads(request.read_text())
    path = Path(current["workspace"]) / "normal" / current[kind]["metadata_path"]
    metadata = json.loads(path.read_text())
    capabilities = metadata["capabilities"]
    if case == "unordered":
        if kind == "dataset": capabilities.append("dataset.extra/v1")
        capabilities.reverse()
    elif case == "duplicate": capabilities.append(capabilities[0])
    elif case == "empty": capabilities.insert(0, "")
    else:
        capabilities.append(7)
        # Mixed element types have no valid canonical capability ordering; write
        # directly so the fixture does not fail in the checksum helper first.
        path.write_text(json.dumps(metadata))
    if case != "non_string":
        change_capabilities(request, kind, capabilities)
        # Prove these counterexamples reach admission with self-consistent refs.
        current = json.loads(request.read_text())
        for artifact_kind in ("dataset", "partition"):
            artifact_path = Path(current["workspace"]) / "normal" / current[artifact_kind]["metadata_path"]
            artifact = json.loads(artifact_path.read_text())
            assert artifact["aggregate_checksum"] == pc._aggregate_artifact_metadata(artifact)
            assert artifact["aggregate_checksum"] == current[artifact_kind]["aggregate_checksum"]
        assert artifact["source_artifacts"] == [{key: current["dataset"][key] for key in
                                                ("artifact_type", "format_version", "aggregate_checksum")}]
    reason = ("capabilities must contain only non-empty strings" if case in ("empty", "non_string")
              else "capabilities must be strictly increasing (sorted and unique)")
    assert_cli_input_failure(request, capsys, reason)


@pytest.mark.parametrize("kind", ["dataset", "partition"])
def test_required_capability_still_rejected_by_schema(tmp_path, capsys, kind):
    request = fixture(tmp_path)
    change_capabilities(request, kind, [])
    with pytest.raises(ValidationError) as failure:
        pc.evaluate_request(request)
    assert failure.value.validator == "contains"
    assert list(failure.value.absolute_path) == ["capabilities"]
    assert_cli_input_failure(request, capsys, str(failure.value))


@pytest.mark.parametrize("kind", ["dataset", "partition"])
@pytest.mark.parametrize("identity", [[], "not-an-object"], ids=["array", "string"])
def test_metadata_dataset_type_cli_failure(tmp_path, capsys, kind, identity):
    request = fixture(tmp_path)
    current = json.loads(request.read_text())
    path = Path(current["workspace"]) / "normal" / current[kind]["metadata_path"]
    metadata = json.loads(path.read_text())
    metadata["dataset"] = identity
    # Do not invoke the aggregate helper, which requires dataset_checksum.
    path.write_text(json.dumps(metadata))
    assert_cli_input_failure(request, capsys, "metadata.dataset must be an object")


@pytest.mark.parametrize("batch_rows", [1, 2, 3, 99])
@pytest.mark.parametrize("centroids", [True, False])
def test_full_means_unequal_weights_batching_and_immutable_inputs(tmp_path, batch_rows, centroids):
    request = fixture(tmp_path, batch_rows=batch_rows, centroids=centroids)
    before = {path: sha256_file(path) for path in tmp_path.rglob("*") if path.is_file()}
    report = pc.evaluate_request(request)
    assert [item["node_count"] for item in report["partitions"]] == [2, 3]
    assert [item["compactness"] for item in report["partitions"]] == pytest.approx([1, 8 / 3])
    assert report["weighted_compactness"] == pytest.approx(2)
    assert report["weighted_compactness"] != pytest.approx(11 / 6)
    assert report["evaluator"]["source_sha256"] == sha256_file(pc.__file__)
    assert report["inputs"]["dataset_content_sha256"] != report["inputs"]["dataset"]["aggregate_checksum"]
    pc.report_validator().validate(report)
    output = tmp_path / "reports/quality.json"
    assert json.loads(output.read_text()) == report
    assert all(sha256_file(path) == checksum for path, checksum in before.items())
    digest = sha256_file(output)
    with pytest.raises(pc.CompactnessError, match="already exists"):
        pc.evaluate_request(request)
    assert sha256_file(output) == digest


def test_singleton_and_identical_vectors(tmp_path):
    report = pc.evaluate_request(fixture(tmp_path, [[2, 3], [8, 9], [8, 9]], [[0], [1, 2]]))
    assert [item["compactness"] for item in report["partitions"]] == [0, 0]
    assert report["weighted_compactness"] == 0


@pytest.mark.parametrize("batch_rows", [1, 2, 4])
def test_large_offset_small_variation(tmp_path, batch_rows):
    # These offsets remain distinct in float32; C1=128/3, weighted C=32.
    vectors = [[2**26], [2**26], [2**26 + 8], [2**26 + 16]]
    report = pc.evaluate_request(fixture(tmp_path, vectors, [[0], [1, 2, 3]], batch_rows=batch_rows))
    assert report["partitions"][1]["compactness"] == pytest.approx(128 / 3, rel=1e-13)
    assert report["weighted_compactness"] == pytest.approx(32, rel=1e-13)


@pytest.mark.parametrize("batch_rows", [1, 3])
def test_compensated_mean_with_mixed_signs(tmp_path, batch_rows):
    # Cancellation would lose the middle 1 in ordinary summation.
    request = fixture(tmp_path, [[2**60], [1], [-2**60]], [[0, 1, 2]], batch_rows=batch_rows)
    report = pc.evaluate_request(request)
    expected = math.fsum((x - 1 / 3) ** 2 for x in [2**60, 1, -2**60]) / 3
    assert report["weighted_compactness"] == pytest.approx(expected, rel=1e-15)


def test_neumaier_retains_small_mixed_sign_contribution():
    sums, corrections = [0.0], [0.0]
    for value in [float(2**60), 1.0, -float(2**60)]:
        pc._add(sums, corrections, 0, value)
    assert sums[0] + corrections[0] == 1.0


@pytest.mark.parametrize("maps,reason", [([[0, 0], [1, 2, 3, 4]], "duplicate"),
    ([[0, 1], [1, 2, 3, 4]], "multiple partitions"), ([[0, 1], [2, 3]], "missing IDs"),
    ([[0, 1], [2, 3, 5]], "out of range"), ([[1, 0], [2, 3, 4]], "ascending"),
    ([[], [0, 1, 2, 3, 4]], "minimum")])
def test_invalid_or_unsupported_maps(tmp_path, maps, reason):
    rejected(fixture(tmp_path, maps=maps), reason)


@pytest.mark.parametrize("value", [math.nan, math.inf, -math.inf])
def test_nonfinite_vectors(tmp_path, value):
    rejected(fixture(tmp_path, [[value], [1], [2]], [[0], [1, 2]]), "non-finite")


@pytest.mark.parametrize("case", ["payload_checksum", "aggregate", "identity", "source", "count",
                                  "header", "length", "centroid_nan", "centroid_shape", "missing_centroid"])
def test_fresh_identity_and_payload_checks(tmp_path, case):
    request = fixture(tmp_path)
    def change(metadata, directory):
        record = metadata["payloads"][0]
        path = directory / record["path"]
        if case == "payload_checksum": record["sha256"] = "a" * 64
        elif case == "identity": metadata["dataset"]["dataset_id"] = "different-dataset"
        elif case == "source": metadata["source_artifacts"][0]["aggregate_checksum"] = "a" * 64
        elif case == "count": metadata["structural"]["partitions"][0]["local_node_count"] = 3
        elif case == "missing_centroid": metadata["payloads"].pop()
        else:
            if case.startswith("centroid"):
                record = metadata["payloads"][-1]
                path = directory / record["path"]
                matrix(path, [[math.nan, 0], [0, 0]] if case == "centroid_nan" else [[0, 0]])
            elif case == "header": path.write_bytes(struct.pack("<II", 2, 2) + path.read_bytes()[8:])
            elif case == "length": path.write_bytes(path.read_bytes()[:-1])
            record.update(byte_length=path.stat().st_size, sha256=sha256_file(path))
    if case == "aggregate":
        data = json.loads(request.read_text()); data["partition"]["aggregate_checksum"] = "a" * 64
        request.write_text(json.dumps(data))
    else:
        change_metadata(request, "partition", change)
    rejected(request)


@pytest.mark.parametrize("case", ["checksum", "header", "length", "structural"])
def test_dataset_checks_with_consistently_rebound_partition(tmp_path, case):
    request = fixture(tmp_path)
    def change(data, directory):
        declaration = data["payloads"][0]
        path = directory / declaration["path"]
        if case == "checksum":
            path.write_bytes(path.read_bytes()[:-4] + struct.pack("<f", 7))
        elif case == "structural":
            data["structural"]["vector_dimension"] = 3
        else:
            path.write_bytes((struct.pack("<II", 4, 2) + path.read_bytes()[8:])
                             if case == "header" else path.read_bytes()[:-1])
            declaration.update(sha256=sha256_file(path), byte_length=path.stat().st_size)
            data["dataset"]["dataset_checksum"] = declaration["sha256"]
    change_metadata(request, "dataset", change)
    current = json.loads(request.read_text())
    data_path = Path(current["workspace"]) / "normal" / current["dataset"]["metadata_path"]
    data = json.loads(data_path.read_text())
    def rebind(partition, _):
        partition["dataset"] = data["dataset"]
        partition["source_artifacts"] = [{key: current["dataset"][key] for key in
                                         ("artifact_type", "format_version", "aggregate_checksum")}]
    change_metadata(request, "partition", rebind)
    rejected(request)


def test_centroid_payload_without_capability_is_invalid(tmp_path):
    request = fixture(tmp_path)
    change_metadata(request, "partition", lambda metadata, _: metadata["capabilities"].remove("partition.centroids/v1"))
    rejected(request, "declared together")


def test_dataset_shape_is_checked_before_allocating_ownership(tmp_path, monkeypatch):
    request = fixture(tmp_path)
    def change(data, _):
        data["dataset"]["global_node_count"] = 100
        data["structural"]["global_node_count"] = 100
    change_metadata(request, "dataset", change)
    current = json.loads(request.read_text())
    data_path = Path(current["workspace"]) / "normal" / current["dataset"]["metadata_path"]
    def rebind(partition, _):
        partition["dataset"] = json.loads(data_path.read_text())["dataset"]
        partition["source_artifacts"] = [{key: current["dataset"][key] for key in
                                         ("artifact_type", "format_version", "aggregate_checksum")}]
    change_metadata(request, "partition", rebind)
    monkeypatch.setattr(pc, "array", lambda *args: pytest.fail("allocated from unverified shape"))
    rejected(request, "native matrix length mismatch")


def test_optional_shard_vectors_are_validated(tmp_path):
    request = fixture(tmp_path)
    def add_shard(metadata, directory):
        path = directory / "shard.bin"
        matrix(path, [[0, 0], [2, 0]])
        metadata["structural"]["partitions"][0]["shard_vectors_role"] = "shard/0"
        metadata["payloads"].insert(0, payload(path, "shard/0", pc.FLOAT_MEDIA))
    change_metadata(request, "partition", add_shard)
    assert pc.evaluate_request(request)["weighted_compactness"] == pytest.approx(2)
    def corrupt_shard(metadata, directory):
        path = directory / "shard.bin"
        matrix(path, [[math.inf, 0], [2, 0]])
        metadata["payloads"][0] = payload(path, "shard/0", pc.FLOAT_MEDIA)
    change_metadata(request, "partition", corrupt_shard)
    current = json.loads(request.read_text()); current["report_path"] = str(tmp_path / "second.json")
    request.write_text(json.dumps(current))
    rejected(request, "non-finite")


@pytest.mark.parametrize("batch", [0, -1, True, 1.5, "2"])
def test_invalid_batch_rows(tmp_path, batch):
    rejected(fixture(tmp_path, batch_rows=batch), error=(pc.CompactnessError, ValueError))


@pytest.mark.parametrize("case", ["overlap", "format", "scalar", "encoding"])
def test_outside_scope_is_not_public_contract_failure(tmp_path, case):
    request = fixture(tmp_path)
    def change(metadata, _):
        if case == "overlap": metadata["capabilities"].append("partition.overlap/v1")
        elif case == "scalar": metadata["dataset"]["scalar_type"] = "uint8"
        elif case == "encoding": metadata["payloads"][0]["media_type"] = "application/json"
    if case == "format":
        data = json.loads(request.read_text()); data["dataset"]["format_version"] = "fake-dataset-json/v1"
        request.write_text(json.dumps(data))
    else: change_metadata(request, "partition", change)
    rejected(request, "不在当前评估器支持范围", pc.UnsupportedInput)


@pytest.mark.parametrize("target", ["vectors.bin", "artifact.json", "centroids.bin", "request.json"])
def test_input_mutation_before_publication_rejected(tmp_path, monkeypatch, target):
    request = fixture(tmp_path)
    original = pc._compute
    def compute(*args, **kwargs):
        result = original(*args, **kwargs)
        path = next(tmp_path.rglob(target))
        path.write_bytes(path.read_bytes() + b" ")
        return result
    monkeypatch.setattr(pc, "_compute", compute)
    rejected(request, "changed during evaluation")


def test_vector_change_between_passes_is_not_hidden_by_a_valid_old_checksum(tmp_path, monkeypatch):
    request = fixture(tmp_path)
    original = pc._rows
    calls = 0
    def rows(path, *args):
        nonlocal calls
        if path.name == "vectors.bin":
            calls += 1
            if calls == 2:
                path.write_bytes(path.read_bytes()[:-4] + struct.pack("<f", 123))
        yield from original(path, *args)
    monkeypatch.setattr(pc, "_rows", rows)
    rejected(request, "checksum mismatch during read")


def test_payload_cannot_escape_artifact_directory(tmp_path):
    request = fixture(tmp_path)
    change_metadata(request, "partition", lambda metadata, _: metadata["payloads"][0].update(path="../other.bin"))
    rejected(request, error=(ValueError, ValidationError))


def test_nonfinite_computation_and_source_change_rejected(tmp_path, monkeypatch):
    request = fixture(tmp_path)
    original = pc._compute
    monkeypatch.setattr(pc, "_compute", lambda *args: (original(*args)[0], math.inf))
    rejected(request, "non-finite")
    monkeypatch.setattr(pc, "SOURCE_SHA256", "a" * 64)
    rejected(request, "source changed")


def test_cli_success_and_failure_have_no_fake_metrics(tmp_path, capsys):
    request = fixture(tmp_path)
    assert pc.main(["--request", str(request)]) == 0
    assert json.loads(capsys.readouterr().out)["status"] == "SUCCEEDED"
    assert pc.main(["--request", str(request)]) == 2
    failure = json.loads(capsys.readouterr().err)
    assert failure["outcome"] == "FAILED"
    assert "already exists" in failure["failure"]["reason"]
    assert "weighted_compactness" not in failure


def test_report_schema_is_independent_of_graph_findings(tmp_path):
    report = pc.evaluate_request(fixture(tmp_path))
    assert "findings" not in report and "means" not in report
    invalid = copy.deepcopy(report)
    invalid["weighted_compactness"] = -1
    with pytest.raises(ValidationError): pc.report_validator().validate(invalid)
