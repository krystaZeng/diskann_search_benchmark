# Quickstart: Planned cpp_main P0+B0+M0 Structural Closure

This is a post-implementation validation guide. It does not run complete Graph Parity, Search
Evaluation, or final Baseline certification. The fixed Feature-local Structural/Contract Gate is
limited to Feature 002 P0+B0+M0 and never registers a validated Baseline. All generated paths must
remain outside upstream/diskann-cpp-main.

## 1. Verify immutable inputs

From the repository root:

~~~sh
git ls-files --stage upstream/diskann-cpp-main
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
~~~

Expected:

- gitlink mode 160000 and object 78256bbab4685e1774e78d331e081a153be26823;
- submodule HEAD 78256bbab4685e1774e78d331e081a153be26823;
- empty upstream porcelain status.

Any mismatch stops configure and execution.

The SIFT1M Dataset admission must establish:

~~~text
repository-relative path: sift1M_dataset/base_vector.bin
format: little-endian uint32 npts, uint32 dim, then little-endian float32 values
npts: 1000000
dim: 128
byte length: 512000008
SHA-256: 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e
non-finite values: 0
~~~

Do not read or convert sift1M_dataset/selected_query.csv for this graph-build closure.

## 2. Configure and build outside upstream

~~~sh
cmake -S . -B build/cpp-main-baseline \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON
cmake --build build/cpp-main-baseline --parallel
~~~

Expected:

- upstream is configured with an explicit binary directory below build/cpp-main-baseline;
- the Feature links the real diskann_s target at the pinned source;
- no generated file appears under upstream/diskann-cpp-main;
- the build evidence records actual compiler, CMake, generator, build type, effective definitions
  and compile options, actual commands, dependency identities, and binary checksums.

The Native Probe toolchain values in research.md are historical evidence only. The current build
must record its own identity.

## 3. Run focused tests before SIFT1M

### Native formats and source/build admission

~~~sh
ctest --test-dir build/cpp-main-baseline --output-on-failure \
  -R '^cpp_main_(source_build_admission|native_formats)$'
~~~

Expected coverage:

- exact commit/gitlink/clean-state admission and read-only external build paths;
- DiskANN float32 Dataset header, finite payload, overflow-safe length, and checksum;
- uint32 ID maps and medoids;
- float32 centroids;
- local graph, local .data, and merged graph header/record/length parsing;
- truncated, trailing, overflowed, wrong-type, non-finite, and out-of-range negative fixtures.

### Descriptors, stages, and small complete Pipeline

~~~sh
ctest --test-dir build/cpp-main-baseline --output-on-failure \
  -R '^cpp_main_(descriptors_preflight|p0|b0|m0|pipeline)$'
~~~

Expected positive findings:

- all three descriptors declare their Artifact/version/ID-space/capability/service requirements,
  one provided Artifact, and stable parameter_schema_path;
- P0 calls partition_with_ram_budget and produces a map-only aggregate PartitionArtifact with
  centroids and no shard-vector claim;
- B0 receives an explicit PartitionId, reconstructs rows in exact map order, calls the pinned
  retrieve/build/save sequence, validates the .data companion, and publishes a local graph;
- M0 receives the exact complete Partition/local-graph set, calls merge_shards, and publishes a
  global graph plus medoids;
- the existing Pipeline orders P0, all explicit B0 selections, and M0, and validates every Artifact
  before downstream execution.

Required negative findings include source mismatch, invalid parameters, missing/invalid Dataset,
missing or invalid PartitionId/map/centroids, shard row-order mismatch, local neighbor or degree
error, incomplete/duplicate/extra M0 inputs, global neighbor error, and missing/invalid medoids.
Preflight failures create no factory. Invalid or cancelled stage output invokes no downstream stage.

## 4. Inspect the draft structural Profile

The planned configuration is:

~~~text
experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml
~~~

It declares baseline_validation and the exact three implementation identities:

~~~yaml
study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none
status: draft
implementations:
  partition: cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1
  local_build: cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1
  merge: cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1
profile:
  R: 16
  L: 32
  build_DRAM_budget_gib: 0.6
  num_threads: 6
  data_type: float32
  metric: l2
  filtered: false
  labels_enabled: false
  build_pq_bytes: 0
  disk_pq_bytes: 0
  opq_enabled: false
execution:
  workspace: build/cpp-main-baseline/runs/sift1m-unified
  runner: build/cpp-main-baseline/src/baseline/cpp_main/cpp_main_baseline_runner
  report: specs/002-cpp-main-upstream-baseline/unified-sharded-report.json
~~~

The implementation-specific P0/B0/M0 parameter documents also record every derived/default value
defined in data-model.md. The baseline-local normalizer reuses the existing safe YAML loading and
JSON-compatible tree validation, validates cpp-main-run-request.schema.json, writes immutable
normalized JSON during non-running preflight, and the later `execute --request` mode invokes the
runner exactly once. This does not modify or reuse Feature 001's Fake-only
infrastructure-validation request contract.

Before execution, the runner recomputes the source branch predicate. For the probed ABI, fixed
Dataset identity, and draft Profile values:

~~~text
estimated full index RAM = 707519996.643066406 bytes
M budget                = 644245094.4 bytes
single condition        = estimated RAM < M budget
result                  = false; sharded path required
~~~

The exact compiled ABI value must be recomputed and recorded rather than copied blindly from this
guide.

## 5. Preflight SIFT1M without starting the runner

Only after all focused tests pass:

~~~sh
.venv/bin/python -m diskann_harness.cpp_main_baseline preflight \
  --config experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml \
  --workspace build/cpp-main-baseline/runs/sift1m-unified
~~~

Preflight must validate the draft Profile, pinned Source/Build Identity, and complete Dataset;
publish the confined immutable DatasetArtifact and its ValidationEvidence; recompute the strict
sharded-path predicate; and write exactly:

~~~text
build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json
~~~

Its terminal response returns that request path and the admitted Dataset ArtifactRef. It must not
start `cpp_main_baseline_runner`, create a Stage factory, or invoke P0, B0, or M0; every stage
invocation count is zero. A failed preflight retains failure evidence but produces no executable
request.

## 6. Run one Unified structural closure from the admitted request

Only after preflight succeeds:

~~~sh
.venv/bin/python -m diskann_harness.cpp_main_baseline execute \
  --request build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json \
  --workspace build/cpp-main-baseline/runs/sift1m-unified \
  --runner build/cpp-main-baseline/src/baseline/cpp_main/cpp_main_baseline_runner
~~~

Execute consumes the immutable normalized request and its validated DatasetArtifact; it does not
accept `--config` as an implicit second admission path or rewrite the request. The command performs
one retained structural run. It must not alter a parameter, retry with a new profile, run a second
statistical repetition, execute the Native Reference, or start Graph Parity.

Expected stage evidence:

1. P0 returns at least two Partitions and publishes centroids plus one complete map for every
   contiguous PartitionId, with no shard vectors.
2. The executor passes each explicit PartitionId to B0. Every reconstructed shard row matches its
   selected Dataset row; every LocalGraph has the same node count as its map, only valid
   LocalNodeIds, and observed degree at most 10.
3. M0 receives exactly the complete validated set. The final graph has 1,000,000 records, only
   valid GlobalNodeIds, and observed degree at most 16. Medoids exist and validate.
4. The GlobalGraphArtifact aggregate checksum covers both global_graph and medoids.
5. One build-stage BuildManifest plus the existing per-stage BuildManifests and separate structural
   ValidationEvidence collectively retain complete Source/Build/Dataset/configuration/stage and
   lifecycle identity. The separate graph-integrity EvaluationReport is generated by
   `cpp_main_native_graph_integrity_evaluator` from the already validated structural claims and
   corresponding ValidationEvidence; it is not a Graph Parity Report or performance report. The
   existing `fake_graph_integrity_evaluator` remains limited to its Fake JSON adjacency path.
6. `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json` is written as a Feature-local
   structural summary that references those separate records rather than embedding them.

The P0 Partition count is not required to equal the Native Probe's observed count of three. Native
P0 randomness remains unchanged. Fewer than two Partitions fails the sharded closure rather than
silently changing M.

## 7. Verify terminal scope and upstream state

The terminal evidence must retain:

~~~yaml
study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none
validation_scope: structural_and_contract
profile_ref: experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml
profile_status: draft
formal_graph_parity: false
validated_graph_baseline_registered: false
run_class: unified_sharded_closure
~~~

Then verify:

~~~sh
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
git diff --check
~~~

Expected: the pinned upstream HEAD is unchanged and its porcelain status is empty.

Report all executed, failed, and applicable unexecuted tests separately. A valid structural closure
must not be described as Graph Parity PASS, production-ready, complete DiskANN parity, or
validated_graph_baseline. Do not run Recall, QPS, latency evaluation, a larger dataset, parameter
search, or Feature 001 deferred hardening from this guide.

## 8. Validate the fixed Structural/Contract Parity Gate

The Feature-local role identities are fixed as:

~~~text
Reference: cpp_main_native_sharded_reference_v1
Framework: cpp_main_p0_b0_m0_framework_candidate_v1
Framework state: candidate
~~~

Validate the fixed Policy, four-state Gate derivation, report schema, current Policy checksum, and
claim boundary with:

~~~sh
.venv/bin/python -m pytest tests/python/test_cpp_main_structural_parity.py -q
sha256sum specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.json
.venv/bin/python -m json.tool \
  specs/002-cpp-main-upstream-baseline/structural-parity-report.json >/dev/null
~~~

The expected Policy checksum is:

~~~text
fcc6310415bdae8ce62231c2e6edf3ed8330b8234b8e8a90cb708e830299bd1d
~~~

The current report result is `NOT_REACHED`, not `PASS`: the Native Probe completed successfully,
but its transient merged graph was not retained as a complete immutable Reference graph with
matching structural validation evidence. The native disk-layout output must not be substituted for
that missing evidence. The completed Unified structural closure remains valid independent
evidence, but it cannot fill the Reference side of this Gate.

Even a future limited Gate `PASS` would prove only Feature 002 P0+B0+M0 structural-and-contract
parity. It would not prove Search Semantics, Quality, Recall, full Native-versus-Framework parity,
or final Framework Baseline certification, and the Framework role would remain `candidate` until
separate Search Evaluation/S0 and later Search Semantics/Quality work complete.
