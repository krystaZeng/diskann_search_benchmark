# cpp_main Native Format Contract

This document defines only the native byte layouts and structural checks needed by Feature 002 at
the pinned cpp_main commit 78256bbab4685e1774e78d331e081a153be26823 on the normative Linux
amd64 little-endian platform.

It is a Baseline-local implementation contract. It does not create a public Artifact version,
generic binary matrix abstraction, sidecar framework, or Adapter. Public Artifact metadata,
identity, checksum, ID-space, capability, and lineage rules remain those of Feature 001.

## 1. Dispatch and identity rules

The internal Artifact validator selects these readers only when all of the following match:

1. the expected public Artifact type and format version;
2. the exact cpp_main payload role and reviewed media type;
3. the pinned Baseline Source Identity;
4. the implementation descriptor that declared the format/capability.

An unknown media type or another source identity fails closed. A media type match never bypasses
payload SHA-256, byte length, aggregate checksum, source lineage, ID-space, or capability checks.

| Logical use | Artifact format | Payload role | Baseline-local media type |
|---|---|---|---|
| Dataset | diskann-float32-dataset/v1 | vectors | application/vnd.diskann.cpp-main.float32-bin;version=1 |
| P0 map | partition-artifact/v1 | implementation-declared map role | application/vnd.diskann.cpp-main.uint32-id-map;version=1 |
| P0 centroids | partition-artifact/v1 | centroids | application/vnd.diskann.cpp-main.float32-centroids;version=1 |
| B0 local graph | local-graph-artifact/v1 | local_graph | application/vnd.diskann.cpp-main.local-graph;version=1 |
| M0 merged graph | global-graph-artifact/v1 | global_graph | application/vnd.diskann.cpp-main.global-graph;version=1 |
| M0 medoids | global-graph-artifact/v1 | medoids | application/vnd.diskann.cpp-main.uint32-medoids;version=1 |

The B0 .data companion and reconstructed shard vectors use the Dataset matrix reader as temporary
lifecycle files. They do not acquire Artifact payload roles.

## 2. Common parsing rules

- Read exact-width fields without reinterpret-cast alignment assumptions.
- Perform every multiplication/addition with checked arithmetic before comparing file length.
- Require the exact expected file length; truncation and trailing bytes are invalid.
- Interpret native scalar/ID fields according to the normative little-endian Linux amd64 build.
- Reject a header value that cannot be represented by the public count or ID type.
- Do not normalize vectors, sort maps, sort adjacency, deduplicate records, or repair malformed data.
- Return parsed structural facts to the validator; do not publish an Artifact inside a reader.

## 3. DiskANN float32 Dataset and matrix form

Byte layout:

~~~text
offset 0: little-endian uint32 npts
offset 4: little-endian uint32 dim
offset 8: npts * dim little-endian IEEE-754 float32 values, row-major
~~~

Validation:

- npts and dim are positive and equal the bound DatasetIdentity for the Dataset Artifact;
- expected bytes equal 8 + npts * dim * 4 using overflow-safe arithmetic;
- every scalar is finite;
- Dataset payload SHA-256 equals DatasetIdentity.dataset_checksum.

For SIFT1M, npts=1,000,000, dim=128, and the exact byte length is 512,000,008.
Raw fvecs with a dimension prefix per row is not this format.

The same physical matrix layout is used for B0 shard vectors and the Index::save .data companion.
For those temporary files, npts equals selected map length and dim equals Dataset dimension. Under
the scoped L2 path, .data rows must be byte-equivalent to the validated shard-vector rows.

## 4. Partition ID map

Byte layout:

~~~text
offset 0: little-endian uint32 row_count
offset 4: little-endian uint32 dim, required to equal 1
offset 8: row_count little-endian uint32 GlobalNodeIds
~~~

Validation:

- exact byte length is 8 + row_count * 4;
- row_count equals the owning Partition record local_node_count;
- payload position i is LocalNodeId i;
- every GlobalNodeId is less than Dataset global_node_count;
- map order is retained exactly and is not inferred from a filename;
- the aggregate P0 set has exactly one map for every contiguous PartitionId;
- for cpp_main P0 with replication factor 2, every Dataset GlobalNodeId occurs in exactly two
  Partition maps.

Duplicate GlobalNodeIds across different Partitions are expected. A duplicate within one map is
invalid for this P0 output.

## 5. Centroids

Centroids use the float32 matrix layout in section 3.

Validation:

- npts equals the P0-returned Partition count;
- dim equals Dataset dimension;
- all values are finite;
- exact byte length and payload checksum match metadata.

Centroids are required for the cpp_main P0 descriptor even though the public PartitionArtifact
allows this payload to be absent for other implementations.

## 6. Local graph

For the normative pinned Linux amd64 build, the Index::save graph contains:

~~~text
little-endian uint64 declared_file_size
little-endian uint32 maximum_observed_degree
little-endian uint32 local_entry_point
little-endian uint64 frozen_point_count
then, for each expected local node:
  little-endian uint32 degree
  degree little-endian uint32 LocalNodeIds
~~~

The node count is supplied by the selected map/Artifact; it is not a separate graph-header field.

Validation:

- declared_file_size equals actual file length;
- frozen_point_count equals zero for the scoped static build;
- exactly local_node_count adjacency records are consumed with no trailing bytes;
- local_entry_point is less than local_node_count;
- every neighbor is less than local_node_count;
- every degree and the header maximum are at most floor(2R/3);
- the maximum parsed degree equals the Artifact structural claim;
- the retained map payload/checksum is exactly the selected P0 map.

No canonical neighbor ordering is imposed and no adjacency is rewritten.

## 7. Local graph .data companion

The .data file is parsed with section 3 using the selected local_node_count and Dataset dimension.
It must have the same logical rows and exact row bytes as the validated B0-preparation shard
vectors. Its path, byte length, SHA-256, creation time, validation result, and cleanup outcome are
recorded in the BuildManifest.

It is not a LocalGraphArtifact payload, is not required by merge_shards, and is not published for
downstream reuse.

## 8. Merged global graph

The merged graph uses the graph layout in section 6, interpreted in GlobalNodeId space.

Validation:

- declared_file_size equals actual length;
- exactly Dataset global_node_count adjacency records are consumed;
- frozen_point_count is the pinned unfiltered expected value;
- the header entry point and every neighbor are valid GlobalNodeIds;
- every degree and header maximum are at most Profile R;
- the maximum parsed degree equals the Artifact structural claim;
- there are no trailing bytes.

These checks do not add distance pruning, RobustPrune, global refinement, or connectivity repair.

## 9. Medoids

Medoids use the uint32 matrix envelope:

~~~text
little-endian uint32 row_count
little-endian uint32 dim, required to equal 1
row_count little-endian uint32 GlobalNodeIds
~~~

Validation:

- row_count equals the complete M0 input Partition count;
- exact byte length is 8 + row_count * 4;
- every medoid is less than Dataset global_node_count;
- each value equals the corresponding local graph entry point mapped through that Partition's exact
  LocalToGlobalIdMap;
- the merged graph header entry point is one of the medoids.

The cpp_main M0 descriptor requires graph.medoids/v1 and cannot publish a graph-only output. Both
global_graph and medoids payload identities enter the existing aggregate checksum projection.

## 10. Failure behavior

Any format, range, length, checksum, lineage, or cross-file consistency failure rejects the
candidate before normal publication. The candidate follows existing invalid-output/quarantine
rules, cannot enter a downstream stage, and is never automatically rewritten into a valid file.
