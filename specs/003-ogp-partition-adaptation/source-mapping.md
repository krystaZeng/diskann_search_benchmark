# Source Mapping: GP-ANN OGP Partition

This file stores symbol-level facts only. Design conclusions, integration choices and contract
effects are in [research.md](research.md).

## Source identity and audit scope

Formal source selected by the read-only official repository audit:

```yaml
repository: https://github.com/larsgottesbueren/gp-ann.git
branch: main
commit: df5efb4a6b303011c795666ed267f29d994b558c
```

Recursive gitlinks at that commit:

```yaml
parlaylib: ea671f6fbae17e6d6cd25fe9b48134d3c0601787
kaminpar: a9e4b1f6d92b4110d4d4a31ee2e7dee5a0761b05
hnswlib: 359b2ba87358224963986f709e593d799064ace6
message_queue: f9b0856dde02248cc91a63077f7b6d5267004f25
```

The local audit candidate has no independent `.git`, resolves into an unrelated parent repository,
and has empty submodule directories. Candidate key-file SHA-256 values are retained only to identify
the bytes that were read:

| Candidate file | SHA-256 |
|---|---|
| `partition.cpp` | `95ce18800486759590ca82fbcd7b6b1d0dce672074f8246d7b48b1695b4095d4` |
| `src/overlapping_partitioning.cpp` | `d6a13630a06152157b75fc945aef6f0c19eff9d0b0169c9af60deb2f89436a82` |
| `src/partitioning.cpp` | `10c09397aff0b0c691c39e747da4aacc11e772932757269720387f2274523f45` |
| `src/points_io.cpp` | `bf00e644477a7d3d6e53fe175226bbcc7d6535422eb67ce08d7a40c9cd2696fe` |
| `src/metis_io.cpp` | `febcc2215f29181242560e665352068dad5cac22c7e3c0d0b210d4952e0187f1` |
| `CMakeLists.txt` | `f177962ec32b53772bc4be6d81a9e33756b2e753e2815cec01ce0b3dd093c762` |
| `.gitmodules` | `3c1011c37432dd7099dff7a176da48d3ba56b260d5efe9afacd122d665f3e6f5` |

Candidate files match the selected commit except candidate `partition.cpp:146-147`, whose
`WRITE clusters.size()` diagnostic is not in official history. No fact sourced only from that print
is required by the integration.

Line references below use the audited source layout; implementation must recheck them against the
clean formal checkout before build admission.

## Entry points and CLI

| Symbol/location | Fact |
|---|---|
| `partition.cpp:36-40`, `main` | CLI requires 6 or 7 argv entries and prints `Partition input-points output-path num-clusters partitioning-method (default|strong) [overlap]` on mismatch. |
| `partition.cpp:42-47` | `argv[1]` is input, `argv[2]` output prefix, `argv[3]` is parsed by `stoi` as requested cluster count, `argv[4]` is method; output begins `<prefix>.k=<argv3>.<method>`. |
| `partition.cpp:49-55` | Config accepts exactly `strong` or `default`; `strong` maps to `true`. |
| `partition.cpp:57-62` | Optional overlap is parsed by `stod`; the exact argv token is appended to output path as `.o=<token>`. |
| `partition.cpp:81-82` | The CLI loads the full PointSet with `ReadPoints` before partitioning. |
| `partition.cpp:84-86` | Method `GP` plus nonzero overlap is internally renamed `OGP`; this implicit path exists but is not the selected Feature argv. |
| `partition.cpp:88` | CLI epsilon is the constant `0.05`. |
| `partition.cpp:111-112` | Explicit method `OGP` calls `OverlappingGraphPartitioning(points, k, eps, overlap, strong)`. |
| `partition.cpp:141-144` | If no Clusters were produced, the CLI converts a single Partition; OGP already returns Clusters; output uses `WriteClusters`. |

## Callable and data types

| Symbol/location | Fact |
|---|---|
| `src/overlapping_partitioning.h:6` | Callable signature is `Clusters OverlappingGraphPartitioning(PointSet&, int requested_num_clusters, double epsilon, double overlap, bool strong)`. |
| `src/defs.h:10-20`, `PointSet` | Holds contiguous `std::vector<float> coordinates`, `size_t d`, `size_t n`; point `i` starts at `coordinates[i*d]`. |
| `src/defs.h:25-32` | `Partition` maps point to one cluster; `Clusters` maps cluster to `vector<uint32_t>` point IDs; `Cover` maps point to potentially several clusters. |
| `src/defs.cpp:30-36`, `ConvertPartitionToClusters` | Iterates point IDs from zero upward and appends each ID to its assigned cluster. Point IDs are PointSet row positions. |
| `src/defs.cpp:39-44`, `ConvertPartitionToCover` | Creates one initial cluster membership for every point in the single Partition. |

## Point input and cluster output

| Symbol/location | Fact |
|---|---|
| `src/points_io.cpp:14-23` | Float reader reads native `uint32 n`, `uint32 d` header. |
| `src/points_io.cpp:31-60` | Allocates `n*d` floats and reads row-major float bytes in Parlay-sized chunks. |
| `src/points_io.cpp:114-123`, `ReadPoints` | Dispatch is suffix-only: `.fbin` for float, `.u8bin`, `.i8bin`; other suffixes throw. |
| `src/metis_io.cpp:40-53`, `ReadClusters` | Reads one text line per cluster and parses whitespace-separated `uint32_t` values; it performs no range, duplicate, empty or coverage validation. |
| `src/metis_io.cpp:56-63`, `WriteClusters` | Writes one line per cluster, each decimal uint32 ID followed by a space, then newline. Empty clusters produce empty lines. |

## OGP cluster-count and overlap path

| Symbol/location | Fact |
|---|---|
| `src/overlapping_partitioning.cpp:50-56` | `max_cluster_size=size_t((1+epsilon)*n/requested_k)`; requested extra assignments=`size_t(overlap*n)`; internal cluster count=`ceil((n+extra)/max_cluster_size)`. |
| `src/overlapping_partitioning.cpp:60-68` | Builds an approximate degree-10 kNN graph; `strong` changes builder FANOUT and REPETITIONS to 5. |
| `src/knn_graph.h:254-265` | Default builder seed is 555, FANOUT 3, REPETITIONS 3 and fixed internal sketch/bucket constants. |
| `src/knn_graph.h:61-68` | Leader sampling uses `std::mt19937(seed)`. |
| `src/knn_graph.h:121-125` | Degenerate recursive buckets use deterministic seed expression `seed + depth + ids.size()` for shuffle. |
| `src/overlapping_partitioning.cpp:70` | OGP passes `min(32, parlay::num_workers())` as the KaMinPar thread argument through `PartitionAdjListGraph`. |
| `src/overlapping_partitioning.cpp:71-72` | Converts the initial KaMinPar Partition into both one-membership-per-point Cover and initial Clusters. |
| `src/overlapping_partitioning.cpp:24-47`, `TopMove` | A target is valid only if not already in `cover[u]`; target capacity and positive affinity are required. |
| `src/overlapping_partitioning.cpp:89-100` | Overlap loop stops with assignments remaining if maximum affinity becomes zero. |
| `src/overlapping_partitioning.cpp:102-128` | Groups best moves by target, caps them by capacity and remaining budget, appends target to Cover and point ID to target Cluster. |
| `src/overlapping_partitioning.cpp:131-137` | Logs remaining assignments; distance-based completion is disabled under `#if false`. |
| `src/overlapping_partitioning.cpp:139-140` | Returns the Clusters as constructed; it does not sort memberships or remove empty clusters. |

## KaMinPar path and workers

| Symbol/location | Fact |
|---|---|
| `src/partitioning.cpp:172-180`, `PartitionGraphWithKaMinPar` | Chooses default or strong KaMinPar context, sets context epsilon, then constructs `kaminpar::KaMinPar(num_threads, context)`. |
| `src/partitioning.cpp:184-189` | Gives CSR graph to KaMinPar and calls `compute_partition(k, ...)`. |
| `src/partitioning.cpp:194-198` | Copies returned KaMinPar block IDs into the point-to-cluster Partition vector. |
| `src/partitioning.cpp:246-249`, `PartitionAdjListGraph` | Symmetrizes/converts adjacency to CSR, then forwards cluster count, epsilon, thread count and strong flag. |
| ParlayLib commit `ea671...`, `parlay/parallel.h:158-175` | First scheduler initialization reads `PARLAY_NUM_THREADS`; absent that variable it uses hardware concurrency. |
| Official CLI output surface | No direct `parlay::num_workers()` or active KaMinPar worker count is emitted. |

## Build surface

| Symbol/location | Fact |
|---|---|
| `CMakeLists.txt:1-2` | Project requires CMake 3.10 and is named `GP-ANNS`. |
| `CMakeLists.txt:7-9` | KaMinPar 64-bit weights and edge IDs default ON; timers default OFF. |
| `CMakeLists.txt:11` | Official project sets C++20. |
| `CMakeLists.txt:13-16` | Adds warnings; Release uses `-mtune=native -march=native -ffast-math`; executable links math. |
| `CMakeLists.txt:19-21` | Adds ParlayLib, KaMinPar and hnswlib subdirectories. |
| `CMakeLists.txt:28` | Defines the official `Partition` executable target. |
| `CMakeLists.txt:46-50` | Targets link Parlay; `Partition` additionally links `kaminpar_shm`. |
| `CMakeLists.txt:54-59` | `MIPS_DISTANCE` defaults OFF and adds a compile definition only when enabled. |
| `CMakeLists.txt:61-68` | `Partition` links tcmalloc when the library is found; absence is logged. |
| `.gitmodules` | Declares ParlayLib, KaMinPar, hnswlib and message-queue source URLs. |
| `README.md` | Requests recursive submodule initialization and suggests removing an hnswlib metrics block; that edit would dirty formal source and is not automatically authorized. |

## Current Pipeline facts used by this Feature

| Symbol/location | Fact |
|---|---|
| `include/diskann_pipeline/pipeline.hpp:22-40` | Current request selects three implementation identities and carries one SourceIdentity. |
| `src/pipeline/pipeline_executor.cpp:348-388` | StageContext, Dataset, descriptor chain and all parameter schemas validate before factory creation. |
| `src/pipeline/pipeline_executor.cpp:427-506` | Partition executes, candidate validates, B0 handoff checks, then publication occurs; invalid output returns before B0. |
| `src/pipeline/pipeline_executor.cpp:509-607` | Executor sorts explicit PartitionIds and runs/validates one Local Build per ID. |
| `src/pipeline/pipeline_executor.cpp:609-700` | M0 runs only after all local outputs validate; final GlobalGraph validation precedes EvaluationReport. |
| `src/pipeline/pipeline_executor.cpp:77-101` | Every stage manifest currently receives `request.source`. |
| `src/baseline/cpp_main/descriptors.cpp:29-47` | B0 requires Dataset plus PartitionArtifact/v1, complete and ascending native maps, and VectorAccessor/cancellation/metrics services. |
| `src/baseline/cpp_main/b0_local_build.cpp:183-219` | B0 selects an explicit Partition record and requires the native map media type and ascending IDs. |
| `src/baseline/cpp_main/m0_merge.cpp:165-210` | M0 accepts contiguous Partition records and loads each native map independently; it has no cross-shard uniqueness check. |
| `upstream/diskann-cpp-main/src/disk_utils.cpp:255-284` | `merge_shards` builds the GlobalNodeId-to-shards relation from all maps, including replicas. |
| `upstream/diskann-cpp-main/src/disk_utils.cpp:403-468` | M0 relabels local neighbors, aggregates replica candidates, deduplicates, shuffles and truncates to R. |
