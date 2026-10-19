# cpp_main P0+B0+M0 Stage Binding Contract

This contract defines the three minimal Feature 002 implementation bindings. They implement the
existing public C++17 interfaces and use the existing Registry, preflight, Artifact validator,
StageContext, and Pipeline executor. No public signature or Contract version changes.

## 1. Fixed identity

~~~yaml
study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none
upstream:
  repository: https://github.com/microsoft/DiskANN.git
  branch: cpp_main
  commit: 78256bbab4685e1774e78d331e081a153be26823
formal_gate_status: not_reached
baseline_registration: prohibited
~~~

Each factory is formal and has test_only=false. The three factories may be registered in the
Feature-local Production-admission Registry for structural validation. This registration is not
validated_graph_baseline registration.

## 2. Public interface mapping

P0 implements the existing Partitioner operation over a Dataset ArtifactRef, stage manifest path,
and StageContext.

B0 implements the existing LocalGraphBuilder operation:

~~~cpp
StageResult build(
    const ArtifactRef& partition_artifact,
    PartitionId partition_id,
    const LocalBuildConfig& config,
    StageContext& context);
~~~

The PartitionArtifact is aggregate and PartitionId is explicit.

M0 implements the existing GraphMerger operation over one aggregate Partition ArtifactRef, a
read-only LocalGraph ArtifactRef collection, stage manifest path, and StageContext.

ArtifactRefs point to immutable Artifact metadata. BuildManifest and structural validation evidence
use separate paths/references.

## 3. Descriptor contracts

### 3.1 P0

~~~yaml
implementation_id: cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1
implementation_version: 1.0.0
stage: partition
requires:
  input_artifacts:
    - artifact_type: dataset
      accepted_format_versions: [diskann-float32-dataset/v1]
      input_id_space: none
  capabilities:
    - dataset.diskann_float32_bin/v1
  runtime_services:
    - cancellation_token/v1
    - metrics_sink/v1
provides:
  artifact_type: partition
  format_version: partition-artifact/v1
  output_id_space: local
  capabilities:
    - partition.complete_local_to_global_map/v1
    - partition.local_to_global_ascending/v1
    - partition.overlap/v1
    - partition.centroids/v1
parameter_schema_path: contracts/baseline-v1/schemas/cpp-main-p0-parameters.schema.json
test_only: false
~~~

The P0 parameter schema accepts only the frozen values needed by the callable: M, R from which
local degree is derived, source-derived sample probability, and the fixed replication/mode fields.
It does not expose P0 random seeds or a tunable initial Partition count.

### 3.2 B0

~~~yaml
implementation_id: cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1
implementation_version: 1.0.0
stage: local_build
requires:
  input_artifacts:
    - artifact_type: dataset
      accepted_format_versions: [diskann-float32-dataset/v1]
      input_id_space: none
    - artifact_type: partition
      accepted_format_versions: [partition-artifact/v1]
      input_id_space: local
  capabilities:
    - dataset.diskann_float32_bin/v1
    - partition.complete_local_to_global_map/v1
    - partition.local_to_global_ascending/v1
  runtime_services:
    - vector_accessor/v1
    - cancellation_token/v1
    - metrics_sink/v1
provides:
  artifact_type: local_graph
  format_version: local-graph-artifact/v1
  output_id_space: local
  capabilities:
    - graph.local_adjacency/v1
parameter_schema_path: contracts/baseline-v1/schemas/cpp-main-b0-parameters.schema.json
test_only: false
~~~

The executor passes the aggregate PartitionArtifact and exact PartitionId. Dataset availability is
preflighted as a multi-input requirement and bound by the Feature-local runner to a private
cpp_main float32 VectorAccessor carrying the exact validated native payload identity. B0 verifies
that private binding before using its confined read-only path as the upstream filename argument.
No method is added to the public VectorAccessor. The detailed callable behavior is authoritative in
b0-callable-wrapper.md.

### 3.3 M0

~~~yaml
implementation_id: cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1
implementation_version: 1.0.0
stage: merge
requires:
  input_artifacts:
    - artifact_type: partition
      accepted_format_versions: [partition-artifact/v1]
      input_id_space: local
    - artifact_type: local_graph
      accepted_format_versions: [local-graph-artifact/v1]
      input_id_space: local
  capabilities:
    - partition.complete_local_to_global_map/v1
    - partition.local_to_global_ascending/v1
    - graph.local_adjacency/v1
  runtime_services:
    - cancellation_token/v1
    - metrics_sink/v1
provides:
  artifact_type: global_graph
  format_version: global-graph-artifact/v1
  output_id_space: global
  capabilities:
    - graph.global_adjacency/v1
    - graph.medoids/v1
parameter_schema_path: contracts/baseline-v1/schemas/cpp-main-m0-parameters-v1.1.0.schema.json
test_only: false
~~~

ContractRequirement matches both input artifact kinds before factory creation. It does not add
cardinality fields; complete PartitionId coverage is an M0 semantic input check after every input
Artifact has independently passed contract validation and before merge_shards is called.
The prior `cpp-main-m0-parameters.schema.json` remains the frozen historical parameter contract.
The reusable M0 descriptor selects revision 1.1.0; the fixed Baseline runner independently
enforces `R=16` and `use_filters=false` before workspace or stage creation.

## 4. P0 callable boundary

Required sequence:

1. validate Source/Build/Dataset identity and P0 parameters;
2. derive sample probability min(1,256000/N) and local degree floor(2R/3);
3. call pinned partition_with_ram_budget<float> with M and replication factor 2;
4. read its returned Partition count and resolve only its exact native centroids/maps outputs;
5. validate all outputs using native-formats.md;
6. publish one map-only aggregate PartitionArtifact with centroids.

P0 must not materialize shard vectors or claim temporary B0 vectors as its output. It must not fix
or expose a random seed, rewrite k-means, or retry with a modified M.

## 5. B0 callable boundary

For each explicit PartitionId:

1. select the exact validated Partition record and unchanged complete map;
2. call pinned retrieve_shard_data_from_ids<float> to create a temporary shard matrix;
3. validate exact row identity/order against the Dataset and map;
4. create native IndexWriteParameters using Profile L, floor(2R/3), saturation false, Profile T,
   and unchanged upstream defaults;
5. construct pinned Index<float>;
6. call Index::build(filename,count);
7. call Index::save(prefix);
8. validate native graph and .data;
9. publish one LocalGraphArtifact with owning PartitionId and exact Dataset/Partition/map lineage.

The B0 BuildManifest records both the Dataset ArtifactRef and aggregate Partition ArtifactRef as
inputs. A generic VectorAccessor without the validated native cpp_main binding is insufficient for
this implementation and fails before the retrieval call.

The local graph is accepted only structurally and contractually. No byte or adjacency comparison
with a graph created inside the top-level native composite is required.

## 6. M0 callable boundary

Required sequence:

1. validate one aggregate PartitionArtifact and every LocalGraphArtifact independently;
2. require `partition.local_to_global_ascending/v1` and verify every actual map is in ascending
   GlobalNodeId order; verify exactly one local graph for every PartitionId and exact
   Dataset/source/map lineage;
3. stage maps and local graphs byte-for-byte under stage-temporary filenames accepted by the pinned
   callable;
4. call pinned merge_shards with complete inputs, Partition count, and explicit Merge R;
5. validate merged global graph and medoids using native-formats.md;
6. publish one GlobalGraphArtifact containing both required payloads.

The pinned merge traverses sorted `(GlobalNodeId, shard)` pairs while reading each shard's graph
rows sequentially (SM-025/SM-027). Therefore M0 requires both the ascending capability and its
truthfulness, including when the wrapper is invoked directly; rejection precedes native staging
and the `merge_shards` call. The public Artifact validator checks actual order for native and JSON
maps only when ascending is declared. Generic Partitions need not be ascending, and M0 still
accepts only its reviewed native encoding. No map sorting, node renumbering, or graph rewriting is
performed to adapt an incompatible input.

Physical staging is not an Adapter: it preserves bytes, logical record order, adjacency order,
checksums, and provenance. Any needed logical conversion beyond the native callable's own
LocalNodeId-to-GlobalNodeId merge behavior is outside this contract.

M0 semantics are exactly map reading, local-to-global relabeling, replica candidate union,
deduplication, native random shuffle, and truncation to R. M0 does not read vector coordinates,
compute distances, call RobustPrune, perform global refinement, or repair connectivity.

The callable M0 exposes positive R as its merge-output bound; this is a parameter of the same
pinned merge_shards algorithm, not a new Merge implementation. The frozen Feature 002 Baseline
Profile and runner still pass R=16, and B0 still derives local degree 10. Those frozen values do
not define a universal input-degree limit for reusable M0.

## 7. Validation and publication gates

### Before factory creation

Reject:

- source/gitlink/commit/clean-state mismatch;
- missing, unvalidated, wrong-version, wrong-ID-space, wrong-capability, wrong-checksum, or
  wrong-Dataset input;
- missing/invalid parameter schema or invalid normalized parameters;
- an incompatible implementation chain or missing StageContext service.

Factory and stage invocation counts remain zero.

### Before each downstream stage

- P0 output must be a valid aggregate map-only PartitionArtifact with centroids.
- Every B0 input PartitionId must exist; every B0 output must be valid before selecting another
  Partition or allowing M0.
- M0 receives only the exact complete validated input set.

### Before normal publication

The implementation validates exact payload lengths/checksums, aggregate identity, structural
claims, ID ranges, degree bounds, capabilities, and lineage. cpp_main M0 additionally requires
medoids and includes their identity in the aggregate checksum.

Failure or cancellation publishes no normal Artifact and starts no downstream stage. Existing
quarantine and BuildManifest terminal-status rules apply.

## 8. Cancellation boundary

Each wrapper checks the existing CancellationToken at stage start, after wrapper-controlled input
resolution/validation, after every non-interruptible upstream callable returns, and before output
publication. The Feature does not patch upstream, add a new IPC mechanism, or claim interruption
inside a running upstream call.

## 9. Evidence boundary

The first-round evidence is:

- implementation selection and complete requires/provides admission;
- exact pinned upstream callable sequence;
- native-format and structural validation;
- complete Artifact lineage and terminal BuildManifest;
- one SIFT1M sharded Unified closure with at least two Partitions;
- unchanged upstream before and after.

It excludes Native-versus-Unified graph comparison, a Graph Parity result, repeated-run statistics,
Recall/QPS/latency, production-readiness, and validated_graph_baseline registration.

## Validated physical input admission

`register_cpp_main_stage_bindings` binds `b0_input_admission` and `m0_input_admission`.
B0 checks the actual `CppMainVectorAccessor` type with RTTI and every known Partition map's
existing native uint32 media type. M0 checks all known Partition maps and each LocalGraph map
and native graph encoding independently. Shared metadata helpers live in `native_formats.cpp`;
callbacks do not read payloads or run algorithms. Existing independent validators and direct
stage checks remain authoritative for structure and execution safety.

Known inputs are incremental as specified by the public Registry API. New outputs are checked
after VALID publication and producer success, reused inputs after fresh validation, and merge-only
inputs before Merge factory creation. Legal JSON maps/graphs remain valid Artifacts, but these
consumers reject them before their factories. No new persisted label, format version, algorithm,
ID, checksum rule or M0 semantic is introduced. Existing native artifacts need no rewrite.
Manual factories wrapping B0/M0 must bind the corresponding admission callback.

## Composition-only P0/B0 1.1.0 extension

The existing IDs retain their frozen 1.0.0 descriptors and factories. Only the composition entry
additionally registers P0/B0 1.1.0, with the same requires/provides and B0 input admission.
`register_cpp_main_stage_bindings()` still registers exactly P0/B0/M0 1.0.0; M0 is unchanged.
The new parameter schemas are `contracts/baseline-v1/schemas/cpp-main-p0-parameters-v1.1.0.schema.json`
and `contracts/baseline-v1/schemas/cpp-main-b0-parameters-v1.1.0.schema.json` (repository-relative).
The shared version-aware parser is authoritative for integer ranges and derived arithmetic.
P0 accepts R; B0 accepts R/L; all other frozen rules and native call sequences remain in force.
Both derive floor(2R/3) after widening R; R is an integer in [2, UINT32_MAX], L in [1, UINT32_MAX].
B0 uses that bound for native construction, saved graph validation and Artifact declaration, and
its effective filter_list_size equals L. No all-stage equal-R compatibility rule is introduced.
The existing Artifact contracts and validators require no version change. New configurations
carry exact implementation versions, effective parameters and source/build evidence; they do not
inherit any certification of the frozen profile.
