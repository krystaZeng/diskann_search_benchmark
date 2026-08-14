# HNSW-Merger and DiskANN Source Mapping

## Binding and scope

This file records symbol-level source facts only. Conclusions, adaptations and implementation
decisions live in [research.md](research.md).

Official repository identity:

```yaml
repository: https://github.com/Kimchuls/HNSWMerger.git
branch: main
commit: 88f6b7179f651cf5f284bd4e52249a4b1323b5af
dirty: false
submodules: none
```

DiskANN identity:

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
source_path: upstream/diskann-cpp-main
dirty: false
```

Line numbers refer to those exact commits.

## Official HNSW-Merger source facts

| ID | Symbol / surface | Fixed source | Inputs | Observed behavior | Evidence |
|---|---|---|---|---|---|
| HM-001 | `HierarchicalNSW::search2Layer` | `HNSW-Merger/extension.h:31-152` | query, entry, higher/lower layer, lambda, offset | greedy upper-level descent followed by bounded bottom-layer candidate/top queues; returns at most lambda candidates | `source_verified` |
| HM-002 | `HNSWMerger` entry | `HNSW-Merger/extension.h:435-455` | two HNSW indexes, `L2Space`, lambda=4, alpha=1.05, optional M/efc | swaps inputs when index1 is larger or has a higher max level; allocates an out-of-place merged HNSW index | `source_verified` |
| HM-003 | forward stage | `HNSW-Merger/extension.h:619-699` | source nodes, target graph, lambda | OpenMP loop searches source nodes into target; records reverse candidates; unions forward hits with source original neighbors; prunes only over degree | `source_verified` |
| HM-004 | reverse candidate store | `HNSW-Merger/extension.h:619-655` | forward hits | each target hit receives the source node in `candidateSetIndex2`, protected by a per-target mutex | `source_verified` |
| HM-005 | backward direct-connect | `HNSW-Merger/extension.h:704-755` and following | reverse candidates, target original neighbors | unions recorded candidates and original target adjacency and prunes if over degree; does not execute reverse search | `source_verified` |
| HM-006 | HNSW prune heuristic | `HNSW-Merger/hnswalg.h:530-569` | distance-ordered queue, M, alpha | applies HNSW diversification heuristic with alpha and returns at most M | `source_verified` |
| HM-007 | multi-index mode | `HNSW-Merger/experiment.cpp:310-373` | HNSW index paths, multi mode | loads all indexes, supports LARGE_FIRST/SMALL_FIRST/RANDOM, repeatedly merges the first pair and reinserts result according to mode | `source_verified` |
| HM-008 | large-first comparator | `HNSW-Merger/experiment.cpp:83-92` | element counts | comparator is `a.count >= b.count`; no stable lineage tie-break | `source_verified` |
| HM-009 | lambda in multi mode | `HNSW-Merger/experiment.cpp:343-358` | config lambda | adaptive formulas are commented; active call passes the configured lambda unchanged | `source_verified` |
| HM-010 | config defaults | `HNSW-Merger/test_config.h:44-70` | config file | M=32, efc=64, thread=1, lambda=4, alpha=1.05, LARGE_FIRST | `source_verified` |
| HM-011 | build surface | `HNSW-Merger/Makefile:1-22` | GNU C++, OpenMP, local headers | builds `experiment.cpp` directly with `-O3` and `-fopenmp`; no paper Merge library target | `source_verified` |
| HM-012 | repository CMake | `CMakeLists.txt:1-77` and following | HNSWlib headers/examples | exposes an HNSWlib interface and examples/tests, not a Vamana Merge target | `source_verified` |
| HM-013 | README Vamana claim | `README.md:199-205` | figure link | headings for multi-index and Vamana extension; no callable or storage contract | `repository_claim_only` |
| HM-014 | vendored dependency | root and `HNSW-Merger/hnswalg.h` | repository tree | modified HNSWlib sources are in the same Git tree; no submodule identity exists | `source_verified` |
| HM-015 | thread configuration | `HNSW-Merger/experiment.cpp:20-80,107,172,201`; `extension.h:623,704`; `test_config.h:61,296` | config `thread`, OpenMP environment | config thread drives the experiment's `ParallelFor` build calls; merger forward/backward loops use OpenMP pragmas and no `omp_set_num_threads` call is present | `source_verified` |

## Paper facts used by the adaptation

| ID | Paper location | Fact | Evidence |
|---|---|---|---|
| P-001 | Algorithm 1 | graph search starts from an entry, maintains candidate/top sets and returns bounded nearest candidates | `paper_verified` |
| P-002 | Algorithm 4, lines 7-15 | source nodes search target, forward hits are retained for reverse candidates, and source existing neighbors plus hits are pruned to M | `paper_verified` |
| P-003 | Algorithm 4, lines 16-22 | target nodes use recorded candidates plus existing target neighbors; there is no backward search | `paper_verified` |
| P-004 | Section 3.3 | smaller graph searches into larger graph; merge is out-of-place and search target stays immutable | `paper_verified` |
| P-005 | Theorem 6.2 / Section 6 | repeatedly merge the two largest indexes and replace the pair with its result | `paper_verified` |
| P-006 | Section 7.1 | Vamana may be treated as a single-layer graph using the same forward candidates, backward records and pruning sequence | `paper_verified` |
| P-007 | Section 7.2 | two-index lambda defaults to 4; multi-index lambda interpolates in log larger-index size from `(lambda0,N0)` to `(M,M*N0)`, clamps at M and resets | `paper_verified` |
| P-008 | Section 4 | forward searches can run in parallel with synchronized reverse candidate collection | `paper_verified`; parallel implementation deferred |

## Pinned DiskANN primitives and contracts

| ID | Symbol / contract | Fixed source | Observed behavior | Planned use |
|---|---|---|---|---|
| DA-001 | `GraphMerger::merge` | `include/diskann_pipeline/stage_interfaces.hpp:40-50` | accepts Partition ArtifactRef, LocalGraph vector, manifest path and StageContext | unchanged M1 interface |
| DA-002 | `PipelineExecutionRequest` | `include/diskann_pipeline/pipeline.hpp:28-47` | supports full chain and optional Partition reuse only | minimal optional Merge-only input required |
| DA-003 | `PipelineExecutor::execute` | `src/pipeline/pipeline_executor.cpp` | validates Dataset, resolves full chain, executes/reuses Partition, executes all Local Builds, then Merge | reuse all validation/publication/evidence logic |
| DA-004 | `ArtifactReuseService::resolve` | `src/pipeline/artifact_reuse.cpp:23-58` | fresh-validates sources and checks type/version/ID space/capabilities | reuse for Merge-only inputs |
| DA-005 | `VectorAccessor` / `DistanceFunction` | `include/diskann_pipeline/stage_context.hpp:26-39` | GlobalNodeId vector access and distance evaluation | M1 search/prune services |
| DA-006 | `Index::search` | `upstream/diskann-cpp-main/include/index.h:134` and `src/index.cpp:1975+` | callable only on a fully loaded Index/data store | not used |
| DA-007 | `Index::prune_neighbors`, `occlude_list` | `include/index.h:267-278`, `src/index.cpp:1072-1210` | Index-owned occlusion/pruning using internal datastore and scratch | source-map semantics; not directly callable |
| DA-008 | `InMemGraphStore` | `include/in_mem_graph_store.h:11-34`, `src/in_mem_graph_store.cpp` | public native graph load/get/set/store and header entry | native input/output reuse |
| DA-009 | M0 `merge_shards` call | `src/baseline/cpp_main/m0_merge.cpp:250-407` | stages exact maps/graphs, invokes pinned M0, validates graph+medoids, publishes GlobalGraph/v1 | unchanged Baseline comparison |
| DA-010 | M0 descriptor | `src/baseline/cpp_main/descriptors.cpp:48-70` | Partition+Local requires; GlobalGraph+medoids provides; cancellation/metrics services | M1 parallel descriptor shape plus vector/distance |
| DA-011 | GlobalGraph validation | `src/validation/artifact_validator.cpp:410+`, `src/baseline/cpp_main/native_formats.cpp:120-209` | validates graph/medoids capability, native structure, range/degree, Dataset and complete source identities; current native loop does not compare neighbor with current node | add only native GlobalGraph self-edge rejection; retain as sole M1 Artifact validity grant |
| DA-012 | native evaluator | `src/pipeline/evaluator.cpp:54-105` | accepts validated cpp_main native GlobalGraph with medoids and creates separate integrity EvaluationReport | unchanged for M1 |
| DA-013 | P0 map capability | `src/baseline/cpp_main/descriptors.cpp:8-29` | complete ascending LocalToGlobal map and overlap capabilities | input mapping truth |
| DA-014 | fixed graph profile | `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml` | R=16, L=32, L2, B0 alpha=1.2, max occlusion=750, no saturation | inherited M1 DiskANN parameters |

## Explicit source limits

- The pinned repository does not supply a DiskANN/Vamana implementation, DiskANN native parser or
  compatible callable library.
- The source swaps on HNSW max level as well as count; max-level behavior is inapplicable to a
  single-layer Vamana graph.
- The source multi-index executable uses fixed lambda; paper adaptive lambda is not active code.
- The source comparator has no stable equal-size ordering.
- Neither paper nor source defines cross-shard replica normalization.
- Paper Section 7.1 reports two Vamana indexes; combining it with Section 6 for many overlapping
  DiskANN shards is a faithful adaptation whose compatibility rules must remain explicit.
- No Query/Search or performance result in the paper is validation evidence for this structural
  Feature.
