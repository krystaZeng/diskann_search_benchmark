# B0 Upstream Callable Wrapper Contract

This document defines the minimal Feature 002 B0 design contract. The public v1 representation
amendment is approved: Partition shard vectors are optional, while the Local Build signature and
StageContext service boundary remain unchanged.

## 1. Identity and Scope

```yaml
study_class: baseline_validation
reproduction_mode: upstream_faithful
replacement_scope: none
upstream:
  repository: https://github.com/microsoft/DiskANN.git
  branch: cpp_main
  commit: 78256bbab4685e1774e78d331e081a153be26823
b0:
  separability: separable
  contract_status: compatible
  availability: planned
  binding_mode: upstream_callable_wrapper
  validation_scope: structural_and_contract
```

The wrapper is an orchestration boundary around unchanged upstream callables. It is not a Vamana
implementation, Adapter, deterministic variant, Native Graph Reference, or validated full Baseline.

## 2. Public Stage Input Boundary

Local Build retains the existing C++17 execution interface:

```cpp
StageResult build(
    const ArtifactRef& partition_artifact,
    PartitionId partition_id,
    const LocalBuildConfig& config,
    StageContext& context);
```

All types remain in the C++17 public contract and expose no upstream or C++20 type. The
PartitionArtifact remains aggregate; no `partition_shard` ArtifactRef is introduced. PartitionId is
explicit. Each map is mandatory and the public contract permits optional `shard_vectors_role`.
This cpp_main binding nevertheless uses the validated Dataset-backed
`StageContext::vector_accessor` and pinned retrieval callable in exact map order for every
invocation. The Feature-local runner binds a private cpp_main float32 accessor to the exact
validated native Dataset payload/checksum; the wrapper verifies that private concrete binding and
uses its confined read-only path as the upstream filename argument without changing the public
VectorAccessor interface. It does not bypass `retrieve_shard_data_from_ids` when a different
producer includes shard vectors. Missing or differently bound Dataset access fails before upstream
work. Passing a Dataset path in implementation parameters or finding it through process-global
state is forbidden.

The planned Registry declaration is:

```yaml
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
  capabilities: [graph.local_adjacency/v1]
parameter_schema_path: contracts/baseline-v1/schemas/cpp-main-b0-parameters.schema.json
test_only: false
```

This descriptor is exercised in the non-installed B0 validation assembly. This plan does not add
it to the installed production runner and does not register `validated_graph_baseline`.

## 3. Required Inputs

1. A validated float32/L2 DatasetArtifact whose native DiskANN binary payload, header, byte length,
   finite values, and checksum match its DatasetIdentity, exposed to the stage through its bound
   VectorAccessor.
2. A validated aggregate PartitionArtifact with the same DatasetIdentity and a mandatory complete
   map for every record; a selected shard-vector payload is optional.
3. An explicit PartitionId in range and present as a selected Partition record.
4. The selected record's complete LocalToGlobalIdMap, with length equal to local node count and
   every GlobalNodeId in Dataset range.
5. A normalized parameter document validated before factory creation.

The cpp_main binding materializes vectors through the Dataset VectorAccessor and pinned
`retrieve_shard_data_from_ids` path in map order. This materialization is B0 preparation, not P0
output, and the temporary file cannot be labeled as a P0 shard-vector payload. The generic public
contract remains backward-compatible with other Partition producers that include shard vectors;
this binding does not use that optional shortcut.

Because the pinned retrieval callable streams Dataset rows in ascending GlobalNodeId order, B0 also
requires a capability asserting that the selected map is already in that consumable order. The
wrapper validates the actual payload and never trusts the capability alone. It never sorts,
deduplicates, or relabels the map.

## 4. Parameter Contract

The caller supplies `R`, `L`, and `T` plus explicit fixed mode values for float32, L2, unfiltered,
labels disabled, build PQ disabled, and OPQ disabled. The implementation derives and records:

- `local_degree = floor(2R/3)`;
- unfiltered `with_filter_list_size(0)` resolves the stored filter list size to Profile `L`;
- `saturation = false`;
- upstream default `alpha = 1.2`;
- upstream default `max_occlusion_size = 750`;
- upstream static `num_frozen_points = 0`.

The derived/default fields are not user overrides. The SIFT1M structural profile freezes `R=16`,
`L=32`, `T=6`, hence `local_degree=10`. No parameter search or automatic retry is allowed.

## 5. Required Call Sequence

The implementation must perform this sequence without substituting project-owned algorithm code:

1. write the selected map, unchanged, in the pinned native uint32 ID-map format under stage
   temporary storage;
2. call pinned upstream `retrieve_shard_data_from_ids<float>(dataset, id_map, shard_file)`;
3. validate the shard header, count, dimension, finiteness, and row equality to the Dataset/map;
4. build `IndexWriteParameters` from `L` and `floor(2R/3)`, call
   `with_filter_list_size(0)`, set saturation false and `T`, and leave alpha/max occlusion at
   upstream defaults;
5. construct pinned upstream `Index<float>` for L2, full-precision, unfiltered, non-dynamic,
   tags-disabled, zero-frozen-point operation;
6. call `Index::build(shard_file, map_length)`;
7. call `Index::save(temporary_prefix)`;
8. validate the graph candidate and atomically publish the standard LocalGraphArtifact.

The reconstructed shard and saved `.data` companion are temporary lifecycle files. Only the exact
map and native graph are LocalGraphArtifact payloads. Cleanup never writes under upstream.

## 6. Fail-Closed Gates

Before factory creation, reject a missing/invalid Dataset or Partition Artifact, incompatible
format/version/ID space/capability, Dataset mismatch, missing/out-of-range PartitionId, malformed or
incomplete map, out-of-range GlobalNodeId, invalid parameter document, or missing schema.

Before `Index::build`, reject a failed upstream retrieval, wrong shard header/count/dimension,
non-finite scalar, or any row that differs from the Dataset vector named by the corresponding map
entry.

Before normal publication, reject a graph whose node count differs from map length, whose neighbor
is not a valid LocalNodeId, whose observed maximum degree exceeds `floor(2R/3)`, whose checksum is
wrong, or whose Dataset/Partition/map lineage is incomplete or inconsistent. Failure publishes no
normal Artifact and cannot trigger a downstream stage.

## 7. Output and Lineage

The output is `LocalGraphArtifact` in the existing standard LocalNodeId space. It records the owning
PartitionId, complete exact map payload/checksum, local node count, declared and observed degrees,
source PartitionArtifact checksum/identity, DatasetIdentity, graph checksum, and capabilities. The
BuildManifest separately records both input ArtifactRefs, source/build identity, exact parameters,
temporary lifecycle, timings, and terminal status.

The `local_graph` payload is the exact pinned `Index::save` graph binary with a reviewed native
media type. A format-specific semantic parser validates it; the wrapper does not translate it to
the Fake JSON representation. The existing LocalGraphArtifact metadata shape and aggregate checksum
projection remain unchanged.

The wrapper observes the existing CancellationToken before native work, between wrapper-controlled
steps, immediately after each upstream call returns, and before publication. There is no claim that
`Index::build` is cooperatively interruptible internally, and this contract authorizes no upstream
instrumentation or new cancellation IPC.

## 8. Explicitly Excluded Evidence

The B0 gate does not require or imply:

- byte-identical local graph output with the Native Probe;
- adjacency equality with a graph built inside the top-level composite process;
- equivalent process state, allocation history, OpenMP interleaving, or thread scheduling;
- repeated runs, determinism statistics, Graph Parity, Recall, QPS, or latency;
- proof that P0/M0 executed successfully, formal Graph Parity, or
  `validated_graph_baseline` registration.

## Registry admission boundary

B0's registered factory is paired with `b0_input_admission`: the actual accessor must be a
`CppMainVectorAccessor`, and each known Partition map must have the already validated native
uint32 encoding. Generic accessors may satisfy the public vector service contract but do not
satisfy this implementation's concrete dependency. No new accessor capability or Artifact tag
is added. Dataset binding and the existing direct B0 defenses remain unchanged; legacy native
artifacts and parameters remain usable after affected components are rebuilt.
