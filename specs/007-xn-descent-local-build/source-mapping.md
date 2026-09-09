# XN-Descent Algorithm and Source Mapping

**Feature**: `007-xn-descent-local-build`

**Paper**: *XN-Graph: A High-Performance Disk-based Graph Index for Approximate Nearest Neighbor
Search*

**Paper file**: `/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/Sigmod_2026/sigmod2026_papers/0721_论文/其他_results/其他_papers/XN-graph.pdf`

**DiskANN format/M0 reference**: `microsoft/DiskANN`, `cpp_main`,
`78256bbab4685e1774e78d331e081a153be26823`

This mapping separates paper-stated semantics, repository-required faithful adaptations, and
engineering safety policies. Planned paths name expected implementation ownership; they do not
assert that code exists in this Plan phase.

## 1. Paper Algorithm 1 mapping

| Paper location | Paper-stated semantic | Frozen design interpretation | Planned implementation/test ownership |
|---|---|---|---|
| Section 3.2, graph definitions | `C[i]` keeps up to `omega` nearest intermediate neighbors; `R[i]` is the reverse neighborhood from the preceding iteration; `G[i]` keeps up to `lambda` diversified extended neighbors | `C/G` are owner-distance ordered and bounded; `R` is unique but not capacity-bounded | `xn_descent_algorithm.*`; state/oracle tests |
| Algorithm 1, initialization | Initialize every `G[i]` and `R[i]` empty and `C[i]` with `omega` random points | Use all available unique non-self points when fewer than `omega`; otherwise use the versioned per-node seeded sampler | `parameters.*`, `xn_descent_algorithm.*`; initialization tests |
| Algorithm 1, convergence loop | Repeat until `Converged`; the paper does not define the predicate | Execute full deterministic synchronous rounds; converge immediately on complete normalized `C/R/G` equality or after three consecutive stable-`C/G` rounds covering a possible `R` two-cycle; cap is separate | round engine; fixed-point/two-cycle/cap tests |
| Algorithm 1, `C_hat[i] <- C[i] union R[i]` | Current near and prior reverse neighborhoods form candidates | Candidate membership comes from the round-start snapshot and is traversed by `(d2(i,u),u)` | candidate builder; oracle tests |
| Algorithm 1, clear `R` | Reverse state is regenerated each iteration | Build `R_(t+1)` from current-round reverse proposals; commit next round | reverse proposal/reducer; round tests |
| Algorithm 1, `R[u] <- R[u] union {i}` | Every candidate relation creates a reverse relation | Emit the reverse proposal before candidate acceptance/occlusion outcome; do not cap `R` | reverse proposal/reducer; occluded-candidate tests |
| Algorithm 1, `flag[u] <- true` | Per-candidate acceptance starts true | `flag` is local logical state for the current `(i,u)` check, not a global or cross-thread shared flag | node-owned working loop; race/determinism tests |
| Algorithm 1, iterate `v in G[i]`, `v != u` | Compare the candidate with every relevant current graph neighbor | Iterate canonical working `G[i]`; do not terminate the loop after finding an occluder | working-G loop; multi-occluder oracle |
| Algorithm 1, `TryInsert(u,C[v],omega)` | Cross-update `u` into the neighborhood centered on `v` | Target center is `v`; compare/store `d2(v,u)` | C proposal `(target=v,neighbor=u)`; center-specific tests |
| Algorithm 1, `TryInsert(v,C[u],omega)` | Cross-update `v` into the neighborhood centered on `u` | Target center is `u`; compare/store `d2(u,v)` | C proposal `(target=u,neighbor=v)`; center-specific tests |
| Algorithm 1, occlusion test | An occluding `v` clears the candidate flag | Accumulate `flag=false` but continue all later `v` visits so every required cross-`TryInsert` has equivalent effect | working-G loop; occluded-cross-update oracle |
| Algorithm 1, if flag then remove occluded `v` | A surviving candidate removes current graph members it occludes | Remove from the current node-owned working `G[i]` before candidate insertion | working-G mutation; removal tests |
| Algorithm 1, `TryInsert(u,G[i],lambda)` | Insert accepted candidate and retain bounded closest members | Apply set insertion/top-`lambda`; if `u` is then evicted, prior removals remain | TryInsert helper; capacity/rollback tests |
| Algorithm 1, loop order | `G[i]` is changed during the candidate loop | Earlier changes to working `G[i]` are visible to later candidates for the same `i` in the same round | per-node sequential candidate loop; ordered oracle |
| Algorithm 1, return `G` | Final extended-neighborhood graph is `G` | Serialize normalized `G` as LocalNodeId adjacency after convergence only | B1 stage/native writer; Artifact tests |

## 2. Equation (2)

Section 2.2 defines candidate `u` as occluded by `v` for center `i` only when both strict ordinary-
distance comparisons hold:

```text
dist(i,v) < dist(i,u)
alpha * dist(v,u) < dist(i,u)
```

For the current service's squared L2 values, the exact intended mathematical form is:

```text
d2(i,v) < d2(i,u)
alpha^2 * d2(v,u) < d2(i,u)
```

Both comparisons remain strict. Equality in either one is non-occluding. Sorting and LocalNodeId
tie-breaking are independent of this predicate. The planned scale-safe comparator evaluates the
`alpha^2` product relation without overflowing an intermediate `double`; it never uses
`alpha * d2(v,u)`.

For the later removal direction—an accepted candidate `u` occluding an existing member `v`—the
roles reverse and both of these must hold:

```text
d2(i,u) < d2(i,v)
alpha^2 * d2(u,v) < d2(i,v)
```

Reusing the candidate-rejection labels without this role reversal would remove the wrong nodes.

Planned ownership: private Equation (2) helper in `xn_descent_algorithm.*`, with equality,
wrong-`alpha`, non-finite, zero-distance, overflow-range, and ordinary cases in
`xn_descent_equation_try_insert_test.cpp`.

## 3. TryInsert

The paper's `TryInsert(u, N, L)` performs logical set insertion, then repeatedly removes the member
farthest from the target center until size is at most `L`. This design freezes the unspecified tie
case as ascending `(squared_distance_to_target, LocalNodeId)`: the first `L` records survive.

Important center mapping:

- `TryInsert(u, C[v], omega)` is centered on `v`, not on the outer-loop `i`;
- `TryInsert(v, C[u], omega)` is centered on `u`, not on `i` or `v`'s former owner;
- `TryInsert(u, G[i], lambda)` is centered on `i`.

Duplicate insertion is idempotent. Self candidates are invalid state and cannot be used to fill a
capacity. The helper must preserve the closest-`L` meaning regardless of proposal arrival order.

## 4. Paper-unspecified behavior

The paper does not uniquely define:

- PRNG algorithm, default seed, per-node derivation, or sampling mapper;
- candidate order and equal-distance tie-breaking;
- cross-thread write visibility or conflict resolution;
- the exact `Converged` predicate or an iteration cap;
- local graph entry-point selection;
- native LocalGraphArtifact encoding, Manifest fields, failure status, or downstream admission;
- resource-budget accounting and runtime allocation policy.

These items are not attributed to Algorithm 1. Their chosen policies are versioned faithful
adaptations in `research.md` and the Feature-local contracts.

## 5. Clarification-bound faithful adaptations

| Adaptation | Confirmed observable rule | Design owner |
|---|---|---|
| Degree fields | `declared_max_degree=lambda`; observed metadata and native header equal payload actual maximum; observed `<=` declared | B1 serializer/metadata builder |
| Round visibility | Candidates from boundary `C/R`; cross-`C` and reverse-`R` next-round-visible; earlier node-owned `G[i]` mutations current-round-visible | synchronous round engine |
| Conflict outcome | Snapshot reads and deterministic target reductions yield a unique result independent of valid threads/scheduling | proposal reducers |
| Convergence | Adjacent full normalized `C/R/G` equality is the immediate fixed point; otherwise three consecutive complete stable-`C/G` rounds cover both possible `R` phases | direct state comparator, stable-round counter, condition evidence encoder |
| Cap result | Non-converged cap hit is unsuccessful, records evidence, and publishes no valid Artifact | B1 stage/lifecycle owner |
| Replay scope | Exact within one declared platform/toolchain/numerical environment; valid thread changes invariant; no macOS/Linux equality promise | parameter/Manifest contract and replay tests |
| P0 Partition as `S` | Selected Partition membership substitutes for the paper's full collection `S` | B1 binding; deviation record |
| Local entry point | Deterministic selected-Partition local medoid | B1 binding; adaptation record |

## 6. Current repository and pinned Baseline mapping

| Concern | Current authoritative source | Constraint on B1 |
|---|---|---|
| Local stage interface | `include/diskann_pipeline/stage_interfaces.hpp` | Reuse `LocalGraphBuilder::build(PartitionArtifact, PartitionId, LocalBuildConfig, StageContext)` |
| Runtime services/budgets | `include/diskann_pipeline/stage_context.hpp` | Reuse vector/distance/cancel/metrics and never exceed positive memory/thread budgets |
| LocalGraph metadata | `include/diskann_pipeline/artifacts.hpp`; `contracts/v1/schemas/local-graph-artifact.schema.json` | Reuse v1 and LocalNodeId; do not add XN fields or Artifact v2 |
| Descriptor | `include/diskann_pipeline/implementation.hpp` | Declare exact Dataset/Partition requirements, services, LocalGraph output, ID spaces, and capability |
| Candidate admission | `src/pipeline/pipeline_executor.cpp`; `src/validation/artifact_validator.cpp` | Existing Validator and producer/consumer handoff run before publication/downstream |
| Parameter validator gap | `src/validation/parameter_validator.cpp` | Runtime B1/runner must enforce numeric, enum, finite, and conditional semantics, not rely on schema keywords alone |
| P0 membership/maps | `specs/002-cpp-main-upstream-baseline/source-mapping.md` SM013–SM018; `p0_partition.cpp` | Select exact PartitionId and copy its complete map; P0 centroid is not an entry point |
| B0 entry concept | pinned `in_mem_data_store.cpp` centroid/medoid selection and `index.cpp` link/save path | B1 local medoid is conceptually aligned but explicitly a separate deterministic adaptation |
| Native local graph | `specs/002-cpp-main-upstream-baseline/contracts/native-formats.md` section 6; pinned `in_mem_graph_store.cpp` | Emit exact reviewed header/rows; actual observed maximum in header; zero frozen points |
| Native validator | `src/baseline/cpp_main/native_formats.cpp`; `src/validation/artifact_validator.cpp` | Reuse length/range/header/degree checks; add producer tests/checks for self/duplicate adjacency |
| M0 input | `src/baseline/cpp_main/m0_merge.cpp`; source mapping SM025–SM028 | Preserve exact native map/graph media, lineage, local entry mapping; do not add pruning/refinement |
| M0 degree limit | `m0_merge.cpp` checks declared degree `<=10` only while loading built locals | Feature runner must reject incompatible composition before P0/B1; M0 remains unchanged |
| Generic executor modes | `include/diskann_pipeline/pipeline.hpp`; `pipeline_executor.cpp` | Full P→B→M and merge-only exist; paper-style needs small Feature-local local-only lifecycle |
| Manifest evidence | `include/diskann_pipeline/provenance.hpp`; stage-owned manifest handling in `pipeline_executor.cpp` | Reuse BuildManifest/status/failure/metrics; precise non-convergence reason does not need public enum |
| Registry pattern | existing OGP/HNSW paper runners; empty `production_registry.cpp` | Register B1 only in Feature-local runner; do not alter production Registry |

## 7. Native-format provenance boundary

The cpp_main native-format document is Baseline-local and binds admission to the reviewed media,
payload role, pinned format source, and declaring descriptor. B1 does not claim to be B0. Its
Feature-local encoding contract therefore records:

1. distinct B1 algorithm/implementation identity and current project source;
2. exact pinned DiskANN commit as native byte-layout source identity;
3. exact local graph/map roles and media required by current M0;
4. the B1 descriptor and runner as the admitted producer/execution identity;
5. existing formal Validator evidence plus B1-specific self/duplicate checks.

This is an additive producer binding, not an alteration of the byte layout, public Artifact
schema, M0, or upstream source. Any encoding deviation must fail closed rather than be labeled
cpp_main-native.

## 8. Deviations and substitutions

| Paper/system element | Current Feature choice | Classification and claim effect |
|---|---|---|
| Full in-memory set `S` | One selected P0 Partition | faithful adaptation; local component only |
| Boundary-adaptive Balanced Partition | Current fixed P0 k-means overlap partition | component substitution; no BBP claim |
| Unspecified concurrency | deterministic synchronous rounds/proposals | faithful adaptation; required for replay |
| Unspecified convergence/cap | immediate full normalized fixed point, or proved three-round stable-`C/G` closure across a possible `R` two-cycle, plus safety-cap failure | faithful adaptation based on deterministic cycle evidence; stable `G` alone remains insufficient |
| Unspecified entry point | deterministic local medoid | pipeline-contract adaptation |
| Paper graph serialization | existing LocalGraphArtifact/v1 plus pinned cpp_main native payload | repository/M0 integration adaptation |
| `lambda=64` all-memory evaluation | isolated B1-only `paper-style` path | algorithm validation only; not current M0-compatible pipeline |
| Secondary Index, IMF Search, SSD layout/cache | not implemented | no full-system, SSD, hop, QPS, or latency reproduction claim |

## 9. Planned implementation ownership

```text
src/papers/xn_graph/xn_descent/
├── parameters.*                   # schema companion, semantic/profile validation, policy IDs
├── xn_descent_algorithm.*         # initialization, ordering, Equation (2), TryInsert, rounds
├── xn_descent_local_build.cpp     # Artifact binding, budgets, entry point, serialization/Manifest
├── descriptor.cpp                 # requires/provides and identity
├── stage_binding.hpp              # private factory/identity declarations
└── xn_descent_experiment_runner.cpp
                                    # early composition gate and isolated local-only lifecycle
```

The implementation may consolidate private files, but it must not move these responsibilities into
generic pipeline, M0, production Registry, Harness, Loop Engine, or upstream code.
