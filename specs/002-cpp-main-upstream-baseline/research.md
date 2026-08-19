# Research: cpp_main Upstream Graph Baseline

## Research status

| Field | Result |
|---|---|
| Study class | `baseline_validation` |
| Reproduction mode | `upstream_faithful` |
| Replacement scope | `none` |
| Pinned source | `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823` |
| Source research | completed for the scoped native graph-build chain |
| Out-of-tree Native Probe | completed once on Linux amd64 with the pinned executable and SIFT1M; exit `0` |
| Sharded branch proven at runtime | **yes**; positive P0, three-B0, and M0 log sequence retained |
| Native Partition count observed | **yes**; `3` |
| Gate A decision | `PASS`; P0, B0, and M0 are separable, Contract-compatible, and planned |
| Native Reference reachable | `yes`; native sharded execution completed, but complete merged-graph structural evidence was not retained |
| Unified structural closure | completed; P0+B0×3+M0 and final GlobalGraph validation passed |
| Feature-local Structural/Contract Parity Gate | fixed policy implemented; current evidence result `NOT_REACHED` |
| Framework Baseline role | `cpp_main_p0_b0_m0_framework_candidate_v1`, state `candidate` |
| Final Baseline certification | deferred to Search Evaluation/S0 plus later Search Semantics and Quality Parity |
| Current Contract assessment | P0, B0 preparation, and M0 representation are `compatible`; Stage Bindings are implemented and structurally validated |

`source-mapping.md` is the commit-bound record of per-symbol source facts. This document keeps the
derived parameter mapping, stage conclusions, Contract assessment, parity reachability, probe
record, and open questions. Evidence labels have the following fixed meanings: `source_verified`
means established from the pinned source; `probe_verified` means observed in the retained Native
Probe; `uncertain` means not established by either; and `blocked_by_environment` means an attempted
check could not execute in its stated environment. A successful Gate A decision is not a parity
result and does not authorize final Baseline certification. The later limited Structural/Contract
Gate has its own fixed Feature-local policy and never grants final certification.

## 1. Actual CMake target and native entry

The actual executable target is `build_disk_index`, defined by the upstream CMake application
surface. Its native process entry is `main` in `apps/build_disk_index.cpp`; for this Feature's
float32, L2, unfiltered profile it dispatches to `diskann::build_disk_index<float>`. The build also
requires the upstream `diskann` library and its declared native dependencies. See SM-001 through
SM-007.

The repository documentation names the resulting binary as `apps/build_disk_index` relative to an
upstream-style build directory. In this project it must instead be produced from an out-of-tree
build directory outside `upstream/`. The README's in-source `mkdir build` example was not used.

## 2. Candidate mapping from the fixed Profile to native parameters

This is a candidate mapping for a later frozen Baseline Profile, not a formal run configuration.
No unresolved numeric value is silently replaced by an upstream default.

| Fixed Profile field | Native CLI or derived input | Candidate value | Status |
|---|---|---|---|
| scalar type | `--data_type` | `float` | `source_verified`, `probe_verified` |
| metric | `--dist_fn` | `l2` | `source_verified`, `probe_verified` |
| dataset | `--data_path` | `sift1M_dataset/base_vector.bin`; little-endian float32, `1000000 x 128`, 512000008 bytes, SHA-256 `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e` | `probe_verified` |
| output root | `--index_path_prefix` | `/output/index`, host directory `build/native-probe/runs/sift1m-sharded/` | `probe_verified` |
| final maximum degree | `--max_degree` / `-R` | `16` | `source_verified`, `probe_verified` |
| build search list | `--Lbuild` / `-L` | `32` | `source_verified`, `probe_verified` |
| search RAM budget | `--search_DRAM_budget` / `-B` | `0.03` GiB, deriving 32 PQ chunks for one million points | `source_verified`, `probe_verified` |
| build RAM budget | `--build_DRAM_budget` / `-M` | `0.6` GiB; below the calculated full-index estimate and observed to enter sharded build | `source_verified`, `probe_verified` |
| threads | `--num_threads` / `-T` | `6` | `source_verified`, `probe_verified`; actual runtime team behavior beyond native logs remains `uncertain` |
| disk PQ | `--PQ_disk_bytes` | `0` | `source_verified`, `probe_verified` |
| build PQ | `--build_PQ_bytes` | `0` | `source_verified`, `probe_verified` |
| OPQ | `--use_opq` | absent / false | `source_verified`, `probe_verified` |
| disk reorder data | `--append_reorder_data` | absent / false | `source_verified`, `probe_verified` |
| quantized-dimension override | `--QD` | `0` | `source_verified`, `probe_verified` |
| codebook | `--codebook_prefix` | empty default | `source_verified`, `probe_verified` |
| filters/labels | `--label_file`, `--universal_label`, `--FilteredLbuild`, `--filter_threshold` | empty, empty, `0`, `0` defaults | `source_verified`, `probe_verified` |
| label storage type | `--label_type` | `uint` default, operationally unused because labels are disabled | `source_verified`, `probe_verified` |

The formal profile must spell out every value, including values equal to native defaults. It must
also preserve the exact textual values passed to the CLI because the float CLI values are converted
to strings before the composite build parses them again (SM-005, SM-006, SM-008).

## 3. Source and semantics of R, L, build_DRAM_budget, and num_threads

- `R` is the requested final merged maximum degree and is used by the full-index RAM estimate. In
  the sharded path, each B0 local build and the P0 shard-memory estimate instead use integer
  `floor(2R/3)`; M0 truncates its global candidate union to `R` (SM-011, SM-013, SM-019, SM-027).
- `L` is the `IndexWriteParameters.search_list_size` used during each native graph build. It is not
  the final degree and is not the search-time evaluation list size (SM-019, SM-022).
- `build_DRAM_budget` (`M`) is parsed as a positive floating GiB value. It alone controls the
  single-versus-sharded graph branch through the strict predicate described in section 6. P0 then
  uses the same `M` to grow the number of Partitions until its estimated largest shard fits
  (SM-008, SM-011, SM-014).
- `num_threads` (`T`) defaults at the CLI to `omp_get_num_procs()`. A nonzero value is set on both
  OpenMP and MKL by the composite build and is set again by `Index::link` through the write
  parameters. A supplied zero is converted by the parameter builder to `omp_get_num_procs()`;
  therefore zero is not a stable single-thread request (SM-005, SM-008, SM-021, SM-022).

`search_DRAM_budget` (`B`) does **not** choose the graph sharding branch. It determines the
in-memory quantized-vector chunk count used by the native disk-index build. Confusing `B` with `M`
would change the profile semantics.

## 4. Other parameters and defaults actually consumed by the Graph Gate path

The CLI consumes the full option set in section 2. Even with disk PQ, build PQ, and OPQ disabled,
the native composite entry still derives a quantized dimension from `B`/point count (or `QD`) and
unconditionally generates PQ pivots and compressed vectors before the graph call. Those files are
part of native full-index preparation, not evidence that B0 uses PQ distance: `build_PQ_bytes=0`
constructs each `Index` with full-precision build distance (SM-009, SM-010, SM-019).

Additional source-fixed graph values are:

- P0 sample probability `min(1, 256000 / npts)` for each independent sample;
- P0 initial Partition count `3`, increments of `2`, k-means maximum repetitions `10`, and
  replication factor `2`;
- B0 local maximum degree `floor(2R/3)`, alpha `1.2`, maximum occlusion size `750`, saturation
  false, zero static frozen points, and unfiltered `Lf=0` resolved by
  `with_filter_list_size(0)` to Profile `L`;
- M0 final maximum degree `R` and unfiltered operation.

These are native behavior, not new tunable Feature parameters (SM-009, SM-013 through SM-016,
SM-019, SM-022, SM-027).

## 5. Environment variables and thread conditions affecting graph build

No direct `getenv` read was found in the scoped P0/B0/M0 call chain. The following indirect
runtime conditions still belong in Build Identity because the graph build uses OpenMP, MKL, and
parallel mutation:

- exact OpenMP and MKL runtime libraries and versions;
- `T`, `omp_get_num_procs()` result, visible logical CPU count, and any runtime reduction of the
  requested team size;
- OpenMP dynamic/thread-limit, placement, binding, and vendor affinity environment, including the
  effective values of `OMP_DYNAMIC`, `OMP_THREAD_LIMIT`, `OMP_PLACES`, `OMP_PROC_BIND`, and any
  linked-runtime-specific affinity controls;
- MKL dynamic/thread controls when the native composite entry performs quantization;
- host CPU/ISA and `PORTABLE` build choice, because upstream compile flags otherwise permit
  `-march=native -mtune=native`;
- allocator/runtime linkage, including tcmalloc configuration, as Build Identity even where it is
  not proven to alter logical graph output.

`Index::link` uses explicit `schedule(dynamic, 2048)` parallel loops, so `OMP_SCHEDULE` does not
replace that schedule clause. Placement, dynamic teams, actual thread count, timing, and concurrent
inter-insert ordering can nevertheless change the interleaving (SM-021). Those conditions remain
BuildManifest provenance, but byte stability and equivalence of thread scheduling or process state
between the wrapper and the top-level composite loop are not B0 acceptance requirements.

## 6. Real condition for single-build versus sharded branch

Let `N` be point count, `D` dimension, `S=sizeof(float)`, and `R` the requested final degree. The
upstream estimate is:

```text
full_index_ram = 1.1 * (
    N * ROUND_UP(D, 8) * S
  + N * R * sizeof(uint32_t) * 1.3
  + N * sizeof(non_recursive_mutex)
  + N * sizeof(ptrdiff_t))
```

The single branch is selected only when:

```text
full_index_ram < M * 1024 * 1024 * 1024
```

Equality enters the sharded branch. For the probed Linux build, the compiled ABI contributes
`sizeof(non_recursive_mutex) + sizeof(ptrdiff_t) = 48` bytes per point and the calculation is:

```text
full_index_ram
  = 1.1 * 1,000,000 * (128*4 + 16*4*1.2999999523162842 + 48)
  = 707,519,996.643066406 bytes
  = 0.658929344866 GiB

M budget = 0.6 * 2^30 = 644,245,094.4 bytes
707,519,996.643066406 < 644,245,094.4 is false
```

The predicate and the fact that equality enters sharded are `source_verified`; the selected
`M=0.6` reaching the sharded path is `probe_verified`. The single-build predicate remains
`source_verified` only: no single-build Probe was requested or executed.

## 7. Proving the sharded branch without changing upstream

The successful Native Probe used the unchanged native entry and retained the following independent
positive evidence:

1. freeze the exact dataset identity, `R`, `L`, `M`, and `T`, and calculate the source predicate;
2. preserve stdout/stderr showing P0's `Processing global k-means`, one or more `With N parts`, the
   `Partitioned ... across N shards` line, per-shard `Shard has` lines, and M0's `# nodes`,
   `Starting merge`, and `Finished merge` sequence;
3. record the returned/printed Partition count and observe matching ID-map/local-graph path names
   before native cleanup;
4. if transient files must be retained, copy exact bytes from an external watcher to a separate
   capture directory without modifying source, process parameters, random state, or control flow;
5. verify the source tree remains at the pinned clean commit after the process.

The retained stdout contains `Processing global k-means`, `With 3 parts`, the exact three shard
sizes, three `Shard has`/`Index built` sequences, `# nodes: 1000000`, `Starting merge`, and `Finished
merge`. Together with the calculated predicate, exact argv, exit `0`, and clean upstream state,
sharded execution is `probe_verified`. The earlier macOS configure attempts remain separately
recorded as `blocked_by_environment` and do not weaken the successful Linux evidence.

## 8. How the Partition count is decided

P0 starts at three Partitions and uses random train/test samples plus random k-means++ pivots. For
each candidate count it estimates every shard's full population from the test sample, computes a
RAM estimate using degree `floor(2R/3)`, and adds two Partitions if the largest estimate exceeds
`M`. The first fitting odd count is returned. Each dataset point is subsequently assigned to its
two closest centers (SM-014 through SM-017).

Consequently Partition count is not a simple closed-form function of `N`, `D`, `R`, and `M`; it can
vary with the native random draws when a profile lies near a threshold. In this run it is
`probe_verified` as `3`: estimated cluster sample sizes were `189798`, `189691`, and `132151`; P0
reported maximum estimated RAM `0.464832GB`; actual replicated shard sizes were `741551`, `741793`,
and `516656`, totaling `2000000` assignments. This observation does not predict another native run.

## 9. Real P0, B0, and M0 call chain

```text
apps/build_disk_index::main
  -> diskann::build_disk_index<float>
     -> generate_quantized_data<float>                 [native full-index preparation]
     -> build_merged_vamana_index<float>
        -> branch on estimate_ram_usage(..., R) vs M
        -> P0 partition_with_ram_budget<float>(..., floor(2R/3), ..., k_base=2)
        -> for p in [0, num_parts):
             retrieve_shard_data_from_ids<float>
             B0 Index<float>::build(shard_file, shard_points)
             B0 Index<float>::save(local_graph_prefix)
        -> M0 merge_shards(..., num_parts, R, merged_graph, medoids)
        -> native temporary cleanup
     -> create_disk_layout<float>(base, merged_graph, disk_index)
     -> warmup sampling and merged-graph cleanup
```

See SM-004, SM-006, SM-009, SM-010, SM-013, SM-018 through SM-024, and SM-032.

## 10. B0's actual `Index::build/save` path

For each Partition, native orchestration reconstructs a shard `.bin` in the ID-map order, creates
write parameters with `L`, local degree `floor(2R/3)`, saturation false, `T`, and native defaults,
then constructs `Index<float>`. The unfiltered path calls the filename overload of
`Index::build`, which validates and loads the shard before `build_with_data_populated` invokes
`link`. `Index::save` writes the local graph and full local vector `.data`; tags are not written
because tags are disabled. The shard vector file is then removed (SM-018 through SM-023).

The approved independent B0 boundary is the following `upstream_callable_wrapper` sequence:

1. validate the Dataset identity and complete float32 vector payload, explicit PartitionId, and
   complete LocalToGlobalIdMap;
2. write the selected map in the pinned native ID-map format and call the unchanged upstream
   `retrieve_shard_data_from_ids<float>` against the complete Dataset file;
3. verify that the reconstructed shard rows follow the map and retain their original coordinates;
4. construct `IndexWriteParameters` with Profile `L`, local degree `floor(2R/3)`, unfiltered
   `with_filter_list_size(0)` resolving to `L`, saturation false, Profile `T`, and upstream defaults
   alpha `1.2` and max occlusion `750`;
5. construct the unchanged upstream `Index<float>`, call `Index::build(filename, count)`, and call
   `Index::save(prefix)`;
6. publish only a validated standard LocalGraphArtifact with exact Dataset, Partition, map, source,
   and parameter lineage.

SM-017 and SM-018 show that native P0 maps are written in ascending dataset traversal order and
that `retrieve_shard_data_from_ids` streams the Dataset in that same order. The wrapper MUST NOT
sort or relabel an arbitrary map: if the supplied map order cannot be faithfully reconstructed by
the pinned callable, shard row-order validation fails closed before graph publication.

This independently callable composition is B0. It does not include P0, M0, PQ preparation, warmup,
or the upstream top-level composite branch. Lack of a standalone B0 CLI is not a separability
failure because all algorithmic work remains in the pinned exported upstream callables. The wrapper
does not claim byte identity, process-state equivalence, or thread-scheduling equivalence with the
top-level loop.

## 11. M0's real behavior and explicit exclusions

M0 reads every complete LocalToGlobal ID map and local graph, forms the GlobalNodeId-to-shard
relation, maps local medoids and local neighbor IDs to GlobalNodeIds, unions and deduplicates the
candidate neighborhoods across replicas, randomly shuffles each candidate vector, truncates it to
`R`, and writes the global-ID graph plus a separate medoids file (SM-024 through SM-027).

M0 does **not** read vectors, recompute distances, distance-prune, call `RobustPrune`, perform global
refinement, test connectivity, or repair connectivity (SM-028). It must not be described using any
of those terms.

## 12. Randomness in Partition, B0, and M0

| Stage | Source of variation | Current conclusion |
|---|---|---|
| P0 | independent `random_device`-seeded train/test sampling and `random_device`-seeded k-means++ | Partition count, centroids, membership, and maps may vary; no seed control exists in the native CLI |
| B0 | ascending logical visit vector but dynamic OpenMP scheduling and concurrent reciprocal insertion/cleanup | variation is retained as provenance; exact repeat behavior is not part of `structural_and_contract` acceptance and no repeated-run study is planned |
| M0 | `random_device`-seeded shuffle before every truncation | neighbor order varies; when candidate union exceeds `R`, retained neighbor membership may also vary |
| Composite-only preparation | PQ preparation plus explicitly random warmup sampling outside the P0/B0/M0 graph semantics | affects native full-index side files and timing; PQ repeatability was not separately assessed, and native behavior must remain unchanged |

No seed was fixed, exposed, or proposed. See SM-009, SM-015, SM-016, SM-021, SM-027, and SM-032.

## 13. Native intermediate-file lifecycle

| Native path pattern | Generated by | Read by | Rename/delete behavior | Successful Probe evidence |
|---|---|---|---|---|
| `<prefix>_pq_pivots.bin`, `<prefix>_pq_compressed.bin` | pre-graph quantization | later disk-index/search assets | no scoped cleanup for the non-disk-PQ case | `probe_verified`: retained, 135836 and 32000008 bytes |
| `<prefix>_mem.index_tempFiles_centroids.bin` | P0 | rename operation | renamed to `<prefix>_disk.index_centroids.bin` immediately after P0 | `probe_verified`: temporary name absent; final 1544-byte centroids retained and copied |
| `<prefix>_mem.index_tempFiles_subshard-P_ids_uint32.bin` | P0 | shard reconstruction and M0 | deleted after M0 | `probe_verified`: three maps observed, copied, checksummed, then absent at success |
| `<prefix>_mem.index_tempFiles_subshard-P.bin` | B0 preparation | B0 `Index::build` | deleted immediately after that B0 | `probe_verified`: each reconstructed file observed during its B0 and absent afterward; not copied |
| `<prefix>_mem.index_tempFiles_subshard-P_mem.index` | B0 save | M0 | deleted after M0 | `probe_verified`: all three opened by M0; shard 0 and 1 graphs copied, shard 2 not captured before cleanup |
| local graph `.data` companion | B0 save | not used by M0 | deleted after M0 | `probe_verified`: all generated; shard 0 and 1 companions copied, shard 2 not captured before cleanup |
| `<prefix>_mem.index` | M0 | `create_disk_layout` | deleted after disk-layout and warmup generation | `probe_verified` by native create/open/size logs and final absence; bytes were not captured |
| `<prefix>_disk.index_medoids.bin` | M0 | later native consumers | not removed in scoped path | `probe_verified`: retained, 20 bytes |
| `<prefix>_disk.index` | disk-layout step | later native search | not removed in scoped path | `probe_verified`: retained, 585150464 bytes |
| `<prefix>_sample_data.bin`, `<prefix>_sample_ids.bin` | warmup sampling | later native search | not removed in scoped path | `probe_verified`: retained, 51081224 and 399080 bytes |

Generation/read/rename/delete semantics remain `source_verified` by SM-029 through SM-032. The
successful-run observations in the final column are `probe_verified` from `stdout.log`, manual
read-only file snapshots, captured copies, and the final inventory. The automatic filename watcher
failed after its first event because a zsh special variable overwrote `PATH`; that monitor failure is
retained as a warning and was not hidden by rerunning the native process.

## 14. Artifact observability and non-invasive capture

| Stage/output | Source-level observability | Probe observation | Capture conclusion |
|---|---|---|---|
| P0 Partition count and centroids | returned count/log plus centroids file; centroids are renamed | `3`; final centroids observed and copied | `probe_verified`, exact-byte copy retained |
| P0 LocalToGlobal maps | separate ID-map files live until after M0 | all three observed and copied before B0 completed | `probe_verified`, exact-byte copies and SHA-256 retained |
| P0 shard vectors | not emitted by P0; reconstructed one at a time immediately before each B0 | three reconstruction files observed | not a native P0 output; transient B0-preparation file is observable, but no copy was retained |
| B0 local graph | separate native graph file lives until after M0 | all three observed and opened by M0; shard 0 and 1 copied | partial complete-set capture: copyability is `probe_verified`, but the complete three-graph capture is `uncertain` for this run because shard 2 was not retained |
| B0 local vector `.data` | separate companion lives until after M0 | all three generated; shard 0 and 1 copied | partial complete-set capture with the same limitation as local graphs |
| M0 merged graph | separate graph exists while disk layout is generated, then is deleted | native log records 57167268-byte expected graph and disk-layout consumption; final path absent | observability is `probe_verified`; exact-byte capture was not obtained |
| M0 medoids | separate persistent native file | 20-byte file retained | `probe_verified`, SHA-256 retained |

The P0 copies and two B0 copies were made host-side after their files were complete, without changing
the native argv or originals; the process still exited `0`. This proves mechanical copyability for
those files, not zero timing impact. A formal protocol that depends on complete native intermediate
capture still needs a predeclared capture method and complete-set evidence.

## 15. Final Gate A stage separability and availability

| Stage | Separability | Native output boundary | Contract status | Availability | Final Gate A basis |
|---|---|---|---|---|---|
| P0 | `separable` | exported `partition_with_ram_budget<float>` returns Partition count and emits centroids plus ID maps | `compatible` | `planned` | PartitionArtifact preserves Dataset lineage, PartitionId, ownership, complete maps, and optional centroids; shard-vector materialization belongs to B0 preparation |
| B0 | `separable` | `upstream_callable_wrapper`: `retrieve_shard_data_from_ids` -> `Index::build` -> `Index::save` -> LocalGraphArtifact | `compatible` | `planned` | validation is `structural_and_contract`; byte identity with the native top-level loop is not required, and Vamana may not be copied or rewritten |
| M0 | `separable` | exported `merge_shards` consumes all ID maps/local graphs and emits global graph plus medoids | `compatible` | `planned` | GlobalGraphArtifact represents graph plus optional medoids; cpp_main M0 must declare `graph.medoids/v1` and produce medoids |

The final Gate A decision is fixed as:

```yaml
p0:
  separability: separable
  contract_status: compatible
  availability: planned
b0:
  separability: separable
  contract_status: compatible
  availability: planned
  binding_mode: upstream_callable_wrapper
  validation_scope: structural_and_contract
m0:
  separability: separable
  contract_status: compatible
  availability: planned
native_reference_reachable: yes
unified_graph_parity_reachable: yes
formal_gate_status: not_reached
baseline_registration: prohibited
```

At the time of this Gate A decision, no Stage Binding had been implemented. The decision authorized
a minimal implementation plan, not Baseline registration or complete Graph Parity. The bindings
were subsequently implemented and structurally validated; each must call the pinned symbols in the
source-verified sequence, preserve their fixed parameters and defaults, and fail closed on invalid
inputs or outputs; it does not need to reproduce the top-level composite process state, scheduling,
or output bytes.

### B0 design decision record

**Decision**: expose B0 as an `upstream_callable_wrapper` over the complete Dataset, explicit
PartitionId/map, `retrieve_shard_data_from_ids<float>`, and upstream `Index<float>::build/save`;
accept it only through `structural_and_contract` validation.

**Rationale**: the pinned source exports every algorithm-bearing callable, the Native Probe
observed the same sequence for three Partitions, and structural validity plus exact lineage can be
checked without asserting properties that OpenMP interleaving and native randomness do not support.
The absence of a standalone CLI does not prevent direct library invocation.

**Alternatives considered**:

- keeping B0 `composite_only`: rejected because it confuses process-entry availability with
  callable separability;
- copying the per-Partition Vamana logic: rejected because it would create a project-owned algorithm
  and violate upstream fidelity;
- requiring Native Probe byte/process/thread equivalence: rejected because it is unnecessary for
  the approved structural boundary and unsupported by current evidence;
- passing the full Dataset as an unvalidated path in stage parameters: rejected because it bypasses
  Artifact admission and lineage; the existing validated Dataset-backed StageContext
  `VectorAccessor` is the only fallback when a Partition record has no shard-vector payload.

## 16. Mapping to the existing public Contract

The comparison baseline is Feature 001's C++17 API, data model, checksum contract, and v1 schemas.

| Native fact | Existing Contract mapping | Result |
|---|---|---|
| ID-map value is original zero-based dataset ID | `GlobalNodeId` (`uint32_t`) | compatible |
| local vector/graph position indexes one ID-map row | Partition-scoped `LocalNodeId` plus complete LocalToGlobalIdMap | compatible |
| P0 produces all maps under one native call | aggregate PartitionArtifact with ordered Partition records | compatible in aggregation semantics |
| P0 does not produce shard vectors | `shard_vectors_role` is optional while ownership, lineage, and every complete map remain mandatory | compatible; optional aggregate `centroids` retains the other native P0 output |
| B0 consumes the complete Dataset and the selected Partition map | `LocalGraphBuilder::build` receives the aggregate PartitionArtifact and explicit PartitionId; `StageContext` supplies the validated Dataset-backed VectorAccessor | compatible at the public boundary; runtime materialization is B0 preparation, not P0 output, and a raw path or process-global lookup remains forbidden |
| B0 graph adjacency contains local IDs and has an owning Partition | LocalGraphArtifact `id_space: local`, `partition_id`, map and graph payloads | compatible in logical shape |
| B0 source identity is Dataset + P0 aggregate + exact selected map | LocalGraphArtifact dataset binding, source Partition checksum/identity, selected map payload checksum, and BuildManifest lineage | compatible once the Dataset-backed VectorAccessor and valid source PartitionArtifact pass their gates |
| M0 consumes all maps and all local graphs | GraphMerger multi-input ContractRequirement and aggregate PartitionArtifact | compatible in input semantics |
| M0 graph adjacency is in GlobalNodeId space | GlobalGraphArtifact `id_space: global` | compatible |
| M0 emits merged graph plus separate medoids file | GlobalGraphArtifact requires `global_graph` and permits `medoids`; `graph.medoids/v1` pairs the implementation declaration with payload validation | compatible; cpp_main M0 must declare that capability and cannot publish graph-only output |
| native files have DiskANN-specific binary headers | arbitrary payload media type plus exact payload checksum, with a format-specific semantic validator | compatible; the later implementation added the scoped parser/validator outside upstream |

Artifact data, BuildManifest provenance, and parity/evaluation findings remain separate. Native
paths and timestamps belong in BuildManifest, not Artifact identity. The reviewed change is limited
to the backward-compatible v1 representation and its validator/tests; no native Stage Binding or
algorithm behavior was added.

## 17. Contract classification

The two confirmed representation gaps are closed by the reviewed v1-compatible amendment:

1. P0 representation is `compatible`: every map, PartitionId, ownership relation, and Dataset
   lineage remains required; shard vectors are optional; aggregate centroids are optional.
2. M0 representation is `compatible`: the graph remains required; medoids are optional for the
   generic contract but required for cpp_main M0 through `graph.medoids/v1`; both payloads enter the
   aggregate checksum projection.

B0 is also compatible at the existing public execution boundary because a map-only Partition can
be combined with the validated Dataset-backed `StageContext::vector_accessor`; materialization is
B0 preparation. Native float32 Dataset and local-graph parsers, descriptors, build integration,
and Stage Bindings were later implemented without changing this Contract conclusion. No
`blocking_incompatible` fact has been proven. The completed Framework structural closure remains
valid evidence; the fixed limited Structural/Contract Gate is nevertheless `NOT_REACHED` until
complete Reference merged-graph structural evidence exists.

## 18. Fields suitable for checksum identity

For every captured native payload, SHA-256 should cover exact stored bytes. An Artifact aggregate
identity should include, through a reviewed format-specific metadata projection:

- dataset payload checksum and interpretation (`float32`, row count, dimension, L2);
- Artifact type, format version, ID space, and native binary format version/ABI assumptions;
- P0: Partition count, ordered PartitionIds, exact centroid checksum, ordered exact ID-map
  checksums, and replication factor;
- B0: owning PartitionId, exact source aggregate checksum, exact selected ID-map checksum, local
  node count, requested local degree `floor(2R/3)`, observed degree, local graph byte checksum, and
  any retained local data checksum;
- M0: ordered source Partition/local-graph identities, global node count, requested `R`, observed
  degree, merged graph byte checksum, and medoids byte checksum;
- source Artifact identities in semantic order.

Filesystem paths, timestamps, run IDs, compiler/build settings, environment, logs, and metrics must
remain outside Artifact identity and in BuildManifest/evidence. Exact bytes identify one native
result; they do not imply cross-run semantic equality.

## 19. Output comparison scope

Because native random state is not controlled:

- P0 centroids, membership maps, Partition count near a threshold, and their exact checksums cannot
  be presumed equal across independent runs.
- B0 exact graph bytes and neighbor order are not acceptance inputs for the callable wrapper. Its
  declared comparison is limited to structural validity and exact Contract/lineage binding; no
  repeated-run or byte-identity requirement is introduced.
- M0 exact graph bytes cannot be presumed equal. Sorting each adjacency can remove order-only
  differences, but it cannot remove membership differences caused when a shuffled union is
  truncated to `R`.
- PQ preparation and warmup outputs belong to the native composite run but are outside Graph
  Parity. Warmup is explicitly random; PQ-output repeatability was not assessed in this research.

For the B0 wrapper, the complete acceptance projection is: valid PartitionId; complete in-range
map; reconstructed shard rows in exact map order; local node count equal to map length; valid
LocalNodeId neighbors; maximum degree at most `floor(2R/3)`; and exact Dataset, Partition, map,
parameter, source, and build lineage. Complete report equality, native-versus-wrapper graph bytes,
logical adjacency parity, process-state equivalence, scheduling equivalence, repetitions, and
statistical thresholds are outside this B0 plan. The fixed limited Structural/Contract Policy does
not add them; complete Search/Quality parity remains deferred.

## 20. Native-versus-Unified Source and Build Identity principles

Source Identity must match on repository URL, branch, full commit, root gitlink, checked-out
submodule HEAD, and clean/dirty state. “Current cpp_main” is invalid. A Unified wrapper may have an
additional project commit identity, but it may not obscure or substitute for the pinned upstream
identity.

Build Identity must compare and retain:

- compiler executable and complete version, target/ABI, OS, architecture, and CPU features;
- CMake executable/version, generator, build type, complete configure/build commands, effective
  CMake definitions, project compile options, and linked dependency/runtime identities;
- `PORTABLE`, `PYBIND`, and every option that changes the target or native build path;
- executable/library checksums and whether Native and Unified share the same upstream-built library
  or use independently built but identity-equivalent copies;
- complete runtime CLI/stage parameters, dataset checksum, environment/thread conditions, and
  resource budgets.

Source equality does not imply Build equality. A difference must be recorded and judged by a
predeclared parity protocol; it must never be silently normalized after results are seen.

## Out-of-tree Native Probe record

### Successful Linux amd64 SIFT1M sharded Probe

The only executed graph-build Probe began at `2026-08-06T03:11:44Z`, ended at
`2026-08-06T03:33:19Z`, and exited `0`. It was not a formal Graph Parity run. It did not run a
single-build case, did not read `sift1M_dataset/selected_query.csv`, did not change a random seed,
and was not retried with modified parameters.

#### Admission, input, build, and runtime identity

| Check | Observed value | Evidence status |
|---|---|---|
| root gitlink | `160000 78256bbab4685e1774e78d331e081a153be26823 upstream/diskann-cpp-main` | `probe_verified` |
| submodule before/after | HEAD `78256bbab4685e1774e78d331e081a153be26823`; empty porcelain status | `probe_verified` |
| input path | repository-relative `sift1M_dataset/base_vector.bin`; resolved absolute path `/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/0803_diskann_methods/sift1M_dataset/base_vector.bin` | `probe_verified` |
| input format | ordinary file; little-endian float32; `npts=1000000`; `dim=128`; 512000008 bytes | `probe_verified` |
| input payload | 128000000 values scanned in eight 64 MiB chunks; non-finite count `0` | `probe_verified` |
| input SHA-256 | `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`, identical before and after | `probe_verified` |
| executable | `build/native-probe/linux-release/apps/build_disk_index`; SHA-256 `f4e100a556b75853b737e4c0b03a01f9343734d18a3d7a2351201e6c0ba1d0ac`; ELF BuildID `0233eaa41c35d55abf0b4ed563e2c035bd52305b` | `probe_verified` |
| build toolchain | `/usr/bin/c++`, GNU 11.4.0; CMake 3.22.1; Unix Makefiles; Release; C++17 | `probe_verified` from generated build files |
| effective compile surface | AVX2/FMA/SSE2, OpenMP, `-O3 -DNDEBUG -Ofast -march=native -mtune=native`, `-m64`, `-std=gnu++17`, and recorded target definitions | `probe_verified` from `flags.make` |
| linked/runtime libraries | OpenMP, tcmalloc, Boost program_options 1.74, MKL ILP64/thread/core, libaio and standard runtime; no `not found` entry | `probe_verified` |
| runtime image | `sha256:7b2782df15ca76874f902fd6336c66cab7b077f8eeb788adf148165c65f9dfcb`, Ubuntu 22.04, Linux amd64 | `probe_verified` |
| container controls | `--network none`, `--read-only`, `--pull=never`; executable tree and input file read-only; only the run directory read-write; `TMPDIR=/output/tmp` | `probe_verified` |

The exact historical configure/build commands that produced the already supplied Linux build tree
were not present in the supplied evidence and remain `uncertain`; the generated CMake target files,
compiler identity, effective compile/link command, executable checksum, ELF identity, and dynamic
library identity are retained. This limitation is acceptable for an exploratory Native Probe but
must be closed before treating a build as a formal Baseline BuildManifest.

#### Frozen RAM decision and argv

For `N=1000000`, `D=128`, `R=16`, the full-index estimate is 707519996.643066406 bytes
(`0.658929344866` GiB). `M=0.6` supplies 644245094.4 bytes, so the strict single predicate is false.
P0/B0 uses local degree `floor(2R/3)=10`; at replication `2`, a three-way average has 666666.67
assignments, while `M=0.6` accommodates about 956989 assignments under the same estimator. This
provided 43.55% average-load headroom without choosing a very small sharding budget. `B=0.03`
becomes 32212254.72 search-budget bytes and deterministically derives 32 PQ chunks for one million
points; it does not control the graph branch.

```sh
docker run --rm \
  --platform linux/amd64 \
  --pull=never \
  --network none \
  --read-only \
  --mount "type=bind,src=$ROOT/build/native-probe/linux-release,dst=/probe-build,readonly" \
  --mount "type=bind,src=$INPUT,dst=/input/base.bin,readonly" \
  --mount "type=bind,src=$ROOT/build/native-probe/runs/sift1m-sharded,dst=/output" \
  --env TMPDIR=/output/tmp \
  sha256:7b2782df15ca76874f902fd6336c66cab7b077f8eeb788adf148165c65f9dfcb \
  /probe-build/apps/build_disk_index \
    --data_type float --dist_fn l2 \
    --data_path /input/base.bin --index_path_prefix /output/index \
    -R 16 -L 32 -B 0.03 -M 0.6 -T 6 \
    --QD 0 --PQ_disk_bytes 0 --build_PQ_bytes 0
```

The complete resolved argv and mounts are retained in
`build/native-probe/runs/sift1m-sharded/argv.txt`; stdout, stderr, timing, exit status, input identity,
parameter calculation, build/runtime identity, and before/after upstream states are retained beside
it.

#### Branch and P0/B0/M0 evidence

| Evidence | Observed result | Status |
|---|---|---|
| sharded branch | `Processing global k-means`; no single-build Probe was executed | `probe_verified` |
| P0 fit decision | `With 3 parts, max estimated RAM usage: 0.464832GB, budget given is 0.6` | `probe_verified` |
| P0 output | actual shard sizes `741551`, `741793`, `516656`; replication 2; total 2000000 assignments | `probe_verified` |
| B0 shard 0 | 741551 nodes; local max degree 10; save completed | `probe_verified` |
| B0 shard 1 | 741793 nodes; local max degree 10; save completed | `probe_verified` |
| B0 shard 2 | 516656 nodes; local max degree 10; save completed | `probe_verified` |
| M0 input | `# nodes: 1000000`; three inverse maps and three local graph files opened | `probe_verified` |
| M0 output | max input width 10, output width 16; `Starting merge` then `Finished merge`; merged expected graph size 57167268 bytes | `probe_verified` |
| post-merge native path | disk layout wrote 585150464 bytes; warmup wrote 99768 sample points | `probe_verified`, outside Graph Parity claims |
| process result | exit `0`; empty stderr | `probe_verified` |

Post-run read-only checks established that all three captured P0 maps have exact headers and lengths,
strictly increasing in-range GlobalNodeIds, total 2000000 assignments, and exactly two assignments
for every one of the 1000000 GlobalNodeIds. Captured B0 graphs 0 and 1 parse to their exact declared
sizes, have width/observed maximum degree 10, and contain zero out-of-range LocalNodeIds. These checks
are `probe_verified` only for the captured files; shard 2 and transient M0 graph structure were not
post-run validated because their bytes were not retained. The first B0 checker invocation failed due
to a missing `numpy` import; that auxiliary-script failure is retained, then the corrected read-only
check passed without rerunning the Native Probe.

All three P0 maps and centroids were copied after P0 completed. The complete graph and `.data`
companions for B0 shards 0 and 1 were copied after their saves completed. These copies, their sizes,
and SHA-256 values are retained under `captured/`; the originals were not changed. Shard 2 local
outputs and the merged memory graph were observed and consumed by native M0/disk layout, but were
not captured before native cleanup. The automatic filename watcher failed after its first event
because its zsh loop variable replaced the special `path` array; this is a retained Probe warning,
not a native failure, and the native process was not rerun.

#### Final retained native files

| File | Bytes | SHA-256 |
|---|---:|---|
| `index_disk.index` | 585150464 | `78e8c53a14d2837ae98f97bc4d2dc2410456974e61f02c0f1e4d089299a32b64` |
| `index_disk.index_centroids.bin` | 1544 | `64e090472238c7d096e8a6a299c6c4bec0b420b53b22f072c33d85015a89cc3d` |
| `index_disk.index_medoids.bin` | 20 | `a0b6d3f881cab805c036589a0e1bcc95ec878ea13785c0843a95008de444abce` |
| `index_pq_compressed.bin` | 32000008 | `e177df7dca38269bf8acc22f96d8b12e33704344117d1ca104565c0d22e66e7a` |
| `index_pq_pivots.bin` | 135836 | `a10699b724c03c5004486e86e2748cf1d4a4938d564ec9072d68d7c42784768f` |
| `index_sample_data.bin` | 51081224 | `844e2202230ce8dab8eadc92cf662a5a828199f7383f870c9450605c8f482960` |
| `index_sample_ids.bin` | 399080 | `298aac7c16e0f02f782e9782cff2f0da4b60b31a4c487b592fbbf2fdd4365cc0` |

These are Native Probe outputs, not validated project Artifacts and not parity results.

### Earlier blocked macOS admission attempts

This subsection is retained historical evidence. Its source and host observations are
`source_verified` or locally observed as stated, while target build/run conclusions are
`blocked_by_environment`; they are not the successful Linux Probe.

#### Admission and host identity

| Check | Observed value | Result |
|---|---|---|
| root gitlink | `160000 78256bbab4685e1774e78d331e081a153be26823 upstream/diskann-cpp-main` | pass |
| submodule HEAD before probe | `78256bbab4685e1774e78d331e081a153be26823` | pass |
| upstream branch | `cpp_main` | pass |
| upstream status before probe | empty `git status --porcelain=v1` | clean |
| host | macOS 13.2.1 (22D68), Darwin 22.3.0, x86_64 | recorded |
| online processors visible to `getconf` | `12` | recorded; `omp_get_num_procs()` not observed |
| CMake | `/usr/local/bin/cmake`, 4.3.3 | recorded |
| generator | `Unix Makefiles`; `/usr/bin/make` 3.81 | recorded |
| detected C/C++ compiler | `/usr/bin/cc`, `/usr/bin/c++`, AppleClang 14.0.0.14000029 | recorded at configure |
| source-requested non-MSVC C++ command | `g++` -> `/usr/bin/g++`, also AppleClang 14 | recorded; final target compile not reached |
| detected native dependencies | Homebrew `libomp` 22.1.6 only; Boost, MKL, tcmalloc and libaio not found in the checked locations | incomplete |

`hw.ncpu`/`hw.logicalcpu` queries were denied by the managed environment, so `getconf` is the only
host CPU-count observation. The native program did not run and therefore its actual default `T`
was not observed.

#### Commands and outcomes

Probe A began at approximately `2026-08-06T02:13:57+0800` and used an out-of-tree directory:

```sh
cmake -S "upstream/diskann-cpp-main" \
  -B /tmp/diskann-native-probe-78256-20260806-a \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPORTABLE=ON
```

Result: exit `1` during configure. Preserved terminal evidence:

```text
Could NOT find OpenMP_C
Could NOT find OpenMP_CXX
CMake Error at CMakeLists.txt:89 (message):
  No OpenMP support
```

Probe B began at approximately `2026-08-06T02:14:14+0800` and only supplied the installed libomp
discovery values; it did not edit upstream source:

```sh
cmake -S "upstream/diskann-cpp-main" \
  -B /tmp/diskann-native-probe-78256-20260806-b \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPORTABLE=ON \
  -DCMAKE_PREFIX_PATH=/usr/local/opt/libomp \
  -DOpenMP_C_FLAGS="-Xpreprocessor -fopenmp" \
  -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp" \
  -DOpenMP_C_LIB_NAMES=omp \
  -DOpenMP_CXX_LIB_NAMES=omp \
  -DOpenMP_omp_LIBRARY=/usr/local/opt/libomp/lib/libomp.dylib
```

Result: OpenMP discovery passed, then configure exited `1`. Preserved terminal evidence:

```text
Found OpenMP: TRUE
Could NOT find Boost (missing: Boost_INCLUDE_DIR program_options)
CMake Error at CMakeLists.txt:126 (message):
  Couldn't find Boost dependency
```

The requested native build command was then attempted against Probe B:

```sh
cmake --build /tmp/diskann-native-probe-78256-20260806-b \
  --target build_disk_index --parallel 2
```

Result: exit `2`; configure had not generated a Makefile. No target, library, or native executable
was built. No run command was issued because
`/tmp/diskann-native-probe-78256-20260806-b/apps/build_disk_index` did not exist.

#### Effective build identity and input identity

The only effective configured values are the host/compiler/generator, `Release`, `PORTABLE=ON`, and
the Probe B OpenMP discovery entries above. Target compile/link options and linked-library identity
are **not available** because configure did not complete. Source-declared options in SM-001 through
SM-003 are not misreported as an actual build.

No dataset was opened or consumed, no exploratory payload was created, and no SIFT-named payload
was found in the repository file inventory. Therefore input dataset path, byte length, vector count,
dimension, and checksum are `not reached`, not omitted evidence. No experiment of any size ran.

#### Branch, Partition, lifecycle, and upstream evidence

| Required evidence | Probe result |
|---|---|
| native process exit | not reached |
| single/sharded branch | not reached; no branch claim |
| sharded positive logs | none |
| Partition count | unobserved / uncertain |
| runtime intermediate files | none generated |
| rename/delete lifecycle | `source_verified` only; not observed by this blocked attempt |
| captured native graph | none |
| formal parity | not started |
| submodule HEAD after probe | `78256bbab4685e1774e78d331e081a153be26823` |
| upstream status after probe | empty `git status --porcelain=v1`; empty diff stat |

All generated CMake probe files are under `/tmp/diskann-native-probe-78256-20260806-{a,b}`. No file
was generated under `upstream/diskann-cpp-main`.

## Structural/Contract Parity decision

The approved decision is a single fixed Structural/Contract Parity projection. It compares only:

- exact Reference/Framework role, pinned source, Dataset, and draft Profile/control identities;
- clean upstream state, sharded branch, and at least two Partitions;
- the observed native P0/B0/M0 sequence and Framework P0 once, B0 once per Partition, M0 once;
- Framework Artifact handoff validation and lineage;
- complete Reference and Framework final-graph structural evidence: GlobalNodeId coverage, legal
  neighbors, no self edge, degree at most R, required medoids, Dataset lineage, and validation.

The decision explicitly excludes cross-output graph checksum or adjacency equality, topology
statistics, repeated-run statistics, Search Semantics, Quality, Recall, QPS, latency, and
performance. These exclusions resolve the earlier projection/repetition/aggregation uncertainty;
they are not deferred knobs in this Feature.

The fixed Feature-local identities are:

~~~yaml
reference_baseline_id: cpp_main_native_sharded_reference_v1
framework_baseline_id: cpp_main_p0_b0_m0_framework_candidate_v1
framework_state: candidate
~~~

They are role labels, not Registry identities, certification IDs, or BuildIdentity replacements.
The Gate derives `PASS`, `FAIL`, `INCONCLUSIVE`, or `NOT_REACHED` from the frozen Policy and cited
evidence. A limited `PASS` could prove only Feature 002 P0+B0+M0 structural-and-contract parity and
would leave the Framework state `candidate`. Search Evaluation/S0 plus later Search Semantics and
Quality Parity are required before any final Baseline certification decision.

### Current evidence decision

The successful Native Probe proves the pinned source, exact Dataset and effective graph-build
controls, sharded branch, three native Partitions, P0/three-B0/M0 call sequence, process success,
and clean upstream. Its retained external capture contains all three P0 maps but only two of the
three transient B0 graphs, and the transient native merged graph was consumed/removed before a
complete immutable structural record was retained. The final native `index_disk.index` is the
post-merge disk layout and must not be reclassified or reverse-inferred as that missing merged
graph.

The later Framework execution has complete P0+B0×3+M0 Artifacts, ValidationEvidence, a valid
GlobalGraphArtifact, and a graph-integrity EvaluationReport. That does not fill the missing
Reference evidence. Therefore the current policy-derived report is:

~~~yaml
structural_contract_gate: NOT_REACHED
reason: complete Reference merged-GlobalGraph structural evidence unavailable
framework_state: candidate
final_framework_baseline_certified: false
~~~

No additional native or Unified execution was performed for this decision. The earlier macOS
configure attempts remain historically `blocked_by_environment`; the successful Linux Native
Probe itself is not environment-blocked.
