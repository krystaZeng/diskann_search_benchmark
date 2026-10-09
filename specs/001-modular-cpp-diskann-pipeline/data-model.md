# Phase 1 Data Model: Modular cpp_main DiskANN Pipeline

## 1. Scalar Types and Identity Domains

| Type | Representation | Validity |
|---|---|---|
| `GlobalNodeId` | C++ `uint32_t`, JSON integer | Stable within one dataset; `0 <= id < global_node_count` |
| `LocalNodeId` | C++ `uint32_t`, JSON integer | Meaningful only with one `PartitionId`; `0 <= id < local_node_count` |
| `PartitionId` | C++ `uint32_t`, JSON integer | Unique in one PartitionArtifact; `0 <= id < partition_count` |
| `Sha256` | 64 lowercase hexadecimal characters | Digest of exact bytes named by the containing field |
| `FormatVersion` | Non-empty stable string | Exact-match compatibility in v1; no implicit downgrade |
| `CapabilityId` | Stable string plus version suffix | Compared as an exact set member in v1 |

Node counts are unsigned logical counts. A count may be represented as `uint64_t` internally but
must not exceed `2^32`, because the largest valid ID is `2^32 - 1`. The canonical Fake fixture uses
only IDs `0..5`.

## 2. Common Reference Types

### 2.1 ArtifactRef

An immutable, copyable reference passed between stages. Its `metadata_path` points to the immutable
Artifact metadata document represented by the other three fields.

| Field | Type | Rules |
|---|---|---|
| `artifact_type` | string enum | `dataset`, `partition`, `local_graph`, or `global_graph` in v1 |
| `format_version` | string | Must be accepted by the consumer descriptor |
| `aggregate_checksum` | `Sha256` | Recomputed before every new or reused downstream handoff |
| `metadata_path` | workspace-relative path | Normalized, no absolute path and no `..`; excluded from identity |

The path resolves to Artifact metadata, not a normalized stage Manifest, BuildManifest, or
EvaluationReport. BuildManifest and EvaluationReport use separate stored references. An
ArtifactRef in the normal resolver cannot resolve a quarantine entry.

### 2.2 PayloadRef

| Field | Type | Rules |
|---|---|---|
| `role` | stable string | Unique within one Artifact; determines ordered checksum position |
| `path` | workspace-relative path | Normalized and confined to the Artifact directory |
| `media_type` | string | Selects the payload parser/validator |
| `byte_length` | non-negative integer | Must equal the exact stored byte count |
| `sha256` | `Sha256` | SHA-256 of exact stored bytes |

Payload order is explicit and semantically significant for the aggregate checksum. A physical path
is never checksum input.

### 2.3 DatasetIdentity

| Field | Type | Rules |
|---|---|---|
| `dataset_id` | string | `fake-six-node-v1` for the canonical fixture |
| `dataset_checksum` | `Sha256` | Binds the exact vector payload and its interpretation; canonical Fake value is `2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b` |
| `global_node_count` | positive integer | `6` for the canonical fixture |
| `vector_dimension` | positive integer | `2` for the canonical fixture |
| `scalar_type` | enum | `float32` in v1 Fake data |
| `distance_metric` | enum | `l2` in v1 Fake data |

Dataset identity is copied into stage Artifact metadata and BuildManifests. It is not inferred from
a filename.

## 3. Artifact Metadata

All stage Artifact types share these logical fields:

- `schema_version`: metadata schema version, initially `1.0.0`.
- `artifact_type` and `format_version`.
- `dataset`: complete `DatasetIdentity` or an exact dataset ID/checksum binding.
- `id_space`: `none`, `local`, or `global` as appropriate.
- interpretation-affecting structural fields defined by the concrete Artifact.
- `capabilities`: sorted, unique capability IDs actually provided by validated data.
- `source_artifacts`: ordered path-independent source identities.
- `payloads`: ordered `PayloadRef` values.
- `aggregate_checksum`: digest defined by `contracts/checksum-canonicalization.md`.

Metadata is immutable after publication. Timestamps, toolchain information, execution parameters,
metrics, and evaluation findings are not Artifact fields.

### 3.1 DatasetArtifact

The test Harness represents its immutable vector input as a dataset ArtifactRef so the Partition
stage also receives a validated reference. Its structural fields are the complete DatasetIdentity;
its required payload role is `vectors`. The canonical bytes, payload checksum, and DatasetArtifact
aggregate checksum are fixed in `contracts/fake-fixture.md`. It is an input Artifact, not one of the
three stage output Artifact types.

### 3.2 PartitionArtifact

| Field | Type | Rules |
|---|---|---|
| `dataset` | `DatasetIdentity` | Must match the DatasetArtifact input |
| `id_space` | constant `local` | Each shard creates its own LocalNodeId domain |
| `partition_count` | positive integer | Equals `partitions.length`; `2` in canonical fixture |
| `partitions` | ordered `PartitionRecord[]` | Sorted by ascending PartitionId; IDs are contiguous in v1 |
| `capabilities` | set | Includes complete-map capability after validation |
| `source_artifacts` | array | Contains the DatasetArtifact identity |

`PartitionRecord` contains `partition_id`, `local_node_count`, a mandatory complete
LocalToGlobal-map `PayloadRef`, and an optional shard-vector `PayloadRef`. The decoded map length
equals `local_node_count`; every value is a legal GlobalNodeId. Duplicate GlobalNodeIds across
different Partitions are valid. The aggregate Artifact may additionally carry one final
`centroids` payload and declare `partition.centroids/v1`. The PartitionArtifact is one aggregate
Artifact containing every Partition record; a record and its payloads are not a separate
ArtifactRef or a `PartitionShardArtifact`.

Local Build uses a selected record's shard-vector payload when present. When it is absent and the
implementation needs vectors, it must materialize them from the validated Dataset-backed
`StageContext::vector_accessor` in exact LocalToGlobal-map order. That temporary materialization is
Local Build preparation, not Partition output, and cannot be published or described as a native P0
shard-vector payload. Missing both sources rejects Local Build before algorithm execution.

For each Local Build invocation, the executor passes the validated aggregate PartitionArtifact
ArtifactRef and one explicit `PartitionId`. The argument is mandatory, must satisfy
`0 <= partition_id < partition_count`, and must identify a record actually present in the
PartitionArtifact. `LocalBuildConfig` carries the immutable normalized stage-Manifest reference and
implementation parameters but does not replace or infer the explicit PartitionId.

### 3.3 LocalGraphArtifact

| Field | Type | Rules |
|---|---|---|
| `partition_id` | `PartitionId` | Identifies one exact source Partition record |
| `local_node_count` | positive integer | Equals map length and graph node count |
| `id_space` | constant `local` | Nodes and neighbors are LocalNodeIds |
| `declared_max_degree` | non-negative integer | Configuration-bound upper limit |
| `observed_max_degree` | non-negative integer | Recomputed from graph; never above declaration |
| `source_partition_checksum` | `Sha256` | Exact aggregate checksum of the aggregate PartitionArtifact containing the owning Partition record |
| `payloads` | ordered array | Required roles `local_to_global_map`, then `local_graph` |

The map carried by the LocalGraphArtifact must byte-match or logically match the validated owning
Partition map according to its declared payload format. Every local node and neighbor is in the
owning Partition range. The recorded `partition_id` must equal the explicit Local Build selection,
and `source_partition_checksum` must equal the validated aggregate PartitionArtifact checksum used
for that invocation. A present empty adjacency list is valid.

### 3.4 GlobalGraphArtifact

| Field | Type | Rules |
|---|---|---|
| `global_node_count` | positive integer | Equals the DatasetIdentity count |
| `id_space` | constant `global` | Nodes and neighbors are GlobalNodeIds |
| `declared_max_degree` | non-negative integer | Configuration-bound upper limit |
| `observed_max_degree` | non-negative integer | Recomputed and not above declaration |
| `source_partition_artifact` | source identity | Exact validated PartitionArtifact |
| `source_local_graph_artifacts` | ordered identities | One validated local graph per selected Partition |
| `payloads` | ordered array | Required role `global_graph`, followed by optional `medoids` |

The graph has exactly one adjacency list for every GlobalNodeId in `0..global_node_count-1`. No
LocalNodeId, unresolved replica identity, or out-of-range neighbor may remain. Generic Merge may
produce graph-only output. An implementation that declares `graph.medoids/v1` must also produce the
`medoids` payload, and the payload may not appear without that capability.

## 4. Provenance and Evaluation Objects

### 4.1 BuildManifest

BuildManifest is separate from Artifact metadata and remains mutable only while the owning stage
is active. On terminal status it becomes append-closed.

Required groups are:

- identity: `manifest_version`, `run_id`, `stage`, `implementation_id`, implementation version;
- source: repository, branch, full commit/equivalent source fingerprint, dirty-worktree state;
- toolchain: OS, compiler path/version, CMake generator/version, build type, definitions, complete
  compile/link flags and commands;
- execution: complete normalized parameters, dataset identity, resource budgets, input and output
  ArtifactRefs, start/end timestamps, and workspace-relative paths;
- outcome: lifecycle status, exit code, failure category/reason, cancellation, reuse source, and
  validator evidence reference as applicable.

It may contain timing and resource metrics because those fields are provenance, but they never
participate in Artifact identity.

### 4.2 EvaluationReport

EvaluationReport contains:

- independent report identity and evaluator version;
- one validated normal ArtifactRef;
- validation evidence reference and evaluation timestamp;
- graph integrity, logical node count, invalid-neighbor count, observed/declared degree, and
  pass/fail findings required by this Feature;
- optional metrics collected by the StageContext MetricsSink.

Evaluation refuses unvalidated, quarantined, missing, or checksum-mismatched inputs. It cannot
mutate the Artifact or serve as its BuildManifest.

For the two-run Fake determinism check, comparison uses a deterministic evaluation projection, not
byte-for-byte equality of complete EvaluationReport JSON documents. In v1 the projection is the
entire `findings` object: graph integrity, logical node count, invalid-neighbor count, declared and
observed maximum degree, pass/fail degree-bound outcome, and any future Fake evaluation field that
its contract explicitly marks deterministic. Optional metrics are excluded unless explicitly
declared deterministic.

The comparison projection excludes `report_id`, `generated_at`, every run ID, absolute and
workspace-relative paths, validation/BuildManifest references, run-specific time fields, and
non-deterministic runtime metrics. Each retained run therefore has an independent run identity,
report identity, timestamps, and provenance even though its deterministic findings match.

### 4.3 ValidationEvidence

| Field | Type | Rules |
|---|---|---|
| `validator_version` | string | Exact validator implementation identity |
| `artifact_candidate` | object | Candidate type/version/path/checksum claims |
| `checks` | ordered array | Check ID, pass/fail, expected, observed, message |
| `result` | enum | `VALID` or `INVALID` |
| `failure_category` | optional enum | Required for `INVALID` |

ValidationEvidence is stored with the BuildManifest and, for invalid output, in quarantine. A
successful evidence record authorizes publication only for the exact bytes it checked.

## 5. Registry and Compatibility Model

### 5.1 ArtifactRequirement and ContractRequirement

Each `ArtifactRequirement` contains one Artifact type, its accepted format versions, and its
accepted input ID space. `ContractRequirement.input_artifacts` is a non-empty ordered collection of
those entries; required capability IDs and required StageContext service capability IDs remain at
the enclosing ContractRequirement level. No v1 field expresses cardinality, multiplicity, an
optional input, or a fallback input.

A Merge descriptor has separate `input_artifacts` entries for PartitionArtifact and
LocalGraphArtifact. Preflight matches every entry. A missing entry, incompatible accepted version,
or incompatible per-input ID space rejects the chain before any factory or implementation is
created or invoked. A provide declaration remains a single produced
type/version/output-ID-space/capability contract.

V1 comparison is exact and explicit: no version range negotiation, implicit Adapter insertion,
capability aliasing, or fallback implementation selection.

### 5.2 ImplementationDescriptor

| Field | Type | Rules |
|---|---|---|
| `implementation_id` | string | Globally unique and immutable in one Registry |
| `implementation_version` | string | Exact implementation contract version |
| `stage` | enum | `partition`, `local_build`, or `merge` |
| `requires` | requirement object | Contains `input_artifacts[]` plus requirement-level capabilities and StageContext services |
| `provides` | provide object | Includes output type/version/ID/capabilities |
| `parameter_schema_path` | path | Stable repository-relative path to the implementation parameter JSON Schema |
| `test_only` | boolean | True for all Fake descriptors |

Registry construction requires `RegistryAdmissionPolicy::kProduction` or
`RegistryAdmissionPolicy::kTest`; there is no default construction. Production admission rejects
`test_only: true`, while test admission accepts both test-only and formal descriptors. Either mode
still rejects duplicate identity, stage mismatch, mutation after sealing, overwrite, and fallback.
Rejected admission leaves Registry state unchanged. The installed production runner uses
`kProduction`; the non-installed Fake runner uses `kTest`. Admission policy is Registry state and
is not an ImplementationDescriptor field.

## 6. StageContext

StageContext is a per-run service bundle, not serialized graph state.

| Service | V1 behavior |
|---|---|
| `WorkspaceRef` | Real isolated normal, temporary, manifest, report, and quarantine roots |
| memory/thread budgets | Real positive configured limits; copied into BuildManifest |
| `TemporaryStorageRef` | Real managed stage directory with atomic-publication boundary |
| `VectorAccessor` | Real read-only in-memory access to the canonical Fake dataset |
| `DistanceFunction` | Real deterministic L2 implementation over Fake vectors |
| `MetricsSink` | Real in-memory lifecycle events and counters |
| `CancellationToken` | Real cooperative token observed by Fake Partition, Local Build, and Merge at stage start, after input resolution/validation, and before output publication |
| `CheckpointStore` | Explicit unavailable stub; rejects calls and advertises no capability |

An implementation requiring checkpoint storage fails preflight and is never invoked.

## 7. RunRequest and Experiment Configuration

The human-authored YAML document and normalized JSON RunRequest carry the same experiment
semantics. The normalized JSON additionally materializes the workspace paths derived from the
human-authored root so every downstream path is explicit and confined:

- `request_version`, `request_id`, and `study_class: infrastructure_validation`;
- dataset ArtifactRef;
- explicit implementation identity for each of three stages;
- complete implementation-specific parameter objects;
- positive memory and thread budgets;
- workspace root, derived normal/temporary/manifest/report/quarantine roots, and optional validated
  reuse ArtifactRef;
- fixed repetition policy (`count: 2`, `aggregation: retain_all`) for two retained deterministic
  runs with identical input and configuration and distinct run provenance.

The Harness creates distinct per-run `run_id` values, report identities, timestamps, and provenance
for the two retained runs. Both results remain available under `retain_all`; only payload and
aggregate Artifact checksums and the deterministic evaluation projection are required to match.
This Feature-local request contract accepts only `infrastructure_validation`. A future Feature that
executes a real Baseline must define and version its own request contract using
`baseline_validation`; the pinned-but-not-executed Baseline Reference here does not change this
Feature's study class.

The Python Harness uses duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation.
It uses one project subclass of `yaml.SafeLoader` with a custom mapping constructor, requires a
top-level mapping and string mapping keys, and recursively accepts only `dict`, `list`, `str`,
`bool`, `int`, finite `float`, and `None`. It rejects duplicate keys, Python object or custom tags,
unsafe constructors, NaN, Infinity, date/datetime, bytes, set, and every other non-JSON result type.
JSON Schema then determines whether a concrete field permits null, float, or another otherwise
JSON-compatible value. The normalized JSON is validated and written immutably before C++ is
invoked; no second YAML parser is used.

## 8. Lifecycles

### 8.1 Stage lifecycle

```text
PLANNED ──preflight pass──> RUNNING ──candidate valid──> SUCCEEDED
    │                          ├── implementation error──> FAILED
    │                          ├── cancellation──────────> CANCELLED
    │                          └── candidate invalid─────> INVALID_OUTPUT
    ├── validated reuse─────────────────────────────────> REUSED
    └── preflight failure (no stage invocation; run remains rejected)
```

Terminal manifests are immutable. `INVALID_OUTPUT` is not interchangeable with `FAILED` or
`CANCELLED`.

Factory creation precedes RUNNING and does not count as an invocation. Private Executor state
tracks the actual method start and terminal write; an exception after method start must leave
FAILED/CANCELLED evidence when writing is possible, with the original category and reason.
Factory failure or a boundary cancellation does not invent an executed stage. Typed cancellation
exceptions are CANCELLED; ordinary failures are not reclassified based on token state.
Explicit stage-owned evidence remains separately linked by checksum, including legacy CANCELLED
records without failure and CANCELLED records whose failure category is `cancelled`.

Stage and run terminals differ: cancellation observed after successful final publication leaves
Merge SUCCEEDED and the graph VALID, but the run CANCELLED with no automatic evaluation.
A cancelled temporary candidate has no VALID claim and is not quarantined. Success publication
and its evidence finish without intervening cancellation checks; this is not a disk transaction.

Runner results with exact counts keep version `1.0.0`. Version `2.0.0` is restricted to Harness
interrupted execute/CANCELLED/exit 4 with a cancellation reason and `stage_invocations: null`.
Null denotes the entire count set being unknown; it cannot be inferred from completed Manifests,
coerced to zero, or used with a successful result. No historical result or Artifact is reinterpreted.

### 8.2 Artifact publication lifecycle

```text
TEMPORARY_CANDIDATE ──validator VALID──> NORMAL_IMMUTABLE
         └────────── validator INVALID──> QUARANTINED_IMMUTABLE
```

There is no transition from quarantine to normal storage. Corrected bytes form a new candidate and
new identity. Explicit deletion is an administrative diagnostic action outside normal resolution.

### 8.3 Reuse lifecycle

```text
NORMAL_IMMUTABLE ──recompute checksum + compatibility pass──> CONSUMED_AS_REUSED_INPUT
                 └─any mismatch──────────────────────────────> REUSE_REJECTED
```

Rejection neither mutates nor quarantines a previously valid cached Artifact.

## 9. Validation Order

1. Parse JSON and validate its schema/version.
2. Resolve only inside the expected normal or explicit diagnostic namespace.
3. Verify Artifact type, format version, dataset identity, and declared ID space.
4. Verify every payload exists, is complete, has the declared byte length, and matches SHA-256.
5. Recompute the path-independent aggregate checksum.
6. Parse payloads and validate map completeness, ownership, ID ranges, node counts, and degrees.
7. Verify source Artifact checksums and provided capabilities.
8. Match consumer `requires` against upstream `provides` and StageContext services.
9. Publish or reuse only after all preceding checks pass.
10. Permit evaluation only for an exact validated normal ArtifactRef.

## 10. Canonical Fake Fixture

```text
vectors: G0=(0,0), G1=(1,0), G2=(2,0), G3=(0,1), G4=(1,1), G5=(2,1)

P0 LocalToGlobal: [0,1,2,3]
P1 LocalToGlobal: [2,3,4,5]

both local graphs:
L0:[L1,L2], L1:[L0,L2], L2:[L1,L3], L3:[]

expected global graph:
G0:[G1,G2], G1:[G0,G2], G2:[G1,G3,G4],
G3:[G2,G4], G4:[G3,G5], G5:[]
```

The Fake Merge's ordered exact-deduplication/truncation rule is test implementation behavior only.
It is not an Adapter and is not a description of cpp_main `merge_shards`.
