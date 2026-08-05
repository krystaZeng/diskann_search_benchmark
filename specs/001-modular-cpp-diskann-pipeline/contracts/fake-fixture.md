# Canonical Fake Pipeline Fixture v1

This contract freezes the exact positive fixture required by FR-045 through FR-047. Every payload
is the UTF-8 byte sequence shown between quotation marks below, excluding the quotation marks and
with no trailing newline. JSON key and array order are part of the raw payload bytes.

## 1. Raw Payloads

| Role | Exact UTF-8 content | Bytes | SHA-256 |
|---|---|---:|---|
| `vectors` | `{"vectors":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]}` | 49 | `2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b` |
| `partition/0/vectors` | `{"vectors":[[0,0],[1,0],[2,0],[0,1]]}` | 37 | `a61d72902e4771175bf0767a13cc9006dd129ea92439fc092d167b632d35437c` |
| `partition/0/local_to_global_map` | `{"local_to_global":[0,1,2,3]}` | 29 | `76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c` |
| `partition/1/vectors` | `{"vectors":[[2,0],[0,1],[1,1],[2,1]]}` | 37 | `119a82674838210f5d056a5f1b784d91e11e84bf2b4113afa236a70736abd242` |
| `partition/1/local_to_global_map` | `{"local_to_global":[2,3,4,5]}` | 29 | `90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c` |
| `local_graph` for P0 and P1 | `{"adjacency":[[1,2],[0,2],[1,3],[]]}` | 36 | `e54ec8ef5014c15c861cef23ea8aa2edddd1a587b1a30614abd9b0a4defbeddd` |
| `global_graph` | `{"adjacency":[[1,2],[0,2],[1,3,4],[2,4],[3,5],[]]}` | 50 | `9cbfeade7ef24a5b7b1911da4ff7baead15b7444e338e3b0a5ee42ed780d622f` |

`dataset_checksum` equals the exact `vectors` payload SHA-256. It is not a filename or a checksum
of YAML configuration.

## 2. Ordered Artifact Inputs

Canonical capabilities and payload/source order are:

| Artifact | Capabilities | Payload role order | Source Artifact order |
|---|---|---|---|
| Dataset | `dataset.vector_access/v1` | `vectors` | empty |
| Partition | `partition.complete_local_to_global_map/v1`, `partition.overlap/v1` | P0 vectors, P0 map, P1 vectors, P1 map | Dataset |
| Local P0 | `graph.local_adjacency/v1` | P0 map as `local_to_global_map`, `local_graph` | Partition |
| Local P1 | `graph.local_adjacency/v1` | P1 map as `local_to_global_map`, `local_graph` | Partition |
| Global | `graph.global_adjacency/v1` | `global_graph` | Partition, Local P0, Local P1 |

Source identities omit paths and contain only Artifact type, format version, and aggregate
checksum. Capabilities are already in canonical UTF-8 order.

## 3. Structural Projections

- Dataset: node count `6`, dimension `2`, scalar `float32`, metric `l2`.
- Partition: count `2`; P0 record `(id=0, local_count=4, P0 vector role, P0 map role)`; P1 record
  `(id=1, local_count=4, P1 vector role, P1 map role)`.
- Local P0/P1: owning PartitionId `0`/`1`, local count `4`, declared and observed degree `2`, and
  the exact Partition aggregate checksum.
- Global: node count `6`, declared and observed degree `3`, and exact Partition/Local source
  identities in the order above.

Objects are serialized with recursively sorted keys under `diskann-artifact-c14n-v1`; arrays retain
the orders declared here.

## 4. Aggregate Golden Values

| Artifact | Format version | Aggregate SHA-256 |
|---|---|---|
| Dataset | `fake-dataset-json/v1` | `4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d` |
| Partition | `partition-artifact/v1` | `a640037c2d07b2dcdff98cd22b82b5c4f5da7e6323fc8da2a61538c1cbbb2984` |
| Local P0 | `local-graph-artifact/v1` | `31d93424d2d0a826891c5d2cc6d3b5b7da6f646a9c6f878603d532ddfae0c9c1` |
| Local P1 | `local-graph-artifact/v1` | `ca02d15d4709d62aea8f404de57d2143be1205f6fa517b4d2d550fe2dab265f6` |
| Global | `global-graph-artifact/v1` | `443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe` |

Any implementation change that alters these bytes or projections must deliberately version the
Fake fixture and update both language golden tests. Tests must also prove that changing only paths,
timestamps, BuildManifest, EvaluationReport, or MetricsSink data leaves these aggregates unchanged.

## 5. Expected Graph Properties

- Every map has length `4`, all map values are in `0..5`, and GlobalNodeIds `2` and `3` overlap.
- Every local neighbor is in `0..3`; local observed maximum degree is `2`; LocalNodeId `3` has an
  explicitly present empty adjacency list.
- The Global graph has exactly six adjacency lists, only neighbors in `0..5`, maximum degree `3`,
  and an explicitly present empty adjacency list for GlobalNodeId `5`.
- The exact Global adjacency is `0:[1,2]`, `1:[0,2]`, `2:[1,3,4]`, `3:[2,4]`, `4:[3,5]`,
  `5:[]`.

These Merge rules and golden values belong only to `fake_merge_v1`; they do not describe the fixed
cpp_main Baseline and are not Adapter semantics.
