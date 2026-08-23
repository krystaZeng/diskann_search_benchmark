# Quickstart: DiskANN Post-GlobalGraph Search Evaluation

## Status

The Feature implementation and its deterministic/small-real validation paths now exist. This guide
still does not authorize formal SIFT1M PQ, layout, QuerySet normalization, GroundTruth, or search
execution; all five operations remain behind the non-task manual boundary.

Observed implementation checkpoint on 2026-08-22:

- the out-of-tree Linux build at `build/diskann-search-evaluation-linux` completed with GNU C++
  11.4.0, CMake 3.22.1, `Release`, `-O3 -DNDEBUG`, and the fixed image
  `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`;
- the complete applicable CTest selection passed 17/17, and the applicable Python selection passed
  63 tests with the opt-in small-real test skipped in that ordinary invocation; the isolated
  `test_harness_fake_pipeline_e2e.py` regression also passed after the Feature-off link regression
  was corrected;
- the opt-in 2048-point, 32-dimensional, two-query small-real closure at
  `build/diskann-search-evaluation-linux/native-closure.resume4-20260822` completed in 94.31 seconds,
  including native PQ/Layout materialization and load, pinned exact GroundTruth, zero-cache and
  fixed-cache search, 18 upstream oracle points, 18 complete observations, two offline evaluations,
  and PASS parity evidence for both cache protocols; it did not use SIFT1M;
- the actual readiness invocation at `build/diskann-search-evaluation/readiness-audit` returned
  operation counts of zero for PQ, layout, QuerySet, GroundTruth, search, Partition, Local Build,
  and Merge, and emitted no Protocol or request. Dataset, selected-query, binary, resource, cache,
  and pinned DiskANN source checks passed. Project clean-source admission, the exact GlobalGraph
  identity, and the normative performance environment remain pending, so
  `formal_search_ready=false`.

Feature 002 remains:

```yaml
graph_structural_parity_gate: NOT_REACHED
framework_graph_composition: candidate
```

Search Runner Parity in this guide is not Graph Parity and cannot certify that candidate.

## 1. Build

All output is outside `upstream/`; the pinned source stays read-only.

```bash
cmake -S . -B build/diskann-search-evaluation \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_SEARCH_EVALUATION=ON

cmake --build build/diskann-search-evaluation --parallel
```

Before treating this build as admitted, record the exact compiler, generator, flags, linked
dependencies, runner/oracle hashes, pinned DiskANN commit, and upstream clean state.

## 2. Small fixture preparation

Use project test fixtures no larger than the existing small graph. Do not substitute SIFT1M into
these commands while implementing contracts.

Expected fixture inputs:

- validated float32/L2 DatasetArtifact;
- validated GlobalGraphArtifact with positive degree and medoids;
- a small QuerySet reference with opaque source IDs;
- exact L2 GroundTruth with width >=100;
- frozen zero-cache and fixed-cache fixture protocols using K=100 and L>=100.

The Dataset and graph must be copied into a confined test workspace without metadata or payload
rewrites. Fresh validation precedes all native operations.

## 3. PQ materialization

The Harness first writes an immutable normalized request; the fixed runner consumes it.

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation materialize \
  --operation pq \
  --config tests/fixtures/search/pq-materialization.yaml \
  --workspace build/diskann-search-evaluation/runs/small-pq
```

Expected success:

- Dataset fresh validation PASS;
- PQ BuildManifest `SUCCEEDED`;
- PQ pivots and compressed vectors parse and match Dataset count/dimension;
- candidate validation PASS and immutable `PQArtifact/v1` publication;
- no GlobalGraph source in the PQ Artifact;
- no P/B/M execution.

Expected failure/invalid behavior:

- malformed request/input mismatch fails before the callable;
- nonzero callable failure retains manifest/logs but publishes nothing;
- malformed payload or checksum mismatch produces `INVALID_OUTPUT` and quarantine.

## 4. DiskLayout materialization

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation materialize \
  --operation disk_layout \
  --config tests/fixtures/search/disk-layout-materialization.yaml \
  --workspace build/diskann-search-evaluation/runs/small-layout
```

Expected success:

- Dataset and complete GlobalGraph lineage fresh validation PASS;
- every graph node has positive degree before native materialization;
- input graph checksum is identical before/after;
- DiskLayout uses full-precision vectors, disk PQ 0, reorder false;
- medoids preserve the GlobalGraph order; declared-centroid policy requires a traceable source
  GlobalGraph centroid payload, while fallback forbids one and uses the pinned medoid/vector path;
- `DiskLayoutArtifact/v1` validation and PQFlash load probe PASS;
- no P0, B0, or Merge output or invocation.

A zero-degree graph remains a valid graph for other consumers but fails this post-graph
compatibility gate before `create_disk_layout`.

### Feature-private query input materialization

Small-fixture QuerySet normalization and exact-L2 GroundTruth preparation reuse the same Python
Harness `materialize` operation; they are not C++ runner operations or new top-level commands.

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation materialize \
  --operation query_set \
  --request tests/fixtures/search/requests/materialize-query-set.json

.venv/bin/python -m diskann_harness.diskann_search_evaluation materialize \
  --operation ground_truth \
  --request tests/fixtures/search/requests/materialize-ground-truth.json
```

The first operation produces the immutable native QuerySet payload, row-index-to-opaque-source-ID
mapping, QuerySetReference, independent BuildManifest, and ValidationEvidence. The second invokes
the pinned `compute_groundtruth` binary with shell-free argv and produces a validated
GroundTruthReference plus separate terminal BuildManifest and ValidationEvidence. Neither operation
creates a public Artifact or enters the search timing region. Formal SIFT1M invocations remain
behind the non-task manual boundary.

## 5. Validate reusable assets

The implementation extends the existing validator/catalog rather than introducing another
validator. Validation commands use the existing operation:

```bash
build/diskann-search-evaluation/src/cli/diskann_pipeline_runner validate \
  --workspace build/diskann-search-evaluation/runs/small-pq \
  --artifact normal/pq/metadata.json

build/diskann-search-evaluation/src/cli/diskann_pipeline_runner validate \
  --workspace build/diskann-search-evaluation/runs/small-layout \
  --artifact normal/disk-layout/metadata.json
```

Each command must persist a new independent ValidationEvidence file. A prior evidence path is
provenance, not a substitute for current validation.

## 6. PQFlash load probe

```bash
build/diskann-search-evaluation/src/search/diskann_search_evaluation_runner load-probe \
  --pq-artifact normal/pq/metadata.json \
  --disk-layout-artifact normal/disk-layout/metadata.json \
  --workspace build/diskann-search-evaluation/runs/small-load
```

Expected success proves the exact loader file set, point/dimension agreement, no disk-PQ sidecar,
reorder false, and the declared end-to-end medoid/centroid policy. It does not publish a graph or
search result.

## 7. Instrumented-vs-upstream Search Runner Parity

Run only with a small frozen protocol and identical assets/controls:

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation parity \
  --protocol tests/fixtures/search/protocol-zero-cache.json \
  --oracle build/diskann-search-evaluation/cpp-main-upstream/apps/search_disk_index \
  --runner build/diskann-search-evaluation/src/search/diskann_search_evaluation_runner \
  --workspace build/diskann-search-evaluation/runs/small-parity
```

The gate checks exact ordered IDs, distance tolerance (`atol=1e-5`, `rtol=1e-6`), Recall@10,
Recall@100, Mean IO semantics, QPS/latency formulas, and the timing boundary. Independent wall-clock
values are not required to be equal. GroundTruth is used for parity evaluation, never passed to
PQFlash search.

Before parity passes, structured performance observations cannot support the formal performance
claims governed by the existing Search Runner Parity gate. The gate's scope is unchanged.

## 8. Small instrumented search

```bash
build/diskann-search-evaluation/src/search/diskann_search_evaluation_runner search \
  --protocol tests/fixtures/search/protocol-zero-cache.json \
  --workspace build/diskann-search-evaluation/runs/small-search-zero
```

Run the fixed-cache protocol separately. Each protocol has three repetitions for every declared L
and writes one immutable SearchObservation/v1 per raw point. A successful document includes every
query and exactly 100 ordered IDs/distances. Failed/invalid/cancelled documents remain retained and
are never replaced.

## 9. Offline observation evaluation and recomputation

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation evaluate \
  --protocol tests/fixtures/search/protocol-zero-cache.json \
  --observations build/diskann-search-evaluation/runs/small-search-zero/reports/observations \
  --workspace build/diskann-search-evaluation/runs/small-evaluation-zero
```

Expected report behavior:

- Recall@10 and Recall@100 use the same K=100 result rows;
- the Python Harness `evaluate` invocation computes and displays QPS from query count / batch wall
  time and mean/P99.9 latency plus Mean IOs from raw QueryStats for every complete successful
  quality-eligible observation;
- each repetition remains visible;
- each cache/L group reports separate quality/performance eligible counts and medians, or no
  aggregate for a metric family when none of its repetitions are eligible;
- failed, invalid, cancelled, identity-mismatched, and excluded points remain audit/exclusion
  references with identity, path, checksum, terminal status, and reason; they contribute no metric,
  aggregate, median, or comparison finding and cannot make either eligibility true;
- serialization and evaluation occur outside the search timer.

An evaluation-only GroundTruth/evaluator/aggregation change may generate a new report from the same
complete observations when the search execution checksum is unchanged. Any metric not derivable
from saved raw fields must state `requires new search execution`.

## 10. Normative Linux versus non-normative development

Formal QPS, latency, and Mean IO require all of:

- real Linux host;
- declared local SSD and filesystem/mount;
- successful O_DIRECT aligned-read probe on the actual disk-index filesystem;
- successful libaio setup/submit/getevents probe;
- fixed hardware/OS/storage/toolchain fingerprint;
- admitted source/binary/protocol identities.

A container is normative only on a real Linux host with a local-SSD bind mount and successful
in-container probes. macOS, Docker Desktop/macOS, network/remote storage, or an unproved device is
always `non_normative` for performance. A complete valid observation from such an environment may
produce formal Recall@10/Recall@100 when GroundTruth and quality eligibility pass. The Python
Harness `evaluate` call still computes and displays its QPS, mean/P99.9 latency, and Mean IOs from
the raw observation; persisting those values is optional, and any persisted values are
`informational_non_formal` and cannot enter formal performance aggregates.

There is no buffered-I/O fallback that retains formal performance eligibility.

## 11. Formal SIFT1M gates (not executed here)

Implementation ends with a **non-executing formal-readiness preflight** using the one
Feature-specific `preflight` path. It checks the source, binaries, paths, resources, environment,
and currently frozen controls that are already known, and reports the not-yet-created exact
PQArtifact, DiskLayoutArtifact, normalized QuerySetReference, GroundTruth, and zero/fixed-cache
SearchProtocol identities as
pending. It must not generate any of those objects or a SearchObservation, and its completion must
not be reported as formal-preflight PASS or formal SIFT1M readiness.

Inside the non-task manual execution boundary, the sole normalization owner is
`harness/python/diskann_harness/diskann_search_evaluation.py`. Before any formal search:

1. implementation unit/contract/small-real tests pass;
2. an approved `materialize --operation pq` invocation generates and validates the exact
   PQArtifact;
3. an approved `materialize --operation disk_layout` invocation generates and validates the exact
   DiskLayoutArtifact;
4. approved `materialize --operation query_set` and `materialize --operation ground_truth`
   invocations generate and validate the exact QuerySetReference and GroundTruthReference;
5. the same Feature-specific `preflight --mode formal-search` command reads the exact zero-cache
   and fixed-cache input declarations, normalizes and writes exactly two immutable SearchProtocols
   once, then immediately rereads and validates their schema, checksums, search-execution
   identities, Artifact/input identities, runner binary, paths, resources, frozen controls, current
   ABI/dependencies, and load-probe/small-search evidence without running search;
6. common search admission PASS presents the resulting exact immutable search requests for separate
   human review and explicit approval;
7. each approved `search` consumes its frozen SearchProtocol without renormalizing it.

Common search admission permits a Mac Docker quality request when the exact inputs, runner,
ABI/dependencies, fixed search path, paths, resources, and controls are usable; it does not require
Linux/local-SSD/O_DIRECT/libaio or Search Runner Parity. If the binary or fixed path cannot actually
run in that environment, admission fails. The same preflight separately reports performance
admission. Formal QPS/latency/Mean IOs require the actual Linux Search Runner Parity PASS plus the
normative Linux/local-SSD/O_DIRECT/libaio conditions in section 10; their absence does not block an
approved quality run or formal Recall.

Readiness and formal-search modes are two input-completeness modes of that one preflight command and
implementation, not separate commands, runners, schemas, or frameworks. There is no `protocol`
top-level command or Protocol builder subsystem.

The first formal protocols use selected_query.csv, no ID-based self exclusion, K=100,
L `[100,200,400]`, W=2, six threads, three repetitions, no warmup, UINT32_MAX IO limit, and
end-to-end entry policy. Planning these values is not approval to execute them.

No command in this quickstart runs Query/Search larger than SIFT1M or upgrades the Feature 002
graph gate/candidate Baseline.
