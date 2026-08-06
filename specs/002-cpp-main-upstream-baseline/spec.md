# Feature Specification: cpp_main Upstream Graph Baseline

**Feature Branch**: `feature/cpp-main-upstream-baseline`

**Created**: 2026-08-06

**Status**: Draft

**Input**: User description: "Create a read-only, auditable, reproducible Graph Baseline for the fixed Microsoft DiskANN cpp_main commit, limited to float32 L2 unfiltered SIFT1M sharded graph build; implement evidence-approved P0, B0, and M0 Stage Bindings and execute one Unified sharded structural closure under a draft Baseline Profile."

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `baseline_validation`

**Baseline Reference**: Repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit `78256bbab4685e1774e78d331e081a153be26823`, source location `upstream/diskann-cpp-main`, reproduction mode `upstream_faithful`.

**Replacement Scope**: `none`. This Feature exposes and validates the fixed upstream graph-build behavior; it does not replace Partition, Local Build, Merge, or any other Baseline stage with a paper or project-owned algorithm.

**Implementation Identities**: The Native Graph Reference and any evidence-supported Unified stage bindings MUST have distinct, stable identities and MUST remain distinguishable from test-only infrastructure, instrumented variants, deterministic variants, paper implementations, and Adapters. No implementation may claim `validated_graph_baseline` status before a formal Graph Parity result of `PASS`.

**ID-Space Contract**: `GlobalNodeId` remains stable over the complete dataset, while any native or standard `LocalNodeId` is meaningful only within its owning Partition. Research MUST document the native ID spaces and mappings for P0, B0, and M0. Any Unified standard local graph Artifact MUST contain a complete LocalToGlobalIdMap. A native representation that cannot be expressed faithfully by the current public contract MUST produce an explicit Contract assessment rather than an implicit conversion.

**Artifact Flow**: Native inputs, observable intermediate outputs, Native Graph Reference output, Unified P0/B0/M0 outputs, and final graph output MUST be immutable, checksummed, and linked by complete source identity. Artifact data, BuildManifest generation provenance, structural validation evidence, EvaluationReport, and the Feature-local structural closure summary MUST remain separate. An unavailable or unobservable intermediate output MUST be recorded as such and MUST NOT be reconstructed from the final graph and presented as a native stage Artifact.

**Validation Gate**: Before use, the workflow MUST validate the gitlink, submodule HEAD, fixed commit, upstream cleanliness, source/build identity, dataset checksum, parameter completeness, Artifact format and version, checksum, ID space, capabilities, and lineage. Any mismatch, missing evidence, invalid Artifact, or incompatible handoff MUST fail closed before downstream execution, evaluation, reuse, registration, or aggregation.

**Experiment Protocol**: The current delivery is limited to float32 vectors, L2 distance, unfiltered data, labels disabled, PQ/OPQ disabled, SIFT1M, and a proven sharded graph-build path with multiple Partitions. It executes one Unified structural closure from the immutable DatasetArtifact through P0, one B0 invocation per explicit PartitionId, and M0 under `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml` with `status: draft`. It validates every Artifact handoff and writes `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json` with `formal_graph_parity: false` and `validated_graph_baseline_registered: false`. No experiment larger than SIFT1M is authorized by this Feature.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Establish a trustworthy Native Graph Reference (Priority: P1)

As a Baseline maintainer, I need the fixed upstream source identity and native sharded graph-build behavior verified without modifying upstream so later pipeline comparisons have an auditable scientific control.

**Why this priority**: No stage binding or parity claim is meaningful until the actual native entry, branch conditions, parameters, randomness, outputs, and intermediate-file lifecycle are established from source and an out-of-tree Native Probe.

**Independent Test**: Review the source-identity record, source mapping, and Native Probe record and verify that every required identity matches the pinned commit, the probe entered the native sharded path, all writes stayed outside upstream, and upstream remained clean before and after the probe.

**Acceptance Scenarios**:

1. **Given** the repository and submodule are available, **When** Baseline admission runs, **Then** it verifies the gitlink, submodule HEAD, exact 40-character commit, and clean upstream state before any probe or build.
2. **Given** the fixed clean upstream source, **When** read-only research and the out-of-tree Native Probe are completed, **Then** the evidence identifies the real build target and native entry, complete consumed parameters and defaults, relevant threading and environment conditions, single-versus-sharded branch conditions, randomness, outputs, and intermediate-file lifecycle without changing upstream behavior.
3. **Given** any source-identity mismatch, dirty upstream state, unapproved source mutation, or write target inside upstream, **When** the workflow evaluates admission, **Then** it stops and emits no valid Native Graph Reference.

---

### User Story 2 - Implement Approved P0, B0, and M0 Stage Bindings (Priority: P2)

As a pipeline designer, I need the evidence-approved P0, B0, and M0 boundaries exposed as thin bindings over the pinned upstream callables so every stage can participate in the public Pipeline without copying or rewriting a DiskANN algorithm.

**Why this priority**: Gate A established that all three stages are separable and Contract-compatible. The implementation must now preserve those exact source-mapped boundaries and validate their native outputs before composition.

**Independent Test**: Invoke P0, B0, and M0 independently on small native fixtures and verify their pinned callable paths, output Artifact structure, ID spaces, lineage, checksum identity, fail-closed behavior, and absence of copied or rewritten algorithm logic.

**Acceptance Scenarios**:

1. **Given** the approved Gate A decision and a validated float32/L2 Dataset, **When** P0 runs, **Then** it invokes pinned `partition_with_ram_budget<float>` exactly once and emits a map-only PartitionArtifact containing the returned Partition count, centroids, complete ID maps, ownership, and Dataset lineage without materializing shard vectors.
2. **Given** an explicit valid PartitionId, its complete LocalToGlobalIdMap, and confirmed B0 parameters, **When** B0 runs, **Then** it reconstructs shard vectors through pinned `retrieve_shard_data_from_ids<float>` in map order, invokes pinned `Index<float>::build` and `Index<float>::save`, and emits a structurally valid LocalGraphArtifact with exact lineage.
3. **Given** the complete validated PartitionArtifact and one valid LocalGraphArtifact per PartitionId, **When** M0 runs, **Then** it invokes pinned `merge_shards` exactly once and emits a GlobalGraphArtifact containing the global graph and required cpp_main medoids.
4. **Given** an invalid input, incompatible descriptor, invalid stage output, or cancellation, **When** a Stage Binding reaches its gate, **Then** it publishes no normal Artifact and no downstream stage is invoked.
5. **Given** any implementation change, **When** its diff is reviewed, **Then** no partitioning, Vamana, merge, pruning, refinement, or connectivity algorithm has been copied or rewritten outside upstream.

---

### User Story 3 - Execute SIFT1M Unified Sharded Structural Closure (Priority: P3)

As a Baseline maintainer, I need the approved P0, B0, and M0 bindings to execute through the public Pipeline on SIFT1M so the complete Unified sharded structural path is auditable without claiming Graph Parity or Baseline registration.

**Why this priority**: The structural closure proves that the fixed-source bindings, ID spaces, immutable Artifact handoffs, and validation gates work together before any future scientific comparison is designed.

**Independent Test**: Execute the draft Profile once and review the ordered DatasetArtifact → P0 → PartitionArtifact → B0 × PartitionCount → LocalGraphArtifact[] → M0 → GlobalGraphArtifact evidence. Verify exact stage invocation counts, valid LocalNodeId and GlobalNodeId ranges, Artifact format/version/capability/lineage/checksum validation, fixed clean upstream state before and after, and the Feature-local structural closure report.

**Acceptance Scenarios**:

1. **Given** the fixed DatasetArtifact, admitted source/build identity, and draft Profile, **When** the Unified closure executes, **Then** P0 is invoked exactly once, one B0 is invoked for every explicit PartitionId, and M0 is invoked exactly once only after every LocalGraphArtifact validates.
2. **Given** a LocalGraphArtifact candidate, **When** it is validated before the next stage, **Then** its node and neighbor identifiers are legal Partition-scoped LocalNodeIds and its format, version, capability, lineage, and checksum are valid.
3. **Given** the complete validated PartitionArtifact and LocalGraphArtifact collection, **When** M0 completes, **Then** the resulting GlobalGraphArtifact uses only legal GlobalNodeIds and passes format, version, capability, lineage, and checksum validation.
4. **Given** a terminal success or retained failure, **When** the closure summary is written, **Then** `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json` records the run and states `formal_graph_parity: false` and `validated_graph_baseline_registered: false`.
5. **Given** any source mismatch, dirty upstream state, invalid Artifact, or failed stage, **When** the workflow reaches the corresponding gate, **Then** it fails closed, does not invoke downstream stages, and retains evidence without changing the pinned upstream commit or tree.

### Edge Cases

- The gitlink exists but the checked-out submodule HEAD differs from the pinned commit.
- The upstream tree is dirty before admission or becomes dirty during an out-of-tree probe.
- The native entry executes a single-build path because the actual sharding condition is not met.
- The native process creates an intermediate file but deletes or renames it before it can be observed without changing behavior.
- A stage symbol is visible in source but its output cannot be independently observed or attributed at runtime.
- Only one or two of P0, B0, and M0 are separable.
- Native randomness prevents a direct byte-for-byte comparison, while the appropriate logical comparison rule is not yet frozen.
- A B0 LocalToGlobalIdMap is complete and in range but its ordering cannot be consumed faithfully by the pinned upstream shard-retrieval callable.
- A current public Artifact or ID-space contract cannot represent an observed native format faithfully.
- A produced or reused Artifact has the correct path but the wrong checksum, source identity, format version, ID space, capability, or lineage.
- The Native Probe fails before proving the sharded path or exceeds the SIFT1M scope.
- A structural closure summary incorrectly claims Formal Graph Parity, Baseline registration, Recall, QPS, search latency, or complete DiskANN parity.

## Requirements *(mandatory)*

### Functional Requirements

#### Fixed source and native evidence

- **FR-001**: The Feature MUST bind the Baseline to repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, and full commit `78256bbab4685e1774e78d331e081a153be26823` in `upstream_faithful` mode.
- **FR-002**: Before any Baseline build, probe, or stage execution, the Feature MUST verify the repository gitlink, checked-out submodule HEAD, exact pinned commit, and clean upstream state.
- **FR-003**: The Feature MUST reject any operation that would edit, generate, format, add, delete, or overwrite a file under `upstream/diskann-cpp-main`.
- **FR-004**: Every build, run, log, temporary file, captured native intermediate, Artifact, Manifest, and report produced by this Feature MUST be stored outside `upstream/diskann-cpp-main`.
- **FR-005**: Before any stage binding implementation is approved, the Feature MUST complete read-only source research and an out-of-tree Native Probe against the pinned source.
- **FR-006**: The research record MUST identify the actual native build target and entry, consumed parameters and defaults, algorithm-relevant environment and thread conditions, single-versus-sharded branch conditions, randomness, outputs, and intermediate-file lifecycle, marking any unverified fact as uncertain.
- **FR-007**: The Native Probe record MUST preserve source and build identity, input dataset identity, complete effective parameters, branch evidence, observed Partition count, output and intermediate-file evidence, upstream state before and after the probe, and all failures or unresolved observations.
- **FR-008**: Native Probe evidence MUST prove that the observed execution entered the native sharded graph-build path before it may support a sharded Baseline claim.
- **FR-009**: The Feature MUST NOT change upstream parameters, defaults, random behavior, thread behavior, control flow, or outputs to make a branch observable or a parity comparison easier.

#### Stage mapping, separability, and Contract assessment

- **FR-010**: The Feature MUST maintain a commit-bound source mapping for the native branch decision and for P0, B0, and M0, with verified source facts separated from research conclusions.
- **FR-011**: For each of P0, B0, and M0, the Feature MUST independently record separability, binding mode, validation scope, Artifact observability, Contract status, availability, supporting evidence, and any unavailable reason, and MUST implement the approved thin binding by invoking pinned upstream `partition_with_ram_budget<float>`, the FR-014 B0 callable sequence, or pinned upstream `merge_shards`, respectively.
- **FR-012**: A stage separability decision MUST distinguish at least faithful independent exposure, composite-only behavior, unsupported behavior, Contract blockage, and uncertainty. For B0, the approved decision is `separability: separable`, `binding_mode: upstream_callable_wrapper`, and `validation_scope: structural_and_contract`.
- **FR-013**: The Feature MUST NOT infer an unobservable native stage Artifact or stage implementation result solely from the final Global Graph.
- **FR-014**: The Feature MUST NOT copy, translate, or reimplement an upstream algorithm to manufacture stage separability or claim upstream-faithful behavior. The B0 callable wrapper MUST use the validated complete Dataset vectors and selected LocalToGlobalIdMap to invoke pinned upstream `retrieve_shard_data_from_ids`, construct the upstream `IndexWriteParameters` with Profile `L`, local degree `floor(2R/3)`, unfiltered filter-list behavior resolved from Profile `L`, saturation false, Profile `T`, and unchanged upstream alpha/max-occlusion defaults, then construct `Index<float>`, call upstream `Index::build(filename, count)`, and call upstream `Index::save(prefix)`.
- **FR-015**: A native behavior that the public contract cannot faithfully express MUST be classified as `minimal_amendment_required` or `blocking_incompatible`, with evidence and affected stages recorded before any proposed contract change.
- **FR-016**: A Contract gap MUST block the affected stage until the required amendment is separately reviewed and approved; no contract, validator rule, or capability declaration may be changed silently.
- **FR-017**: The current Unified closure MUST execute DatasetArtifact → P0 → PartitionArtifact → B0 once for every explicit PartitionId → LocalGraphArtifact[] → M0 → GlobalGraphArtifact, validate every handoff before downstream execution, and retain exact stage invocation results in the Feature-local structural closure summary.
- **FR-018**: For the pinned commit, the M0 source mapping and description MUST reflect the verified native behavior and MUST NOT misclassify it as distance pruning, `RobustPrune`, global refinement, connectivity detection, or connectivity repair.

#### Artifact, identity, and provenance gates

- **FR-019**: Every valid Native or Unified run MUST bind repository, branch, full commit, dirty state, compiler and version, complete effective build options, dataset checksum, complete experiment parameters, algorithm-relevant environment, and resource conditions.
- **FR-020**: Artifact data, BuildManifest provenance, structural ValidationEvidence, EvaluationReport, and the Feature-local structural closure summary MUST remain separate objects and references.
- **FR-021**: Every produced, captured, or reused Artifact MUST be immutable and MUST record its type, format version, checksum identity, ID space, source identity, and complete lineage before downstream use.
- **FR-022**: Any standard local graph Artifact MUST use Partition-scoped LocalNodeIds and MUST provide a complete LocalToGlobalIdMap unless a separately approved, explicit contract states otherwise. A B0 output is valid only when the selected PartitionId exists, the map length is complete and every mapped GlobalNodeId is in Dataset range, reconstructed shard-vector row order matches the map, local graph node count equals map length, every neighbor is a valid LocalNodeId, observed maximum degree is at most `floor(2R/3)`, and Dataset/Partition/map lineage is exact.
- **FR-023**: Before any Artifact enters downstream execution, structural evaluation, reuse, or aggregation, the Feature MUST validate its format, version, checksum, ID space, capabilities, source identity, and lineage.
- **FR-024**: Missing, incompatible, unvalidated, or invalid inputs and outputs MUST fail closed and MUST remain excluded from downstream stages and results.
- **FR-025**: Native and Unified paths MUST use distinguishable implementation identities while referring to the same pinned Baseline source where upstream-faithful equivalence is claimed. B0 validation MUST NOT require byte-identical output or equivalence of process state, thread scheduling, or output bytes with the top-level native composite loop, and MUST NOT add a formal Graph Parity protocol or repeated-run statistics to the B0 structural-and-contract gate.

#### Scale and evidence retention

- **FR-034**: The Feature MUST NOT execute or authorize experiments larger than SIFT1M.
- **FR-035**: The Feature MUST preserve failed, invalid, interrupted, excluded, and unresolved runs with their reasons, without allowing them into successful result aggregation.

### Scope Boundaries

This Feature includes only the fixed upstream Graph Baseline for float32, L2, unfiltered, labels/PQ/OPQ disabled, SIFT1M sharded graph build; P0, B0, and M0 Stage Bindings; one SIFT1M Unified sharded structural closure; the draft Baseline Profile at `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml`; `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json`; and Artifact format, version, ID-space, capability, lineage, and checksum validation.

This Feature excludes Formal Graph Parity, a frozen Profile, `parity-protocol.md`, a Graph Parity Report, `validated_graph_baseline` registration, Recall, QPS, search latency or other performance evaluation, complete DiskANN build/search parity, paper algorithms, Loop Engine, Instrumented Baseline, deterministic Baseline variants, a generic Adapter framework, online updates, parameter search, automatic retry, and experiments larger than SIFT1M.

## Deferred Formal Parity *(non-normative)*

- A future, separately approved Feature or planning round may freeze a complete Baseline Profile and a parity protocol only after the Unified structural closure and all required Native-versus-Unified comparison prerequisites are reviewable.
- A future formal gate may be considered reachable only when P0, B0, and M0 are faithfully separable, required native intermediates are observable or capturable without behavior change, all contracts are compatible, and the Native Graph Reference is executable.
- A future formal comparison would need predeclared graph properties, randomness and ordering treatment, repetitions, aggregation, exclusions, decision rules, identical pinned source and Dataset identity, and retention of every declared run rather than best-run selection.
- A future gate that cannot start would be reported as `not_reached`; a reached gate could report only `PASS`, `FAIL`, or `INCONCLUSIVE` under its frozen protocol.
- Only a future protocol-valid `PASS` could be considered for `validated_graph_baseline` registration, and any such conclusion would remain limited to the validated graph-build behavior rather than Recall, QPS, latency, full DiskANN parity, or paper reproduction.

### Key Entities

- **Baseline Source Identity**: Fixed repository, branch, full commit, gitlink, submodule HEAD, and clean/dirty state used to admit or reject Baseline work.
- **Native Probe Record**: Auditable evidence from the out-of-tree native execution, including build and input identity, effective parameters, branch proof, observed outputs, intermediate lifecycle, upstream state, and unresolved facts.
- **Source Mapping Entry**: One commit-bound native source fact for a build decision, branch, P0, B0, or M0, including its evidence and verification status.
- **Stage Availability Record**: Independent P0, B0, or M0 conclusion covering separability, binding mode, validation scope, observability, Contract status, availability, supporting evidence, and unavailable reason.
- **P0 Stage Binding**: Project-owned orchestration outside `upstream/` that invokes pinned `partition_with_ram_budget<float>` and publishes a map-only PartitionArtifact with centroids and complete ID maps.
- **B0 Callable Binding**: Project-owned orchestration outside `upstream/` that invokes the pinned upstream shard-retrieval and `Index<float>` build/save callables without copying Vamana, and whose acceptance is limited to structural-and-contract validation.
- **M0 Stage Binding**: Project-owned orchestration outside `upstream/` that invokes pinned `merge_shards` over the complete validated input set and publishes a GlobalGraphArtifact containing global graph and medoids payloads.
- **Contract Assessment**: Evidence-backed conclusion that the public contract is compatible, requires a minimal separately approved amendment, or is blocking-incompatible for an affected stage.
- **Draft Baseline Profile**: Repository-relative YAML at `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml` with `status: draft`, containing the source, build, Dataset, complete stage parameters, environment, resources, and sharded branch conditions used by this structural closure.
- **Artifact**: Immutable native or Unified stage data with type/version, checksum, ID space, source identity, and lineage, separate from generation and evaluation records.
- **BuildManifest**: Complete provenance for one build or stage execution, including source, toolchain, inputs, parameters, resources, outputs, timing, status, and failure reason.
- **Unified Sharded Report**: Feature-local structural closure summary at `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json`; it references separate evidence, states that Formal Graph Parity and Baseline registration are false, and is not a public Artifact or scientific comparison report.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of admitted Baseline builds, probes, and runs match the pinned repository, branch, full commit, gitlink, and submodule HEAD; every mismatch is rejected before execution.
- **SC-002**: 100% of Feature-generated writes occur outside `upstream/diskann-cpp-main`, and the upstream tree has the same clean state before and after every admitted probe or run.
- **SC-003**: P0, B0, and M0 each have exactly one independently reviewable separability, binding-mode, validation-scope, observability, Contract, and availability decision backed by commit-bound source and probe evidence, and each implemented binding invokes only its approved pinned upstream callable boundary; B0 records the three approved values in FR-012.
- **SC-004**: 100% of facts required by the native research and probe requirements are either verified with evidence or explicitly marked uncertain; none is silently guessed.
- **SC-005**: Zero unobservable, composite-only, unsupported, uncertain, or Contract-blocked stages are registered or reported as independent upstream-faithful stages.
- **SC-006**: 100% of Artifacts entering downstream execution or structural evaluation have passed source identity, format, version, checksum, ID-space, capability, and lineage validation; every admitted B0 LocalGraphArtifact also passes all structural checks listed in FR-022.
- **SC-010**: Zero reports from this Feature claim Recall, QPS, search latency, complete DiskANN parity, paper reproduction, or scope beyond the validated graph-build behavior.

## Assumptions

- The repository contains a gitlink and checkout for the pinned upstream source, but their identity and clean state remain facts to verify rather than assumptions accepted for admission.
- SIFT1M and its recorded identity data are available for the structural closure; the draft Profile remains unfrozen throughout this delivery.
- Existing public pipeline contracts from Feature 001 are the starting point, but compatibility with native Baseline facts is not presumed. Any required change follows the explicit Contract-gap process.
- The Native Graph Reference means execution of the fixed upstream behavior through a verified native path, without instrumentation, source modification, fixed random seed, or control-flow change.
- Any future logical graph comparison, treatment of nondeterministic ordering, repetitions, and statistical decision criteria require a separate approved scope and are not specified by this delivery.
- Human implementation approval occurs only after source research, Native Probe evidence, and independent P0/B0/M0 decisions are reviewable.

## Dependencies

- The fixed upstream gitlink and source checkout must be available read-only at the pinned commit.
- Feature 001 supplies the current contract MVP and remains independently frozen; this Feature does not rename, renumber, or overwrite it.
- The Gate A human review approving the P0, B0, and M0 Stage Binding boundaries must remain reviewable.
- The structural closure depends on the identified SIFT1M payload/checksum and on evidence that Unified execution enters the intended sharded path.
