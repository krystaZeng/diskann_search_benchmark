# Feature Specification: OGP Partition Adaptation

**Feature Branch**: `feature/opg-partition-adaptation` (the current Git branch's historical
spelling; the Feature short name and directory remain `ogp-partition-adaptation`; no branch rename
or migration is part of this Feature)

**Created**: 2026-08-10

**Status**: Draft

**Input**: User description: "Adapt the official GP-ANN implementation of OGP as a faithful,
independently selectable Partition implementation in the existing modular DiskANN cpp_main
pipeline, compose it with the existing cpp_main B0 and M0, and establish one bounded SIFT1M
structural validation without claiming full GP-ANN reproduction or query-performance improvement."

## Clarifications

### Session 2026-08-10

- Q: What is the minimum useful delivery? → A: A real official OGP invocation that publishes a
  valid existing `PartitionArtifact/v1`, followed by a small real OGP→B0→M0 closure through the
  existing executor. SIFT1M is a later validation story, not the MVP.
- Q: Which implementation boundary is authorized? → A: Invoke only the pinned official GP-ANN
  `Partition` CLI with method `OGP`; do not add a callable backend, copy the algorithm, or implement
  GP, Router, query routing, or search.
- Q: Which membership rules are authoritative? → A: Reject empty shards, illegal IDs,
  same-shard duplicates, and missing global coverage before B0; retain valid cross-shard overlap;
  sort valid memberships only after duplicate detection.
- Q: How is mixed source provenance represented? → A: Add one backward-compatible optional
  Partition/Local Build/Merge source set. When absent, preserve the existing request-level source;
  when present, bind OGP to GP-ANN and B0/M0 to DiskANN.
- Q: How is SIFT1M bounded? → A: Freeze one configuration and prohibit tuning or preferred-result
  selection. Infrastructure retries may use new run IDs only when every attempt and reason is
  retained. No dataset larger than SIFT1M is authorized.

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `single_paper_reproduction`, limited to a `faithful_adaptation` of the paper's OGP
Partition method. This Feature is not an exact or complete GP-ANN reproduction.

**Baseline Reference**: Microsoft DiskANN repository
`https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
`78256bbab4685e1774e78d331e081a153be26823`. The Baseline source remains fixed and read-only. B0
and M0 retain reproduction mode `upstream_faithful`; OGP uses `faithful_adaptation`.

**Paper Source Reference**: GP-ANN repository `https://github.com/larsgottesbueren/gp-ann.git`,
branch `main`, full commit `df5efb4a6b303011c795666ed267f29d994b558c`, including the recursive
dependency identities recorded in `source-mapping.md`.

**Replacement Scope**: `partition` only. OGP coexists with P0 under stable implementation identity
`gp_ann_ogp_partition_v1`. Existing cpp_main B0, M0, and the fixed Baseline runner remain unchanged.

**ID-Space Contract**: OGP consumes a Dataset in GlobalNodeId space and produces the existing
`PartitionArtifact/v1`. Each Partition defines its LocalNodeId space through one complete
LocalToGlobalIdMap. Cross-shard replicas are valid overlap; duplicates inside one shard are invalid.
Existing B0 produces local-ID `LocalGraphArtifact/v1`; existing M0 produces global-ID
`GlobalGraphArtifact/v1`.

**Artifact Flow**: Validated DatasetArtifact → official OGP → validated `PartitionArtifact/v1` →
existing B0 once per explicit PartitionId → validated `LocalGraphArtifact/v1[]` → existing M0 →
validated `GlobalGraphArtifact/v1`. Artifact, BuildManifest, ValidationEvidence, and
EvaluationReport remain separate files and references.

**Validation Gate**: Before execution, validate the Dataset, exact implementation chain, source
identities, parameters, required services, Artifact types/versions, ID spaces, and capabilities.
Before B0, validate every OGP map, checksum, Dataset lineage, non-empty shard, ID range,
same-shard uniqueness, ascending order, and complete Dataset coverage. Any failure prevents all
downstream execution, reuse, evaluation, and aggregation.

**Experiment Protocol**: Unit and fixture tests precede a small real official OGP run; the small
real composed closure precedes SIFT1M. SIFT1M uses one predeclared configuration and no parameter
sweep. Every attempt is retained; an infrastructure retry requires the same configuration, a new
run ID, and a recorded reason. The Feature performs no repeated-run aggregation or query evaluation
and authorizes no dataset larger than SIFT1M.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Produce a Valid Official OGP Partition (Priority: P1)

As a reproduction engineer, I need the pinned official GP-ANN OGP CLI to produce an existing
Partition Artifact, so I can verify the paper Partition behavior without reimplementing the
algorithm or starting expensive downstream graph construction.

**Why this priority**: A trustworthy, independently valid OGP Partition is the smallest useful
vertical slice and the prerequisite for every composed experiment.

**Independent Test**: Build and invoke the clean pinned official CLI on a small Dataset, normalize
its cluster output, and verify that the existing common Validator accepts the published
`PartitionArtifact/v1`; exercise invalid membership fixtures before any B0 invocation.

**Acceptance Scenarios**:

1. **Given** the exact clean GP-ANN source and a validated float32 Dataset, **When** official OGP is
   invoked with an admitted parameter set, **Then** the run records exact source/build/binary,
   Dataset, parameter, and configured-resource provenance.
2. **Given** valid OGP clusters with cross-shard replicas, **When** they are converted, **Then** all
   replicas are retained, each shard map is sorted, and the existing Validator accepts the
   resulting `PartitionArtifact/v1`.
3. **Given** an empty shard, illegal ID, same-shard duplicate, missing Dataset node, malformed
   output, or invalid checksum, **When** publication is attempted, **Then** no normal Partition
   Artifact is published and B0/M0 invocation counts remain zero.
4. **Given** a configuration that supplies a free epsilon or unsupported parameter, **When**
   admission runs, **Then** it is rejected because the selected official CLI fixes epsilon to
   `0.05` and exposes only cluster count, overlap, and partitioning mode.

---

### User Story 2 - Compose OGP with Existing B0 and M0 (Priority: P2)

As an experiment operator, I need OGP, existing cpp_main B0, and existing cpp_main M0 selected
through the current execution flow, so I can evaluate a Partition-only replacement without forking
the pipeline or changing Baseline algorithms.

**Why this priority**: The paper method becomes useful to this project only after a real validated
Partition can flow through the unchanged downstream Baseline stages.

**Independent Test**: Execute one small real OGP→B0→M0 run through the existing executor and
verify source selection, stage order, invocation counts, Artifact validation order, final lineage,
and isolation of the fixed Baseline runner.

**Acceptance Scenarios**:

1. **Given** explicit stage sources, **When** the chain is admitted, **Then** Partition resolves to
   GP-ANN while Local Build and Merge resolve to DiskANN; a request without stage sources preserves
   the existing request-level fallback.
2. **Given** a valid OGP Partition Artifact, **When** the chain executes, **Then** B0 runs exactly
   once per explicit PartitionId and M0 runs exactly once after every LocalGraph Artifact validates.
3. **Given** invalid OGP output or an incompatible implementation chain, **When** preflight or the
   publication gate fails, **Then** no downstream stage, evaluation, reuse, or aggregation begins.
4. **Given** the fixed cpp_main Baseline runner, **When** OGP selection is requested, **Then** it
   rejects OGP and continues to admit only P0+B0+M0.

---

### User Story 3 - Validate One SIFT1M Structural Configuration (Priority: P3)

As a research maintainer, I need one frozen SIFT1M structural configuration after the small real
closure passes, so I can establish bounded scale evidence without turning this Feature into a
tuning or query-performance study.

**Why this priority**: SIFT1M is required validation for an algorithm-semantic addition but is not
needed to prove the MVP integration on small data.

**Independent Test**: Freeze one configuration before execution and verify that each retained
attempt uses the same Dataset, implementations, parameters, and resource budget; a successful
attempt produces the complete validated Artifact chain and separate existing EvaluationReport.

**Acceptance Scenarios**:

1. **Given** the small real composed test passes and one SIFT1M configuration is frozen, **When** an
   attempt executes, **Then** its request, source/build/data identity, Artifacts, Manifests,
   ValidationEvidence, result, and failure reason are retained.
2. **Given** an infrastructure failure, **When** a retry is authorized, **Then** it uses the same
   frozen configuration, receives a new run ID, retains the earlier failure, and records the retry
   reason.
3. **Given** a parameter sweep, changed retry configuration, preferred-result selection, or Dataset
   larger than SIFT1M, **When** admission evaluates it, **Then** the request is rejected as outside
   this Feature.
4. **Given** a structurally successful run, **When** its evidence is reported, **Then** it makes no
   Recall, QPS, latency, Graph Parity, full GP-ANN reproduction, or query-performance claim.

### Edge Cases

- The GP-ANN path exists but lacks trustworthy Git identity, has a mismatched commit or recursive
  dependency, or is dirty for a formal run.
- The official executable is missing, was built from another source tree, or its checksum does not
  match the admitted build identity.
- Cluster count is zero or greater than Dataset node count; overlap is negative, non-finite, or
  overflows assignment arithmetic; mode is not `default` or `strong`.
- The `.fbin` staging view changes bytes, size, order, or checksum relative to the validated Dataset.
- Official output contains an empty or malformed line, trailing tokens, a same-shard duplicate, an
  out-of-range GlobalNodeId, or incomplete global coverage.
- One GlobalNodeId appears in several shards; this valid overlap must not be rejected or collapsed.
- The requested cluster count differs from the effective non-empty Partition count, or requested
  overlap differs from the assignments observable in the Artifact.
- A legacy request has no stage-specific sources and must retain existing fallback behavior.
- The selected implementations are individually valid but their Artifact version, ID space,
  capability, Dataset identity, or runtime services are incompatible.
- A failed SIFT1M attempt is retried with a changed parameter or hidden instead of retained.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The Feature MUST preserve the fixed read-only DiskANN Baseline at repository
  `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
  `78256bbab4685e1774e78d331e081a153be26823`, and MUST build and run entirely outside both
  admitted upstream source trees.
- **FR-002**: OGP MUST use the clean pinned official GP-ANN repository, branch, full commit, and
  recursive dependency identities recorded in `source-mapping.md`; formal admission MUST reject
  missing, mismatched, ambiguous, or dirty source before OGP creation or execution.
- **FR-003**: OGP MUST coexist with P0 under stable identity `gp_ann_ogp_partition_v1`, study class
  `single_paper_reproduction`, reproduction mode `faithful_adaptation`, and replacement scope
  `partition`; it MUST NOT overwrite or relabel any Baseline implementation.
- **FR-004**: The Feature MUST invoke only the pinned official `Partition` CLI using method `OGP`;
  it MUST NOT add a callable/backend hierarchy, copy or reimplement OGP/GP, or patch upstream code.
- **FR-005**: The admitted parameter surface MUST contain only requested cluster count, requested
  overlap, and `default|strong` partitioning mode; evidence MUST record the fixed effective epsilon
  `0.05` without exposing it as a user control.
- **FR-006**: The OGP descriptor MUST declare the existing Dataset input and
  `PartitionArtifact/v1` output, GlobalNodeId-to-LocalNodeId contract, `requires`, `provides`,
  capabilities, and required runtime services needed by existing B0.
- **FR-007**: OGP MUST emit the existing `PartitionArtifact/v1` with one non-empty, complete native
  LocalToGlobalIdMap for each contiguous effective PartitionId and MUST NOT create an OGP-specific
  Artifact family or embed generation/evaluation facts in the Artifact.
- **FR-008**: Before sorting and publication, OGP MUST reject malformed output, empty shards,
  out-of-range GlobalNodeIds, same-shard duplicates, and missing Dataset coverage; after those
  checks, each map MUST be sorted ascending while all valid cross-shard replicas remain present.
- **FR-009**: Every new or reused Artifact MUST pass the existing checks for format, version,
  checksum, ID space, capability, Dataset identity, lineage, and applicable structural invariants
  before any downstream execution, evaluation, reuse, comparison, or aggregation.
- **FR-010**: The composed OGP→B0→M0 flow MUST use the existing `PipelineExecutor`; existing B0,
  M0, their implementation identities and parameters, and the fixed cpp_main Baseline runner MUST
  remain unchanged and free of OGP-specific branches.
- **FR-011**: The execution request MUST support one backward-compatible optional complete stage
  source set; absence MUST preserve request-level source fallback, while presence MUST bind
  Partition to admitted GP-ANN and Local Build/Merge to admitted DiskANN.
- **FR-012**: Artifact, BuildManifest, ValidationEvidence, and EvaluationReport MUST remain separate
  objects and references. Every formal run MUST bind complete source/build/toolchain, Dataset,
  parameter, resource, stage-selection, output, status, and failure provenance without claiming
  unavailable worker observations.
- **FR-013**: Tests MUST cover source and parameter admission, map conversion and negative cases,
  valid overlap, source fallback/split, invalid-Partition downstream blocking, a small real official
  OGP Partition, a small real composed closure, and fixed Baseline runner isolation.
- **FR-014**: SIFT1M validation MUST use one configuration frozen before execution. Every attempt
  MUST be retained; an infrastructure retry MUST preserve the configuration, use a new run ID, and
  record its reason. Parameter sweeps and preferred-result selection are prohibited.
- **FR-015**: The Feature MUST reject datasets larger than SIFT1M and MUST NOT implement or claim
  GP, Router, shard query/search, Recall, QPS, latency, Graph Parity, SearchIndexArtifact,
  Materializer, generic Adapter infrastructure, full GP-ANN reproduction, or query-performance
  improvement.

### Key Entities

- **OGP Implementation Declaration**: Stable identity, Partition stage, existing input/output
  contracts, ID spaces, capabilities, runtime services, parameter contract, and admitted source.
- **Stage Source Identities**: Optional complete Partition, Local Build, and Merge source set with
  request-level fallback when absent.
- **OGP Parameter Record**: Requested cluster count, requested overlap, partitioning mode, and the
  source-backed fixed effective epsilon.
- **PartitionArtifact**: Existing immutable map-only `PartitionArtifact/v1` with effective
  Partition count, complete canonical maps, Dataset lineage, capabilities, payload checksums, and
  aggregate checksum.
- **Composed Run**: One request selecting OGP, existing B0, and existing M0 with validated handoffs
  and separate Artifact, BuildManifest, ValidationEvidence, and EvaluationReport references.
- **Frozen SIFT1M Configuration**: One immutable Dataset/implementation/parameter/resource selection;
  individual attempts add run identity, status, outputs, and retry reason without changing it.

### Scope Boundaries

This Feature includes exact GP-ANN/DiskANN source admission, official OGP CLI invocation, the
minimal optional stage-source extension, conversion to the existing Partition Artifact, targeted
negative and real small-data tests, composition with unchanged B0/M0, and one frozen SIFT1M
structural configuration with retained attempts.

This Feature excludes callable or generic backend infrastructure, an OGP-specific Artifact or
report family, detailed worker-observation modeling, GP, Router, HNSW shard query, search serving,
Recall/QPS/latency evaluation, query-performance comparison, SearchIndexArtifact, Materializer,
generic Adapter infrastructure, parameter sweeps, SIFT100M+, and any query-performance claim.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of formal OGP attempts bind trustworthy GP-ANN and DiskANN source/build identity,
  Dataset checksum, complete effective parameters, resource budget, stage selections, outputs, and
  terminal status before they can enter successful evidence.
- **SC-002**: 100% of published OGP shards are non-empty, in range, same-shard unique, ascending,
  and collectively cover the Dataset; 100% of valid cross-shard replicas are retained.
- **SC-003**: All covered source, parameter, parse, membership, checksum, compatibility, and lineage
  negative cases fail before B0, with B0 and M0 invocation counts equal to zero.
- **SC-004**: The small real composed run invokes official OGP exactly once, B0 exactly once per
  effective PartitionId, and M0 exactly once after all local outputs validate, while the fixed
  Baseline runner continues to reject OGP.
- **SC-005**: At most one SIFT1M configuration exists; every attempt and retry reason is retained,
  and zero parameter-sweep, changed-retry, hidden-failure, or best-result-only records exist.
- **SC-006**: Zero Feature outputs claim Recall, QPS, latency, Graph Parity, full GP-ANN
  reproduction, validated Baseline status, or query-performance improvement.

## Assumptions

- Feature 001 supplies the existing public pipeline, Registry, contracts, validators, executor,
  workspace, evidence stores, and request-level source fallback.
- Feature 002 supplies the reviewed cpp_main P0, B0, and M0 identities and fixed source/build
  admission; B0 and M0 remain unchanged.
- The pinned official GP-ANN CLI is the only formal OGP integration boundary for this Feature.
- Requested/effective Partition counts and assignment amplification can be derived from parameters
  plus the validated Partition Artifact when needed; a new structural report is not required for
  the MVP.
- Configured subprocess thread budget is recorded as provenance; unavailable observed worker counts
  are not inferred or promoted into a separate completeness lifecycle.
- SIFT1M begins only after source admission, targeted negative tests, a small real official OGP
  Partition, and a small real composed closure pass.

## Dependencies

- `source-mapping.md` remains the authority for exact GP-ANN symbols, CLI behavior, formats,
  dependencies, parameter semantics, and worker observability.
- A trustworthy clean GP-ANN checkout with initialized pinned recursive dependencies must be
  available outside `upstream/diskann-cpp-main` for formal build and execution.
- The fixed SIFT1M Dataset identity and checksum must be available for the single permitted
  configuration.
