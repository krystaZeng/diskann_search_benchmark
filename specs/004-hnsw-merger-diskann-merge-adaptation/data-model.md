# Data Model: HNSW-Merger DiskANN Merge Adaptation

## 1. PinnedPaperSourceIdentity

| Field | Rule |
|---|---|
| repository | `https://github.com/Kimchuls/HNSWMerger.git` |
| branch | `main` |
| commit | `88f6b7179f651cf5f284bd4e52249a4b1323b5af` |
| dirty | must be false for formal source-backed evidence |
| dependency identity | vendored HNSWlib covered by the same commit; no submodules; actual compiler/OpenMP identity captured if the official experiment is probed |
| role | algorithm/source-mapping authority; not an M1 runtime binary dependency |

The stable M1 implementation ID never embeds this commit. Formal M1 stage-owned BuildManifest
parameters use this complete SourceIdentity as `paper_source_identity`; the parent Manifest `source`
identifies the project-owned adaptation code actually compiled and references the stage-owned
document by path and SHA-256.

## 2. M1ImplementationDeclaration

```yaml
implementation_id: hnsw_merger_vamana_merge_v1
implementation_version: 1.0.0
stage: merge
test_only: false
requires:
  input_artifacts:
    - artifact_type: partition
      accepted_format_versions: [partition-artifact/v1]
      input_id_space: local
    - artifact_type: local_graph
      accepted_format_versions: [local-graph-artifact/v1]
      input_id_space: local
  capabilities:
    - partition.complete_local_to_global_map/v1
    - graph.local_adjacency/v1
  runtime_services:
    - vector_accessor/v1
    - distance_function/l2-v1
    - cancellation_token/v1
    - metrics_sink/v1
provides:
  artifact_type: global_graph
  format_version: global-graph-artifact/v1
  output_id_space: global
  capabilities:
    - graph.global_adjacency/v1
    - graph.medoids/v1
parameter_schema_path: contracts/papers/hnsw-merger-v1/schemas/hnsw-merger-vamana-parameters.schema.json
```

It coexists with existing M0. Registry duplicate/stage/sealing/no-fallback rules remain unchanged.

### Parent and stage-owned BuildManifest projection

| Location | Identity |
|---|---|
| parent `source` | project-owned M1 adaptation repository/branch/full commit/dirty state |
| parent `parameters.stage_execution_manifest` | workspace-relative immutable path and SHA-256 of the M1 stage-owned BuildManifest |
| stage-owned `source` | project-owned M1 adaptation repository/branch/full commit/dirty state |
| stage-owned `parameters.paper_source_identity` | pinned clean HNSWMerger SourceIdentity from section 1 |
| stage-owned `parameters.diskann_source_identity` | pinned clean DiskANN cpp_main identity |
| stage-owned `toolchain` | actual compiler/CMake/generator/options/commands used to build M1 and linked DiskANN |
| stage-owned execution facts | effective policy, evidence classifications and ordered `PairwiseMergeDecision[]` |

The stage-owned `parameters` object uses the fixed keys `paper_source_identity`,
`diskann_source_identity`, `paper_identity` (DOI and PDF SHA-256), `evidence_classifications`,
`adaptation_deviations`, `integration_mode`, `official_binary_linked`, `build_identity` (actual
configure/build commands and effective project options), `effective_merge_policy`, and
`pairwise_merge_decisions`. These are observed/configured execution facts, not accepted algorithm
parameters.

Every actual M1 invocation returns the stage-owned existing BuildManifest/v1 through unchanged
`StageResult.build_manifest_path`. Existing PipelineExecutor ingestion confines and validates it,
persists an immutable copy, and adds only the reference above to the parent. The algorithm parameter
schema is unchanged and contains no execution facts. Empty paths preserve M0/legacy behavior.
Neither Manifest grants Artifact validity. Formal admission fails if the actual adaptation repository
lacks trustworthy repository identity; an absolute local path is not a formal repository identity.

For an actual invocation, semantic failure produces stage-owned `FAILED`; cancellation produces
stage-owned `CANCELLED`; successful algorithm completion produces stage-owned `SUCCEEDED`. In every
case stage-owned `output_artifacts` is empty. A later candidate validation failure leaves that
execution Manifest `SUCCEEDED` while the parent records `INVALID_OUTPUT` and quarantine. A
pre-invocation admission failure creates no M1 stage-owned Manifest.

## 3. M1Parameters

All values are frozen constants for this Feature, not a parameter-search surface.

| Field | Value | Basis |
|---|---:|---|
| R | 16 | fixed cpp_main output degree |
| alpha | 1.2 | fixed cpp_main B0/Vamana profile |
| max_candidate_size | 750 | fixed cpp_main B0 occlusion limit |
| metric | `l2` | Dataset/profile contract |
| use_filters | false | Feature scope |
| multi_shard_strategy | `large_first_pairwise_v1` | paper Section 6 |
| lambda_policy | `paper_adaptive_log_v1` | paper Section 7.2 plus deterministic compatibility rounding |

Lambda values are derived per pair and recorded in BuildManifest; there is no user `lambda` field.
Thread budget is a StageContext resource. The first structural profile uses one thread and makes no
parallel-performance claim.

## 4. MergeOnlyInputSet

An optional C++17 field in `PipelineExecutionRequest`:

| Field | Type | Rule |
|---|---|---|
| partition | ArtifactRef | one published PartitionArtifact/v1 |
| local_graphs | `std::vector<ArtifactRef>` | non-empty, exactly one per PartitionId, ascending PartitionId order |

### Admission invariants

1. `reused_partition` and `merge_only_inputs` cannot both be present.
2. `request.dataset` is the Dataset source for the set.
3. Dataset validates first; Partition validates against Dataset; each LocalGraph validates against
   Partition.
4. Every reference is freshly checked for type/version/checksum/ID-space/capabilities, Dataset,
   owner, selected map and source lineage.
5. Missing, duplicate, unknown or out-of-order Partition ownership rejects before Merge factory
   creation.
6. The selected Merge descriptor matches the available contracts and StageContext services.
7. Successful execution invokes only Merge: counts `partition=0`, `local_build=0`, `merge=1`.

Absence preserves the legacy executor flow. This is not serialized into the public RunRequest v1.

## 5. InitialLogicalGraph

A Feature-private immutable view derived from one LocalGraph and owning map.

| Field | Rule |
|---|---|
| lineage_key | one PartitionId |
| source_local_graph | complete ArtifactRef identity |
| logical_nodes | unique GlobalNodeIds from the owning map |
| adjacency | mapped GlobalNodeId candidates; range-valid; no self edge |
| entry | native local entry mapped to GlobalNodeId |
| logical_size | count of unique logical nodes, not replica assignments |

Vectors are not stored in this object. They are read through VectorAccessor.

## 6. IntermediateLogicalGraph

An ephemeral M1-only object, never an Artifact.

| Field | Rule |
|---|---|
| lineage_key | sorted unique contributing PartitionIds |
| source_local_graphs | original Artifact identities in PartitionId order |
| logical_nodes | union of operand GlobalNodeIds, one record per ID |
| adjacency | at most R unique legal non-self GlobalNodeIds per node |
| entry | selected target operand entry |
| logical_size | cardinality of logical_nodes |

State transition:

```text
two admitted LogicalGraphs
-> immutable target forward-search phase
-> source/backward/shared candidate sets
-> bounded pruning
-> one IntermediateLogicalGraph
```

With one initial graph, the transition is skipped and the initial graph is emitted after global
mapping and output validation. Formal multi-shard evidence requires at least one pairwise step.

Cancellation is observed at M1 stage start, after input loading/semantic preparation, between
pairwise transitions, and before a normal candidate is returned. An observation transitions directly
to existing `CANCELLED` with no `output_candidate`; no subsequent pairwise transition starts.

## 7. PairwiseMergeDecision

Recorded in M1 stage-owned BuildManifest parameters for each merge step.

| Field | Rule |
|---|---|
| step | zero-based deterministic order |
| left_lineage / right_lineage | sorted PartitionId vectors |
| left_size / right_size | unique logical node counts |
| source_lineage / target_lineage | direction after size/tie rule |
| epoch_N0 | current lambda epoch base |
| lambda_policy | integer internal policy before target-size cap |
| effective_lambda | min(policy lambda, target logical size) |
| shared_node_count | intersection cardinality |
| output_lineage | sorted union |
| output_size | unique union cardinality |

## 8. ReplicaCandidateProjection

For one GlobalNodeId in a pairwise merge:

| Node class | Candidate sources |
|---|---|
| source-exclusive | source existing neighbors + forward hits |
| target-exclusive | target existing neighbors + recorded backward candidates |
| shared replica | both operand existing-neighbor lists + recorded backward candidates |

Projection rules:

1. validate all IDs before normalization;
2. de-duplicate by candidate GlobalNodeId across channels;
3. reject the node itself and out-of-range IDs;
4. sort by `(distance, GlobalNodeId)`;
5. if count exceeds R, apply the fixed M1 DiskANN/Vamana pruning helper;
6. retain at most R.

Cross-shard replicas are legal. Duplicate output logical nodes are not.

## 9. GlobalGraphArtifact/v1 (existing)

M1 publishes the existing entity, not a new family.

| Field | M1 rule |
|---|---|
| artifact_type / format_version | `global_graph` / `global-graph-artifact/v1` |
| id_space | `global` |
| Dataset identity | exact validated input Dataset identity |
| global_node_count | Dataset node count |
| declared_max_degree | 16 |
| observed_max_degree | computed from native output; <=16 |
| source_partition_artifact | full input Partition identity |
| source_local_graph_artifacts | full Local identities in ascending PartitionId order |
| source_artifacts | same Partition then Local identity order |
| global_graph payload | existing cpp_main native global graph media type |
| medoids payload | mapped initial local entries, ascending PartitionId order |
| capabilities | global adjacency + medoids |
| aggregate checksum | existing canonical identity over metadata and both payload identities |

The existing Validator remains the validity grant. Its native GlobalGraph path is minimally
amended to reject `neighbor == node`, closing an enforcement gap without a schema/version/checksum
change. M1 checks paper/replica/candidate/coverage properties before offering a candidate for
publication.

## 10. MergeStructuralRun

One M0 or M1 Merge-only invocation.

| Field | Rule |
|---|---|
| run_id | unique |
| merge identity | exact implementation ID/version |
| shared inputs | Dataset, Partition, ordered Local ArtifactRefs |
| fresh evidence | one ValidationEvidence per input and one for output when produced |
| BuildManifest | independent, includes identical ordered inputs and implementation-specific source/parameters |
| output | optional GlobalGraph ArtifactRef on success |
| EvaluationReport | independent existing native graph-integrity report after validation |
| terminal status/failure | retained for every attempt |

M0 and M1 generation/evaluation documents are never combined.

## 11. SameInputStructuralComparison

Feature-local machine-readable summary; schema:
[contracts/same-input-structural-comparison.schema.json](contracts/same-input-structural-comparison.schema.json).

| Field | Rule |
|---|---|
| comparison_id / generated_at | unique run evidence identity |
| study/reproduction/replacement | fixed Feature classification |
| Dataset/Partition/locals | one ordered shared ArtifactRef projection |
| input_validation_evidence | independent fresh evidence refs for both admissions |
| m0 / m1 | implementation, BuildManifest, output/evidence/evaluation refs and status |
| shared_input_identity | PASS only if all type/version/checksum tuples match exactly |
| structural_validation | PASS only if both output validators/evaluators pass |
| performance/query fields | prohibited by schema |

This summary is not an Artifact, EvaluationReport, Graph Parity report, aggregation grant or public
report family. It does not add invocation-count fields. The enclosing unchanged runner-result
records successful aggregate counts `partition=0`, `local_build=0`, `merge=2`; the summary's one
`m0` record and one `m1` record, each with its BuildManifest reference, bind those two invocations
to their respective arms. Each individual executor result is separately asserted as 0/0/1 in
focused tests.

## 12. Evidence separation and lifecycle

```text
immutable source Artifacts
    -> fresh ValidationEvidence
    -> Merge execution + BuildManifest
    -> candidate GlobalGraph
    -> existing Validator + output ValidationEvidence
    -> existing graph-integrity EvaluationReport

two independent successful Merge lifecycles
    -> Feature-local same-input structural comparison summary
```

Common contract, identity, lineage, capability/service and compatibility errors stop before factory
creation. M1-specific semantic errors requiring the complete parsed logical input stop inside M1
with an existing failure status and empty `output_candidate`. Invalid M1 output is quarantined by the
existing executor and cannot be evaluated/reused. One failed Merge does not erase the other outcome,
but the pair is not a successful fair comparison. The common Validator does not gain paper-specific
semantics.

## 13. FeaturePrivateComparisonRequest

Python preflight is the only profile normalization owner. It writes one immutable request with one
shared Dataset/Partition/ordered-Local tuple; one C++ Feature runner invocation consumes it and
derives two Merge-only executions from those same parsed ArtifactRef objects. The exact fields,
validation rules and argv are authoritative in `contracts/merge-only-execution.md`. This entity is
Feature-private and does not amend public RunRequest or runner-result schemas.

For formal SIFT1M, preflight writes the request and performs no Merge. Test-first runner coverage
uses only small fixture requests. The reviewed formal request is consumed once by one Feature
runner process, which executes M0 once and M1 once.

## 14. FrozenSift1mStructuralConfiguration

| Field | Rule |
|---|---|
| status | draft until small-real gate and review; then frozen before execution |
| Dataset | fixed SIFT1M identity/checksum |
| shared Artifact set | one exact Dataset/Partition/ordered Local set, fully revalidated |
| Merge identities | existing M0 and M1 v1 |
| M1 parameters | exact constants from section 3 |
| resources | one declared budget; M1 thread budget=1 for this Feature |
| attempts | one planned; infrastructure retry only with identical config/input and retained failed evidence |
| exclusions | no query, performance, tuning, best-result selection or >SIFT1M |
