# Implementation Plan: HNSW-Merger DiskANN Merge Adaptation

**Branch**: `feature/merge-adaptation` | **Date**: 2026-08-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from
`specs/004-hnsw-merger-diskann-merge-adaptation/spec.md`.

## Summary

Add one production Merge implementation,
`hnsw_merger_vamana_merge_v1` version `1.0.0`, beside the unchanged cpp_main M0. M1 consumes the
existing validated PartitionArtifact and complete LocalGraphArtifact collection, maps local
adjacency through each LocalToGlobal map, and performs the paper-backed sequence:

```text
single-layer forward graph search
-> cross-graph candidates
-> lazy backward direct-connect
-> existing-neighbor union
-> bounded DiskANN/Vamana pruning
-> existing GlobalGraphArtifact/v1
```

The official paper source is pinned to
`https://github.com/Kimchuls/HNSWMerger.git`, branch `main`, commit
`88f6b7179f651cf5f284bd4e52249a4b1323b5af`. The repository implements HNSW merging but does not
contain a directly callable DiskANN/Vamana implementation. M1 is therefore a project-owned
faithful adaptation: paper Section 7.1 is the Vamana authority, while the pinned HNSW source maps
forward search, candidate retention, backward direct-connect, and pruning control flow. No HNSW
index or HNSW storage representation enters the pipeline.

One minimal backward-compatible executor amendment adds an optional Merge-only input set to
`PipelineExecutionRequest`. It fresh-validates one DatasetArtifact, one PartitionArtifact, and the
complete ordered LocalGraphArtifact collection, then invokes only the selected Merge factory. When
the option is absent, the existing full P0 -> B0 -> Merge path is byte-for-byte contract-compatible.
This is not an arbitrary resume API or DAG executor.

## Technical Context

**Language/Version**: Public and shared implementation boundary C++17; Python 3.10+ only for the
existing Harness/config layer. The pinned HNSW source is audited but not compiled or linked into
M1.

**Primary Dependencies**: Existing `diskann_pipeline_core`, static Registry, Validator,
`ArtifactReuseService`, cpp_main native format support, fixed `diskann_s`, `VectorAccessor`,
`DistanceFunction`, `InMemGraphStore`, nlohmann JSON internally, CMake and CTest. No new external
runtime dependency.

**Storage**: Immutable JSON Artifact metadata, BuildManifest/v1, ValidationEvidence and
EvaluationReport documents; native DiskANN uint32 ID maps, local graphs, merged graph and medoids;
one Feature-local same-input comparison summary.

**Testing**: CTest for unit/contract/integration tests; pytest for the thin comparison Harness and
SIFT1M admission; existing cpp_main and Fake pipeline regressions.

**Target Platform**: Linux x86_64 is the formal structural-validation platform. macOS may run
unit/contract tests that do not execute the fixed Linux DiskANN build.

**Project Type**: C++ library plus thin experiment runner integrated into the existing modular
pipeline.

**Study Class**: `single_paper_reproduction`; `faithful_adaptation`; replacement scope `merge`.

**Baseline Reference**: Microsoft DiskANN, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`, fixed read-only path
`upstream/diskann-cpp-main`. Baseline composition remains P0+B0+M0.

**Paper Source Reference**: HNSWMerger, branch `main`, commit
`88f6b7179f651cf5f284bd4e52249a4b1323b5af`, clean, no Git submodules. Vendored HNSWlib content is
covered by the same commit. The official repository is an algorithm/source-mapping authority, not
an M1 binary dependency.

**Compiler/Build Flags**: Formal builds use out-of-tree CMake with `CMAKE_BUILD_TYPE=Release`,
`BUILD_TESTING=ON`, `DISKANN_CPP_MAIN_BASELINE=ON`, and the Feature option
`DISKANN_HNSW_MERGER=ON`. The terminal BuildManifest records the actual compiler path/version,
generator, CMake version, all effective definitions and project compile options, and actual
configure/build commands. No file is generated in either upstream source tree.

**Dataset Identity**: Small deterministic and small real float32/L2 fixtures first. The only scale
run is SIFT1M `sift1M_dataset/base_vector.bin`, 1,000,000 x 128 float32, 512,000,008 bytes, SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`. Query and ground-truth
files are out of scope.

**Performance Goals**: None. Merge time, memory, Recall, QPS and latency are not acceptance
criteria.

**Constraints**: R=16, L2, alpha=1.2 and max candidate count 750 are inherited from the fixed
cpp_main graph profile where applicable. One deterministic large-first strategy, one frozen
SIFT1M structural configuration, no tuning, no changed-config retry, no best-run selection, and no
dataset above SIFT1M.

**Scale/Scope**: Multiple real LocalGraph shards including cross-shard replicas; unit fixtures,
small real closure, same-input small M0/M1 comparison, then one bounded SIFT1M structural run.

## Constitution Check

*GATE: Passed before research and re-checked after the design below.*

- [x] **Baseline immutability**: DiskANN and HNSWMerger sources are read-only; all project code,
      builds and outputs remain outside those trees.
- [x] **Pinned provenance**: DiskANN and HNSWMerger repository/branch/full commits are fixed.
      Formal manifests must capture the project adaptation source, actual compiler/build identity,
      Dataset checksum, immutable inputs and full effective parameters.
- [x] **Implementation coexistence**: M1 has stable identity
      `hnsw_merger_vamana_merge_v1`; M0 and the fixed Baseline runner remain unchanged.
- [x] **Node identity**: Every LocalNodeId is resolved only through its owning complete
      LocalToGlobal map; M1's intermediate and output domains use GlobalNodeId.
- [x] **Three-layer outputs**: Artifact, BuildManifest, ValidationEvidence and EvaluationReport
      remain separate. The Feature-local comparison summary is not a validity grant.
- [x] **Compatibility gate**: M1 declares multi-input requirements, output capabilities and
      runtime services. Every reused input and output is fresh-validated before downstream use.
- [x] **Study/report separation**: The work is reported as a single-paper faithful adaptation,
      not a Baseline result or complete HNSW-Merger reproduction.
- [x] **Fair experiment protocol**: Both Merge runs consume the exact same immutable references;
      one strategy/configuration is frozen and no parameter search is authorized.
- [x] **Validation and approval**: Unit, contract, deterministic fixture, small real, same-input
      comparison, regressions and SIFT1M gates are ordered. No larger run is planned.

## Evidence Classification

Every algorithm statement in research, source mapping, code comments and manifests uses one of:

| Class | Meaning |
|---|---|
| `paper_verified` | Stated by the SIGMOD paper, including its Vamana discussion. |
| `source_verified` | Observed in the pinned official repository or pinned DiskANN source. |
| `repository_claim_only` | Claimed by README/figures but not backed by a repository Vamana implementation. |
| `DiskANN_compatibility_adaptation` | Required to map disjoint HNSW/Vamana paper inputs to current LocalGraph replicas, DiskANN degree/pruning, native output or lineage. |
| `implementation_probe_required` | Design is fixed, but behavior must be demonstrated by unit/small-real execution before SIFT1M. |
| `deferred/unsupported` | Explicitly excluded from this Feature. |

## M1 Implementation Boundary

### Identity and declaration

M1 is `hnsw_merger_vamana_merge_v1` version `1.0.0`, stage `merge`,
`test_only=false`.

It requires:

- `partition-artifact/v1`, Local ID space, capability
  `partition.complete_local_to_global_map/v1`;
- `local-graph-artifact/v1`, Local ID space, capability `graph.local_adjacency/v1`;
- StageContext services `vector_accessor/v1`, `distance_function/l2-v1`,
  `cancellation_token/v1`, and `metrics_sink/v1`.

It provides `global-graph-artifact/v1`, Global ID space, with
`graph.global_adjacency/v1` and `graph.medoids/v1`. Its parameter schema is
[contracts/hnsw-merger-vamana-parameters.schema.json](contracts/hnsw-merger-vamana-parameters.schema.json).
The schema fixes the one approved algorithm profile; it does not expose a strategy selector or
search space.

### Source and build identity

For M1's parent BuildManifest, authoritative `source` is the repository/branch/full commit and
dirty state of the project-owned adaptation code actually compiled into M1. M1-specific execution
provenance is carried by an existing stage-owned BuildManifest/v1, not by the algorithm parameter
schema or request-level `source_config`. Every actual M1 Merge invocation creates that existing
document and returns its path through the unchanged `StageResult.build_manifest_path`.

The stage-owned BuildManifest records:

- `paper_source_identity`: pinned HNSWMerger repository/branch/full commit/clean state;
- `diskann_source_identity`: the fixed cpp_main repository/branch/full commit used for native graph
  I/O and DiskANN-compatible pruning semantics;
- paper DOI and PDF SHA-256;
- explicit evidence classifications and deviations;
- the actual toolchain/build commands and frozen effective parameters;
- the actual effective Merge policy and every `PairwiseMergeDecision` reached by the invocation;
- its terminal invocation status and failure evidence when applicable.

The same stage-owned parameters record `integration_mode=source_mapping_only` and
`official_binary_linked=false` separately from the SourceIdentity-shaped
`paper_source_identity`. The formal build identity is the actual compiled M1 adaptation and its fixed
DiskANN linkage; no official HNSW binary is built merely to manufacture provenance.

The existing PipelineExecutor confines, parses and validates the returned BuildManifest, persists an
immutable copy in the current run manifest area, computes its SHA-256, and adds only
`parameters.stage_execution_manifest {path, sha256}` to the parent Merge BuildManifest. It does not
copy M1 execution facts into the parent parameters. The stage-owned document records M1 execution;
the parent records the pipeline Merge stage, validation/publication/quarantine outcome and reference
to that execution evidence. Neither document grants Artifact validity: the existing Validator and
PipelineExecutor publication path remain authoritative. An empty `build_manifest_path` preserves M0
and all other legacy-stage behavior.

The stage-owned lifecycle is fixed: source/build admission failure before invocation creates no M1
execution Manifest; M1 semantic failure creates a terminal `FAILED` stage-owned Manifest and returns
an empty candidate; cancellation creates a terminal `CANCELLED` stage-owned Manifest and returns an
empty candidate; successful algorithm completion creates a terminal `SUCCEEDED` stage-owned
Manifest and returns a candidate. If later existing validation rejects that candidate, the
stage-owned execution status remains `SUCCEEDED` while the parent Merge Manifest records
`INVALID_OUTPUT` and quarantine. Stage-owned `output_artifacts` remains empty because publication is
owned by the parent executor lifecycle.

This records all three sources without pretending the unlinked official HNSW code generated the
Artifact, without embedding a commit in `implementation_id`, changing BuildManifest/v1 or adding a
provenance framework. The current parent checkout has no configured `origin`; planning and unit
research are not blocked, but a formal source-backed run MUST fail admission until a trustworthy
canonical project repository identity and full adaptation commit are available and recorded. An
absolute local path is not a substitute.

## Algorithm Mapping

### Initial graph materialization

1. Fresh-validate the Dataset, aggregate Partition and every LocalGraph in ascending PartitionId
   order.
2. Read each native uint32 ID map and local graph through existing cpp_main native-format support
   and `InMemGraphStore`; map the local entry point and every local neighbor to GlobalNodeId.
3. Create one Feature-private immutable `LogicalGraph` per Partition. Nodes are ordered by
   GlobalNodeId. Adjacency candidates are de-duplicated by GlobalNodeId only after validating
   range and ownership; self edges fail rather than being normalized away.
4. Keep vectors external and resolve them by GlobalNodeId through the validated `VectorAccessor`.
   Distances use the StageContext L2 `DistanceFunction`.

No HNSW index, level, label offset, storage layout or round-trip conversion is created.

### Two-graph faithful merge

For each selected pair:

1. Choose the operand with fewer unique logical GlobalNodeIds as the source and the larger as the
   target. Equal sizes use the stable lineage tie-break below.
2. Keep the target adjacency immutable during all forward searches (`paper_verified` out-of-place
   rule).
3. For every source-exclusive node, run the Feature-local single-layer form of paper Algorithm 1
   from the target entry and retain the effective top-lambda distinct target GlobalNodeIds.
4. Record each source node in every forward hit's backward candidate list.
5. For each source-exclusive node, union its original neighbors with forward hits. For each target
   node, union its original neighbors with recorded backward candidates. Shared replica nodes use
   the overlap rule below.
6. De-duplicate candidates by GlobalNodeId, reject self/out-of-range candidates, sort by
   `(distance, GlobalNodeId)`, and prune only if the candidate count exceeds R.
7. Write a new Feature-private logical graph. No backward search is performed.

The forward search helper and pruning helper are private to this implementation. They are not
`GenericGraphSearch`, `GenericGraphPruner`, an Adapter, or a public framework.

### Search and pruning primitives

The pinned DiskANN `Index::search` requires a loaded Index/data-store representation; its
`prune_neighbors` and `occlude_list` helpers are not public callable APIs. Reusing them would
require constructing another index representation or modifying upstream, both prohibited.

Therefore:

- `hnsw_merger_single_layer_search` is a minimal Feature-local implementation of paper Algorithm
  1 over an immutable GlobalNodeId adjacency view, with distance/ID stable ordering;
- `hnsw_merger_vamana_prune` is a minimal Feature-local translation of the pinned cpp_main
  `occlude_list` behavior for R=16, alpha=1.2, max candidates=750 and no saturation: distance/ID
  sort, max-candidate truncation, progressive `cur_alpha` 1.0 then 1.2, and the L2 occlusion ratio
  `distance(query,candidate) / distance(selected,candidate)`. This is explicitly a
  `DiskANN_compatibility_adaptation`, because the official source's HNSW heuristic and degree
  layout are not the target Vamana contract;
- existing `VectorAccessor`, `DistanceFunction`, native reader/validator and `InMemGraphStore`
  load/store functions are reused directly.

No random or node-0 entry is allowed.

## Multi-Shard Strategy and Lambda Policy

### Large-first pairwise chaining

The only formal strategy is the paper's large-first pairwise chaining:

1. Logical graph size is the count of unique GlobalNodeIds in the operand; overlap replicas are not
   counted twice.
2. Select the two operands with largest logical sizes.
3. Sort equal-size operands by the lexicographic ascending vector of contributing PartitionIds.
4. Within an equal-size pair, the lexicographically smaller lineage is the target and the other is
   the source. Otherwise the smaller operand is source and larger operand target.
5. Replace the pair with the merged logical graph and repeat until one graph remains.
6. Intermediate lineage is the sorted union of contributing PartitionIds and original
   LocalGraph Artifact identities. Intermediate graphs are in-memory implementation state, not new
   Artifacts.
7. A valid one-Partition input has no pairwise step: M1 maps that one local graph to GlobalNodeId,
   applies the same coverage/self/range/degree checks, and writes the existing GlobalGraph/medoids
   representation. This degenerate identity merge is a compatibility adaptation; formal
   multi-shard validation still requires at least two inputs.

The stable tie-break and unique-node size definition are `DiskANN_compatibility_adaptation` rules;
the large-first pairwise strategy and smaller-to-larger direction are `paper_verified`.

### Effective lambda

Lambda is an internal deterministic policy, not a sweepable user parameter.

- Base `lambda0=4` (`paper_verified`, `source_verified`).
- Degree ceiling `lambda_max=R=16` uses the fixed DiskANN output degree rather than HNSW M
  (`DiskANN_compatibility_adaptation`).
- For an epoch with base larger-operand size `N0`, current larger-operand size `N`, and
  `Nmax=R*N0`:

  ```text
  lambda_real = lambda0 + (R-lambda0) * ln(N/N0) / ln(R)
  lambda_policy = clamp(floor(lambda_real), lambda0, R)
  effective_lambda = min(lambda_policy, target_unique_node_count)
  ```

- The first pair establishes N0 and uses lambda0. If a pair reaches R, that pair uses R; before the
  next pair, a new epoch begins with lambda0 and N0 set from that next pair.

The logarithmic interpolation, R ceiling and reset are `paper_verified`. Integer floor and the
DiskANN R substitution are explicit compatibility adaptations. Floor agrees with positive C++
integer truncation shown in the pinned source's commented multi-index formula and makes the paper's
unstated rounding deterministic; every pair's sizes, direction and effective lambda are recorded
in the M1 stage-owned BuildManifest.

### Cooperative cancellation

Because the descriptor requires `cancellation_token/v1`, M1 observes the existing service at stage
start, after input loading and semantic preparation, between pairwise Merge rounds, and immediately
before returning a normal GlobalGraph candidate. Cancellation returns
`StageStatus::kCancelled` with an empty `output_candidate`; no new round starts after cancellation and
M1 never publishes an Artifact. The existing PipelineExecutor owns the remaining cancellation
lifecycle. No cancellation framework, report or instrumentation surface is added.

## Partition-Overlap Compatibility Adaptation

The paper's graph pair assumes distinct logical nodes. Current Partitions may contain cross-shard
replicas. M1 applies exactly this normalization at each pairwise step:

1. An operand is keyed by unique GlobalNodeId. Same-operand duplicate logical records are invalid;
   candidate neighbors are a set by GlobalNodeId.
2. If a node occurs in both operands, it is a shared node in the merged output, never two output
   nodes. It is excluded from the source forward-search iteration because it already exists in the
   target graph.
3. The immutable target view used by forward search remains the target operand only. Source
   replica adjacency does not mutate that view.
4. The shared node's final existing-neighbor candidates are the union of both replica adjacency
   lists plus any backward candidates recorded during source-exclusive searches.
5. Source-exclusive nodes use source existing neighbors plus forward hits; target-exclusive nodes
   use target existing neighbors plus backward candidates.
6. Candidate duplication across replica/forward/backward channels is collapsed by GlobalNodeId
   before distance evaluation. Self edges and out-of-range IDs fail closed. Pruning then enforces R.

This preserves all unique replica-local candidate information, creates one logical node per Dataset
GlobalNodeId, and keeps forward search faithful to an immutable target. It is a
`DiskANN_compatibility_adaptation`, not a claim about the paper's original disjoint-index model.

## Entry Point, Medoids and Native Output

- Each initial operand entry is the native local graph header entry mapped through its owning
  LocalToGlobal map.
- A pairwise result retains the chosen target operand entry. This maps the paper/source target-index
  entry behavior to one Vamana layer.
- The final native graph header entry is the final intermediate entry and must be a legal Dataset
  GlobalNodeId.
- The medoids payload contains every initial mapped local entry in ascending PartitionId order.
  The final header entry must be one of these values. This sidecar is a DiskANN downstream
  compatibility adaptation and uses the existing `graph.medoids/v1` capability, not a new Artifact.
- Output is written with existing `InMemGraphStore` semantics and existing cpp_main media types,
  then published as `GlobalGraphArtifact/v1`. `source_artifacts` and structural source identities
  are the PartitionArtifact followed by original LocalGraphArtifacts in ascending PartitionId
  order; no intermediate graph is published as a source Artifact.

M1 performs pre-publication duplicate logical node, candidate and coverage checks. Source audit
found one narrow existing-contract enforcement gap: the native GlobalGraph reader checks
header/node/range/degree/lineage/checksum but does not reject `neighbor == node`. Add a self-edge
check to the existing native GlobalGraph validation path and its regression tests. This does not
change schema, format version, checksum or Validator architecture; it enforces FR-013 for M0 and
M1 before evaluation/reuse. ValidationEvidence from that path is the authority for zero self
edges. The existing Validator remains the only Artifact validity grant, and the current native
graph-integrity evaluator can consume M1 unchanged because the output uses the reviewed cpp_main
global graph and medoids media/capability pair.

## Merge-Only Same-Input Execution

### Proven gap

`PipelineExecutionRequest` currently supports optional Partition reuse, after which it always
executes every B0 and then Merge. `ArtifactReuseService` can fresh-validate arbitrary Artifacts, but
there is no executor entry that accepts a complete LocalGraph collection and invokes only Merge.
Running P0/B0 twice cannot satisfy FR-019.

### Minimal amendment

Add one optional `MergeOnlyInputSet` to `PipelineExecutionRequest`:

```text
partition: ArtifactRef
local_graphs: vector<ArtifactRef>
```

When absent, legacy behavior is unchanged. When present:

1. reject simultaneous `reused_partition` use and reject empty/incomplete input;
2. validate the request Dataset first;
3. resolve and validate only the selected Merge descriptor and its parameters; do not create or
   invoke Partition or Local Build factories;
4. fresh-validate Partition against Dataset;
5. fresh-validate LocalGraphs against the validated Partition, requiring exactly one owner for
   every PartitionId, no missing/duplicate/unknown owner, ascending PartitionId order, identical
   Dataset and complete lineage;
6. run compatibility preflight using the validated available contracts and StageContext services;
7. pass the validated Partition and LocalGraphs as the source set to existing
   `validate_and_publish`, so GlobalGraph top-level and structural lineage is rechecked in full;
8. invoke exactly one selected Merge through the existing Registry factory, validation/publication,
   BuildManifest, quarantine and EvaluationReport flow;
9. record per-executor invocation counts 0/0/1 and stage order `[merge]`.

The Merge BuildManifest input list is exactly DatasetArtifact, PartitionArtifact, then the ordered
LocalGraphArtifact collection. M0 and M1 requests are constructed from the same in-memory immutable
ArtifactRef values. The thin Feature runner invokes `PipelineExecutor` once for M0 and once for M1;
it does not implement stage orchestration.

For the separate small full closure it registers existing P0, existing B0, existing M0 and M1 in a
production Registry, seals once, and selects P0+B0+M1 in the normal request. Stage sources are
DiskANN for P0/B0 and the project adaptation source for M1. The fixed cpp_main Baseline runner is
not changed and still registers/selects only P0+B0+M0.

[contracts/merge-only-execution.md](contracts/merge-only-execution.md) is authoritative for this
minimal C++ boundary and the Feature-private normalized comparison request wire contract. Python
preflight is the sole profile-to-request normalization owner. It validates and writes one immutable
request containing one shared Dataset/Partition/ordered-Local tuple. One Feature runner invocation
consumes that document and calls the existing PipelineExecutor once for M0 and once for M1; it does
not renormalize the profile, duplicate the shared tuple or let M1 consume M0 output. The runner argv
is exactly:

```text
hnsw_merger_experiment_runner execute --request FILE --result FILE
```

This does not extend the generic RunRequest JSON schema, add a multi-result runner schema,
arbitrary-stage resume, or alter M0's algorithm.

The two per-executor results each report `partition=0`, `local_build=0`, `merge=1`. The unchanged
runner-result belongs to the enclosing Feature runner invocation and, on a successful comparison,
reports aggregate counts `partition=0`, `local_build=0`, `merge=2`. The Feature-local summary keeps
its existing schema: its single M0 and single M1 run records and their BuildManifest references,
together with the aggregate runner count, prove that each arm executed exactly once. Per-arm
`0/0/1` remains a focused executor-test assertion rather than a new summary field.

### Two validation gates

Common Artifact contract, identity, lineage, ID-space, capability/service and selected-implementation
compatibility errors are decided by existing preflight, Validator and Merge-only admission before
the selected M1 factory is created or invoked. M1 invocation count remains zero for these failures.
M1-specific semantic invariants that require its complete parsed logical input—such as the frozen
replica projection's duplicate logical-record and combined-coverage rules—are checked inside M1.
They fail with an existing failure status and an empty `output_candidate` before normal output is
returned. They do not justify a second semantic preflight or an M1-aware common Validator.

## Structural Comparison Evidence

Each run retains its own GlobalGraphArtifact, BuildManifest, ValidationEvidence and existing native
graph-integrity EvaluationReport. Existing EvaluationReport is intentionally single-Artifact and
cannot prove two runs shared the same ordered input set. Therefore the thin runner writes one
Feature-local
`validation/same-input-structural-comparison.json`, validated by
[contracts/same-input-structural-comparison.schema.json](contracts/same-input-structural-comparison.schema.json).

The summary contains only identities/references, fresh-validation evidence, terminal statuses and
structural findings. It is not a public report family, Artifact, validity grant, Graph Parity report
or performance report. Any input identity mismatch marks comparison admission failed before either
Merge factory is created. One Merge failure is retained but cannot be reported as a successful fair
comparison.

## Validation Sequence

1. **Unit/contract fixtures**: descriptor/parameters; single-layer search; forward candidate and
   lazy backward behavior; no reverse search; pruning bound/tie-break; native read/write; entry and
   medoids; lambda epochs.
2. **Deterministic multi-shard fixtures**: large-first order, equal-size lineage tie-break,
   intermediate sizes/lineage, three-or-more inputs, replica normalization, candidate dedup,
   self/range/coverage failures.
3. **Native GlobalGraph contract**: self-edge mutation fails existing validation before evaluation
   or reuse; existing legal M0 and M1 graphs still pass without format/checksum changes.
4. **Merge-only contract**: legacy executor unchanged; fresh Dataset/Partition/Local validation;
   exact owner collection; no P0/B0 factory; per-executor 0/0/1 counts and successful enclosing
   runner aggregate 0/0/2; invalid input blocks M0/M1; identical references and
   mismatched-reference rejection.
5. **M1 cancellation**: deterministic cancellation at stage start, after semantic preparation,
   between pairwise rounds and before returning a candidate produces `CANCELLED`, no candidate and no
   new round after the observed cancellation.
6. **Small real M1**: real cpp_main LocalGraph bytes and maps, actual vectors/L2 service, native
   GlobalGraph+medoids, existing Validator and evaluator.
7. **Small real closures**: P0+B0+M1 once, then a same-input M0/M1 Merge-only comparison without
   rebuilding P0/B0.
8. **Applicable regressions**: M0 tests, cpp_main pipeline, Artifact reuse, pipeline failure gates,
   valid-artifact lineage, Fake pipeline C++/Python, public C++17 surface and Baseline runner
   isolation.
9. **SIFT1M preflight**: freeze one config, admit source/build/dataset/input identities, fresh
   validate the complete reusable set, write the unique immutable formal request for review, and
   prove no Merge factory invocation. Admission and runner tests use only small fixture requests;
   they never launch the formal request.
10. **One bounded SIFT1M execution**: after review, one Feature runner process consumes that exact
   formal request and executes M0 once and M1 once. Both consume the same set, outputs independently
   pass existing validation/evaluation, the comparison summary proves identical inputs, and all
   terminal outcomes are retained. No query or performance evaluator runs.

Existing SIFT1M P0/B0 Artifacts under `build/cpp-main-baseline/runs/sift1m-unified/` are candidates
for reuse, not pre-approved formal evidence. Preflight must revalidate the Dataset, Partition and
every LocalGraph under the repaired complete-lineage validator and verify their BuildManifest/source
identity before selecting them. If they fail, stop; do not silently rebuild P0/B0 inside the formal
comparison.

## Project Structure

### Documentation for this Feature

```text
specs/004-hnsw-merger-diskann-merge-adaptation/
├── spec.md
├── plan.md
├── research.md
├── source-mapping.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── README.md
│   ├── hnsw-merger-vamana-parameters.schema.json
│   ├── merge-only-execution.md
│   └── same-input-structural-comparison.schema.json
└── tasks.md
```

### Planned source and test surface

```text
upstream/diskann-cpp-main/           # fixed read-only; never a change target

include/diskann_pipeline/
└── pipeline.hpp                     # optional MergeOnlyInputSet only

src/pipeline/
└── pipeline_executor.cpp            # minimal Merge-only branch; legacy branch unchanged

src/papers/hnsw_merger/vamana/
├── CMakeLists.txt
├── stage_binding.hpp
├── descriptors.cpp
├── m1_merge.cpp
├── m1_algorithm.hpp                # private LogicalGraph/search/prune test surface
├── m1_algorithm.cpp
└── hnsw_merger_experiment_runner.cpp

contracts/papers/hnsw-merger-v1/schemas/
└── hnsw-merger-vamana-parameters.schema.json  # byte-identical runtime copy

harness/python/diskann_harness/
└── hnsw_merger_merge_adaptation.py            # thin normalization/runner invocation only

experiments/configs/hnsw-merger-diskann-merge-adaptation/
└── sift1m-structural.yaml                      # one config; frozen only after small gate

tests/cpp/papers/hnsw_merger/
├── CMakeLists.txt
├── test_m1_semantics.cpp
├── test_m1_native_output.cpp
├── test_merge_only_execution.cpp
└── test_m1_pipeline.cpp

tests/python/
└── test_hnsw_merger_merge_adaptation.py
```

**Structure Decision**: M1-specific algorithm helpers live only under the paper namespace. The
only shared-code amendment is the optional Merge-only request and executor branch. No official
HNSW source is copied into the repository, no upstream tree is a build/output target, and no
generic search/prune/backend framework is introduced.

## Requirement-to-Design Trace

| Requirements | Design evidence |
|---|---|
| FR-001–FR-004, FR-022, FR-028 | pinned identities, source/build policy, coexistence and project structure |
| FR-005–FR-006 | M1 declaration, parameter schema, existing input/output contracts and cancellation checkpoints |
| FR-007–FR-011 | source mapping, two-graph mapping, large-first chaining, lambda policy |
| FR-012–FR-014 | replica normalization, GlobalNodeId output, native self-edge enforcement, entry/medoids rules |
| FR-015–FR-016 | fixed parameter schema and one strategy/configuration |
| FR-017–FR-021 | Merge-only request, ArtifactReuseService flow, two validation gates and comparison contract |
| FR-023 | parent/stage-owned BuildManifest separation and source/build identity recording |
| FR-024–FR-026 | ordered validation gates and Feature-local same-input summary |
| FR-027 | explicit exclusions in Technical Context, structure and quickstart |

## Post-Design Constitution Re-check

All nine checks remain passed. The optional Merge-only input is required by the exact-same-input
fairness rule and is narrower than a general resume or DAG facility. The Feature-local comparison
summary closes a real two-run evidence gap without changing public Artifact or EvaluationReport
schemas. No constitutional exception or complexity waiver is required.

## Complexity Tracking

No constitution violation is accepted. No future-only abstraction is planned.
