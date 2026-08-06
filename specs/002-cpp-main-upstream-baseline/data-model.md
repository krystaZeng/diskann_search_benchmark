# Data Model: cpp_main P0+B0+M0 Graph Closure

This model describes the minimal Feature 002 implementation over the existing Feature 001
contracts. ArtifactRef, identifiers, BuildManifest, checksum, validation, quarantine, Registry,
StageContext, and Pipeline execution semantics remain authoritative. The model introduces no
public type or Contract version.

## 1. Gate A Stage Records

| Stage | Separability | Contract status | Availability before implementation | Binding and validation boundary |
|---|---|---|---|---|
| P0 | separable | compatible | planned | direct pinned partition_with_ram_budget call; native count, centroids, and maps |
| B0 | separable | compatible | planned | upstream_callable_wrapper; structural_and_contract |
| M0 | separable | compatible | planned | direct pinned merge_shards call; merged graph plus required cpp_main medoids |

~~~yaml
native_reference_reachable: yes
unified_graph_parity_reachable: yes
formal_gate_status: not_reached
baseline_registration: prohibited
~~~

Implementation and structural validation may change stage availability evidence, but cannot change
the retained Gate A fields. This block is historical design evidence and is not part of
`unified-sharded-report.json`, which uses the explicit boolean fields defined in Section 8.

## 2. Shared Identity Entities

### 2.1 BaselineSourceIdentity

| Field | Rule |
|---|---|
| repository | Exact https://github.com/microsoft/DiskANN.git |
| branch | Exact cpp_main |
| commit | Exact full commit 78256bbab4685e1774e78d331e081a153be26823 |
| parent gitlink | Mode 160000 and the same object ID |
| submodule HEAD | Same full commit |
| upstream state | Clean before configure, before execution, and after execution |

Any mismatch fails before a stage factory is created. A branch name without the full commit is not
a Baseline identity.

### 2.2 BuildIdentity

BuildIdentity is recorded in the terminal BuildManifest, separate from every Artifact. It contains:

- parent and upstream repository states, including dirty state;
- actual compiler path/version and C++ standard;
- CMake version, generator, build type, effective definitions, and project/upstream compile options;
- actual configure/build commands and explicit upstream binary directory;
- actual diskann_s target/library identity and checksum;
- linked OpenMP, MKL, allocator, aio, and runtime identities;
- Feature-owned runner/library identities and checksums;
- algorithm-relevant environment and thread settings.

The historical Native Probe build identity is comparison evidence, not a substitute for the actual
implementation BuildIdentity.

### 2.3 DatasetBinding

| Field | Rule |
|---|---|
| ArtifactRef | Validated immutable Dataset Artifact metadata reference |
| DatasetIdentity | Exact dataset ID/checksum, 1,000,000 global nodes, dimension 128, float32, L2 |
| payload | DiskANN little-endian float32 binary |
| payload length | 512,000,008 bytes |
| payload SHA-256 | 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e |
| vector service | Dataset-backed VectorAccessor bound through StageContext |

The Dataset is never normalized, reordered, converted from fvecs, or hidden as a raw parameter
path. selected_query.csv is outside this graph-build model.

The Feature-local runner resolves the validated Dataset payload once and constructs a private
cpp_main float32 VectorAccessor that retains the exact native path and Dataset checksum. B0 may use
that private concrete binding to supply the filename required by the pinned retrieval callable,
after verifying the bound identity. The public VectorAccessor API is unchanged; a different
accessor implementation, parameter path, or process-global lookup fails closed.

### 2.4 CppMainGraphProfile

The only current Profile reference is
`experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml`, and its governance field
is `status: draft`. The file remains draft before and after this structural closure; a frozen
Profile belongs only to future Formal Graph Parity work.

| Field | SIFT1M value | Ownership |
|---|---:|---|
| scalar type / metric / filters | float32 / L2 / unfiltered | fixed Feature scope |
| labels / build PQ / disk PQ / OPQ | disabled | fixed Feature scope |
| status | draft | current structural Profile governance |
| R | 16 | draft Profile |
| L | 32 | draft Profile |
| M GiB | 0.6 | draft Profile and P0 input |
| T | 6 | draft Profile |
| P0 sample probability | 0.256 | derived as min(1,256000/N) |
| P0 local degree estimate | 10 | derived as floor(2R/3) |
| P0 replication factor | 2 | pinned native rule |
| B0 local degree | 10 | derived as floor(2R/3) |
| B0 saturation | false | pinned native rule |
| B0 alpha / max occlusion | 1.2 / 750 | unchanged upstream defaults |
| M0 output degree bound | 16 | Profile R |

Derived and default fields are recorded but not independently tunable. B=0.03 and QD=0 remain part
of the Native Reference profile because the native top-level entry performs PQ preparation; the
callable Unified P0+B0+M0 stages do not consume them.

### 2.5 PreflightAdmission

The Feature-local `cpp_main_baseline preflight` command consumes the draft Profile and fixed
source/build inputs, but never starts the runner or creates a Stage factory. A successful admission
contains:

- one validated immutable DatasetArtifact and its ArtifactRef;
- separate Dataset ValidationEvidence;
- verified BaselineSourceIdentity and BuildIdentity;
- the recomputed strict sharded-path predicate;
- P0, B0, and M0 invocation counts all equal to zero; and
- one immutable normalized request at
  `build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json`.

The preflight terminal response returns the Dataset ArtifactRef and request path. The normalized
request binds the exact Profile path/checksum, Dataset ArtifactRef/checksum, Source/Build Identity,
workspace, implementation identities, parameters, and sharded admission evidence. The later
`execute` command consumes this exact document through `--request`; it does not repeat admission
through `--config` or mutate the request.

## 3. Baseline-Local Native File Entities

All entities in this section are physical cpp_main encodings. They are parsed by fixed internal
readers and do not create public Artifact types or a generic Adapter.

### 3.1 NativeFloat32Dataset

Two little-endian uint32 fields npts and dim followed by exactly npts times dim little-endian
IEEE-754 float32 values. Validation is overflow-safe, requires exact file length, and rejects every
non-finite value.

### 3.2 NativePartitionIdMap

Two little-endian uint32 fields row_count and dimension=1 followed by row_count little-endian
uint32 GlobalNodeIds. Payload position is LocalNodeId. Every GlobalNodeId is less than the Dataset
node count. The map order is immutable.

### 3.3 NativeCentroids

A DiskANN float32 matrix with row_count equal to the P0-returned Partition count and dimension equal
to the Dataset dimension. The file must have exact length and finite scalar values.

### 3.4 NativeLocalGraph

The exact Index::save graph layout bound to the pinned commit: a header containing declared byte
size, maximum observed degree, local entry point, and frozen-point count, followed by one degree
and that many uint32 LocalNodeIds for every local node. Validation requires exact declared/file
length, exactly the selected map row count records, zero frozen points, an in-range entry point,
neighbors within the owning Partition, degree at most floor(2R/3), and no trailing bytes.

### 3.5 NativeLocalGraphData

The .data companion produced by Index::save. It is a DiskANN float32 matrix whose row count,
dimension, length, values, and checksum must match the B0-preparation shard vectors. It is temporary
BuildManifest lifecycle evidence, not a LocalGraphArtifact payload and not an M0 input.

### 3.6 NativeMergedGlobalGraph

The merge_shards graph layout uses the same graph record structure, with one record per Dataset
node and GlobalNodeId neighbors. Validation requires exact length, node count equal to Dataset
global node count, every neighbor in range, observed degree at most R, and no trailing bytes.

### 3.7 NativeMedoids

A DiskANN uint32 matrix with dimension=1 and one in-range GlobalNodeId per input Partition. For the
cpp_main M0 implementation this file is mandatory. The mapped local medoid for each B0 input must
agree with the corresponding entry, and the merged graph header entry point must be one of the
medoids.

## 4. P0 Entities

### 4.1 P0Invocation

Inputs:

- validated DatasetBinding;
- CppMainGraphProfile fields M, R-derived local degree, replication factor, and sample probability;
- StageContext services and stage-temporary output prefix;
- pinned BaselineSourceIdentity and BuildIdentity.

Execution is one call to pinned partition_with_ram_budget<float>. P0 randomness remains controlled
only by upstream.

### 4.2 P0NativeOutputSet

| Field | Rule |
|---|---|
| partition_count | Returned by the callable; at least two for this sharded closure |
| centroids | Exactly one valid NativeCentroids file |
| maps | Exactly one NativePartitionIdMap for every contiguous PartitionId |
| ownership | Every Dataset GlobalNodeId is represented according to native replication factor 2 |
| shard vectors | Absent; P0 must not claim or synthesize them |

Partition count is not frozen to the Native Probe observation of three because native sampling and
k-means initialization use random_device.

### 4.3 P0 PartitionArtifact

One immutable aggregate PartitionArtifact contains:

- exact Dataset lineage;
- partition_count;
- contiguous explicit Partition records;
- owning PartitionId and local node count for each record;
- one complete LocalToGlobalIdMap payload per record;
- optional-at-public-contract but required-for-this-P0 centroids payload and capability;
- no shard_vectors_role.

The existing aggregate checksum covers all map and centroids payload identities. The candidate must
pass validation before any B0 begins.

## 5. B0 Entities

### 5.1 PartitionSelection

The B0 invocation receives the validated aggregate PartitionArtifact and an explicit PartitionId.
The ID must be in range and select exactly one record. Selection never uses a list offset, path
name, or implicit current Partition. The exact map payload and source aggregate checksum form the
selection lineage.

### 5.2 NativeShardVectors

Pinned retrieve_shard_data_from_ids<float> materializes one temporary DiskANN float32 matrix from
the complete Dataset and selected map. Row i must be byte-equivalent to Dataset row
LocalToGlobalIdMap[i]. The file is B0 preparation, not a P0 output or reusable Artifact.

### 5.3 B0Invocation

After shard validation, B0:

1. constructs IndexWriteParameters with L=32 and local degree=10;
2. uses unfiltered filter-list behavior, saturation=false, T=6, alpha=1.2, and max occlusion=750;
3. constructs pinned upstream Index<float>;
4. calls Index::build with the temporary shard and exact row count;
5. calls Index::save with a stage-temporary prefix.

No Feature-owned code performs Vamana search, prune, reciprocal insertion, or graph serialization.

### 5.4 LocalGraphArtifact

| Field/property | Rule |
|---|---|
| Dataset | Exact P0/Dataset identity |
| owning PartitionId | Exact explicit B0 input |
| LocalToGlobal map | Exact selected map payload/checksum |
| source Partition | Exact aggregate Artifact identity/checksum |
| local node count | Selected map length and graph record count |
| node/neighbor space | Partition-scoped LocalNodeId |
| maximum degree | Declared and observed at most 10 |
| graph payload | Exact validated NativeLocalGraph bytes |
| .data | Not an Artifact payload; checksum/lifecycle only in BuildManifest |

Every B0 output is validated before the executor selects the next Partition or starts M0.

## 6. M0 Entities

### 6.1 CompleteMergeInputSet

The M0 input set consists of:

- one validated aggregate P0 PartitionArtifact;
- exactly one validated LocalGraphArtifact for every PartitionId;
- no duplicate or extra LocalGraphArtifact;
- identical Dataset identity across all inputs;
- exact owning PartitionId, selected map checksum, and source Partition checksum for every local graph.

The implementation stages the immutable maps and native graphs byte-for-byte into a temporary
prefix layout accepted by merge_shards. Staging changes no logical record or adjacency order and
creates no Adapter evidence claim.

### 6.2 M0Invocation

Inputs are the complete staged set, P0 Partition count, and R=16. Execution is one call to pinned
merge_shards. Its native random_device-seeded shuffle is not replaced or seeded by the Feature.
The call performs ID relabeling, candidate union, deduplication, shuffle, and truncation exactly as
mapped in source-mapping.md; it does not perform distance pruning, RobustPrune, global refinement,
or connectivity repair.

### 6.3 GlobalGraphArtifact

| Field/property | Rule |
|---|---|
| Dataset | Exact complete Dataset identity |
| source inputs | Ordered aggregate Partition identity plus ordered LocalGraph identities |
| global node count | Dataset global node count |
| node/neighbor space | GlobalNodeId |
| maximum degree | Declared and observed at most R=16 |
| graph payload | Required exact NativeMergedGlobalGraph bytes |
| medoids payload | Required for cpp_main M0; exact NativeMedoids bytes |
| aggregate identity | Covers both graph and medoids payload identities |

The public GlobalGraph contract still permits graph-only outputs for other Merge implementations.
Only the cpp_main descriptor/capability makes medoids mandatory here.

## 7. Descriptor Model

| Implementation identity | Stage | Required Artifacts/services | Provides |
|---|---|---|---|
| cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1 | partition | native float32 Dataset; workspace/cancellation/metrics | map-only PartitionArtifact plus centroids capability |
| cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1 | local_build | native Dataset, aggregate Partition, explicit PartitionId, VectorAccessor/workspace/cancellation/metrics | LocalGraphArtifact with native local adjacency |
| cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1 | merge | aggregate Partition plus complete LocalGraph collection; workspace/cancellation/metrics | GlobalGraphArtifact with global adjacency and medoids capability |

Each descriptor has one repository-relative parameter_schema_path. All input Artifact requirements
must match before factory creation. ContractProvide remains a single output Artifact.

## 8. Separate Provenance and Evidence

### Artifact

Each Artifact metadata document contains only immutable data identity, payload identities, format,
ID space, capabilities, Dataset/source lineage, and structural claims. ArtifactRef.metadata_path
points to that metadata document.

### BuildManifests

One build-stage BuildManifest records complete Source/Build identity and actual configure/build
commands. The existing executor emits a separate schema-valid BuildManifest for P0, every B0, and
M0, recording original configuration and normalized request digests, exact input/output
ArtifactRefs, resolved stage parameters, explicit PartitionId where applicable, temporary-file
lifecycle, actual stage start/end timestamps, publication state, and terminal status. Collectively
these records provide the required provenance without changing the public BuildManifest contract or
implementing Feature 001's deferred generic terminal-manifest hardening.

Each B0 BuildManifest lists both the Dataset ArtifactRef and aggregate Partition ArtifactRef. The
Dataset is an actual declared input even though the C++ LocalGraphBuilder method receives its
vector access through StageContext.

### Structural ValidationEvidence

Separate evidence records source admission, native-format findings, Dataset/map/shard/local/global
ID checks, degree checks, complete merge-set checks, medoid checks, and before/after upstream state.

### EvaluationReport

The existing `fake_graph_integrity_evaluator` reads a JSON adjacency payload and cannot evaluate the
cpp_main native binary merged graph. Its Fake JSON behavior and evaluator identity remain
unchanged.

For a successful Feature 002 closure, the existing public internal evaluator implementation in
`src/pipeline/evaluator.cpp` selects one closed cpp_main conditional branch. This branch is not
runner-private code, although only the Feature-local cpp_main runner's successful path produces
matching input. Its EvaluationReport evaluator object is exactly:

~~~yaml
implementation_id: cpp_main_native_graph_integrity_evaluator
version: 1.0.0
~~~

It accepts only a GlobalNodeId GlobalGraphArtifact that has already passed the cpp_main Artifact
validator, provides the required global-adjacency capability, and declares the exact reviewed
cpp_main global-graph media type. Fixed source identity has already been guaranteed by execution
preflight source admission, normalized request identity, and the corresponding BuildManifest. The
branch creates a separate EvaluationReport from the ValidatedArtifact structural claims and the
corresponding ValidationEvidence, including graph integrity, logical node count, invalid-neighbor
count, declared/observed maximum degree and degree-bound result. It does not read or parse the
native graph payload again.

Unknown or mismatched media types, missing ValidationEvidence, and unvalidated Artifacts fail
closed. This branch is not a Registry, plugin, generic evaluator dispatch, generic media
dispatcher, or general native parser. The existing `fake_graph_integrity_evaluator` name, version,
JSON path, and behavior remain unchanged. The cpp_main report is retained separately but is not a
Graph Parity Report or performance report and contains no Recall, QPS, latency,
Native-versus-Unified, or Baseline experiment conclusion.

### UnifiedShardedReport

`specs/002-cpp-main-upstream-baseline/unified-sharded-report.json` is the Feature-local structural
closure summary. It is not a public Artifact, Graph Parity Report, registration record, or generic
report framework, and no public schema is created for it. It references the separate Artifact,
BuildManifest, ValidationEvidence, and, for a successful closure, the separate native
graph-integrity EvaluationReport rather than embedding them.

| Field | Rule |
|---|---|
| run_id | Identity of this structural closure run |
| run_class | Exact value `unified_sharded_closure` |
| formal_graph_parity | Exact value `false` |
| validated_graph_baseline_registered | Exact value `false` |
| profile_ref | Repository-relative `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml` |
| profile_checksum | SHA-256 of the exact referenced draft YAML bytes |
| profile_status | Exact value `draft` |
| upstream_commit | Exact pinned full commit |
| dataset_checksum | Exact admitted SIFT1M payload checksum |
| implementation_identities | Exact P0, B0, and M0 implementation identities |
| partition_count | P0-returned count used by the run |
| stage_results | Ordered P0, per-Partition B0, and M0 terminal results |
| artifact_references | References to the immutable Dataset, Partition, LocalGraph, and GlobalGraph Artifacts |
| structural_validation | Exactly `PASS` or `FAIL` |
| upstream_clean_before / upstream_clean_after | Recorded clean-state booleans surrounding execution |

## 9. State Transitions

~~~text
PLANNED
  -> SOURCE_AND_BUILD_ADMITTED
  -> DATASET_VALIDATED
  -> PREFLIGHT_REQUEST_WRITTEN (P0/B0/M0 invocation counts = 0)
  -> EXECUTE_REQUEST_ADMITTED
  -> P0_RUNNING
  -> PARTITION_CANDIDATE
  -> PARTITION_VALIDATED
  -> B0_RUNNING(partition_id)
  -> LOCAL_GRAPH_VALIDATED(partition_id)
  -> ALL_LOCAL_GRAPHS_VALIDATED
  -> M0_RUNNING
  -> GLOBAL_GRAPH_CANDIDATE
  -> GLOBAL_GRAPH_VALIDATED
  -> NATIVE_GRAPH_INTEGRITY_REPORT_WRITTEN
  -> PUBLISHED
  -> STRUCTURAL_REPORT_WRITTEN
~~~

An input/preflight failure occurs before factory creation. A stage failure, cancellation, or invalid
candidate publishes no normal Artifact for that stage and sets every downstream invocation count to
zero. Quarantine and terminal evidence follow the existing public lifecycle.

## 10. First-Round Comparison Projection

The structural gate compares only identities, lineage, exact payload checksums within one run, and
the structural properties defined above. It excludes:

- Native-versus-Unified graph byte or adjacency equality;
- run-to-run graph equality or deterministic findings aggregation;
- timing, process state, allocation history, and thread scheduling;
- Recall, QPS, latency, and full DiskANN build/search behavior;
- any PASS, FAIL, or INCONCLUSIVE Graph Parity result.
