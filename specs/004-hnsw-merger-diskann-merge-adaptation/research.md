# Research: HNSW-Merger DiskANN Merge Adaptation

## Research status and identities

Research completed on 2026-08-12. No open planning ambiguity remains.

| Subject | Fixed identity / result | Evidence class |
|---|---|---|
| Paper | *Efficient Vector Index Merging in Vector Databases*, Proc. ACM Manag. Data 4(1), Article 31, February 2026, DOI `10.1145/3786645`; audited PDF SHA-256 `c5293d7c92b7ab5466788ddbc69dda7dad6e680e51a0373fee0b3d1374a2c8d0` | `paper_verified` |
| Official source | `https://github.com/Kimchuls/HNSWMerger.git`, branch `main`, commit `88f6b7179f651cf5f284bd4e52249a4b1323b5af`, clean | `source_verified` |
| Source dependencies | no Git submodules; modified HNSWlib is vendored and covered by the same commit; experiment Makefile uses the selected compiler's OpenMP runtime but pins no separate dependency revision | `source_verified` |
| DiskANN Baseline | `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823`, clean read-only submodule | `source_verified` |
| Current pipeline | public C++17 `MergeStrategy`/`PipelineExecutor`, Registry, Validator, `ArtifactReuseService`, native cpp_main format support and graph evaluator exist | `source_verified` |

The official source was cloned for read-only audit under ignored build space. No file in the source
checkout or either project upstream tree was modified.

## 1. Official implementation surface

### Decision

Treat the pinned repository as the official HNSW mechanism and experiment source, not as a binary
dependency or a ready-made Vamana implementation.

### Rationale

- `HNSW-Merger/extension.h` defines `search2Layer` and templated `HNSWMerger` over the repository's
  HNSW storage internals.
- `HNSW-Merger/experiment.cpp` is the main experiment driver, loads HNSW files and invokes the
  templated merger. `HNSW-Merger/Makefile` builds it directly with GNU C++ and OpenMP.
- Root `CMakeLists.txt` exposes the upstream HNSWlib interface/examples; it does not package the
  paper merger as a Vamana library.
- The repository contains no DiskANN/Vamana graph reader, writer or Merge implementation. README's
  “Extending to Vamana Graph” section contains a figure, not source integration.

Therefore, compiling the official HNSW merger would require the forbidden DiskANN -> HNSW ->
DiskANN round trip. M1 instead follows the paper's Vamana semantics and source-maps the common
control flow.

### Alternatives considered

- **Link `extension.h` directly**: rejected; private HNSW levels, labels, offsets and storage would
  become the working representation.
- **Convert LocalGraphs to HNSW and back**: explicitly prohibited and changes the experiment.
- **Describe README Vamana support as source implementation**: rejected as untruthful.

## 2. Paper method and pinned source mapping

### Decision

Use the following M1 semantic kernel:

```text
forward search from the smaller graph into an immutable larger graph
-> retain top-lambda candidates and reverse candidate records
-> source existing-neighbor union and bounded pruning
-> target existing-neighbor plus recorded-candidate union and bounded pruning
-> out-of-place merged graph
```

### Rationale

Paper Algorithm 4 and Section 7.1 state this sequence for HNSW and its Vamana single-layer
extension. Pinned `extension.h` independently exhibits the same forward/reverse-candidate/pruning
control flow. The backward stage does not perform a second graph search.

### Evidence separation

| Behavior | Classification |
|---|---|
| Algorithm 4, Section 3.3 and Section 7.1 Vamana description | `paper_verified` |
| `search2Layer`, candidate recording and backward direct-connect in pinned `extension.h` | `source_verified` |
| README Vamana figure without corresponding implementation | `repository_claim_only` |
| Single-layer GlobalNodeId view, DiskANN degree/prune rules, replica normalization and native output | `DiskANN_compatibility_adaptation` |
| Actual small-real traversal/pruning/output correctness | `implementation_probe_required` |

## 3. Current PipelineExecutor flow

### Decision

Reuse the existing executor and add only an optional Merge-only input set.

### Current source facts

`PipelineExecutor::execute` currently:

1. validates StageContext and Dataset;
2. resolves all P0/B0/Merge descriptors and runs chain preflight;
3. either creates P0 or fresh-reuses one PartitionArtifact;
4. creates B0 once for every explicit sorted PartitionId;
5. creates Merge once;
6. validates candidates, publishes atomically, persists ValidationEvidence and BuildManifest, and
   evaluates a successful final GlobalGraph.

`ArtifactReuseService` already performs fresh contract validation, accepted type/version/ID-space
checks and required-capability checks. It can validate LocalGraphs when given the validated
Partition source. The missing capability is only “start with the complete local collection.”

### Alternatives considered

- **Thin runner manually invokes M0/M1 factories**: rejected because it duplicates execution and
  publication flow.
- **Run P0/B0 separately for each Merge**: rejected by same-input fairness.
- **Generic arbitrary-stage resume/DAG**: rejected as unsupported over-design.

## 4. LocalGraph and LocalToGlobal contract

### Decision

Accept exactly one valid LocalGraphArtifact per PartitionId and map its native adjacency strictly
through the corresponding native uint32 LocalToGlobal map.

### Current source facts

- Partition maps contain one uint32 GlobalNodeId for each local row; map length equals
  `local_node_count`, values are in Dataset range and ascending for cpp_main P0.
- LocalGraph adjacency and header entry are LocalNodeIds within that map.
- The LocalGraph metadata binds owning PartitionId, Dataset identity, source Partition identity and
  selected map payload checksum.
- The GlobalGraph validator requires exactly one source LocalGraph for every Partition and checks
  source type/version/checksum identities after the complete-lineage repair.

M1 uses those invariants; it does not reinterpret a local ID globally.

## 5. DiskANN primitive audit

### Decision

Reuse public data/distance/native storage primitives; implement only two M1-private algorithm
helpers.

### Findings

| Primitive | Current usability | Decision |
|---|---|---|
| `VectorAccessor` | public StageContext service keyed by GlobalNodeId | reuse |
| `DistanceFunction` | public StageContext service; current runner supplies L2 | reuse |
| cpp_main native readers/validators | project-owned; validate Dataset, ID map, local/global graph and medoids | reuse/extend only as a read view if needed |
| `diskann::InMemGraphStore` | public load/get/set/store graph surface | reuse for native graph materialization/output |
| `diskann::Index::search` | public, but requires loaded Index and data-store companion | do not use; constructing another index changes representation |
| `Index::prune_neighbors` / `occlude_list` | not a safe public callable stage primitive; tied to Index storage/scratch | do not expose or modify upstream |
| M0 `merge_shards` | union/shuffle/truncate Baseline semantics, not paper pruning | keep unchanged; do not reuse for M1 |

`hnsw_merger_single_layer_search` implements paper Algorithm 1 against an immutable adjacency view.
`hnsw_merger_vamana_prune` implements only the fixed cpp_main occlusion semantics needed by M1
(R=16, alpha=1.2, max candidates=750, no saturation). Both remain private to this paper Feature.

### Alternatives considered

- **Use M0 `merge_shards` then refine**: rejected; it would not implement the paper merge sequence.
- **Make DiskANN prune public or patch upstream**: rejected by immutability and scope.
- **Add generic search/prune abstractions**: no consumer beyond M1 and explicitly prohibited.

## 6. Multi-shard strategy

### Decision

Use one deterministic large-first pairwise chaining strategy.

### Rationale

Paper Theorem 6.2 and Section 6 prescribe repeatedly merging the two largest indexes and replacing
the pair with its result. Pinned `experiment.cpp` implements a `LARGE_FIRST` mode. Its comparator
uses a non-strict `>=` and has no stable identity tie-break, so that exact comparator is not suitable
for reproducible Artifact generation.

M1 defines graph size as unique logical GlobalNodeId count, selects descending size, and uses the
lexicographic sorted contributing PartitionId vector for stable ties. Within an equal-size pair, the
lexicographically smaller lineage is the target. This deterministic tie-break and overlap-aware
size definition are compatibility adaptations.

A one-Partition input is admitted as a degenerate identity merge: map its one local graph to global
IDs, validate it under the same output rules and emit the normal GlobalGraph/medoids representation.
It executes no pairwise search and is not evidence for the required multi-shard path.

### Alternatives considered

- small-first and random source modes: rejected; only one formal strategy is authorized.
- tree/parallel variants: rejected; no comparative strategy study.

## 7. Lambda and parameter policy

### Decision

Use paper adaptive lambda internally for multi-shard merges and expose no free lambda parameter.

### Rationale

The paper fixes two-index `lambda0=4`, then linearly interpolates lambda over logarithmic larger
graph size from `(lambda0,N0)` to `(M,M*N0)`, clamps at M and resets after reaching M. Pinned
`experiment.cpp` currently passes one configured fixed lambda; its adaptive formula is commented
out. Consequently adaptive lambda is `paper_verified` but not an active `source_verified` runtime
behavior.

For DiskANN, degree M maps to fixed R=16. Integer floor and reset timing are frozen in plan.md as
compatibility choices because the paper does not state integer rounding. Positive C++ conversion in
the pinned source's commented multi-index formula truncates toward zero, providing a source-backed
reason to choose floor rather than round-to-nearest. Each pair records the inputs and effective
lambda.

Only these configuration fields enter M1's parameter schema:

- R=16;
- alpha=1.2;
- max_candidate_size=750;
- metric=`l2`;
- `large_first_pairwise_v1`;
- `paper_adaptive_log_v1`;
- filters disabled.

They are constants for truthfulness, not a search surface. Formal structural execution uses one
StageContext thread because this Feature does not validate the paper's parallel performance path.
The pinned experiment config's `thread` value is used by its index-build helper, while merger loops
use OpenMP pragmas and do not call `omp_set_num_threads`; an actual merger worker count cannot be
inferred from the config. M1 therefore records its own requested/configured one-thread budget and
does not call that value an observed official-source worker count.

The final descriptor also requires the existing `cancellation_token/v1`, so the implementation must
observe it. The minimum faithful checkpoints are stage start, after input loading/semantic
preparation, between pairwise rounds and before returning a normal candidate. Cancellation uses the
existing `CANCELLED`/empty-candidate lifecycle and adds no algorithm or evidence abstraction.

### Alternatives considered

- user-sweepable lambda: rejected.
- pinned source default alpha 1.05: HNSW-specific; rejected for DiskANN/Vamana output.
- paper HNSW M=32: rejected; downstream DiskANN contract is R=16.

## 8. Overlap and replicas

### Decision

Normalize replicas at each pairwise step without discarding unique adjacency candidates.

### Rationale

Paper notation uses two logical node sets and does not define cross-shard replicas. The existing
Partition contract permits one GlobalNodeId in multiple Partitions. M1 therefore:

- represents one node per GlobalNodeId in each operand;
- excludes shared nodes from source forward iteration;
- keeps target search immutable;
- unions both replica adjacency lists for the shared node at output time;
- includes recorded backward candidates;
- collapses candidate channels by GlobalNodeId, rejects self/range errors, and prunes to R.

This preserves all unique replica-derived information and yields one final logical Dataset node.
It is explicitly a DiskANN compatibility adaptation.

## 9. Entry point and medoids

### Decision

Map every local header entry to GlobalNodeId, retain the target entry at each pair, emit the final
entry in the native graph header, and emit all initial mapped local entries as the existing medoids
payload in ascending PartitionId order.

### Rationale

Paper/source forward search starts from the target index entry. Native M0's downstream format uses
one global graph entry and a separate list of mapped local entries. The generic GlobalGraph contract
permits medoids, and the current native evaluator requires `graph.medoids/v1` for the reviewed
cpp_main media type. Reusing this representation makes M0/M1 outputs enter the same downstream
contract without a new sidecar.

The medoids list is compatibility metadata; it is not described as a paper algorithm result.

## 10. Output and validator mapping

### Decision

Emit the existing cpp_main native global graph plus medoids inside `GlobalGraphArtifact/v1`, then
use the existing Validator and native evaluator unchanged.

### Current validation coverage

The Validator already checks schema, format version, aggregate/payload checksum, Global ID space,
node count, range, degree, payload/capability pairing, Dataset identity, Partition and ordered Local
source count/ownership/lineage identities. The native reader does not currently reject a neighbor
equal to the current node. This is a proven enforcement gap for the existing no-self-edge
GlobalGraph contract. The minimal amendment is to reject native GlobalGraph self edges in the
existing validation path, without schema/version/checksum changes. M1 must additionally check
before publication:

- exactly one logical node per Dataset GlobalNodeId;
- complete coverage;
- duplicate candidate handling and paper-specific merge invariants.

These are implementation checks, not a second validity grant. A candidate still becomes a normal
Artifact only after the existing Validator—including the self-edge check—succeeds. Successful
ValidationEvidence therefore supports a deterministic `self_edge_count=0` finding without adding
an EvaluationReport field.

## 11. Same-input execution and evidence

### Decision

Add `MergeOnlyInputSet` as an optional C++17 field in `PipelineExecutionRequest`; no public JSON
RunRequest change.

### Rationale

Python preflight is the single normalization owner for the Feature-local profile. It emits one
immutable Feature-private comparison request containing one shared
Dataset/Partition/ordered-Local ArtifactRef tuple. One C++ Feature runner invocation consumes that
request without re-normalizing it and constructs both Merge-only C++ requests from the same parsed
ArtifactRef objects. The executor revalidates all sources independently for each run before factory
creation. The wire shape and fixed `execute --request FILE --result FILE` argv are defined by
`contracts/merge-only-execution.md`; the public RunRequest and runner-result schemas do not change.

Each Merge BuildManifest includes the same ordered Dataset+Partition+Local input list. Existing
EvaluationReports remain separate. A Feature-local same-input structural comparison summary is
needed because a single-Artifact EvaluationReport cannot bind two outputs and prove shared inputs.
The summary is schema-validated but is not a public Report family or validity grant.

## 12. Source, build and toolchain provenance

### Decision

Use the project-owned adaptation identity as the M1 parent-stage SourceIdentity. Record the pinned
HNSWMerger and DiskANN source bases plus M1 execution facts in one existing stage-owned
BuildManifest/v1 returned through `StageResult.build_manifest_path`.

### Rationale

M1 is not official HNSWMerger bytecode: it is a faithful adaptation compiled from this repository.
Therefore the parent BuildManifest `source` must describe the actual project code. Its existing
`parameters.stage_execution_manifest {path, sha256}` reference resolves to the immutable stage-owned
BuildManifest whose parameters carry `paper_source_identity`, `diskann_source_identity`, evidence
classifications, effective policy and `PairwiseMergeDecision[]`. Actual compiler,
CMake/generator/build type, definitions/options and commands belong to that existing
ToolchainIdentity/BuildManifest model. These execution facts do not enter the algorithm parameter
schema and are not duplicated into the parent parameters. A formal run is invalid if any source
or build identity is missing, floating or dirty beyond the predeclared state. The current parent
checkout has no configured remote URL, so formal source admission is presently blocked until a
trustworthy canonical repository identity and committed adaptation revision exist; small local
research/tests are not formal evidence.

The paper-source provenance explicitly records `integration_mode=source_mapping_only` and
`official_binary_linked=false`. The build identity required for SC-001 is the actual M1 adaptation
binary/toolchain linked to fixed DiskANN; no fabricated official HNSWMerger binary identity is
recorded. Its vendored HNSWlib dependency identity is the pinned paper-source commit itself.

No BuildManifest or StageResult schema/API extension is needed. Existing PipelineExecutor ingestion
confines, validates and persists the stage-owned document and writes only its path/checksum reference
into the parent. Empty `build_manifest_path` keeps M0 and other legacy stages unchanged. Artifact
validity remains the existing Validator's responsibility.

## 13. Validation and scale gates

### Decision

Use the exact order in plan.md and stop at structural SIFT1M.

### Gate outcomes required

1. unit/contract semantics and failure gates;
2. deterministic three-plus-shard and overlap fixtures;
3. small real M1 native output;
4. small real P0+B0+M1 closure;
5. small same-input M0/M1 Merge-only comparison;
6. applicable regressions and upstream clean checks;
7. SIFT1M preflight over one frozen config and one fully revalidated reusable set;
8. one bounded structural execution.

Existing SIFT1M artifacts are only candidates. They must pass fresh full-lineage validation and
provenance admission before reuse. Failure blocks formal execution rather than causing an implicit
P0/B0 rebuild.

## 14. Deferred and unsupported behavior

- direct use or modification of official HNSW source;
- HNSW representation, levels, HNSW round-trip or complete paper reproduction;
- parallel/memory-efficiency performance reproduction;
- alternate merge orders, tuning, sweep or best-result selection;
- generic Merge/search/prune/Adapter/DAG frameworks;
- Query/Search, Recall, QPS, latency or performance claim;
- SIFT100M or larger Dataset;
- Baseline runner or P0/B0/M0 semantic changes.
