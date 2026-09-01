# Quickstart Validation Guide

This guide records the implemented Feature 001 command surface and the validation commands used by
the repository. All build and workspace paths stay outside `upstream/diskann-cpp-main`.

## 1. Prerequisites

- Linux with GCC 11+ is the normative environment, or macOS with a C++17-capable
  AppleClang/Clang for Feature tests only.
- CMake 3.20+ and an available build backend such as Ninja or Make.
- OpenSSL 3.x development files and `nlohmann_json` 3.11+ discoverable by CMake.
- Python 3.10+.
- The fixed `upstream/diskann-cpp-main` gitlink at
  `78256bbab4685e1774e78d331e081a153be26823`; it will not be built or modified.

Use an out-of-tree build directory outside `upstream/`.

## 2. Install Python Test Dependencies

Using the standard library tooling:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -e '.[test]'
```

`pyproject.toml` is authoritative for test dependencies. uv may be used as an alternative frontend,
but no validation command requires it.

## 3. Run the Harness Build Lifecycle and Verify Its Manifest

For a first reproduction, `build/feature-001` must not already exist. Use the project virtual
environment and the Harness build lifecycle so the runner and its terminal BuildManifest are
created by the same configure/build operation:

```bash
test ! -e build/feature-001
.venv/bin/diskann-harness build \
  --config tests/fixtures/fake_pipeline/experiment.yaml \
  --source-dir . \
  --build-dir build/feature-001 \
  --build-type Debug
test -f build/feature-001/CMakeCache.txt
test -f build/feature-001/build-manifest.json
test -x build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner
```

Expected:

- `build-manifest.json` is schema-valid, terminal `SUCCEEDED` build evidence bound to the actual
  `CMakeCache.txt`, configure/build commands, source configuration digests, and build directory;
- the public core compiles as strict C++17;
- every descriptor exposes `parameter_schema_path`, a stable repository-relative implementation
  parameter JSON Schema path;
- the installed runner uses `RegistryAdmissionPolicy::kProduction` and contains no Fake
  implementation registration;
- the non-installed test runner uses `RegistryAdmissionPolicy::kTest` and includes exactly the three
  Fake identities;
- no generated file or Git change appears under `upstream/diskann-cpp-main`.

Ordinary `cmake` configure/build commands do not replace `diskann-harness build`, because they do
not publish the required terminal BuildManifest. If `build/feature-001` already exists, do not
blindly rerun the Harness build in a directory containing an immutable manifest. Reuse it only when
the execution gate accepts that directory's `build-manifest.json`, `CMakeCache.txt`, and executable
runner as one consistent build. Otherwise stop and prepare a separately approved recovery; do not
overwrite the existing manifest. The build configuration digest is not required to equal the
canonical or reuse execution configuration digest because they describe different lifecycle
inputs.

## 4. Execute the Canonical Fake Pipeline

The Fake runner and configuration are test-only:

```yaml
# Governance excerpt from contracts/examples/fake-experiment.yaml
request_version: 1.0.0
request_id: fake-six-node-two-run-v1
operation: execute
study_class: infrastructure_validation
repetition_policy:
  count: 2
  aggregation: retain_all
```

The complete configuration uses identical input and stage parameters for both repetitions. The
Harness creates an independent `run_id`, `report_id`, timestamps, and provenance for each retained
run; the pinned Baseline Reference remains `not-executed`.

```bash
.venv/bin/python -m diskann_harness.cli execute \
  --config tests/fixtures/fake_pipeline/experiment.yaml \
  --workspace build/feature-001/fake-workspace \
  --runner build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner
```

At the C++ stage boundary, Local Build is invoked once for each record in the validated aggregate
PartitionArtifact. The selector is explicit and is not encoded as a shard ArtifactRef:

```cpp
for (const PartitionRecord& partition : partition_artifact.partitions) {
  local_builder.build(
      partition_artifact_ref,
      partition.partition_id,
      local_build_config,
      stage_context);
}
```

Every selected `partition_id` must exist and be in range. Each resulting LocalGraphArtifact records
that owning PartitionId and the aggregate checksum of the PartitionArtifact used as its source.

Expected result:

```text
Partition 0 map: [0,1,2,3]
Partition 1 map: [2,3,4,5]
Global graph:
0:[1,2] 1:[0,2] 2:[1,3,4] 3:[2,4] 4:[3,5] 5:[]
node_count=6, observed_max_degree=3, invalid_neighbor_count=0
```

The workspace must contain separate normal Artifact metadata/payloads, BuildManifests, validation
evidence, and EvaluationReport references. It must retain both repeated-run results under
`retain_all`, with identical payload and aggregate Artifact checksums and identical deterministic
evaluation findings projections. Complete EvaluationReport JSON documents are not required to be
byte-identical: `report_id`, `generated_at`, run IDs, paths, timestamps, provenance, and
non-deterministic runtime metrics remain independent.

The retained summary is written to
`build/feature-001/fake-workspace/reports/summaries/fake-six-node-two-run-v1.json`.
Normal Artifact metadata is below `normal/<run_id>/`, stage BuildManifests are below
`manifests/<run_id>/stages/`, validation evidence is below `manifests/evidence/`, and Evaluation
Reports are below `reports/`. Invalid candidates use only `quarantine/` and never appear below
`normal/`.

## 5. Validate Reuse

Run the reuse fixture using the published Partition ArtifactRef from the preceding run:

```bash
.venv/bin/python -m diskann_harness.cli execute \
  --config tests/fixtures/fake_pipeline/reuse-partition.yaml \
  --workspace build/feature-001/fake-reuse-workspace \
  --runner build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner
```

Expected: Partition invocation count is zero, current validation succeeds, Local Build and Merge
run, and the consuming BuildManifest records the exact reused input. A dataset/checksum mismatch
fixture must fail without changing or consuming the cached Artifact.

## 6. Run Related C++ and Python Validation

```bash
ctest --test-dir build/feature-001 --output-on-failure
.venv/bin/python -m pytest
```

Expected CTest coverage:

- public headers compile in C++17 and expose no C++20 types;
- Registry Production/Test admission, duplicate/stage mismatch, and capability preflight failures
  invoke no stages;
- Merge descriptor preflight requires both PartitionArtifact and LocalGraphArtifact entries and
  rejects a missing input, incompatible version, or incompatible per-input ID space;
- the simple CMake target/source isolation check confirms the production runner links no Fake target,
  contains no Fake registration source, and keeps the Fake runner independent;
- JSON/schema codecs and C++/Python canonical checksum golden vectors agree;
- LocalToGlobal completeness, local/global ranges, counts, degrees, and source checksums validate;
- exact six-node Fake output, zero-neighbor handling, cancellation, reuse, and quarantine pass;
- production Registry enumeration contains no Fake identity.

Expected pytest coverage:

- duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation accepts an ordinary
  top-level mapping with JSON-compatible values and rejects duplicate keys, top-level sequences,
  non-string keys, Python object/custom tags, timestamp, binary, set, NaN, Infinity, and other
  non-JSON values before the normalized JSON is schema-validated;
- a temporary install prefix contains the production runner but no Fake runner, Fake library, Fake
  fixture, or Fake-only file;
- `pyproject.toml` supports Python 3.10+ without requiring uv;
- subprocess argument handling and exit-code mapping preserve machine failure categories;
- build/execute/validate/evaluate Harness commands consume and produce separate JSON documents;
- quarantine cannot be resolved by ordinary validate/evaluate paths;
- two retained deterministic runs and their results are preserved under `retain_all` rather than
  selecting one; their Artifact checksums and deterministic evaluation findings projections match
  while their run/report identities, timestamps, and provenance remain distinct.

## 7. Run the Negative Matrix

```bash
ctest --test-dir build/feature-001 --output-on-failure \
  -R 'compatibility_preflight|quarantine_isolation|pipeline_failure_gates|negative_matrix_e2e'
.venv/bin/python -m pytest \
  tests/python/test_harness_reuse_e2e.py \
  tests/python/test_feature_governance.py
```

Expected:

- preflight/Registry failures produce zero selected-stage invocations;
- invalid produced candidates return exit `5`, have a specific failure category, and are retained
  only in quarantine;
- ordinary resolution, evaluation, reuse, and aggregation cannot discover quarantined output;
- independent cancellation tests interrupt Fake Partition, Local Build, and Merge at deterministic
  stage checkpoints, prevent all later stages, publish no normal Artifact, and do not fabricate an
  INVALID_OUTPUT result.

## 8. Verify Upstream Immutability

Capture before/after state around the complete suite:

```bash
git submodule status -- upstream/diskann-cpp-main
git status --short -- upstream/diskann-cpp-main
```

Expected gitlink: `78256bbab4685e1774e78d331e081a153be26823`; expected status output for
the upstream path: empty. This Feature runs no real DiskANN command and no SIFT experiment.

## 9. Reporting Validation

Completion reports must separately list:

- CTest commands executed and pass/fail counts;
- pytest commands executed and pass/fail counts;
- failed tests and impact;
- applicable tests not executed and why;
- upstream gitlink/status evidence;
- explicit statement that Baseline parity, SIFT1M, and larger experiments are not applicable to
  this Feature because no real algorithm is integrated.
