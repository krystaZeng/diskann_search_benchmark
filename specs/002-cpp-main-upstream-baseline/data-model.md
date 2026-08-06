# Data Model: cpp_main B0 Upstream Callable Wrapper

This model narrows Feature 002 to one independently callable B0. Existing Feature 001 identifier,
ArtifactRef, BuildManifest, validation, quarantine, and checksum semantics remain authoritative.
The reviewed v1-compatible amendment makes Partition shard vectors optional and permits optional
centroids/medoids without changing the Local Build signature.

## 1. B0 Stage Decision

| Field | Fixed value |
|---|---|
| `separability` | `separable` |
| `binding_mode` | `upstream_callable_wrapper` |
| `validation_scope` | `structural_and_contract` |
| source commit | `78256bbab4685e1774e78d331e081a153be26823` |
| implementation state | planned, not implemented, not registered as `validated_graph_baseline` |

## 2. Input Entities

### 2.1 DatasetBinding

| Field | Rule |
|---|---|
| DatasetArtifactRef | Validated immutable pipeline-root reference; never a raw path hidden in parameters |
| DatasetIdentity | Must match the selected PartitionArtifact and output LocalGraphArtifact |
| payload format | Native DiskANN float32 binary: two little-endian `uint32` header fields followed by `npts * dim` finite little-endian float32 values |
| payload identity | Exact byte length and SHA-256 validated before factory creation |
| scope | float32, L2, unfiltered only |

The B0 invocation receives Dataset vector access through the validated Dataset-backed
`StageContext::vector_accessor`. If its selected Partition record already references shard vectors,
those may be used directly. Otherwise the VectorAccessor and complete map are mandatory; missing
both sources rejects preparation. The temporary vectors are not P0 output.

The SIFT1M validation Dataset has `global_node_count=1000000`, `dimension=128`, byte length
`512000008`, and SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`.

### 2.2 PartitionSelection

| Field | Rule |
|---|---|
| PartitionArtifactRef | Validated aggregate PartitionArtifact, not a shard ArtifactRef |
| PartitionId | Explicit; in range and present as an exact Partition record |
| `local_node_count` | Positive and equal to selected map row count |
| LocalToGlobalIdMap | Complete positional map from every LocalNodeId to one in-range GlobalNodeId |
| map checksum | Exact payload checksum retained in LocalGraphArtifact lineage |
| order capability | Existing order must be consumable by pinned `retrieve_shard_data_from_ids`; native P0 maps are ascending in Dataset traversal order |

The wrapper never derives PartitionId from an array position or filename and never sorts, deduplicates,
or relabels the map. Duplicate GlobalNodeIds across different Partitions remain legal. A selected map
that cannot be reconstructed in its existing order is invalid for this B0 implementation.

### 2.3 B0Parameters

| Field | SIFT1M value | Rule |
|---|---:|---|
| `R` | 16 | Profile final degree used only to derive local degree |
| `L` | 32 | Passed as upstream build search-list size |
| `T` | 6 | Passed to upstream `.with_num_threads(T)` |
| `local_degree` | 10 | Derived as integer `floor(2R/3)`; not independently tunable |
| `filter_list_size` | 32 | Derived by native `.with_filter_list_size(0)` from Profile `L`; operationally unused in the unfiltered build |
| `saturation` | false | Fixed upstream sharded-B0 rule |
| `alpha` | 1.2 | Resolved upstream default; no Feature override |
| `max_occlusion_size` | 750 | Resolved upstream default; no Feature override |
| `num_frozen_points` | 0 | Upstream static-build default |
| scalar/metric/filter/PQ/OPQ | float32/L2/unfiltered/disabled | Fixed Feature scope |

The normalized parameter document records both accepted fields and resolved fixed/derived values.
Unknown fields, missing fields, wrong types, unsupported values, or a mismatch between `R` and
`local_degree` fail before factory creation. Parameters never contain the Dataset payload path.

## 3. Temporary Native Entities

### 3.1 NativeIdMapInput

A private, stage-temporary DiskANN uint32 matrix containing the selected map in unchanged order.
Its header is row count and dimension `1`; its payload checksum is tied to the selected standard map.
It is not an Artifact and cannot be reused independently.

### 3.2 NativeShardVectors

Produced only by pinned `retrieve_shard_data_from_ids<float>` in StageContext temporary storage.
Before graph build it must satisfy:

- header row count equals map length;
- dimension equals Dataset dimension;
- every scalar is finite float32;
- row `i` exactly equals Dataset row `LocalToGlobalIdMap[i]`;
- no normalization, sorting, relabeling, or coordinate transformation occurred.

Failure prevents `Index<float>` construction. The file is deleted at normal terminal cleanup and
is retained only under the existing failure/quarantine evidence policy.

### 3.3 NativeIndexSaveSet

The pinned `Index::save(prefix)` produces a graph file and `.data` companion in temporary storage.
The graph is the candidate LocalGraph payload. The `.data` companion is lifecycle evidence, not a
standard LocalGraphArtifact payload, and is removed at successful cleanup after its identity is
recorded in the BuildManifest.

## 4. Output Entity

### 4.1 B0 LocalGraphArtifact

The existing standard LocalGraphArtifact is retained. Its invariants are:

| Field/property | Validation rule |
|---|---|
| Dataset | Exact DatasetIdentity from DatasetBinding |
| owning PartitionId | Exact explicit input PartitionId |
| LocalToGlobal map | Exact selected map and payload checksum |
| source Partition | Exact aggregate PartitionArtifact identity/checksum |
| local node count | Equals map length and native graph record count |
| node/neighbor ID space | Partition-scoped LocalNodeId only; each neighbor `< local_node_count` |
| declared maximum degree | `floor(2R/3)` |
| observed maximum degree | Parsed from every record and no greater than declared maximum |
| graph payload format | Exact pinned `Index::save` local-graph binary, selected by an explicit reviewed media type |
| graph checksum | SHA-256 of exact saved graph bytes |
| capabilities | Local adjacency plus complete-map/order capability actually validated |

Dataset lineage is present in the Artifact's DatasetIdentity, transitively through the exact source
PartitionArtifact, and directly in the BuildManifest input references. Partition lineage is the
owning ID plus source aggregate checksum. Map lineage is the retained map payload/checksum. No
native-versus-wrapper equivalence claim participates in Artifact identity.

## 5. Separate Provenance and Evidence

### BuildManifest

Records pinned upstream and parent repository identities, clean/dirty state, compiler/CMake/build
identity, linked upstream library checksum, complete normalized B0 parameters, both input
ArtifactRefs, PartitionId, temporary lifecycle, exact callable sequence, timestamps, output
ArtifactRef, and terminal status. It remains separate from Artifact metadata.

### ValidationEvidence

Records every input, shard-order, native-graph, checksum, ID-range, degree, and lineage check. A
failed candidate is not published as a normal Artifact. No EvaluationReport is needed to establish
the B0 structural gate, and any later evaluation remains a separate object.

## 6. State Transitions

```text
PLANNED
  -> INPUTS_VALIDATED
  -> SHARD_RETRIEVED
  -> SHARD_VALIDATED
  -> INDEX_BUILT
  -> OUTPUT_CANDIDATE
  -> OUTPUT_VALIDATED
  -> PUBLISHED
```

Any input failure terminates before factory creation. Any later failure transitions to the existing
FAILED, CANCELLED, or INVALID_OUTPUT path, publishes no normal Artifact, records terminal evidence,
and invokes no downstream stage. Publication is atomic only after output validation.

The wrapper checks cancellation at its own safe boundaries: stage start, after input validation,
after shard validation, immediately after each upstream retrieval/build/save call returns, and
before publication. It does not claim cooperative interruption inside `Index::build` and does not
modify upstream to add one.

## 7. Comparison Projection

B0 acceptance compares only:

- input and lineage identities;
- shard row count, dimension, finiteness, and row-to-map equality;
- graph node count, LocalNodeId ranges, and degree bound;
- Artifact format/version/checksum/capability validity.

It explicitly excludes graph byte equality with Native Probe output, adjacency equality, process
state, thread scheduling, timings, run-to-run determinism, repeated-run aggregation, and any Recall,
QPS, latency, or full Graph Parity conclusion.
