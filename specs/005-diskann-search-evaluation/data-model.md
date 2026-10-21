# Data Model: DiskANN Post-GlobalGraph Search Evaluation

## Explicit evaluation revision 1.1 and SearchEvaluationReport/v2

The v1 descriptions below remain applicable to Protocol 1.0.0/report v1. Revision 1.1.0 changes
only evaluation policy: evaluator `cpp_main_search_metrics_evaluator_v2`, version `2.0.0`, and
`performance_eligibility_policy=complete_valid_measurement_single_invocation_v2`. Mixed version,
policy and evaluator identities are invalid. SearchObservation/v1, public Artifacts, canonical
hash algorithms and the complete search_execution projection remain unchanged.

Report v2 has its own strict `search-evaluation-report-v2.schema.json` and preserves the quality
fields/median semantics. Each retained observation includes its `original_protocol` reference and
parity applicability. `performance_valid` and `performance_validity_reason` replace the v1
eligibility terminology; valid measurements require `performance_metrics` and their own
`environment` path/SHA. Invalid points remain audit-only. No formal/informational classification
is assigned to ordinary v2 measurements.

`aggregates.performance` uses the same median calculation but includes `scope`: search Manifest
path/SHA, execution checksum, run_id, exact member observation paths/SHAs, environment references
and limitations. `performance_aggregation_reason` explains omitted aggregates. Manifest admission
checks the original protocol, full execution, runner identity, thread budget, input Artifacts and
actual observation-reference bytes. Missing, unreadable, malformed or unbound search Manifests
leave invocation unknown: per-point measurements remain, but performance aggregates are withheld
with a reason. An unproven run_id never grants membership. Recorded environment
facts are compared for conflicts; absent hardware/storage/filesystem/container/resource/load data
are listed as unknown. Identical OS/file hashes alone never certify comparable machines.

Parity retains its original result/evidence reference and adds an assessment: NOT_VERIFIED, PASS,
ERROR, UNRESOLVED, or a mixed top-level assessment. Evidence is interpreted per point. Structured
oracle process failure (FAILED Manifest, process_failed, nonzero exit, no observation comparison),
missing oracle outputs, or retained successful instrumented search with zero comparison points
remain incomplete verification, independent of error text. A sparse record never establishes PASS.
Recall parity is explicitly `original_ground_truth_only`. Unreadable/unparseable reports or invalid
result values use `result=INVALID_EVIDENCE`; otherwise the readable original result and available
evidence reference are retained. Unusable auxiliary evidence, including missing logs, bad checksums
and foreign execution bindings, has assessment UNRESOLVED and a specific reason, not PASS or proof
of measurement error. It withholds affected performance groups, or all groups if scope is unknown,
without discarding otherwise valid per-point measurements. Confirmed semantic-error points remain
excluded and cannot be reinstated by a later auxiliary-evidence failure.
An overall FAIL with all point assessments PASS remains unresolved. Missing points in a FAIL
record are also unresolved unless the structured preparation-failure case above applies;
their performance groups are withheld without discarding valid per-point measurements.

`evaluator_provenance` records current project repository/branch/commit/dirty state, dirty-source
checksum when applicable, and hashes of evaluator/support Python sources and Feature schemas.
The independent evaluation Manifest repeats this provenance in parameters and uses its source
as the Manifest source. `search_runner_build` remains a separate input parameter. Report v1 cannot
carry the new fields; its newly generated evaluation Manifest supplies current evaluator provenance.

The fixed v2 Python implementation requires complete SearchObservation/v1, a compatible frozen
Protocol 1.1.0 and GroundTruth, existing source/evidence readers and immutable publication services;
it provides SearchEvaluationReport/v2 with global-ID Recall and scoped measured performance.
It uses the same calculation and base admission functions as v1 and introduces no Registry or
public Artifact capability. Consumers dispatch on report_version and reject unknown versions.

## 1. Model boundary

This Feature adds two reusable public data types and three Feature-local document types. It reuses
existing graph-pipeline provenance and validation objects without redefining them.

```text
DatasetArtifact ────────────────┐
        │                       │
        ├─> PQArtifact          │
        │                       ├─> DiskLayoutArtifact
GlobalGraphArtifact ────────────┘

PQArtifact + DiskLayoutArtifact + QuerySetReference + SearchProtocol.search_execution
        └─> SearchObservation/v1 (one immutable document per raw search point)

SearchObservation/v1[] + GroundTruthReference + SearchProtocol.evaluation
        └─> SearchEvaluationReport/v1
```

The model does not add a Pipeline stage, Search registry, backend hierarchy, retry engine, or
comparison framework.

## 2. Existing reused objects

### 2.1 ArtifactRef and SourceArtifactIdentity

Use the existing public contract without field changes. Identity equality always compares all of:

- `artifact_type`;
- `format_version`;
- `aggregate_checksum`;
- and, for an ArtifactRef, its confined metadata path.

The path locates the object; type/version/checksum prove the immutable data identity. This Feature
does not create a second identity model.

### 2.2 DatasetArtifact

The admitted input remains `diskann-float32-dataset/v1`, `id_space=none`, float32/L2. Feature use
adds no fields. Every materialization preflight fresh-validates the metadata, native header/length,
finite values, payload checksum, aggregate checksum, and complete Dataset identity.

For the first formal Dataset:

```yaml
global_node_count: 1000000
vector_dimension: 128
scalar_type: float32
distance_metric: l2
payload_byte_length: 512000008
dataset_checksum: 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e
```

### 2.3 GlobalGraphArtifact

The input remains `global-graph-artifact/v1`, `id_space=global`. No field, format version, canonical
checksum, or validity rule is redefined. Admission fresh-validates its complete Partition and
LocalGraph lineage before running the narrow pinned post-graph compatibility check.

The Artifact is immutable. Materialization may read its native graph and medoids/centroids but may
not repair, prune, relabel, or write it.

### 2.4 BuildManifest

Use existing BuildManifest/v1 as an independent generation/execution document.

- Query normalization, exact GroundTruth generation, PQ generation, and DiskLayout materialization
  use document `stage=build`.
- Search execution uses the additive document classification `stage=search`.
- Evaluation uses `stage=evaluate`.

The additive `search` string does not create a Pipeline `StageKind`. Manifests contain inputs,
outputs/references, implementation/source/toolchain identity, complete parameters, resources,
timestamps, status, and failures. They never contain PQ/DiskLayout payload bytes or search metrics.

### 2.5 ValidationEvidence

Use existing ValidationEvidence/v1 independently for Dataset, GlobalGraph, PQArtifact, and
DiskLayoutArtifact. QuerySet/GroundTruth conversion validation is also persisted with the existing
evidence shape, using the preparation operation's candidate identity and checks. A previous
evidence file never replaces fresh downstream admission.

## 3. Public immutable data

### 3.1 PQArtifact/v1

**Purpose**: reusable Dataset-derived in-memory PQ search assets.

**Identity**

```yaml
artifact_type: pq
format_version: pq-artifact/v1
id_space: none
```

The aggregate checksum uses the existing Artifact canonicalization projection. The Artifact has
exactly one DatasetArtifact source identity and no GlobalGraph source.

**Structural fields**

| Field | Rule |
|---|---|
| `point_count` | equals Dataset global node count |
| `dimension` | equals Dataset vector dimension |
| `scalar_type` | `float32` |
| `distance_metric` | `l2` |
| `centroid_count` | 256 |
| `pq_chunks` | positive, <= min(dimension, 512); first protocol uses 32 |
| `opq` | false |
| `compressed_code_scalar` | `uint8` |

**Payloads**

1. `pq_pivots`: native DiskANN pivot container, checksum/length bound;
2. `pq_compressed_vectors`: native uint8 matrix with point count and chunk count, checksum/length
   bound.

**Capabilities**

- `search.pq_in_memory/v1`;
- `search.pqflash_loadable/v1`.

**Required provenance**

The separate BuildManifest binds exact Dataset Artifact, DiskANN source, wrapper identity,
compiler/build flags, B/QD/chunk/sample/centroid/OPQ/metric settings, resources, output refs,
random-source behavior, and terminal status. Randomness is not an Artifact field.

**Validation**

- common envelope, payload paths/bytes/SHA-256, sources, and aggregate checksum;
- exact one Dataset source and matching Dataset identity;
- pivot container offsets, 256 x dimension centroids, dimension centroid, chunk offsets;
- compressed header point count/chunks and exact payload length;
- compressed point count/dimension/chunks agree with Dataset and metadata;
- absence of an OPQ rotation payload when `opq=false`.

**Reuse**

Reusable only after VALID evidence. Formal multi-graph comparison must reuse the exact ArtifactRef
and payload checksums, not independently regenerate a statistically equivalent PQ.

**Failure**

Failed/invalid candidates retain manifest, logs, and evidence in quarantine and are not published.

### 3.2 DiskLayoutArtifact/v1

**Purpose**: one Dataset + one GlobalGraph transformed into the native full-precision disk-search
layout consumed by PQFlashIndex.

**Identity**

```yaml
artifact_type: disk_layout
format_version: disk-layout-artifact/v1
id_space: global
```

Ordered sources are exactly:

1. DatasetArtifact identity;
2. GlobalGraphArtifact identity.

**Structural fields**

| Field | Rule |
|---|---|
| `global_node_count` | equals Dataset and GlobalGraph count |
| `dimension` | equals Dataset dimension |
| `scalar_type` | `float32` |
| `distance_metric` | `l2` |
| `disk_vector_encoding` | `float32_full_precision` |
| `pq_disk_bytes` | 0 |
| `reorder` | false |
| `declared_max_degree` | equals native layout width and GlobalGraph declaration |
| `medoid_count` | positive and equals the retained sidecar |
| `entry_policy` | `end_to_end` |
| `centroid_policy` | `declared_centroids` or `medoid_vector_fallback` |
| `frozen_point_count` | 0 |

**Payloads**

1. `disk_index`: native 4096-byte-sector layout;
2. `medoids`: exact ordered medoids bound to the input GlobalGraph;
3. optional `centroids`: present only when supplied by traceable GlobalGraph lineage.

`centroid_policy=declared_centroids` requires payload 3, and that payload must be the centroid
payload already declared by the exact source GlobalGraph lineage. `medoid_vector_fallback` forbids
payload 3 and uses only the pinned loader's traceable medoid IDs plus Dataset vectors. The
materializer never synthesizes, copies into existence, or persists a pseudo-centroid payload. With
the current GlobalGraphArtifact/v1 contract, which has no centroid role, only the pinned
medoid-vector fallback can be admitted.

No disk-PQ pivot sidecar is permitted. Query sample/warmup files, staging memory graph paths,
stdout, and logs are not Artifact payloads.

**Capabilities**

- `search.disk_layout/v1`;
- `search.pqflash_loadable/v1`;
- `search.disk_vectors_full_precision/v1`;
- `graph.global_ids/v1`;
- `search.entry_end_to_end/v1`.

**Required provenance**

The separate BuildManifest binds exact Dataset/GlobalGraph ArtifactRefs, source/build identity,
full-precision/no-disk-PQ/no-reorder policy, exact native paths, effective medoid/centroid fallback,
resources, and output refs.

**Validation**

- common envelope, exact ordered sources, Dataset identity, payload checksums, aggregate checksum;
- native header count/dimension/degree/file-size/sector fields;
- point count agreement with Dataset, GlobalGraph, and a validated PQArtifact load probe;
- frozen count 0, reorder flag false, no disk-PQ sidecar;
- every record has positive degree <= declared width and legal GlobalNodeIds;
- disk record `i` corresponds to GlobalNodeId and Dataset row `i`;
- medoid count/order/range and centroid-policy/payload cardinality; declared centroids must resolve
  to the source GlobalGraph payload identity, while fallback must have no centroid payload;
- `PQFlashIndex::load_from_separate_paths` succeeds with a compatible PQArtifact.

**Reuse**

Reusable only with the exact Dataset/GlobalGraph sources and VALID evidence. A layout is bound to
one and only one GlobalGraph identity.

**Failure**

Any input incompatibility fails before the callable. Failed/invalid output stays quarantined and
does not become a normal reusable layout.

## 4. Feature-local immutable input references

### 4.1 QuerySetReference/v1

**Purpose**: bind the authoritative CSV and its lossless row-ordered native search projection.

**Fields**

```yaml
reference_version: 1.0.0
query_set_id: <stable logical id>
logical_source: sift1M_dataset/selected_query.csv
source:
  path: sift1M_dataset/selected_query.csv
  format: text/csv;header=id,v0..v127
  byte_length: 2905792
  sha256: 0f9c1c0db3207d681694658771422f48de1a7a84e7945681d0999a04f45a52ec
query_count: 4885
dimension: 128
scalar_type: float32
query_identity: query_row_index
source_query_id_semantics: opaque
self_match_policy: no_id_based_exclusion
native_payload:
  path: <confined relative path>
  format: application/vnd.diskann.float32-matrix;version=1
  byte_length: 2501128
  sha256: <actual conversion checksum>
row_mapping:
  path: <confined relative path>
  format: application/vnd.diskann.query-row-map+jsonl;version=1
  byte_length: <actual>
  sha256: <actual mapping checksum>
conversion_manifest_path: <relative path>
validation_evidence_path: <relative path>
canonical_checksum: <full reference checksum>
```

`query_row_index` is the zero-based row after the header and is the only canonical query/alignment
identity. The mapping stores the exact CSV `id` lexeme as `source_query_id`; it is not a row ID,
Dataset row, or GlobalNodeId and never triggers self exclusion.

**Validation/reuse**

Source and native bytes, headers, row mapping, counts, dimensions, finite float conversion, row
order, and canonical checksum must match. Any changed CSV or native projection is a new QuerySet
identity and search execution identity.

### 4.2 GroundTruthReference/v1

**Purpose**: bind exact L2 truth used only in evaluation.

**Fields**

```yaml
reference_version: 1.0.0
ground_truth_id: <stable logical id>
payload:
  path: <confined relative path>
  format: application/vnd.diskann.truthset+binary;version=1
  byte_length: <actual>
  sha256: <actual>
query_count: 4885
width: 200
id_space: global
distance_metric: l2_squared_float32
dataset_artifact: <exact ArtifactRef identity>
query_set_checksum: <exact QuerySet canonical checksum>
self_match_policy: no_id_based_exclusion
rank_100_tie_group_closed: true
generation_manifest_path: <relative path>
validation_evidence_path: <relative path>
canonical_checksum: <full reference checksum>
```

**Validation/reuse**

The native file is header + `query_count*width` uint32 IDs + the same number of float distances.
IDs are legal GlobalNodeIds, rows are nondecreasing by distance, count/dimension/checksum match, and
every rank-100 boundary tie is closed before the final stored rank. GroundTruth changes create a new
evaluation identity but do not invalidate or force rerun of a compatible SearchObservation.

### 4.3 Explicit GroundTruthReference 1.1.0 revision

The 1.0.0 definition above retains its original interpretation. A request must explicitly
select `reference_version: "1.1.0"` to publish the revised reference. Its structure is the
same except `rank_100_tie_group_closed` is optional. Omission means no verified closure is
claimed; explicit null/false are invalid. True requires an actual stored-boundary check.
Content validation (header/length, finite ordered distances, ID range, checksum and bindings)
is independent of closure. It does not prove that imported neighbors are exact.

Native file import uses the existing QuerySetReference, GroundTruthReference, BuildManifest
and ValidationEvidence structures. A reference's historical `conversion_manifest_path` or
`generation_manifest_path` names the actual process record, which distinguishes import from
execution of a generator. Import preserves binary bytes and query row order; synthesized
query IDs are explicitly opaque row labels, never Dataset node IDs.

SearchProtocol 1.0.0 permits only GroundTruthReference 1.0.0. Protocol 1.1.0 / evaluator v2
permits either reference revision. New references and protocols receive new canonical
checksums; historical references, reports and frozen certification inputs are not rewritten.
An omitted closure claim adds a GT-width/boundary limitation to the existing quality reason,
without changing Recall, performance formulas, eligibility or median aggregation scope.

## 5. SearchProtocol/v1

SearchProtocol is a Feature-local versioned immutable config, not an Artifact.

### 5.1 Whole-document fields

```yaml
protocol_version: 1.0.0
protocol_id: <stable id>
status: frozen
search_execution: <object>
search_execution_checksum: <sha256 of canonical projection>
evaluation: <object>
canonicalization: diskann-search-protocol-c14n-v1
protocol_checksum: <sha256 of complete document without this field>
```

### 5.2 search_execution

Required fields:

- exact DatasetArtifact, PQArtifact, DiskLayoutArtifact, and QuerySetReference identities;
- DiskLayout's exact GlobalGraph identity;
- `distance_metric=l2`, `K=100`;
- ordered `L_search=[100,200,400]`, each >= 100;
- `beamwidth=2`, `thread_count=6`, `io_limit=4294967295`;
- one cache object: either zero `{mode: zero, requested_nodes: 0}` or fixed
  `{mode: fixed_bfs, requested_nodes: 10000, shuffle: false}`. `requested_nodes` is the only cache
  budget control;
- `warmup_policy=none`, `query_order=query_row_index_ascending`;
- `entry_policy=end_to_end`;
- canonical timing boundary identity;
- local-SSD storage placement declaration;
- `repetition_count=3`, `schedule=repetition_major_then_L_order`;
- required Linux/O_DIRECT/libaio environment constraints;
- fixed instrumented implementation and pinned DiskANN source identities;
- admitted project runner repository/branch/full commit/clean state, binary path/SHA-256, and its
  independent BuildManifest path/SHA-256.
- optional PQArtifact/DiskLayoutArtifact admission-evidence references. Each reference contains the
  workspace-relative fresh ValidationEvidence path, evidence-file SHA-256, and exact expected
  ArtifactRef. Non-fixture formal-search preflight requires both; legacy and evidence-free fixture
  Protocols omit the object and retain full validation at search startup.

GroundTruth is forbidden from this projection. Zero/fixed cache are two protocol documents, not one
protocol with a changing cache mode.

Admission derives the requested primary-payload projection from requested nodes, pinned aligned
dimension/scalar-width semantics, and the admitted DiskLayout's declared maximum degree. Runtime
recomputes the effective bytes from the actual cached-node count. The BuildManifest/admission
evidence records the requested projection; RawSearchPoint cache evidence records effective nodes
and primary bytes. Neither value is an independent request control. For the fixed
128D/float32/R16 configuration, 10,000 effective nodes imply 5,800,000 primary bytes; that example
does not include allocator/hash-map overhead or RSS and is not a universal schema constant.

### 5.3 evaluation

Required fields:

- GroundTruthReference identity/checksum;
- evaluator identity/version;
- metrics `[recall_at_10, recall_at_100, qps_k100, mean_latency_us_k100,
  p99_9_latency_us_k100, mean_ios_k100]`;
- pinned Recall/mean/percentile semantic versions;
- separate quality-eligibility and performance-eligibility rules;
- aggregation `median_eligible`;
- `no_replacement_repetitions=true`.

### 5.4 Checksums and changes

The `search_execution_checksum` hashes the entire canonical `search_execution` object, including
the optional admission-evidence references when present. The protocol checksum hashes both
sections. Evaluation-only changes can reuse observations when the execution identity/checksum stays
the same. Search input/control/repetition or bound admission-evidence changes require a new
execution identity and actual new search.

## 6. SearchObservation/v1 and raw search point

### 6.1 Document granularity

One immutable SearchObservation document contains exactly one RawSearchPoint. This is intentionally
not a public Artifact or generic observation framework.

### 6.2 Common and point identity

Required fields include:

- `format_version=search-observation/v1`;
- `observation_id`, `run_id`, generation time;
- instrumented implementation/source/build/environment identity;
- SearchProtocol path/identity/checksum;
- search_execution identity/checksum;
- exact PQArtifact, DiskLayoutArtifact, GlobalGraph, and QuerySet identities;
- RawSearchPoint key `(cache protocol, L_search, repetition_index,
  search_execution identity)`;
- exact controls and effective cache state;
- declared/completed query counts;
- canonical `batch_wall_clock_us`;
- terminal status;
- ordered per-query observations;
- whole-document checksum.

GroundTruth is forbidden. Its change cannot affect raw search validity.

SearchObservation retains the environment fingerprint and raw timing/QueryStats only. It contains
neither `quality_eligible` nor `performance_eligible`; both are evaluator-derived report findings.

`source_build` includes the exact admitted runner binary path/SHA-256 and independent search
BuildManifest path/SHA-256 in addition to the pinned DiskANN source identity. It does not merge the
BuildManifest into the observation.

### 6.3 RawSearchPoint

Allowed `repetition_index` values are 0, 1, 2. Each declared cache/L pair has exactly three distinct
points. Point status is one of `SUCCEEDED`, `FAILED`, `INVALID_OUTPUT`, `CANCELLED` using existing
terminal semantics. A point never overwrites another point and has no replacement.

Offline evaluation checks this set before deriving metrics. It constructs the expected cache/L
groups and repetition indices from the frozen protocol's cache, `L_search`, and
`repetition_count`; Feature 005 v1 already fixes that count to three. This is not a rule for other
protocols. After binding protocol and search-execution identities/checksums, each
`(cache mode, L_search, repetition_index)` identifies one slot. Different filenames, paths,
`observation_id`, `point_id`, or `run_id` do not turn a duplicate slot into another trial.
Duplicate slots (including failed/excluded copies), missing repetitions or whole groups,
out-of-range indices, and undeclared parameter points fail evaluation. No copy is silently
selected, removed, renumbered, or substituted.

Retained-record completeness is separate from success and metric eligibility. Identity fields,
terminal status, and the canonical observation checksum must be trustworthy before a record can
fill a slot. An uncheckable or contradictory identity/checksum fails evaluation without default
IDs or repetition numbers. Identifiable foreign-protocol records remain audit exclusions and
cannot fill the current set. A checksum-correct record with invalid metrics (for example negative
`total_us`) still fills its declared slot, but contributes no metrics. Complete sets may therefore
have fewer than three eligible records in a group. A group with zero eligible records has no
aggregate; a batch with no evaluable observation retains the existing evaluation failure behavior.

### 6.4 PerQueryObservation

For a succeeded point every `query_row_index` 0..4884 appears exactly once, in canonical order, with:

- exact opaque `source_query_id` copied from QuerySet mapping;
- exactly 100 ordered GlobalNodeIds;
- exactly 100 corresponding finite float distances;
- `QueryStats.total_us` finite and nonnegative;
- `QueryStats.n_ios` nonnegative.

Optional source-preserving fields are `cpu_us`, `io_us`, `n_cmps`, `n_cache_hits`, `n_hops`,
`read_size`, `n_4k`, `n_8k`, and `n_12k`.

### 6.5 Completeness and reuse

`complete_valid` requires exact identities, 4,885 completed rows, exactly K results, legal IDs,
finite distances/stats, complete point timer, and document checksum. A malformed or partial document
is `retained_invalid`: it remains diagnostic evidence and is never a metric-bearing evaluation
input; the evaluator may read only its identity/path/checksum/status/reason to preserve the required
report audit/exclusion reference. A valid observation is reusable under an evaluation-only protocol
change when the search execution identity/checksum and all raw inputs are unchanged.

For each otherwise eligible observation, the offline evaluator reads the environment fingerprint
bytes, checks their SHA-256 against that observation's declaration, then parses those same bytes
before using environment content for eligibility. Missing/unreadable files, invalid JSON, and
checksum mismatches fail the invocation. Every participating declaration is checked, including
different declarations for the same path. Records already excluded on identity, validity, or
terminal status do not require an available environment file and cannot supply the report's
environment reference. The report reference uses a verified path and its actual SHA-256.
Copies of the same evidence at different paths are permitted. Hash verification alone does not
certify normative performance conditions or permit aggregation across different actual
environments; existing environment and parity requirements remain applicable.

These checks use existing fields and checksum rules, without a new schema or format version.
Admission failure publishes no successful EvaluationReport. Where the existing metadata permits
construction, the independent FAILED evaluation BuildManifest retains the input paths and the
specific failure reason. Inputs and previously published reports remain immutable.

## 7. SearchEvaluationReport/v1

This strict Feature-local report is independent of public graph-integrity EvaluationReport/v1.

### 7.1 Identity and lineage

The report binds:

- report ID/version/checksum/generated time;
- SearchProtocol identity/path/checksum and search execution checksum;
- all referenced SearchObservation identities/paths/checksums and terminal statuses, including
  failed/invalid/cancelled/identity-mismatched/excluded observations, plus their exclusion reasons;
- exact Dataset, GlobalGraph, PQArtifact, DiskLayoutArtifact, QuerySet, and GroundTruth identities;
- evaluator and pinned DiskANN source mapping;
- environment fingerprint;
- entry policy and evaluated GlobalGraph candidate/certification context;
- Search Runner Parity evidence path/checksum/result;
- P0/B0/M0 non-reexecution evidence path/checksum and three explicit `false` execution facts.

### 7.2 Per-repetition findings

For each repetition the evaluator derives two independent findings:

- `quality_eligible`: complete valid observation, compatible Dataset/QuerySet/GroundTruth/
  SearchProtocol/evaluator identities, and legal K/L/result semantics. When true, the report records
  Recall@10 and Recall@100 from the same top-100 observation. It does not require a normative Linux
  performance environment.
- `performance_eligible`: complete valid observation, compatible execution/protocol/controls,
  canonical timing semantics, the existing Search Runner Parity requirement, and admitted Linux
  local-SSD/O_DIRECT/libaio environment. When true, performance metrics are required and classified
  `formal`. When false, performance metrics are optional and cannot enter a performance aggregate;
  if retained, they are classified `informational_non_formal`, while the concrete exclusion stays
  in `performance_eligibility_reason`.

Every failed, invalid, cancelled, identity-mismatched, or otherwise excluded point is retained only
as an audit/exclusion reference with identity, path, checksum, terminal status, and separate quality
and performance reasons. It contributes no Recall/QPS/latency/IO, aggregate, median, or comparison
finding and cannot be `quality_eligible` or `performance_eligible`; it is never silently removed.

### 7.3 Aggregates

For each `(cache protocol,L_search)` group, quality and performance aggregation are separate.
Quality aggregates use only `quality_eligible` Recall values; performance aggregates use only
`performance_eligible` QPS/latency/IO values. Each reports its own eligible count and median. With
three eligible values the middle sorted value is used; with two, use their arithmetic mean; with
one, use it; with zero, that metric family emits no formal aggregate. Observation-stage averaging
is forbidden.

### 7.4 Validity boundary

The evaluator checks observation/protocol/GroundTruth compatibility and metric semantics. It does
not re-decide GlobalGraph validity. A GroundTruth mismatch rejects report generation but does not
retroactively invalidate raw search evidence.

The report always requires compatible GroundTruth; there is no GroundTruth-free performance-only
report. `performance_eligible=true` additionally requires the existing Search Runner Parity
`PASS`. That parity gate's scope is unchanged and never upgrades Feature 002's graph gate.
Non-reexecution evidence is retained as a separate referenced document; it is not a new public
report family or invocation instrumentation system.

## 8. Feature-private implementation declarations

These are fixed compatibility declarations, not entries in the three-stage Registry.

| Implementation | requires | provides | ID spaces | runtime services |
|---|---|---|---|---|
| `cpp_main_post_graph_pq_v1` | validated Dataset float32/L2 + vector payload | PQArtifact/v1; PQ capabilities | none -> none | confined workspace, memory/thread budget, cancellation |
| `cpp_main_post_graph_disk_layout_v1` | validated Dataset + GlobalGraph global IDs + medoids | DiskLayoutArtifact/v1 | none/global -> global | confined workspace, disk budget, cancellation |
| `cpp_main_search_disk_index_oracle_v1` | PQ + DiskLayout + QuerySet + fixed protocol | oracle bins/stdout | global results | Linux aligned reader, OpenMP, timer |
| `cpp_main_pqflash_search_instrumented_v1` | same search inputs | SearchObservation/v1 | global results | Linux aligned reader, OpenMP, timer, confined reports |
| `cpp_main_search_metrics_evaluator_v1` | complete observations + GroundTruth + evaluation policy | SearchEvaluationReport/v1 | global IDs | pinned metrics, confined reports |

The current ImplementationRegistry remains unchanged because each descriptor in that registry must
have a graph `StageKind` and factory.

## 9. Lifecycle transitions

### 9.1 Input admission

```text
unexamined -> admitted
           -> rejected
```

Admission requires current bytes, full lineage, identity/capability compatibility, SIFT1M/L2/format
constraints, and post-graph compatibility. Rejection occurs before materialization or search.

### 9.2 PQ and DiskLayout materialization

```text
planned -> running -> succeeded -> validated -> published
                   -> failed
                   -> cancelled
                   -> candidate -> invalid_output -> quarantined
```

Only validated candidates publish a normal ArtifactRef. Manifests/evidence stay separate.

### 9.3 Search point

```text
planned -> running -> succeeded -> complete_valid
                   -> failed
                   -> cancelled
                   -> invalid_output -> retained_invalid
```

Failed/invalid/cancelled evidence is retained and never replaced.

### 9.4 Evaluation

```text
complete observations + compatible GroundTruth -> report_generated
                                               -> rejected
```

No incomplete observation is silently skipped as if it never existed.

### 9.5 Quality and performance eligibility

```text
complete_valid + compatible GroundTruth/evaluation -> quality_eligible
                                                   -> quality_ineligible

complete_valid + execution/timing/environment admission -> performance_eligible
                                                         -> performance_ineligible
```

Both judgments are derived only during report evaluation. A non-normative environment does not
invalidate a complete SearchObservation or compatible Recall, but it prevents formal QPS, latency,
and Mean IO aggregation.

## 10. Invariants

1. Materialization never mutates Dataset or GlobalGraph; their aggregate checksums are unchanged.
2. PQArtifact depends on Dataset only; DiskLayoutArtifact depends on exactly Dataset + one graph.
3. The disk record index is GlobalNodeId and cannot contain LocalNodeId.
4. GroundTruth is absent from search execution and present in evaluation only.
5. K is always 100; every L is >=100; each complete query has exactly 100 IDs/distances.
6. Zero and fixed cache are distinct protocols.
7. Every cache/L group has exactly three predeclared attempts and no replacement.
8. M0/M1 or other graph identities are not hard-coded in the search evaluator.
9. Search Runner Parity does not change Feature 002's `NOT_REACHED` graph gate or candidate state.
10. No object in this model authorizes Query/Search execution larger than SIFT1M.
11. SearchObservation contains raw facts only; quality/performance eligibility exists only in the
    Feature-local report.
