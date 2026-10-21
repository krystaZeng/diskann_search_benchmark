# cpp_main Upstream Graph Baseline Source Mapping

## Binding and scope

This mapping is authoritative only for the following immutable source identity:

| Field | Value |
|---|---|
| Repository | `https://github.com/microsoft/DiskANN.git` |
| Branch | `cpp_main` |
| Commit | `78256bbab4685e1774e78d331e081a153be26823` |
| Gitlink | `160000 78256bbab4685e1774e78d331e081a153be26823 upstream/diskann-cpp-main` |
| Source root | `upstream/diskann-cpp-main` |

Every entry below is a read-only, per-symbol source fact. Interpretations, stage separability,
Contract status, parity reachability, and implementation consequences live in `research.md` and
must not be inferred from this table alone. Line ranges are bound to the commit above.

## Build surface and native entry

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-001 | Build | Root CMake project | `CMakeLists.txt:18-30,54-205,207-305` | CMake -> `src/`, `apps/` | CMake cache, compiler, installed dependencies | `diskann` libraries and application targets in the out-of-tree build | Requires C++17, OpenMP, Boost program_options, Intel OMP/MKL on non-MSVC, and adds `src` plus `apps` when `PYBIND` is false. Non-MSVC source sets the C++ compiler variable to `g++`. | source_verified |
| SM-002 | Build | Non-MSVC compiler options | `CMakeLists.txt:266-300` | CMake -> all project targets | build type, `PORTABLE`, `PYBIND` | compile definitions/options | Non-MSVC builds add AVX2/FMA/SSE2, OpenMP, vectorization, allocation built-in suppression, loop unrolling, fatal errors, and `USE_AVX2`; non-PYBIND Release adds `NDEBUG` and `Ofast`, and adds native CPU tuning only when `PORTABLE` is false. | source_verified |
| SM-003 | Build | Core and application targets | `src/CMakeLists.txt:4-25`; `apps/CMakeLists.txt:4-41` | root CMake -> target definitions | source files and resolved libraries | shared `diskann`, static `diskann_s`, executable `build_disk_index` | `build_disk_index` is an executable target linked with `diskann`, tool tcmalloc options, async `aio`, and Boost program_options; it is an installed non-MSVC tool. | source_verified |
| SM-004 | Native entry | `main` | `apps/build_disk_index.cpp:16-191` | process -> `diskann::build_disk_index<T, LabelT>` | CLI options | process exit status and prefix-based files | The float32, unfiltered path selects `diskann::build_disk_index<float>` when `data_type=float`, `label_file` is empty, and the default label type is used. | source_verified |
| SM-005 | Native entry | CLI option declaration | `apps/build_disk_index.cpp:31-80` | Boost program_options -> bound variables | required data type, metric, paths, `B`, `M`; optional `T`, `R`, `L`, PQ/filter options | populated option variables | Required options are data type, metric, index prefix, data path, search RAM budget, and build RAM budget. Defaults are `T=omp_get_num_procs()`, `R=64`, `L=100`, `QD=0`, empty codebook, disk PQ `0`, reorder false, build PQ `0`, OPQ false, empty label/universal label, filtered L `0`, filter threshold `0`, and label type `uint`. | source_verified |
| SM-006 | Native entry | Parameter packing and float dispatch | `apps/build_disk_index.cpp:104-140,142-177` | `main` -> templated build | parsed option values | nine-token parameter string | The CLI derives filter use from a non-empty label path, maps `l2` to `Metric::L2`, packs `R L B M T disk_PQ reorder build_PQ QD`, and dispatches the float build. | source_verified |
| SM-007 | Documentation | Native binary format and documented invocation | `workflows/SSD_index.md:4-20,43-60`; `README.md:23-49,92-101` | operator -> `apps/build_disk_index` | `.bin` data and CLI options | prefix-based index files | The documented input starts with `uint32 npts`, `uint32 dim`, then `npts*dim` scalar values; the documented native program is `apps/build_disk_index`. README documents Linux dependencies and an in-source example build, but that example is not an authorization to write into this pinned source tree. | source_verified |

## Top-level graph-build chain and branch

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-008 | Native composite | `build_disk_index<T, LabelT>` parameter parsing | `src/disk_utils.cpp:1101-1169,1235-1261` | CLI dispatch -> composite build | packed `R L B M T ...` | parsed graph and resource values; OpenMP/MKL thread limits | `R` and `L` are integer graph parameters; `B` becomes a search-time byte budget; `M` is the positive indexing RAM budget in GiB; nonzero `T` is passed to both `omp_set_num_threads` and `mkl_set_num_threads`. | source_verified |
| SM-009 | Native composite | Input/sample/PQ preparation | `src/disk_utils.cpp:1286-1316`; `include/pq_common.h:6-11` | composite build -> quantization preparation | dataset, `B`, `QD`, PQ flags | PQ pivots and compressed vectors under the index prefix | Sampling rate is `256000 / points_num`. The in-memory PQ chunk count is derived from search budget per point, clamped to `[1, min(dim, 512)]`, unless positive `QD` overrides it. `generate_quantized_data` is called even when disk PQ and build PQ are both disabled. | source_verified |
| SM-010 | Native composite | Graph build and post-graph steps | `src/disk_utils.cpp:1323-1376` | composite build -> `build_merged_vamana_index` -> `create_disk_layout` -> warmup sampling | prepared dataset, graph parameters, prefix | transient merged memory graph, disk layout, warmup sample, cleanup | The graph call always receives `Metric::L2` after metric-specific preprocessing. After it returns, the native CLI consumes the merged memory graph to create a disk layout, generates a random warmup sample, and removes the merged memory graph and attempted companion `.data`/`.tags` paths. | source_verified |
| SM-011 | Branch | `build_merged_vamana_index<T, LabelT>` branch predicate | `src/disk_utils.cpp:628-682`; `include/index.h:28-43` | composite graph build -> single or sharded branch | dataset count/dim/type, `R`, build RAM `M` | branch-specific graph files | Full-index RAM is estimated as `1.1 * (rounded vector bytes + R*4*1.3 graph bytes + lock bytes + outer-vector bytes)`. The single branch is taken only when this estimate is strictly less than `M * 2^30`; otherwise control enters the sharded branch. | source_verified |
| SM-012 | Single branch | Single `Index::build/save` path | `src/disk_utils.cpp:640-681` | branch -> `Index::build` -> `Index::save` | whole dataset, `L`, `R`, `T`, filter/PQ flags | merged memory graph and data; removal of medoid/centroid outputs | For unfiltered data, the single branch sets saturation true, builds one graph of degree `R`, saves it, removes the medoids and centroids paths, and returns without P0 or M0. | source_verified |

## P0: native partitioning

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-013 | P0 | Sharded P0 call | `src/disk_utils.cpp:684-695` | sharded branch -> `partition_with_ram_budget<T>` | dataset, sampling rate, `M`, graph degree `floor(2R/3)`, prefix, replication `2` | returned Partition count; temporary centroids renamed to final centroids path | P0 is invoked once. Its centroids file is renamed from `<mem>_tempFiles_centroids.bin` to `<disk>.index_centroids.bin`. | source_verified |
| SM-014 | P0 | `partition_with_ram_budget<T>` | `src/partition.cpp:523-604`; declaration `include/partition.h:48-50`; float instantiation `src/partition.cpp:647-649` | P0 -> sampling, k-means++, Lloyd's, cluster-size estimate, ID sharding | dataset, sample rate, `M`, graph degree, prefix, `k_base` | odd Partition count, centroids, per-Partition ID maps | Partition count starts at `3`; `k_base` is capped at the current count; independent train and test samples are drawn; the maximum estimated shard RAM is compared with `M`; count increases by `2` until it fits. The function saves centroids and ID maps and returns the count. | source_verified |
| SM-015 | P0 randomness | In-memory `gen_random_slice` overload | `src/partition.cpp:95-142` | P0 -> sampler | dataset and sampling probability | sampled float matrix | Each call seeds a new `mt19937` from `random_device`; the probability is capped at `1`. P0 calls this overload separately for train and test samples. | source_verified |
| SM-016 | P0 randomness | `kmeanspp_selecting_pivots`; `run_lloyds` | `src/math_utils.cpp:312-354,380-456` | P0 -> k-means initialization/refinement | sampled vectors, number of centers, max repetitions | centroids | K-means++ seeds `mt19937` from `random_device`; Lloyd's runs up to the supplied repetition limit and may stop when relative residual improvement is below `1e-5` or residual is near zero. | source_verified |
| SM-017 | P0 output | `shard_data_into_clusters_only_ids<T>` | `src/partition.cpp:325-407` | P0 -> ID-map writer | full dataset, centroids, count, `k_base` | `<prefix>_subshard-P_ids_uint32.bin` | Each input point is assigned to its `k_base` closest centers. Every ID map is a binary matrix with `uint32` row count, `uint32` dimension `1`, then original zero-based `uint32` dataset IDs in dataset traversal order. | source_verified |

## B0: shard reconstruction and local Vamana build

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-018 | B0 preparation | `retrieve_shard_data_from_ids<T>` | `src/partition.cpp:409-473`; declaration `include/partition.h:41`; float instantiation `src/partition.cpp:651-653` | sharded loop -> shard reconstruction | full dataset and one ID map | one shard vector `.bin` file | The function streams the original dataset and writes vectors whose GlobalNodeIds occur in the ID map. The written local vector order follows the ascending ID-map order, so local position indexes the corresponding map entry. | source_verified |
| SM-019 | B0 | Per-Partition loop | `src/disk_utils.cpp:697-755` | sharded branch -> reconstruction -> `Index::build` -> `Index::save` | one shard vector file, `L`, `floor(2R/3)`, `T`, PQ/filter flags | one local `_mem.index`, companion `.data`; immediate removal of shard vector file | The unfiltered path constructs degree `floor(2R/3)` parameters with saturation false, calls `Index::build(shard_file, shard_points)`, saves to the local graph prefix, then removes the reconstructed shard vector file. | source_verified |
| SM-020 | B0 | `Index::build(filename, count)` -> `build_with_data_populated` | `src/index.cpp:1619-1702,1524-1576` | B0 -> data load -> `link` | local shard `.bin`, expected count | populated store and local graph | The file build validates metadata and bounds, populates the data store, then calls `build_with_data_populated`; that initializes scratch, generates the static frozen-point state, and calls `link`. | source_verified |
| SM-021 | B0 threading | `Index::link` | `src/index.cpp:1286-1387` | `build_with_data_populated` -> graph linking and cleanup | local vectors, `L`, local degree, `T`, defaults | in-memory adjacency lists | Node visit order is ascending, but graph construction and final cleanup use OpenMP parallel loops with `schedule(dynamic, 2048)`. Each node performs search/prune plus reciprocal insertion before the final over-degree prune. | source_verified |
| SM-022 | B0 parameters | `IndexWriteParametersBuilder` defaults | `include/parameters.h:46-117`; `include/defaults.h:6-31`; call at `src/disk_utils.cpp:714-718` | B0 parameter construction -> `Index` | explicit `L`, local degree, unfiltered `Lf=0`, saturation, `T`; implicit defaults | immutable write parameters | The builder defaults alpha to `1.2` and max occlusion size to `750`, maps a zero thread argument to `omp_get_num_procs`, and maps `with_filter_list_size(0)` to `L`; B0 explicitly calls that filter-list setter, sets saturation false, and supplies `T`. Static builds use zero frozen points. | source_verified |
| SM-023 | B0 output | `Index::save`, `save_graph`, `InMemGraphStore::save_graph` | `src/index.cpp:196-248,268-381`; `src/in_mem_graph_store.cpp:200-229` | B0 -> graph/data serialization | in-memory graph and local vectors | graph file plus `.data`; no tags file when tags disabled | The graph file header stores byte size, maximum observed degree, entry point, and frozen-point count, followed by one `uint32` degree and that many `uint32` LocalNodeIds per local node. Save also writes the data companion; tags are skipped when disabled. | source_verified |

## M0: native merge

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-024 | M0 | Sharded M0 call and cleanup | `src/disk_utils.cpp:757-787` | sharded branch -> `merge_shards` -> cleanup | all local graphs, all ID maps, Partition count, `R` | merged memory graph, medoids file; deletion of ID maps, local graphs and local `.data` files | M0 is called once after every B0 completes. Only after M0 returns does the loop remove all ID maps, local graph files, and local data companions. | source_verified |
| SM-025 | M0 | `read_idmap`; inverse relation | `src/disk_utils.cpp:219-284,438-447` | `merge_shards` -> ID-map readers | per-Partition binary ID maps in ascending GlobalNodeId order | sorted `(GlobalNodeId, PartitionId)` traversal relation | ID maps must have dimension `1` and exact expected byte length. Merge derives the global node count from the maximum mapped ID plus one and sorts `(GlobalNodeId, shard)` pairs without retaining LocalNodeId. Each pair consumes the next adjacency row from that shard's stream, not a seek to the original local row; this correspondence requires each map's local-row order to be ascending in GlobalNodeId. | source_verified |
| SM-026 | M0 output setup | Local graph headers and medoids | `src/disk_utils.cpp:341-401` | `merge_shards` -> graph readers/writers | local graph headers and maps | merged graph header and separate medoids matrix | M0 reads each local graph's declared size, width, local medoid, and frozen count; converts each medoid through its ID map; writes every global medoid to the medoids file and writes the last shard's mapped medoid into the merged graph header. | source_verified |
| SM-027 | M0 merge semantics | Candidate union, relabel, shuffle, truncate | `src/disk_utils.cpp:403-479` | `merge_shards` -> merged adjacency writer | local adjacency and ID maps | GlobalNodeId adjacency, degree at most `R` | For each global node occurrence, M0 maps local neighbors to GlobalNodeIds, deduplicates them, unions candidates across replicas, shuffles the candidate vector with an engine seeded from `random_device`, truncates to `R`, and writes it. | source_verified |
| SM-028 | M0 exclusion boundary | Entire unfiltered `merge_shards` body | `src/disk_utils.cpp:241-479` | `merge_shards` internal body | same as SM-027 | same as SM-027 | The function does not load vector coordinates, compute distances, call `RobustPrune`/`prune_neighbors`, perform global refinement, or run connectivity detection/repair. | source_verified |

## Native lifecycle and observability facts

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-029 | Lifecycle | Prefix-derived paths | `src/disk_utils.cpp:1171-1202` | composite build -> path construction | index prefix | memory graph, disk graph, medoids, centroids, PQ, sample and temporary path names | All scoped build outputs are derived from `index_path_prefix`; the temporary P0/B0 prefix is later derived from the merged memory graph path. | source_verified |
| SM-030 | Lifecycle | P0/B0/M0 file sequence | `src/disk_utils.cpp:684-787` | sharded branch sequence | prefix and stage inputs | rename/read/delete lifecycle | Centroids are renamed after P0. ID maps live through B0 and are read by M0. Each reconstructed shard file is deleted after its B0. Local graphs live through M0. ID maps, local graphs and local `.data` files are deleted after M0. | source_verified |
| SM-031 | Lifecycle | `create_disk_layout<T>` | `src/disk_utils.cpp:847-1024` | top-level build -> disk layout writer | base vectors and merged memory graph | SSD layout file | The native disk-layout step validates and reads the merged memory graph header and each adjacency list while combining graph records with vectors into the disk index. It does not rename the memory graph. | source_verified |
| SM-032 | Lifecycle | Final cleanup | `src/disk_utils.cpp:1346-1370`; `src/partition.cpp:33-84` | top-level build -> warmup sampling -> cleanup | base data, sample probability, merged memory graph | persistent sample files; removal of merged memory graph | Warmup sampling writes `<prefix>_sample_data.bin` and `<prefix>_sample_ids.bin` with a new `random_device` seed. The top-level build then removes the merged memory graph and attempts to remove its `.data` and `.tags` companions. | source_verified |

## Explicit source limits

- These entries establish callable symbols and file/control-flow facts; they do not establish that a
  supported build succeeded on the current host.
- They do not prove that an execution took the sharded branch or observed a particular Partition
  count.
- They do not define a Unified Stage Binding, a parity protocol, or a validated Baseline.
- `merge_shards` semantics are exactly SM-025 through SM-028. Any stronger algorithmic description
  would contradict this fixed source.

## Additional input and budget source facts

| ID | Stage | Symbol or construct | Source and lines | Caller -> callee | Native inputs | Native outputs or side effects | Verified source fact | Status |
|---|---|---|---|---|---|---|---|---|
| SM-033 | Input | `get_bin_metadata_impl`; `load_bin_impl<T>` | `include/utils.h:329-363,399-415` | native readers -> binary dataset | binary file and scalar template type | row count, dimension, contiguous scalar array | The reader consumes two native 32-bit integer header fields followed immediately by `npts * dim` values of `T`. For the scoped Linux amd64 float instantiation, a valid input is a little-endian `uint32 npts`, little-endian `uint32 dim`, then contiguous little-endian IEEE-754 float32 payload. A per-vector dimension prefix such as raw `.fvecs` is not accepted by this loader. | source_verified |
| SM-034 | Pre-graph preparation | `get_memory_budget`; PQ chunk derivation | `src/disk_utils.cpp:79-100,1238-1246,1286-1315`; `include/disk_utils.h:38-39` | packed `B` -> byte budget -> PQ chunk count | positive `B`, point count, dimension, optional `QD` | effective search byte budget and generated PQ assets | `B` is converted from GiB to bytes after subtracting 0.25 GiB only when `B - 0.25 > 1.0`; the chunk count is the integer search-budget bytes per point, clamped to `[1, min(dim, 512)]`, unless positive `QD` overrides it. `B` is not an input to the single/sharded predicate. | source_verified |

Registry physical admission is framework integration only; the mapped algorithm is unchanged.
See [contracts/stage-bindings.md](contracts/stage-bindings.md) for validated encoding and actual-service checks before consumer factories.

## Post-graph PQ wrapper version 1.1.0

Bound to the same repository/branch/commit above. SM-009/SM-034 are the authority for the
Feature 005 private policy: `src/disk_utils.cpp:79-87` converts B to bytes, subtracting 0.25 GiB
only when B-0.25>1.0; `:1286-1315` derives sample probability and automatic chunks, then applies
positive QD after the lower/dimension/MAX_PQ_CHUNKS bounds. `include/pq_common.h:6-11` supplies
256 centroids, the 256000 target and the 512-chunk bound. `src/partition.cpp:114` caps sampling
probability at 1; `:116-140` randomly samples and reports its actual count.

The wrapper passes the resulting policy directly to `generate_quantized_data<float>`
(`src/pq.cpp:1095-1131`), preserving native randomness, training and encoding. It does not invoke
the complete graph builder. `calculate_num_pq_chunks` at `src/disk_utils.cpp:96` is not this
call path: it has separate compression-ratio logic and a declaration/definition parameter mismatch.

Additional wrapper input checks reject unsafe numeric conversions, non-integer/negative QD and
positive QD greater than dimension/512; they do not require dimension divisibility or constrain
QD to budget-derived chunks. Frozen 1.0.0 remains the historical 32-chunk/0.256 profile with
<=1M admission. General 1.1.0 is explicit, with no per-scale version switching. SM-031's native
DiskLayout call is unchanged when the wrapper's artificial 1M limit is removed. This mapping
does not certify large-data execution, memory sufficiency or search quality.

## P0/B0 parameterized wrapper version 1.1.0

This addition remains bound to Microsoft DiskANN `cpp_main` commit
`78256bbab4685e1774e78d331e081a153be26823`; it does not revise the frozen 1.0.0 profile.
`apps/build_disk_index.cpp:52-55` declares CLI defaults R=64/L=100. SM-013 and SM-019 map
configured R to floor(2R/3); SM-022 maps unfiltered filter_list_size=0 to configured L.
Project `src/baseline/cpp_main/parameters.cpp` checks and derives those values for the shared
P0/B0 wrappers, whose 1.1.0 descriptors are registered only by `src/module_composition/runner.cpp`.
R=16/L=32 and R=64/L=100 therefore give local bounds 10 and 42 respectively. Calling these
values “upstream default R/L” does not mean all upstream defaults: the wrapper retains budget
0.6 GiB, T=6 and the existing PQ/OPQ/saturation rules. Random sampling, Lloyd iteration behavior,
budget-driven shard count and M0 semantics remain those mapped above. This is a source mapping,
not evidence of parity, SIFT1M/SIFT10M execution, quality or performance.

## Independent fixed-K P0 experimental binding

`cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_fixed_k_v1@1.0.0`
is an additional composition binding, not a revision or certification of the budget P0.
The frozen registration function and both budget-driven factories are unchanged.

| Fixed source | New binding |
|---|---|
| `src/partition.cpp:95-142`, SM-015 | One native random training sample with `min(1,256000/N)` from the validated Dataset. Check actual `K <= S <= 8388608` before pivot allocation/initialization; no resampling. |
| `src/math_utils.cpp:380-456`, SM-016 | Native k-means++ with `random_device`; the sample bound prevents entering its alternative random-pivot branch. |
| `src/math_utils.cpp:312-354`, SM-016 | Native single-nearest-center Lloyd updates, maximum 10 iterations with native early stopping. |
| `src/partition.cpp:325-407`, SM-017 | Native ID-only assignment with `k_base=2`; no shard vector copies. `math_utils.cpp:113-130` pops distinct center IDs, including when distances tie. |
| `src/partition.cpp:482-519` | The complete fixed-K `partition()` is deliberately not called: it returns status 0 and writes both vectors and IDs; it does not expose the sample-count validation boundary. |
| `src/partition.cpp:523-604`, SM-014 | The budget-search loop and independent estimation sample are not executed. No R or partition RAM budget is accepted by the new binding. |

Project `src/baseline/cpp_main/p0_fixed_k_partition.cpp` owns its parameter,
nonempty-block, exact-double-membership and provenance checks. Existing native format
checks and PartitionArtifact/v1 validation remain mandatory. Invalid output is rejected,
not repaired. The existing `src/partition/diskann_kmeans/native_partition.cpp` source is
compiled once into a shared lower-level archive with the upstream options/definitions;
ordinary KMeans retains its identity, single assignment and default 15-iteration policy.
The fixed-K binding is available without enabling that optional implementation.

This is a faithful use of the listed native primitives with an independent experimental
policy; it makes no frozen-budget parity, memory-sufficiency, scale or quality claim.
Source, actual library/binary hashes and compiler/link commands are recorded in the new
stage Manifest; requested/effective parameters and observed counts are not Artifact fields.
