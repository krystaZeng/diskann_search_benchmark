# Feature Specification: HNSW-Merger DiskANN Merge Adaptation

**Feature Branch**: `feature/merge-adaptation`

**Created**: 2026-08-11

**Status**: Draft

**Input**: User description: "Adapt the Merge method from the SIGMOD paper 'Efficient Vector
Index Merging in Vector Databases' as a new Merge-only implementation in the existing DiskANN
graph-build pipeline, compare it structurally with the existing M0 on exactly the same upstream
Artifacts, and stop before Query/Search or performance evaluation."

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `single_paper_reproduction`.

**Reproduction Mode**: `faithful_adaptation`. This Feature adapts the paper's Merge method to
DiskANN/Vamana graph inputs and outputs. It is neither a complete HNSW-Merger reproduction nor a
claim that the official repository already contains a directly usable DiskANN/Vamana implementation.

**Baseline Reference**: Microsoft DiskANN repository
`https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
`78256bbab4685e1774e78d331e081a153be26823`, with fixed read-only source at
`upstream/diskann-cpp-main`. The existing P0, B0 and M0 remain the Baseline composition.

**Paper Source Reference**: Official/source repository
`https://github.com/Kimchuls/HNSWMerger`. Research MUST pin a trustworthy repository, branch,
full 40-character commit, dependency identity and clean state before any formal run. Until that
audit is complete, the repository is research evidence only and no formal source-backed claim is
permitted.

**Replacement Scope**: `merge` only. The Baseline path is P0 → B0 → existing M0 →
GlobalGraphArtifact. The paper-adaptation path is the same P0 → B0 output followed by new M1 →
GlobalGraphArtifact. P0, B0, existing M0 and the fixed cpp_main Baseline runner are unchanged.

**Implementation Identities**: Existing P0/B0/M0 retain their current identities. M1 MUST receive
one stable, distinct Merge implementation identity after the source audit. The pinned HNSW-Merger
commit belongs to SourceIdentity and MUST NOT be embedded in or substituted for the stable M1
implementation identity.

**Algorithm Basis**: The paper's Vamana graph-merging extension is the primary semantic basis for
M1. The paper's HNSW merge method and pinned official source are supporting evidence for the core
sequence of forward search, cross-index candidate discovery, lazy backward direct-connect and
bounded neighbor pruning. Research MUST map these concepts to pinned source facts and clearly
separate source-backed behavior from HNSW-to-Vamana, single-layer, overlap and multi-shard
compatibility adaptations.

**ID-Space Contract**: M1 consumes the existing DatasetArtifact, PartitionArtifact and complete
LocalGraphArtifact collection. Local graph adjacency is interpreted in each owning Partition's
LocalNodeId space through its complete LocalToGlobalIdMap. M1 emits the existing
`GlobalGraphArtifact/v1` using only GlobalNodeId adjacency. Cross-shard replicas are valid input
membership, but each Dataset GlobalNodeId represents exactly one logical output node.

**Artifact Flow**: One validated DatasetArtifact → one validated PartitionArtifact → one
validated LocalGraphArtifact per required PartitionId → either existing M0 or new M1 → separate
existing GlobalGraphArtifact/v1 outputs. Artifact, BuildManifest, ValidationEvidence and
EvaluationReport remain independent objects and references.

**Validation Gate**: Before M0 or M1, all reused upstream Artifacts MUST be freshly validated for
type, format version, aggregate and payload checksum, ID space, capabilities, Dataset identity,
Partition ownership, collection completeness and lineage. M1 output MUST pass the existing
GlobalGraph validator before evaluation, reuse, comparison or aggregation.

**Fair Comparison Protocol**: A formal M0/M1 comparison MUST consume the exact same immutable
DatasetArtifact, PartitionArtifact and ordered LocalGraphArtifact references, including identical
artifact type, format version and aggregate checksum for every input. Separately rebuilt P0/B0
outputs that merely share configuration are not valid same-input evidence. How Merge-only reuse is
reached is deferred to Research/Plan and MUST reuse the existing execution architecture rather
than create another pipeline.

**Experiment Protocol**: Validation progresses from unit and contract fixtures to a small real M1,
a small real P0+B0+M1 closure, a same-input small M0/M1 structural comparison, and one bounded
SIFT1M structural validation. One source-supported multi-shard strategy and one structural
configuration are frozen before formal execution. No parameter sweep, automatic tuning,
changed-configuration retry, best-result selection or dataset larger than SIFT1M is authorized.

### Research and Plan Evidence Required

Research and Plan MUST resolve, justify and freeze exactly one formal choice for multi-shard merge
order and composition, forward-search direction, graph-size interpretation, stable tie-breaking,
lambda policy, intermediate merged-graph representation, overlap normalization, pruning behavior,
required runtime services, Merge-only reuse entry, and graph entry/medoid representation. This
specification deliberately does not select those mechanisms. Any proposed choice MUST be supported
by the paper, pinned source, existing Artifact contracts and the minimum necessary DiskANN/Vamana
compatibility adaptation.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Produce a Valid Multi-Shard M1 Global Graph (Priority: P1)

As a paper-reproduction engineer, I need one source-audited M1 implementation that merges the
existing DiskANN local graphs, so I can study the paper Merge mechanism without converting the
inputs into HNSW or changing the Baseline stages.

**Why this priority**: A valid, independently selectable Merge implementation is the minimum useful
adaptation and the prerequisite for every comparison.

**Independent Test**: Use a small real Dataset with multiple Partitions, including valid overlap,
to provide a complete validated LocalGraph collection to M1; verify that the existing validator
accepts its GlobalGraph output and that no P0/B0/M0 behavior was changed.

**Acceptance Scenarios**:

1. **Given** a pinned clean paper source and a complete validated multi-shard input collection,
   **When** M1 merges it, **Then** one existing `GlobalGraphArtifact/v1` with complete Dataset and
   source lineage is produced and accepted by the existing validator.
2. **Given** cross-shard replicas representing Partition overlap, **When** M1 processes the
   collection, **Then** every Dataset GlobalNodeId appears as one final logical node and every
   output neighbor is a legal GlobalNodeId.
3. **Given** an input with a missing, duplicated or unknown Partition owner, corrupt checksum,
   illegal local mapping or incompatible capability, **When** admission runs, **Then** M1 is not
   invoked and no normal GlobalGraph is published.
4. **Given** an M1 candidate containing an out-of-range neighbor, self edge, missing node, illegal
   degree or incorrect lineage, **When** publication is attempted, **Then** the existing validator
   rejects it before evaluation or reuse.

---

### User Story 2 - Compare M0 and M1 on Identical Upstream Graphs (Priority: P2)

As an experiment operator, I need M0 and M1 to consume exactly the same immutable upstream
Artifacts, so any observed final-graph structural difference can be attributed to the Merge choice
rather than a different Partition or Local Build run.

**Why this priority**: Same-input identity is the minimum fairness condition for a meaningful
Merge-only comparison.

**Independent Test**: Starting from one validated upstream Artifact set, run existing M0 and new M1
without rebuilding P0/B0; verify that both BuildManifests list identical source Artifact identity
tuples and that both independently validated GlobalGraph outputs remain separately identifiable.

**Acceptance Scenarios**:

1. **Given** one immutable Dataset, Partition and LocalGraph collection, **When** M0 and M1 run,
   **Then** every corresponding input reference has identical type, version and aggregate checksum.
2. **Given** two LocalGraph collections produced from separate P0/B0 runs, **When** their identities
   differ despite equal configuration, **Then** the formal same-input comparison is rejected.
3. **Given** the existing M0 and new M1 are both registered, **When** either is selected, **Then**
   they coexist without overwrite and the fixed Baseline runner continues to select only P0+B0+M0.
4. **Given** valid outputs from both Merge implementations, **When** structural evidence is
   reported, **Then** M0 and M1 Artifacts, generation provenance and evaluations remain separate.

---

### User Story 3 - Validate One Bounded SIFT1M Merge Comparison (Priority: P3)

As a research maintainer, I need one bounded SIFT1M structural validation after the small real
gates pass, so the adaptation is tested at the project's approved scale without turning this
Feature into a tuning or query-performance study.

**Why this priority**: SIFT1M is the required scale gate for an algorithm-semantic addition, but it
must not compensate for missing unit, contract or small real correctness evidence.

**Independent Test**: Freeze one SIFT1M structural configuration, reuse one validated upstream
Artifact set for both M0 and M1, and verify both final GlobalGraph outputs and their exact shared
input identities without running Query/Search.

**Acceptance Scenarios**:

1. **Given** all small real gates pass and one SIFT1M configuration is frozen, **When** the
   comparison executes, **Then** M0 and M1 consume the exact same validated Partition and complete
   LocalGraph collection.
2. **Given** M1 completes, **When** its final graph is validated, **Then** node coverage, GlobalNodeId
   range, self-edge exclusion, degree bound, lineage and graph entry/medoid semantics satisfy the
   existing GlobalGraph contract.
3. **Given** a request for parameter sweep, automatic tuning, changed-config retry, best-result
   selection or a dataset larger than SIFT1M, **When** admission evaluates it, **Then** the request
   is rejected as outside this Feature.
4. **Given** structurally valid M0 and M1 results, **When** the Feature is reported, **Then** it makes
   no Recall, QPS, latency or query-performance claim.

### Edge Cases

- The official repository is unavailable, not a Git repository, dirty, on a mismatched branch or
  commit, has ambiguous dependencies, or cannot provide trustworthy build identity.
- The paper or repository mentions Vamana but the pinned source contains no directly callable
  Vamana implementation compatible with current Artifacts.
- The input contains one Partition, two Partitions, or many Partitions rather than the paper's
  simplest two-index presentation.
- A PartitionId is absent, repeated, unknown, out of order, or associated with the wrong
  LocalToGlobalIdMap or Dataset identity.
- The same GlobalNodeId occurs in several shards as valid overlap, while a LocalToGlobal map also
  contains an invalid same-shard duplicate or out-of-range ID.
- Two valid local graphs contain conflicting replica-local adjacency for one GlobalNodeId.
- An intermediate merge step would exceed the final degree bound or leave a Dataset node without
  valid adjacency candidates.
- Paper/source terminology for graph size, lambda, forward direction or entry point is ambiguous
  when applied to single-layer DiskANN/Vamana and more than two shards.
- M0 and M1 configurations are equal but one upstream Artifact type, version or checksum differs.
- One Merge succeeds and the other fails; both terminal states and evidence must remain retained
  without presenting a one-sided result as a fair comparison.
- A retry is requested with a changed strategy, parameter or upstream Artifact set.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The Feature MUST preserve the fixed read-only DiskANN Baseline at repository
  `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
  `78256bbab4685e1774e78d331e081a153be26823`, and MUST NOT modify its gitlink or source tree.
- **FR-002**: Research MUST pin and audit the official HNSWMerger repository, branch, full
  40-character commit, dependency identities and clean state before any formal source-backed run.
- **FR-003**: The work MUST remain a `single_paper_reproduction` with
  `reproduction_mode: faithful_adaptation` and `replacement_scope: merge`; it MUST NOT be reported
  as an exact or complete HNSW-Merger reproduction.
- **FR-004**: M1 MUST have one stable identity and coexist with existing M0 without overwriting,
  modifying, unregistering or redefining M0; the fixed Baseline runner MUST remain P0+B0+M0 only.
- **FR-005**: M1 MUST consume the existing DatasetArtifact, PartitionArtifact and complete
  LocalGraphArtifact collection and MUST produce the existing `GlobalGraphArtifact/v1`; it MUST NOT
  create a new public Artifact family.
- **FR-006**: M1 MUST declare `requires`, `provides`, accepted Artifact versions, input/output ID
  spaces, capabilities and runtime services needed by its final source-audited design.
- **FR-007**: The formal adaptation MUST use the paper's Vamana merge extension as its primary
  semantic basis and MUST map supporting HNSW-Merger mechanisms to the pinned official source.
- **FR-008**: Research and implementation records MUST distinguish behavior directly supported by
  the paper or pinned source from HNSW-to-Vamana, single-layer, Partition-overlap and multi-shard
  compatibility adaptations.
- **FR-009**: The Feature MUST NOT convert DiskANN LocalGraph inputs into HNSW indexes, invoke an
  HNSW-only merge, and convert the result back to DiskANN; HNSW levels and storage layout MUST NOT
  enter the public pipeline contract.
- **FR-010**: M1 MUST support the full validated LocalGraph collection for multiple Partitions and
  MUST NOT be limited to a single two-index toy case.
- **FR-011**: Research/Plan MUST select and freeze exactly one paper/source-supported formal
  multi-shard strategy, including the required merge order, composition, direction, graph-size,
  tie-break, lambda and intermediate-state policies; the Feature MUST NOT implement simultaneous
  strategies for runtime selection.
- **FR-012**: M1 MUST accept legal cross-shard replicas and produce exactly one logical output node
  for each Dataset GlobalNodeId, without silently dropping Dataset coverage.
- **FR-013**: Every M1 GlobalGraph MUST use only legal GlobalNodeIds, contain the Dataset node count,
  exclude illegal self edges and out-of-range neighbors, satisfy the declared degree contract and
  preserve complete Dataset and source lineage.
- **FR-014**: M1 output MUST preserve graph entry/medoid semantics required by the existing
  downstream GlobalGraph contract; Research/Plan MUST determine the minimum truthful physical
  representation without creating an M1-specific sidecar family.
- **FR-015**: M1's parameter surface MUST contain only parameters with paper/source evidence that
  are necessary for the chosen adaptation; existing fixed DiskANN graph parameters MUST be reused
  where semantically applicable.
- **FR-016**: The Feature MUST NOT perform parameter sweep, automatic tuning or best-result
  selection. Any permitted retry MUST retain the failed attempt and preserve the frozen strategy,
  parameters and input Artifact identities.
- **FR-017**: Every new or reused input Artifact MUST pass fresh validation of format, version,
  checksum, ID space, capability, Dataset identity, ownership, collection completeness and lineage
  before M1 begins execution.
- **FR-018**: Every M1 output MUST pass the existing GlobalGraph validator before downstream
  evaluation, reuse, comparison or aggregation; invalid output MUST remain isolated and MUST NOT
  receive a normal Artifact grant.
- **FR-019**: A formal M0/M1 comparison MUST use identical immutable DatasetArtifact,
  PartitionArtifact and LocalGraphArtifact identity tuples, comparing Artifact type, format version
  and aggregate checksum for every input.
- **FR-020**: Separately executing P0/B0 for M0 and M1 is insufficient for formal same-input
  comparison even when configurations match; differing upstream Artifact identity MUST fail the
  comparison gate.
- **FR-021**: Merge-only starting or reuse MUST employ the current Registry, Validator, execution
  and evidence lifecycle. The Feature MUST NOT add a second PipelineExecutor, duplicate P0/B0/Merge
  orchestration or introduce a generic DAG framework.
- **FR-022**: P0, B0, existing M0, their parameters and algorithm semantics MUST remain unchanged
  and MUST contain no M1-specific branch.
- **FR-023**: Artifact, BuildManifest, ValidationEvidence and EvaluationReport MUST remain separate;
  every formal run MUST bind pinned source/build/toolchain, exact input Artifact identities,
  parameters, resources, output, status and failure evidence.
- **FR-024**: Validation MUST proceed through applicable unit and contract fixtures, a small real
  M1, a small real P0+B0+M1 closure, a same-input small M0/M1 structural comparison, and one bounded
  SIFT1M structural validation in that order.
- **FR-025**: The SIFT1M validation MUST use one frozen structural configuration and one validated
  upstream Artifact set for both M0 and M1. No dataset larger than SIFT1M is authorized.
- **FR-026**: Structural evidence MUST separately identify M0 and M1 outputs, prove shared upstream
  identities, and report node coverage, ID legality, self-edge validity, degree validity, lineage
  and existing-validator outcome without claiming query quality.
- **FR-027**: The Feature MUST NOT implement Query/Search, Recall, QPS, latency, query-performance
  comparison, generic Merge/backend/Adapter/search/pruning frameworks, coupled-stage bundles, new
  public report families or experiments at SIFT100M or larger scale.
- **FR-028**: The Feature MUST NOT modify the HNSWMerger source, its dependencies, or
  `upstream/diskann-cpp-main`; all builds, temporary files, logs, Artifacts and reports MUST remain
  outside those source trees.

### Key Entities

- **M1 Implementation Declaration**: Stable Merge identity, reproduction mode, paper/source basis,
  accepted input collection, output contract, ID spaces, capabilities, runtime services and minimal
  parameter contract.
- **Pinned Paper Source Identity**: Official repository, branch, full commit, dependency identities,
  clean state and the build provenance used for a formal adaptation.
- **Shared Upstream Artifact Set**: One immutable DatasetArtifact, PartitionArtifact and complete
  ordered LocalGraphArtifact collection, identified by complete type/version/checksum tuples and
  freshly validated before both Merge runs.
- **M0 Structural Run**: Existing Baseline Merge execution over the shared upstream set, producing
  its own GlobalGraphArtifact, BuildManifest, ValidationEvidence and structural EvaluationReport.
- **M1 Structural Run**: Paper-adaptation Merge execution over that same upstream set, producing
  independent existing lifecycle documents.
- **Merge Comparison Evidence**: A same-input structural record that binds both Merge identities,
  proves identical upstream Artifact references and keeps findings separate from generation data.
- **Frozen SIFT1M Configuration**: One declared Dataset, shared upstream Artifact set, M0/M1
  selection, paper-supported M1 parameters, DiskANN parameters, resource budget and retry rule.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of formal M1 runs bind a pinned clean HNSWMerger repository/branch/full commit,
  dependency and build identity, fixed DiskANN identity, exact input Artifacts, complete parameters,
  resource budget, output and terminal status.
- **SC-002**: M0 and M1 remain simultaneously selectable as distinct Merge implementations, while
  100% of fixed Baseline runner executions continue to use existing M0.
- **SC-003**: All covered common contract, identity, lineage, capability, service or compatibility
  errors are rejected before selected M1 invocation and produce zero normal GlobalGraph candidates.
  Covered M1-specific semantic input errors that can be decided only after M1 parses the complete
  admitted Merge input are rejected inside M1 before it returns a normal `output_candidate`; they
  return an existing failure status with an empty `output_candidate` and therefore produce no normal
  GlobalGraph publication.
- **SC-004**: On small real multi-shard inputs, including valid cross-shard overlap, M1 produces one
  existing-validator-accepted GlobalGraph with logical node count equal to the Dataset node count,
  zero out-of-range neighbors and zero illegal self edges.
- **SC-005**: 100% of valid M1 output nodes and neighbors use GlobalNodeId, the observed degree never
  exceeds the declared bound, and complete Dataset/source lineage validates successfully.
- **SC-006**: 100% of formal M0/M1 comparison records show exactly equal input Artifact
  reference tuples for the Dataset, Partition and every LocalGraph; any mismatch rejects the
  comparison before either result enters aggregation.
- **SC-007**: Exactly one formal multi-shard strategy and at most one SIFT1M structural
  configuration are used; zero parameter-sweep, automatic-tuning, changed-configuration retry or
  best-result-only records are produced.
- **SC-008**: The bounded SIFT1M run, if reached after all prior gates, produces independently valid
  M0 and M1 GlobalGraphArtifacts from the same upstream set and records every attempted terminal
  outcome without running a dataset larger than SIFT1M.
- **SC-009**: Zero Feature outputs claim Recall, QPS, latency, query-performance improvement, exact
  HNSW-Merger reproduction or a new validated Baseline.
- **SC-010**: Zero source or generated-file changes occur under HNSWMerger, its dependencies or
  `upstream/diskann-cpp-main` during research, implementation and validation.

## Assumptions

- Feature 001 provides the existing public Artifact contracts, Registry, capability checker,
  Validator, PipelineExecutor and evidence lifecycle; any still-deferred hardening remains visible
  and is not silently solved inside this Feature.
- Feature 002 provides the fixed cpp_main P0/B0/M0 implementation identities, reviewed M0 source
  mapping and native graph-format support.
- Standard LocalGraphArtifacts use LocalNodeId and retain complete LocalToGlobalIdMaps; legal
  cross-shard replicas are represented by repeated GlobalNodeIds across different maps.
- The paper PDF and a trustworthy clean checkout of the official HNSWMerger repository will be
  available to Research/Plan. No unverified README claim is treated as an implementation contract.
- Research can identify one paper/source-supported multi-shard adaptation that preserves the
  existing GlobalGraph contract. If it cannot, the Plan must fail closed rather than invent an
  unsupported algorithm.
- SIFT1M input and the validated P0/B0 Artifact set are available or can be produced once and then
  reused unchanged for both Merge runs.
- Query/Search evaluation, performance comparison and any experiment larger than SIFT1M require
  separate future Features and approvals.

## Dependencies

- Fixed DiskANN `cpp_main` Baseline and its commit-bound source mapping.
- Existing valid DatasetArtifact, PartitionArtifact, LocalGraphArtifact and
  GlobalGraphArtifact/v1 contracts and validators.
- Pinned paper/source mapping for the HNSW-Merger mechanism and the paper's Vamana extension.
- A Research/Plan decision proving one faithful, bounded multi-shard and overlap-compatible M1
  strategy without HNSW round-trip conversion.
