# Research: DiskANN Post-GlobalGraph Search Evaluation

## Research status and authority

This research is limited to Phase 0 for Feature 005. It binds every DiskANN behavior to the
read-only Baseline below and records project-code facts from the working tree inspected on
2026-08-19.

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
source_path: upstream/diskann-cpp-main
```

The clarification answers in [spec.md](spec.md) are locked experimental semantics, not research
alternatives. No SIFT1M materialization, GroundTruth generation, or search was run during this
research. The pinned upstream tree was inspected read-only.

## R-001 — Current project reuse boundary

**Decision**

Reuse the existing `ArtifactRef`, Artifact metadata envelope, canonical checksum, `BuildManifest`,
`ValidationEvidence`, `Validator`, workspace confinement, immutable persistence, and quarantine
facilities. Add public `PQArtifact/v1` and `DiskLayoutArtifact/v1` to the existing Artifact type and
schema catalog. Keep SearchProtocol, SearchObservation, and SearchEvaluationReport Feature-local.
Do not add a fourth `StageKind`, extend `PipelineExecutor`, or register search as Merge.

**Rationale**

The reusable data assets need normal Artifact identity, validation, lineage, and reuse. Search
execution is not one of the three graph-construction stages and has no second backend that would
justify a registry or hierarchy.

**Source evidence**

- `include/diskann_pipeline/identifiers.hpp:17-34` defines four Artifact types and exactly three
  Stage kinds.
- `include/diskann_pipeline/artifact_ref.hpp:11-39` already expresses full Artifact and source
  identity as type, version, checksum, and path.
- `src/validation/canonical_json.cpp:55-105` includes type, version, Dataset checksum, ID space,
  capabilities, payloads, sources, and structural projection in the aggregate checksum.
- `src/validation/validator.cpp:70-115` and `src/validation/artifact_validator.cpp:16-477` provide
  admission, payload, aggregate, Dataset, source-lineage, and native semantic checks.
- `src/cli/runner_protocol.cpp:370-519` already resolves and fresh-validates a source lineage and
  persists `ValidationEvidence`.
- `include/diskann_pipeline/registry.hpp:30-57` has only Partition, Local Build, and Merge factories.
- `src/core/workspace.cpp:41-142`, `src/core/provenance_store.cpp:9-37`, and
  `src/core/quarantine_manager.cpp:43-93` provide the required confined, immutable, and failure
  behavior outside `PipelineExecutor`.

**Alternatives considered**

- Add a public S0/Search stage: rejected because it violates the Feature boundary.
- Register search as Merge: rejected because the contract and lifecycle would be false.
- Create a Search registry/backend hierarchy: rejected because the MVP has one pinned DiskANN path.
- Duplicate the Validator in the Feature runner or Python: rejected because it would fork contract
  semantics.

**Consequences**

Implementation must extend the public Artifact enum/string codec, runtime common schema,
schema catalog, type-specific validation, canonicalization/codec tests, and source inventory for
the two new Artifact types. Fixed Feature-local implementation declarations carry stable identity,
version, reproduction mode, `requires`, `provides`, ID spaces, capabilities, and runtime services,
but do not enter `ImplementationRegistry`. The full Feature 005 `contracts/common.schema.json` is
the sole future source authority for the amended runtime common target; it differs from the current
runtime v1 schema only by adding `pq` and `disk_layout`. PQ/DiskLayout receive their own Feature 005
source mappings, while all unrelated runtime targets retain their single Feature 001 authority.

**Remaining risk**

`tests/python/test_contract_source_sync.py:20-29` currently assumes every runtime schema originated
in Feature 001. Implementation must remove only that location assumption and make the explicit
`contracts/v1/SOURCES.json` inventory authoritative. Unique target ownership, path confinement,
source/target existence, byte identity, and source SHA-256 verification remain unchanged; no
overlay, patch engine, or multi-source composition is introduced.

## R-002 — Existing input admission and the post-graph compatibility gap

**Decision**

Fresh-validate DatasetArtifact and the complete GlobalGraphArtifact lineage with the existing
Validator before materialization. Add one narrow disk-layout compatibility check after common
validation: every native graph record must have positive degree, Dataset row count/dimension must
match the graph, frozen-point count must be zero, and the actual medoid/centroid sidecars must obey
the pinned loader contract. Do not alter or version-bump GlobalGraphArtifact/v1.

**Rationale**

The existing validator proves normal graph validity but allows zero-degree nodes. The fixed
`create_disk_layout` path asserts positive degree; relying on a Release-build assertion is not a
fail-closed compatibility gate.

**Source evidence**

- `src/validation/artifact_validator.cpp:217-239` and
  `src/baseline/cpp_main/native_formats.cpp:81-109,218-235` validate the native float32 Dataset.
- `src/validation/artifact_validator.cpp:404-475` and
  `src/baseline/cpp_main/native_formats.cpp:148-213,262-275` validate GlobalGraph sources, native
  header, GlobalNodeIds, self edges, degrees, medoids, payload checksums, and Dataset identity.
- `upstream/diskann-cpp-main/src/disk_utils.cpp:989-992,1038-1041` requires `nnbrs > 0` and
  `nnbrs <= width` while creating the layout.
- `upstream/diskann-cpp-main/src/pq_flash_index.cpp:1035-1059` requires disk/PQ point-count
  agreement and maximum graph degree no greater than 512.
- `src/baseline/cpp_main/m0_merge.cpp:340-356` verifies the cpp_main-produced entry/medoids
  relationship at generation time; it is not a substitute for fresh downstream admission.

**Alternatives considered**

- Make zero degree globally invalid for all GlobalGraphArtifact/v1: rejected because it is a
  post-graph consumer constraint, not an existing graph-format rule.
- Repair zero-degree nodes during layout: rejected because it would modify adjacency.

**Consequences**

Compatibility failure occurs before calling any PQ/layout/search function and creates failure
evidence without publishing a DiskLayoutArtifact. A previously valid GlobalGraph remains valid for
other consumers.

**Remaining risk**

The exact multiple-medoid and optional-centroid load behavior must be covered by a small native
load probe; source evidence is conclusive, but no implementation probe was run in Phase 0.

## R-003 — Post-graph decomposition and integration method

**Decision**

Never call `build_disk_index`. Link a project-owned thin C++ wrapper to the already admitted
`diskann_s` target and call:

1. `diskann::generate_quantized_data<float>` once for the Dataset-derived PQ assets;
2. `diskann::create_disk_layout<float>` once per Dataset + GlobalGraph;
3. `diskann::PQFlashIndex<float>` for load/search.

Use distinct stable identities:

| Operation | identity | mode |
|---|---|---|
| PQ generation | `cpp_main_post_graph_pq_v1` | `upstream_faithful` |
| Disk layout | `cpp_main_post_graph_disk_layout_v1` | `upstream_faithful` |
| upstream oracle | `cpp_main_search_disk_index_oracle_v1` | `upstream_faithful` |
| structured runner | `cpp_main_pqflash_search_instrumented_v1` | `instrumented` |
| evaluator | `cpp_main_search_metrics_evaluator_v1` | `instrumented` |

The fixed DiskANN commit remains in SourceIdentity, not in the stable implementation ID.

**Rationale**

The full entry always rebuilds a Vamana graph before layout. The two required templates are
already present in `diskann_s`, so a thin wrapper is smaller and more faithful than subprocess
composition or recompiling copied source.

**Source evidence**

- `upstream/diskann-cpp-main/src/disk_utils.cpp:1286-1316` prepares PQ, then
  `:1323-1329` calls `build_merged_vamana_index`, and only `:1331-1344` creates the layout.
- Callable declarations are in `include/pq.h:88-92`, `include/disk_utils.h:103-106`, and
  `include/pq_flash_index.h:24-82`; float instantiations are in `src/pq.cpp:1183-1188`,
  `src/disk_utils.cpp:1379-1389`, and `src/pq_flash_index.cpp:1791-1796`.
- `cmake/CppMainBaseline.cmake:1-104` already admits the pinned clean source, creates an
  out-of-tree upstream build, requires `diskann_s`, and exposes `diskann_cpp_main_upstream`.
- The upstream source target and dependencies are defined in
  `upstream/diskann-cpp-main/src/CMakeLists.txt:4-22` and root `CMakeLists.txt:69-299`.

**Alternatives considered**

- `build_disk_index`: rejected because it reruns graph construction.
- `apps/utils/generate_pq`: rejected because `generate_pq.cpp:23-35` passes `use_opq=true` when
  generating compressed data, which does not match the canonical `OPQ=false` full-build path.
- `create_disk_layout` subprocess: valid but larger than the already linkable callable.
- Compile selected upstream `.cpp` files separately: rejected because admitted `diskann_s` already
  supplies the fixed source/build identity.

**Consequences**

All Feature targets stay outside `upstream/`. Implementation must add Linux link-smoke tests for
the three exact callable surfaces and record compiler, flags, dependencies, and binary hashes.

**Remaining risk**

An exact Linux build/link probe is required because source-level explicit instantiation does not by
itself prove the final admitted ABI and dependency closure.

## R-004 — PQ preparation and reuse policy

**Additive policy revision 1.1.0**

The decision below remains the frozen 1.0.0 profile. The same implementation ID now offers explicit
1.1.0 with requested B/QD and a single C++ Dataset-dependent derivation. Native Linux uses pinned
`get_memory_budget(double)` directly. Portable policy tests use the exact mapped arithmetic
(`B - 0.25 > 1.0` determines the reserve), and Linux tests compare with the native helper.
Do not call `calculate_num_pq_chunks`: its compression-ratio handling and declaration differ from
the actual `build_disk_index` path mapped in SM-034.

Effective sample probability is min(1,256000/N); chunks use the native budget/N conversion,
lower bound 1, dimension bound and MAX_PQ_CHUNKS=512, then positive QD overrides. The wrapper
rejects non-finite/non-positive B, unsafe conversions, non-integer/negative QD and QD exceeding
dimension/512 rather than passing unsafe values into native code. It introduces no divisibility
restriction and never re-clamps a valid QD to the budget-derived count.

Examples: N=1M/dim=128/B=0.03/QD=0 gives (0.256,32); N=10M gives (0.0256,3);
the same 10M input with QD=32 gives (0.0256,32). These are arithmetic checks, not execution or
Recall evidence. Native random sampling/train/compress and per-Dataset immutable PQ reuse are
unchanged. B/effective_budget_bytes are policy inputs, not peak-RSS estimates or hard memory caps.
The 1.1.0 Manifest records actual inputs and effective outputs; historical 1.0.0 records are
neither rewritten nor reinterpreted. DiskLayout drops its artificial 1M gate without changing
its implementation identity or native semantics. All large-scale execution gates remain.

**Decision**

Generate a single PQArtifact for the admitted Dataset and reuse its exact bytes for all searches
and any later multi-graph comparison. Freeze the first formal PQ inputs to the Feature 002 native
profile projection: `search_DRAM_budget_gib=0.03`, `QD=0`, effective chunks `32`, sample probability
`0.256`, 256 centroids, `OPQ=false`, L2. Do not promise byte-identical regeneration and do not add a
deterministic variant.

**Rationale**

The canonical helper uses uncontrolled random sampling and k-means++ initialization. Experimental
fairness comes from reusing one immutable PQArtifact, not retraining equivalent-looking PQ data for
each graph.

**Source evidence**

- `upstream/diskann-cpp-main/include/pq_common.h:7-11` fixes 256,000 maximum training points,
  256 centers, 12 k-means repetitions, and 512 maximum chunks.
- `src/disk_utils.cpp:1286-1309` derives sample probability and chunks; Feature 002's admitted
  `B=0.03` projection is recorded at
  `specs/002-cpp-main-upstream-baseline/research.md:51-60,523-535`.
- `src/pq.cpp:1094-1132` performs sample/train/compress. Random seeds come from
  `src/partition.cpp:94-141` and `src/math_utils.cpp:380-403` via `std::random_device`.
- Pivot container layout and checks are at `src/pq.cpp:547-558,869-929`; compressed vector format
  is at `:934-948,1037-1059`.

**Alternatives considered**

- Regenerate PQ per graph: rejected because the bytes and search behavior can differ.
- Add a fixed seed: rejected because it would be a distinct deterministic experimental mode.
- Expose all PQ internals as protocol parameters: rejected because they are materialization facts,
  not search-point controls.

**Consequences**

PQArtifact has exactly two payloads: pivots and compressed vectors. Its BuildManifest records full
generation inputs, random/non-deterministic source behavior, and effective chunk/sample facts.
Fresh output confinement prevents accidental reuse of an existing pivots path; reuse occurs only by
admitting the published Artifact.

**Remaining risk**

The first actual generated payload checksum is not known until implementation and a separately
approved run. Phase 0 records the parameter projection, not an invented checksum.

## R-005 — Disk layout format, GlobalNodeId mapping, and entry policy

**Decision**

Feed the validated native GlobalGraph payload directly to `create_disk_layout<float>`. Dataset row
`i`, native graph record `i`, and final disk record `i` are all GlobalNodeId `i`. Use full-precision
float32 disk vectors, `PQ_disk_bytes=0`, `reorder=false`, and no labels. Preserve the GlobalGraph
medoids payload in its original order as `<disk.index>_medoids.bin`. If no GlobalGraph centroid
payload exists, use the pinned medoid-vector fallback and record that effective policy; do not
invent centroids.

**Rationale**

This is the fixed `end_to_end` entry policy. Layout combines existing graph records and Dataset
rows without changing adjacency or node identity.

**Source evidence**

- `src/disk_utils.cpp:850-861,892-925,971-1063` reads Dataset and graph in matching sequential
  order and writes full vector + degree + neighbors into 4096-byte sectors.
- Layout metadata is written at `:948-965,1095-1097` and parsed by
  `src/pq_flash_index.cpp:1029-1086`.
- `PQ_disk_bytes=0` selects the Dataset vector path rather than a disk-PQ path at
  `src/disk_utils.cpp:1142-1155,1331-1335`.
- Empty reorder input leaves the layout flag false at `:863-890,943-965`.
- Loader sidecar names and medoid/centroid logic are at
  `src/pq_flash_index.cpp:788-797,1108-1168`; `use_medoids_data_as_centroids` reads their full
  vectors from the layout at `:492-533`.
- Search chooses the nearest declared centroid and preserves the first medoid on an exact centroid
  tie at `:1360-1373`.

**Alternatives considered**

- Shared/controlled entry: excluded by clarification.
- Promote Partition centroids: rejected because they are not GlobalGraph centroid lineage.
- Convert through an HNSW or rebuilt Vamana representation: rejected because it changes the graph.

**Consequences**

DiskLayoutArtifact includes disk index + medoids; centroids are optional only when actually
lineaged from the input graph. It explicitly records the source GlobalGraph and effective
`global_graph_medoids + source_verified_medoid_vector_fallback` policy.

The payload cardinality is conditional: `declared_centroids` requires the centroid role and an
identity match to a centroid payload already declared by the source GlobalGraph lineage;
`medoid_vector_fallback` forbids the centroid role and derives in-memory entry vectors only through
the pinned medoid IDs plus Dataset rows. It never persists fabricated centroid bytes. Because the
current GlobalGraphArtifact/v1 has no centroid payload role, its admitted path is the pinned
fallback.

**Remaining risk**

The current GlobalGraph family supplies graph and medoids but not centroids. A small loader probe
must confirm the expected fallback using each supported producer identity without changing the
common graph contract.

## R-006 — PQFlashIndex assets, cache protocols, and search controls

**Decision**

Freeze two separate first-phase protocol instances with common controls:

```yaml
K: 100
ordered_L_search: [100, 200, 400]
beamwidth: 2
thread_count: 6
io_limit: 4294967295
warmup_policy: none
query_order: query_row_index_ascending
repetition_count: 3
schedule: repetition_major_then_L_order
entry_policy: end_to_end
```

The zero-cache protocol requests 0 nodes. The fixed-cache protocol requests 10,000 nodes using
the pinned BFS-from-medoids path (`shuffle=false`) and records the effective node count and primary
payload bytes. Cache setup happens once per protocol load, outside every timed point. Each
repetition resets result and QueryStats buffers and gives every L its own batch timer.

**Rationale**

These controls are explicit, source-supported, bounded, and consistent with the fixed Baseline
profile's six threads. Three L values provide the only allowed ordered search-point list; they do
not create a generic sweep.

**Source evidence**

- Required loader paths are `<prefix>_pq_pivots.bin`, `_pq_compressed.bin`, and `_disk.index` at
  `src/pq_flash_index.cpp:761-773`; separate-path load is declared at
  `include/pq_flash_index.h:38-45`.
- `apps/search_disk_index.cpp:200-224` rejects `L<K` and allocates K results.
- The CLI defaults W to 2 and only W=0 triggers tuning at `:210-217,353-360`.
- The CLI parses `search_io_limit` but its calls at `:232-251` use the overload that defaults to
  `UINT32_MAX` in `src/pq_flash_index.cpp:1238-1264`; finite CLI IO control is therefore not
  truthful for this Baseline.
- Cache setup is outside timing at `apps/search_disk_index.cpp:122-130`. BFS policy/cap are at
  `src/pq_flash_index.cpp:350-490`; cache payload allocation is at `:207-252`.
- The upstream SIFT workflow demonstrates 10,000 cached nodes at
  `workflows/SSD_index.md:60`; the source caps any request at 10% of points.
- `WARMUP` is fixed false at `apps/search_disk_index.cpp:30,138-174`.

**Alternatives considered**

- 100,000 cached nodes: valid source cap but rejected as the maximum, not the minimal documented
  SIFT control.
- Finite IO limit: rejected because the pinned CLI does not propagate it.
- Automatic W optimization: explicitly excluded.
- Load/rebuild cache inside each timed batch: rejected because it changes the oracle boundary.

**Consequences**

Only `requested_nodes=10000` is frozen. Admission derives a requested primary-payload projection as
`requested_nodes * (aligned_dimension * scalar_bytes + (declared_max_degree + 1) * 4)` from the
admitted Dataset/DiskLayout and pinned allocation semantics. Runtime repeats the calculation using
the actual effective cache-list size. For 128D float32/R16, 10,000 effective nodes yield
`5,800,000` bytes. This is derived evidence, not a request control, universal constant, allocator/
hash-map overhead, sector padding, or RSS. A zero-cache load still calls the upstream empty-cache
path.

**Remaining risk**

BFS effective count can be below the request if graph traversal exhausts. Observation must record
the actual list size. Formal environment memory and open-file limits remain preflight checks.

## R-007 — Timing, QueryStats, raw observation, and metrics

**Decision**

Persist one immutable `SearchObservation/v1` document per raw search point
`(protocol, L_search, repetition_index)`. Preallocate IDs, distances, and QueryStats. The canonical
batch timer begins immediately before the OpenMP query loop and ends immediately after all queries
return. Conversion, serialization, checksums, manifests, Recall, and reports remain outside.

Compute metrics with pinned semantics:

- QPS = completed query count / canonical batch seconds;
- mean latency = arithmetic mean of `QueryStats.total_us`;
- P99.9 = sorted total_us at zero-based index `floor(0.999 * N)`;
- Mean IOs = arithmetic mean of `QueryStats.n_ios`;
- Recall@10 and Recall@100 are two calls over the same ordered top-100 results.

**Rationale**

Per-point documents are crash-safe, independently immutable, and avoid an appendable multi-point
document. They retain failures without corrupting other repetitions.

**Source evidence**

- Oracle timing is `apps/search_disk_index.cpp:219-256`; persistence/Recall are at `:258-307`.
- Per-query fields are defined in `include/percentile_stats.h:21-36`; mean and percentile are
  `:38-64`.
- `QueryStats.n_ios` increments per uncached DiskANN read request at
  `src/pq_flash_index.cpp:1417-1478`; it is not an OS-sector estimate.
- `QueryStats.total_us` is assigned by the internal query timer after result formation at
  `src/pq_flash_index.cpp:1289-1353,1695-1698`; it is not the batch wall time.
- Recall and exact-distance tie expansion are in `src/utils.cpp:129-165`.

**Alternatives considered**

- One mutable observation accumulating all points: rejected because partial failure complicates
  immutability and failure retention.
- Use summed per-query latency for QPS: rejected because it differs from the oracle batch timer.
- Interpolated percentile: rejected because it differs from the pinned index selection.
- Run K=10 separately: rejected because clarification requires one K=100 observation.

**Consequences**

One protocol with three L values produces nine documents; two cache protocols produce eighteen.
Evaluation references all outcomes and independently derives quality and performance eligibility.
The report labels performance metrics as K=100 execution results even when the quality metric is
Recall@10.

**Remaining risk**

The upstream CLI prints aggregate QueryStats only to limited precision and does not preserve
per-query stats. Per-query mapping parity therefore needs a callable fixture plus source-backed
field tests; it cannot be claimed from stdout alone.

## R-008 — Search Runner Parity

**Decision**

Use the unmodified `search_disk_index` executable as the upstream-faithful oracle and a separate
instrumented callable runner for structured observations. Gate formal performance reports on:

- exact ordered result IDs;
- distances satisfying `abs <= 1e-5 OR rel <= 1e-6`;
- identical Recall@10 and Recall@100 under pinned `calculate_recall`;
- identical Mean IOs under identical controls (with CLI displayed-value precision accounted for);
- exact formula and timing-boundary mapping for QPS and latency;
- proof that observation persistence is outside timing.

Independent wall-clock QPS/latency values need not be numerically equal.

**Rationale**

The instrumented runner must prove search/result/statistic semantics without requiring physical
time determinism or modifying upstream.

**Source evidence**

- Oracle result bins are written after timing at `apps/search_disk_index.cpp:296-307`.
- Search results are sorted by `(distance,id)` through `include/neighbor.h:26-29` and
  `src/pq_flash_index.cpp:1613-1689`.
- Oracle QPS/latency/IO/Recall formulas are at `apps/search_disk_index.cpp:254-289`.

**Alternatives considered**

- Compare only Recall: rejected because IDs, distances, IO semantics, and timing boundaries are
  part of parity.
- Require exact wall-clock values: rejected because independent runtime scheduling is physical,
  not semantic.
- Treat Search Runner Parity as Feature 002 graph parity: prohibited and semantically false.

**Consequences**

Before this gate passes, instrumented observations cannot support the formal performance claims
already governed by this parity gate. Its result/Recall/QueryStats/formula/timing scope and the
distinct oracle/instrumented identities remain unchanged.

**Remaining risk**

Distance tolerance is an implementation probe threshold, not permission to change result order.
Any ordered-ID difference fails parity even if Recall matches.

## R-009 — QuerySet conversion and immutable identity

**Decision**

The only formal QuerySet source is the existing CSV. Normalize it once to a native little-endian
DiskANN float32 matrix containing only `v0..v127` in CSV row order, while retaining an immutable
row mapping of `query_row_index` to the original opaque `source_query_id` lexeme. Treat conversion
as a source-input preparation operation with its own BuildManifest and ValidationEvidence, not as
a public Artifact.

Observed source identity:

```yaml
logical_source: sift1M_dataset/selected_query.csv
format: text/csv; header=id,v0..v127
byte_length: 2905792
sha256: 0f9c1c0db3207d681694658771422f48de1a7a84e7945681d0999a04f45a52ec
query_count: 4885
dimension: 128
native_expected_byte_length: 2501128
```

**Rationale**

PQFlashIndex consumes native vectors, but clarification requires the CSV ID to remain opaque and
row alignment to use the zero-based data-row index.

**Source evidence**

- Direct inspection found 129 columns, 4,885 finite rows, and unique increasing ID lexemes spanning
  0.0..9999.0 with gaps; they are not row ordinals.
- `apps/search_disk_index.cpp:70-75` loads native matrix format.
- Dataset source identity is 1,000,000 x 128 float32, 512,000,008 bytes, SHA-256
  `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`.

**Alternatives considered**

- Use standard public SIFT queries: excluded by clarification.
- Encode the CSV ID in the native vector file or interpret it as GlobalNodeId: excluded by
  clarification.
- Promote QuerySet to a public Artifact: deferred until there is a second independent consumer.

**Consequences**

QuerySetReference binds both source and normalized payload checksums, conversion identity,
query count/dimension, and the opaque-ID mapping. Any source/native/mapping mismatch fails before
search.

**Remaining risk**

The normalized payload checksum is intentionally unknown until the planned converter is
implemented and run; the plan must not invent it.

## R-010 — Exact GroundTruth and tie closure

**Decision**

Use the pinned `compute_groundtruth` L2 path over the admitted Dataset and normalized formal
QuerySet, with no tags and no self exclusion. Generate width 200, then require for every query that
the stored rank-200 distance differs from the rank-100 boundary distance. This verifies that the
equal-distance group relevant to Recall@100 is not truncated. If the closure check fails, reject
the GroundTruth and revise the declared width before any search evaluation; never use approximate
truth.

**Rationale**

Pinned Recall expands equal-distance GroundTruth entries beyond the requested cutoff only when
they are present in the truth file. Width 200 plus an explicit closure check is a bounded,
source-faithful way to protect Recall@100 ties without changing K=100 search.

**Source evidence**

- Exact L2 uses full base/query block computation and MKL SGEMM at
  `apps/utils/compute_groundtruth.cpp:73-235,337-438`.
- The path performs no ID-based self exclusion and writes uint32 IDs followed by float distances
  at `:319-334`.
- Native loader/file-size semantics are at `include/utils.h:483-529`.
- Recall expands equality at the boundary with exact float `==` at `src/utils.cpp:141-152`.

**Alternatives considered**

- Width 100 only: valid basic input but cannot prove no equal-distance neighbor exists just beyond
  rank 100.
- Approximate GroundTruth: rejected.
- Interpret `source_query_id` as Dataset ID and remove self: excluded by clarification.

**Consequences**

GroundTruthReference records width 200, exact-L2 generator identity/source, Dataset and QuerySet
checksums, and `self_match_policy=no_id_based_exclusion`. Evaluation takes the first 10 or 100
search results while passing full GroundTruth distances to pinned Recall semantics.

**Remaining risk**

The exact run evaluates roughly 4.885 billion vector distances. The source uses a 512-query distance
matrix of about 1.91 GiB plus the 488.3 MiB Dataset and other allocations; generation requires a
separate resource gate and is not performed by Plan.

## R-011 — SearchProtocol canonicalization and eligibility

**Decision**

Define SearchProtocol/v1 as strict canonical JSON with:

- a whole-document identity/checksum;
- a `search_execution` projection and independent canonical checksum;
- an `evaluation` projection excluded from the search checksum.

Use JSON Canonicalization Scheme-style sorted-key UTF-8 serialization with integers preserved as
integers and no non-finite values. Create two frozen instances (zero cache and fixed cache) with the
R-006 controls. GroundTruth, evaluator version, exclusion, and median aggregation live only in
`evaluation`.

Quality eligibility requires exact Dataset/QuerySet/GroundTruth/SearchProtocol/evaluator
compatibility, terminal success, 4,885/4,885 completed queries, exactly 100 legal ordered
IDs/distances per query, K/L/result semantics, and a complete valid SearchObservation. It does not
require a normative performance environment. Performance eligibility independently requires a
complete valid observation, exact execution/protocol/search controls, canonical timing semantics,
the existing Search Runner Parity gate, and admitted Linux/local-SSD/O_DIRECT/libaio evidence.
Failed, invalid, cancelled, or identity-mismatched outcomes fail both; a non-normative but otherwise
valid point may remain quality-eligible. Its derived QPS/latency/IO may be retained as
`informational_non_formal`, but are not required and cannot enter a performance aggregate; the
specific exclusion remains in `performance_eligibility_reason`.
Each metric family uses the sorted median of its own eligible repetitions; with two values the
median is their arithmetic mean, and zero eligible values produce no aggregate.

**Rationale**

This cleanly allows evaluation-only changes to reuse raw observations, prevents the environment
gate from suppressing valid Recall, and fixes both exclusion projections before seeing results.

**Source evidence**

- Existing canonical JSON/checksum conventions are in
  `src/validation/canonical_json.cpp:55-105` and can be reused as a project convention without
  making SearchProtocol an Artifact.
- Clarification fixes K, entry, repetitions, and median; R-006 fixes remaining technical controls.

**Alternatives considered**

- Include GroundTruth in search identity: rejected because it is not a search input.
- Use a generic sweep/optimizer: explicitly excluded.
- Add replacement repetitions: excluded by clarification.

**Consequences**

Changing GroundTruth/evaluator/aggregation produces a new protocol/report but may reuse complete
observations when the `search_execution` checksum is unchanged. Changing any search control,
including repetition count, requires a new execution identity and actual rerun.

**Remaining risk**

Hardware-specific storage placement cannot be finalized as a pathname in this design document; a
formal protocol instance must bind the admitted local-SSD location before execution.

## R-012 — SearchEvaluationReport contract choice

**Decision**

Use Feature-local `SearchEvaluationReport/v1`. Do not reuse or version-bump the current public
graph-integrity `EvaluationReport/v1` and do not create a public report family or registry.

**Rationale**

The existing report has one evaluated graph Artifact and fixed graph-integrity findings. It cannot
represent protocol/search checksum, PQ/DiskLayout, Query/GroundTruth, raw repetitions, eligibility,
or the required metrics without changing its meaning.

**Source evidence**

- `include/diskann_pipeline/evaluation.hpp:11-33` defines graph findings.
- `contracts/v1/schemas/evaluation-report.schema.json:32-64` requires those graph findings.
- `src/pipeline/evaluator.cpp:39-159` accepts the existing fake/native graph paths only.

**Alternatives considered**

- Put search metrics in `metrics` on a graph report: rejected because required search lineage is
  missing and graph validation is not search evaluation.
- Public EvaluationReport v2: rejected because only this Feature consumes the new report today.

**Consequences**

The Feature-local report schema is strict and versioned, persisted immutably under the Feature run
report area, and referenced by path/checksum. Artifact, BuildManifest, ValidationEvidence,
SearchObservation, and SearchEvaluationReport remain distinct.

**Remaining risk**

If a future second search evaluator needs the report, promotion must be a separate public-contract
decision rather than silently treating this schema as generic.

## R-013 — BuildManifest semantics for non-Pipeline operations

**Decision**

Use existing BuildManifest/v1 with `stage="build"` for query normalization, GroundTruth generation,
PQ generation, and DiskLayout materialization. Add the backward-compatible document-stage enum
value `search` for an instrumented/oracle search invocation. This is a BuildManifest document
classification only; it does not add `StageKind`, Registry factories, or a Pipeline stage.
Evaluation uses existing `stage="evaluate"`.

**Rationale**

Search failures must retain a BuildManifest, and calling them build/evaluate/merge would be
misleading. The C++ `BuildManifest.stage` is already a string; only the JSON schema enum needs the
additive value.

**Source evidence**

- `include/diskann_pipeline/provenance.hpp:57-75` stores an unconstrained stage string and complete
  provenance.
- `contracts/v1/schemas/build-manifest.schema.json:23-28` currently admits build, three graph
  stages, validate, and evaluate.
- Pipeline StageKind remains independently restricted in
  `include/diskann_pipeline/identifiers.hpp:30-34`.

**Alternatives considered**

- Label search as Merge/evaluate/build: rejected as semantically false.
- Add a Search StageKind: prohibited.
- Add a new SearchManifest family: rejected as unnecessary.

**Consequences**

This additive v1 schema amendment keeps every existing document valid. Schema source inventory and
contract regressions must cover it. The full Feature 005
`contracts/build-manifest.schema.json` becomes the single source authority for the runtime
BuildManifest target and differs only by adding `search`; unrelated targets retain their original
single Feature 001 sources. Search observations remain separate outputs, not embedded
BuildManifest metrics.

Every PQ generation, DiskLayout materialization, QuerySet conversion, exact GroundTruth generation,
instrumented search, offline evaluation, and upstream-oracle invocation emits its own existing
BuildManifest/v1. Each manifest records implementation identity/version, pinned repository/branch/
full commit/dirty state, toolchain and build flags, Dataset and every operation input identity and
checksum, full parameters, resources, timestamps, terminal status, output references, and failure
reason when unsuccessful. Search/oracle invocations use `stage="search"`; generation/conversion use
`build`, and offline evaluation uses `evaluate`. Failed, invalid, and cancelled invocations retain
terminal manifests. Payloads, ValidationEvidence, SearchObservation, and SearchEvaluationReport
remain independent, and Recall/QPS/latency/IO never enter BuildManifest.

**Remaining risk**

Any consumer that hard-codes the old enum must be found by schema/codec regressions; no public C++
enum change is required.

## R-014 — Linux AIO and formal performance eligibility

**Decision**

Normative performance requires a real Linux host, a declared local SSD block device, a filesystem
and mount supporting aligned O_DIRECT reads, functioning libaio, and the pinned Linux reader path.
Preflight records kernel, CPU, RAM, filesystem/mount, block device/model/rotational flag, AIO limit,
compiler/build identity, runner hashes, and storage placement, then performs aligned O_DIRECT and
`io_setup/io_submit/io_getevents` probes on the actual index filesystem.

A container is normative only on a real Linux host when its bind mount maps to that local SSD and
the probes pass inside the exact image. macOS, Docker Desktop/macOS, remote/network storage, or an
unproved device remains `non_normative`. There is no buffered fallback for formal claims.

**Rationale**

QPS, latency, and IO conclusions depend on the fixed DiskANN Linux I/O path, not merely successful
functional search.

**Source evidence**

- `include/linux_aligned_file_reader.h:9-36` is the Linux PQFlash reader.
- `src/linux_aligned_file_reader.cpp:197-225` opens with
  `O_DIRECT|O_RDONLY|O_LARGEFILE` and issues blocking batches through Linux AIO.
- `:18-90,136-163` calls `io_setup`, `io_submit`, and `io_getevents`.
- `include/aligned_file_reader.h:73-90` requires 512-byte aligned requests; disk sectors are 4096
  bytes in `include/defaults.h:22-25`.
- Upstream links `aio` in root `CMakeLists.txt:262-264`.

**Alternatives considered**

- Docker-on-macOS as formal: rejected because the backing I/O path is not the pinned local-SSD
  path.
- Fall back to buffered I/O and label results formal: rejected.

**Consequences**

Non-normative observations may produce formal Recall when quality eligibility passes, and may prove
correctness/parity formulas, but cannot produce formal QPS, latency, or Mean IO claims. Performance
environment admission is evaluated by the report/evaluator separately from raw observation
structural completeness and GroundTruth-based quality eligibility. SearchObservation retains the
environment fingerprint and raw facts, not either derived eligibility decision.

**Remaining risk**

No current host has been admitted by this Plan. Formal environment and storage paths remain an
execution gate, not a claimed fact.

## R-015 — Failure, non-reexecution, and current Feature 002 gate

**Decision**

Use existing terminal statuses, immutable manifests/evidence/logs, and quarantine for failed or
invalid materialization. Retain failed/invalid/cancelled SearchObservation candidates but never
evaluate them. Prove no P/B/M reexecution using fixed runner control flow, absence of graph-stage
factories/registration, unchanged input Artifact checksums, no P/B/M output directories, and
existing invocation evidence where present; do not add production counters.

Record Feature 002's current gate exactly:

```yaml
graph_structural_parity_gate: NOT_REACHED
framework_graph_composition:
  identity: cpp_main_p0_b0_m0_framework_candidate_v1
  state: candidate
```

This does not block format/fixture/non-normative search/runner-parity work. Search Runner Parity is
not Graph Parity and cannot certify the candidate Framework Baseline.

**Rationale**

The Feature is downstream-only and must preserve failed evidence without inventing a lifecycle or
certification subsystem.

**Source evidence**

- Existing statuses/failure objects are in `include/diskann_pipeline/identifiers.hpp:36-44` and
  `include/diskann_pipeline/provenance.hpp:40-75`.
- Feature 002 policy/report are
  `specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.json` and
  `specs/002-cpp-main-upstream-baseline/structural-parity-report.json`; the report records
  `NOT_REACHED` because complete immutable native merged-graph evidence is absent.

**Alternatives considered**

- Add a certification/claim-eligibility subsystem: explicitly excluded.
- Use Search Runner Parity to mark the Graph gate PASS: rejected because it tests a different
  boundary.
- Add P/B/M instrumentation solely for negative proof: rejected as unnecessary.

**Consequences**

Every report binds the exact evaluated GlobalGraph Artifact and its candidate/certification
context. Final Baseline certification remains deferred until independent graph structural, search
semantics, and quality evidence close their own scopes.

**Remaining risk**

Formal performance claims remain blocked by both a normative Linux environment and the existing
Search Runner Parity gate; infrastructure completion alone removes neither blocker. Compatible
Recall quality evidence is governed by its separate eligibility projection.

## Source-backed plan constants

| Item | Phase 1 value | Evidence class |
|---|---:|---|
| Dataset | 1,000,000 x 128 float32 L2; SHA-256 `8c7b...658e` | file + existing contract |
| QuerySet | 4,885 x 128 CSV; SHA-256 `0f9c...52ec` | file inspection |
| GroundTruth width | 200 plus rank-100 tie-closure check | source-derived technical policy |
| PQ chunks | 32 (`B=0.03`, QD=0) | Feature 002 + source mapping |
| disk PQ | 0 | spec lock |
| reorder/filter/range | false/false/unsupported | spec lock |
| K | 100 | clarification lock |
| L | `[100, 200, 400]` | planned bounded search list |
| W | 2 | spec/source default |
| threads | 6 | fixed Baseline profile |
| IO limit | 4,294,967,295 | truthful pinned CLI behavior |
| warmup | none | pinned `WARMUP=false` |
| repetitions | 3 | clarification lock |
| fixed cache | requested 10,000 BFS nodes | upstream SIFT workflow |
| distance parity | exact IDs; `atol=1e-5`, `rtol=1e-6` | implementation-probe threshold |

No item in this table authorizes a formal SIFT1M run.

### Optional native n-hops observation and explicit evaluation

At fixed commit `78256bbab4685e1774e78d331e081a153be26823`,
`src/pq_flash_index.cpp:1454` increments `QueryStats.n_hops` once when the
non-cached frontier is nonempty and disk reads are issued. It counts such search
expansion rounds, not shortest-path length, visited nodes or I/O requests.
The production runner copies the actual counter; it never derives it from
`n_ios` or beamwidth. SearchObservation/v1 already permits this optional integer.
JSON persistence remains after batch timing. SearchOne's signature is unchanged;
its auxiliary capture does not collect this counter and omits it rather than
writing zero or null.

Protocol 1.0.0 / report v1 remain frozen. Protocol 1.1.0 accepts exactly the
original six metrics, or those same six in order followed by
`mean_n_hops_k100`. Only the latter selection emits the optional nonnegative v2
performance metric, even if a six-metric run's raw observation contains counters.
All queries must provide valid counters for the arithmetic mean; a genuine zero
is included. Missing any query makes only n-hops unavailable, explained via the
existing performance reason. Invalid present fields follow ordinary contract
rejection. The existing performance aggregation member set is unchanged; all
members must have complete means for their median to be emitted. Otherwise the
optional value is omitted with a scope limitation, never computed on a smaller
subset. The four original performance metrics are aggregated independently by
explicit keys. Recall, QPS, latency, I/O, Parity and certification rules are
unchanged. Historical files are neither rewritten nor backfilled; new capture
requires a newly identified runner/search invocation, while validated graph,
PQ and Layout inputs may be reused.
