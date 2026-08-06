# Feature Specification: cpp_main Upstream Graph Baseline

**Feature Branch**: `feature/cpp-main-upstream-baseline`

**Created**: 2026-08-06

**Status**: Draft

**Input**: User description: "Create a read-only, auditable, reproducible Graph Baseline for the fixed Microsoft DiskANN cpp_main commit, limited to float32 L2 unfiltered SIFT1M sharded graph build and evidence-based P0/B0/M0 decisions; expose B0 as an upstream callable wrapper with structural-and-contract validation."

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `baseline_validation`

**Baseline Reference**: Repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit `78256bbab4685e1774e78d331e081a153be26823`, source location `upstream/diskann-cpp-main`, reproduction mode `upstream_faithful`.

**Replacement Scope**: `none`. This Feature exposes and validates the fixed upstream graph-build behavior; it does not replace Partition, Local Build, Merge, or any other Baseline stage with a paper or project-owned algorithm.

**Implementation Identities**: The Native Graph Reference and any evidence-supported Unified stage bindings MUST have distinct, stable identities and MUST remain distinguishable from test-only infrastructure, instrumented variants, deterministic variants, paper implementations, and Adapters. No implementation may claim `validated_graph_baseline` status before a formal Graph Parity result of `PASS`.

**ID-Space Contract**: `GlobalNodeId` remains stable over the complete dataset, while any native or standard `LocalNodeId` is meaningful only within its owning Partition. Research MUST document the native ID spaces and mappings for P0, B0, and M0. Any Unified standard local graph Artifact MUST contain a complete LocalToGlobalIdMap. A native representation that cannot be expressed faithfully by the current public contract MUST produce an explicit Contract assessment rather than an implicit conversion.

**Artifact Flow**: Native inputs, observable intermediate outputs, Native Graph Reference output, any reachable Unified P0/B0/M0 outputs, and final graph output MUST be immutable, checksummed, and linked by complete source identity. Artifact data, BuildManifest generation provenance, and EvaluationReport or Graph Parity findings MUST remain separate. An unavailable or unobservable intermediate output MUST be recorded as such and MUST NOT be reconstructed from the final graph and presented as a native stage Artifact.

**Validation Gate**: Before use, the workflow MUST validate the gitlink, submodule HEAD, fixed commit, upstream cleanliness, source/build identity, dataset checksum, parameter completeness, Artifact format and version, checksum, ID space, capabilities, and lineage. Any mismatch, missing evidence, invalid Artifact, or incompatible handoff MUST fail closed before downstream execution, evaluation, reuse, registration, or aggregation.

**Experiment Protocol**: The first version is limited to float32 vectors, L2 distance, unfiltered data, labels disabled, PQ/OPQ disabled, SIFT1M, and a proven sharded graph-build path with multiple Partitions. Formal parity MUST compare a Native Graph Reference with a reachable Unified Graph Pipeline under the same pinned source, dataset, complete parameters, algorithm-relevant environment, and resource conditions. The comparison protocol, repetitions, aggregation rules, treatment of randomness, and decision criteria MUST be declared and frozen only after source research and Native Probe evidence, before the formal run. All valid and invalid runs MUST be retained; selecting only the best run is prohibited. No experiment larger than SIFT1M is authorized by this Feature.

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

### User Story 2 - Decide P0, B0, and M0 availability independently (Priority: P2)

As a pipeline designer, I need an evidence-backed separability and availability decision for each of P0, B0, and M0 so an inaccessible stage is never fabricated from the final graph or from a rewritten approximation.

**Why this priority**: The native implementation may expose some stages only as a composite path. Treating all stages as jointly available or unavailable would either discard useful verified behavior or overstate what can be reproduced faithfully.

**Independent Test**: For each of P0, B0, and M0, inspect its commit-bound source mapping, Native Probe evidence, Artifact observability, separability finding, Contract assessment, and availability decision, and confirm that the decision follows the recorded evidence without copying or rewriting the upstream algorithm.

**Acceptance Scenarios**:

1. **Given** source and probe evidence for all three stages, **When** stage availability is assessed, **Then** P0, B0, and M0 each receive an independent separability, observability, Contract, and availability decision with its supporting evidence.
2. **Given** a stage that is composite-only, unsupported, unobservable, uncertain, or blocked by an incompatible Contract, **When** implementation scope is proposed, **Then** that stage fails closed and is not presented as an independent upstream-faithful stage.
3. **Given** a native fact that the public contract cannot faithfully represent, **When** the Contract is assessed, **Then** it is classified as `minimal_amendment_required` or `blocking_incompatible`, with affected stages and evidence recorded, and no silent contract change or generic Adapter is introduced.
4. **Given** a validated float32/L2 Dataset, an explicit valid PartitionId, its complete LocalToGlobalIdMap, and the confirmed B0 parameters, **When** B0 runs, **Then** it reconstructs the shard vectors through the pinned upstream callable path in map order, invokes the pinned upstream local graph build/save path, and emits a LocalGraphArtifact whose structural properties and lineage pass validation.

---

### User Story 3 - Reach a controlled Graph Parity conclusion (Priority: P3)

As an experiment reviewer, I need formal Graph Parity to run only when the complete Native and Unified paths are genuinely reachable, and I need the result reported without turning an infrastructure limitation into a false parity claim.

**Why this priority**: A trustworthy negative or not-reached conclusion is preferable to a fabricated Unified path or a comparison whose inputs, randomness, and decision rules were changed after observing results.

**Independent Test**: Review the frozen Baseline Profile, frozen Graph Parity Protocol, BuildManifests, validated Artifacts, and Graph Parity Report and verify that prerequisites were satisfied before execution, both paths used equivalent declared conditions, and the result follows the predeclared decision rules.

**Acceptance Scenarios**:

1. **Given** P0, B0, and M0 are all separable, their required Artifacts are observable without upstream modification, their contracts are compatible, and the Native Reference is executable, **When** the human implementation gate approves the complete path, **Then** a parity protocol may be frozen and the formal SIFT1M Graph Parity run may proceed.
2. **Given** any complete-path prerequisite is not met, **When** the Feature is reported, **Then** formal gate status is `not_reached`, affected stages and reasons are retained, and no full Unified parity or validated Baseline registration is claimed.
3. **Given** the formal gate was actually reached, **When** evidence is evaluated under the frozen protocol, **Then** the result is exactly one of `PASS`, `FAIL`, or `INCONCLUSIVE`, without post-run parameter, randomness, comparison, or aggregation changes.
4. **Given** a result other than `PASS`, **When** registration is considered, **Then** `validated_graph_baseline` is not registered.

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
- A formal run starts but incomplete evidence prevents a `PASS` or `FAIL` decision.
- A parity result is `PASS` for graph construction but is incorrectly presented as Recall, QPS, search latency, or complete DiskANN parity.

## Requirements *(mandatory)*

### Functional Requirements

#### Fixed source and native evidence

- **FR-001**: The Feature MUST bind the Baseline to repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, and full commit `78256bbab4685e1774e78d331e081a153be26823` in `upstream_faithful` mode.
- **FR-002**: Before any Baseline build, probe, stage execution, or parity run, the Feature MUST verify the repository gitlink, checked-out submodule HEAD, exact pinned commit, and clean upstream state.
- **FR-003**: The Feature MUST reject any operation that would edit, generate, format, add, delete, or overwrite a file under `upstream/diskann-cpp-main`.
- **FR-004**: Every build, run, log, temporary file, captured native intermediate, Artifact, Manifest, and report produced by this Feature MUST be stored outside `upstream/diskann-cpp-main`.
- **FR-005**: Before any stage binding implementation is approved, the Feature MUST complete read-only source research and an out-of-tree Native Probe against the pinned source.
- **FR-006**: The research record MUST identify the actual native build target and entry, consumed parameters and defaults, algorithm-relevant environment and thread conditions, single-versus-sharded branch conditions, randomness, outputs, and intermediate-file lifecycle, marking any unverified fact as uncertain.
- **FR-007**: The Native Probe record MUST preserve source and build identity, input dataset identity, complete effective parameters, branch evidence, observed Partition count, output and intermediate-file evidence, upstream state before and after the probe, and all failures or unresolved observations.
- **FR-008**: Native Probe evidence MUST prove that the observed execution entered the native sharded graph-build path before it may support a sharded Baseline claim.
- **FR-009**: The Feature MUST NOT change upstream parameters, defaults, random behavior, thread behavior, control flow, or outputs to make a branch observable or a parity comparison easier.

#### Stage mapping, separability, and Contract assessment

- **FR-010**: The Feature MUST maintain a commit-bound source mapping for the native branch decision and for P0, B0, and M0, with verified source facts separated from research conclusions.
- **FR-011**: For each of P0, B0, and M0, the Feature MUST independently record separability, binding mode, validation scope, Artifact observability, Contract status, availability, supporting evidence, and any unavailable reason.
- **FR-012**: A stage separability decision MUST distinguish at least faithful independent exposure, composite-only behavior, unsupported behavior, Contract blockage, and uncertainty. For B0, the approved decision is `separability: separable`, `binding_mode: upstream_callable_wrapper`, and `validation_scope: structural_and_contract`.
- **FR-013**: The Feature MUST NOT infer an unobservable native stage Artifact or stage-level parity result solely from the final Global Graph.
- **FR-014**: The Feature MUST NOT copy, translate, or reimplement an upstream algorithm to manufacture stage separability or claim upstream-faithful behavior. The B0 callable wrapper MUST use the validated complete Dataset vectors and selected LocalToGlobalIdMap to invoke pinned upstream `retrieve_shard_data_from_ids`, construct the upstream `IndexWriteParameters` with Profile `L`, local degree `floor(2R/3)`, unfiltered filter-list behavior resolved from Profile `L`, saturation false, Profile `T`, and unchanged upstream alpha/max-occlusion defaults, then construct `Index<float>`, call upstream `Index::build(filename, count)`, and call upstream `Index::save(prefix)`.
- **FR-015**: A native behavior that the public contract cannot faithfully express MUST be classified as `minimal_amendment_required` or `blocking_incompatible`, with evidence and affected stages recorded before any proposed contract change.
- **FR-016**: A Contract gap MUST block the affected stage until the required amendment is separately reviewed and approved; no contract, validator rule, or capability declaration may be changed silently.
- **FR-017**: Partial stage availability MAY support a separately scoped stage result, but it MUST NOT be used to claim that the complete Unified P0+B0+M0 Graph Pipeline is reachable.
- **FR-018**: For the pinned commit, the M0 source mapping and description MUST reflect the verified native behavior and MUST NOT misclassify it as distance pruning, `RobustPrune`, global refinement, connectivity detection, or connectivity repair.

#### Artifact, identity, and provenance gates

- **FR-019**: Every valid Native or Unified run MUST bind repository, branch, full commit, dirty state, compiler and version, complete effective build options, dataset checksum, complete experiment parameters, algorithm-relevant environment, and resource conditions.
- **FR-020**: Artifact data, BuildManifest provenance, and EvaluationReport or Graph Parity findings MUST remain separate objects and references.
- **FR-021**: Every produced, captured, or reused Artifact MUST be immutable and MUST record its type, format version, checksum identity, ID space, source identity, and complete lineage before downstream use.
- **FR-022**: Any standard local graph Artifact MUST use Partition-scoped LocalNodeIds and MUST provide a complete LocalToGlobalIdMap unless a separately approved, explicit contract states otherwise. A B0 output is valid only when the selected PartitionId exists, the map length is complete and every mapped GlobalNodeId is in Dataset range, reconstructed shard-vector row order matches the map, local graph node count equals map length, every neighbor is a valid LocalNodeId, observed maximum degree is at most `floor(2R/3)`, and Dataset/Partition/map lineage is exact.
- **FR-023**: Before any Artifact enters downstream execution, parity evaluation, reuse, registration, or aggregation, the Feature MUST validate its format, version, checksum, ID space, capabilities, source identity, and lineage.
- **FR-024**: Missing, incompatible, unvalidated, or invalid inputs and outputs MUST fail closed and MUST remain excluded from downstream stages and results.
- **FR-025**: Native and Unified paths MUST use distinguishable implementation identities while referring to the same pinned Baseline source where upstream-faithful equivalence is claimed. B0 validation MUST NOT require byte-identical output or equivalence of process state, thread scheduling, or output bytes with the top-level native composite loop, and MUST NOT add a formal Graph Parity protocol or repeated-run statistics to the B0 structural-and-contract gate.

#### Formal Graph Parity and reporting

- **FR-026**: The formal Graph Parity gate MUST be considered reachable only when P0, B0, and M0 are each faithfully separable, all required native intermediates are observable or capturable without behavior change, all required contracts are compatible, and the Native Graph Reference is executable.
- **FR-027**: Before a formal Graph Parity run, the Feature MUST freeze a complete Baseline Profile and a Graph Parity Protocol covering compared graph properties, treatment of ordering and randomness, dataset and parameter identity, repetitions, aggregation, exclusions, and decision rules.
- **FR-028**: The formal Graph Parity comparison MUST use SIFT1M with float32 vectors, L2 distance, unfiltered data, labels disabled, PQ/OPQ disabled, a proven sharded path, and the same frozen algorithm inputs and controlled conditions for Native and Unified paths.
- **FR-029**: The Feature MUST retain every declared run and MUST NOT report only the best, fastest, or most favorable result.
- **FR-030**: If the formal Graph Parity gate cannot start, the Graph Parity Report MUST record `formal_gate_status: not_reached`, the blocking reasons, affected stages, and available evidence; it MUST NOT use `INCONCLUSIVE` as a substitute for a gate that was never reached.
- **FR-031**: If the formal Graph Parity gate starts, its report MUST record exactly one result from `PASS`, `FAIL`, or `INCONCLUSIVE` according to the frozen protocol.
- **FR-032**: Only a formal Graph Parity result of `PASS` MAY authorize registration of `validated_graph_baseline`; `FAIL`, `INCONCLUSIVE`, `not_reached`, missing evidence, or invalid runs MUST NOT authorize registration.
- **FR-033**: A `PASS` result MUST be scoped only to the validated graph-build behavior and MUST NOT be presented as complete DiskANN build/search parity, Recall, QPS, search latency, or paper-reproduction evidence.
- **FR-034**: The Feature MUST NOT execute or authorize experiments larger than SIFT1M.
- **FR-035**: The Feature MUST preserve failed, invalid, interrupted, excluded, and unresolved runs with their reasons, without allowing them into successful result aggregation.

### Scope Boundaries

This Feature includes only the fixed upstream Graph Baseline for float32, L2, unfiltered, labels/PQ/OPQ disabled, SIFT1M sharded graph build, P0/B0/M0 evidence, the B0 upstream callable wrapper, Native Graph Reference, and conditionally reachable Unified Graph Parity.

This Feature excludes Recall, QPS, search latency, complete DiskANN build/search parity, paper algorithms, Loop Engine, Instrumented Baseline, deterministic Baseline variants, a generic Adapter framework, online updates, parameter search, automatic retry, and experiments larger than SIFT1M.

### Key Entities

- **Baseline Source Identity**: Fixed repository, branch, full commit, gitlink, submodule HEAD, and clean/dirty state used to admit or reject Baseline work.
- **Native Probe Record**: Auditable evidence from the out-of-tree native execution, including build and input identity, effective parameters, branch proof, observed outputs, intermediate lifecycle, upstream state, and unresolved facts.
- **Source Mapping Entry**: One commit-bound native source fact for a build decision, branch, P0, B0, or M0, including its evidence and verification status.
- **Stage Availability Record**: Independent P0, B0, or M0 conclusion covering separability, binding mode, validation scope, observability, Contract status, availability, supporting evidence, and unavailable reason.
- **B0 Callable Binding**: Project-owned orchestration outside `upstream/` that invokes the pinned upstream shard-retrieval and `Index<float>` build/save callables without copying Vamana, and whose acceptance is limited to structural-and-contract validation.
- **Contract Assessment**: Evidence-backed conclusion that the public contract is compatible, requires a minimal separately approved amendment, or is blocking-incompatible for an affected stage.
- **Baseline Profile**: Frozen identity of the source, build, dataset, complete parameters, environment, resources, and native branch conditions used by a formal Graph Parity study.
- **Artifact**: Immutable native or Unified stage data with type/version, checksum, ID space, source identity, and lineage, separate from generation and evaluation records.
- **BuildManifest**: Complete provenance for one build or stage execution, including source, toolchain, inputs, parameters, resources, outputs, timing, status, and failure reason.
- **Graph Parity Protocol**: Frozen pre-run declaration of the Native-versus-Unified logical comparison, randomness and ordering treatment, repetitions, aggregation, exclusions, and decision rules.
- **Graph Parity Report**: Terminal record stating whether the gate was reached and, if reached, whether the result was `PASS`, `FAIL`, or `INCONCLUSIVE`, together with evidence and scope.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of admitted Baseline builds, probes, and runs match the pinned repository, branch, full commit, gitlink, and submodule HEAD; every mismatch is rejected before execution.
- **SC-002**: 100% of Feature-generated writes occur outside `upstream/diskann-cpp-main`, and the upstream tree has the same clean state before and after every admitted probe or run.
- **SC-003**: P0, B0, and M0 each have exactly one independently reviewable separability, binding-mode, validation-scope, observability, Contract, and availability decision backed by commit-bound source and probe evidence; B0 records the three approved values in FR-012.
- **SC-004**: 100% of facts required by the native research and probe requirements are either verified with evidence or explicitly marked uncertain; none is silently guessed.
- **SC-005**: Zero unobservable, composite-only, unsupported, uncertain, or Contract-blocked stages are registered or reported as independent upstream-faithful stages.
- **SC-006**: 100% of Artifacts entering downstream execution or parity evaluation have passed source identity, format, version, checksum, ID-space, capability, and lineage validation; every admitted B0 LocalGraphArtifact also passes all structural checks listed in FR-022.
- **SC-007**: A formal Graph Parity run starts in 0% of cases where any complete-path prerequisite in FR-026 is unmet; every such case produces a `not_reached` report with blocking reasons.
- **SC-008**: 100% of reached formal gates produce exactly one protocol-derived result of `PASS`, `FAIL`, or `INCONCLUSIVE`, with every declared run retained and no post-run rule change.
- **SC-009**: 100% of `validated_graph_baseline` registrations reference a protocol-valid `PASS`, and 0% of `FAIL`, `INCONCLUSIVE`, `not_reached`, invalid, or incomplete-evidence cases produce that registration.
- **SC-010**: Zero reports from this Feature claim Recall, QPS, search latency, complete DiskANN parity, paper reproduction, or scope beyond the validated graph-build behavior.

## Assumptions

- The repository contains a gitlink and checkout for the pinned upstream source, but their identity and clean state remain facts to verify rather than assumptions accepted for admission.
- SIFT1M and its identity data will be available before any formal Graph Parity run; dataset checksums and physical input details will be established before freezing the Baseline Profile.
- Existing public pipeline contracts from Feature 001 are the starting point, but compatibility with native Baseline facts is not presumed. Any required change follows the explicit Contract-gap process.
- The Native Graph Reference means execution of the fixed upstream behavior through a verified native path, without instrumentation, source modification, fixed random seed, or control-flow change.
- The appropriate logical graph comparison, treatment of nondeterministic ordering, formal repetitions, and statistical decision criteria depend on source research and Native Probe evidence and therefore remain to be frozen later rather than specified here.
- Human implementation approval occurs only after source research, Native Probe evidence, and independent P0/B0/M0 decisions are reviewable.

## Dependencies

- The fixed upstream gitlink and source checkout must be available read-only at the pinned commit.
- Feature 001 supplies the current contract MVP and remains independently frozen; this Feature does not rename, renumber, or overwrite it.
- A human review is required before implementing Stage Bindings or starting the formal Graph Parity gate.
- Formal Graph Parity depends on an identified SIFT1M payload and checksum and on evidence that both Native and Unified execution enter the intended sharded path.
