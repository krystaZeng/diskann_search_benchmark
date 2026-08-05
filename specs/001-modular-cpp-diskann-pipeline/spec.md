# Feature Specification: Modular cpp_main DiskANN Pipeline

**Feature Branch**: `master` (no `before_specify` branch hook configured)

**Created**: 2026-08-04

**Status**: Draft

**Input**: Create the common modular Partition, Build Local Graph, and Merge Graph pipeline,
with Registry selection, independently validated and reusable stage Artifacts, compatibility
checks, a minimal Harness, and a deterministic Fake Pipeline.

## Clarifications

### Session 2026-08-04

- Q: Which algorithm, calculation object, and coverage define Artifact checksums? → A: Use
  SHA-256 for every stored payload's raw bytes and for one path-independent aggregate checksum over
  canonical interpretation metadata plus the ordered payload role, byte length, and SHA-256 list;
  exclude absolute paths, timestamps, BuildManifest, EvaluationReport, runtime metrics, and the
  aggregate checksum field itself.
- Q: Are INVALID_OUTPUT Artifacts retained, and how are retained outputs isolated? → A: Retain
  the immutable payload, Artifact metadata, failed BuildManifest, and validator evidence in a
  separate quarantine namespace that normal Registry, cache, resolver, downstream, evaluation, and
  aggregation paths cannot discover; allow diagnostic reads and explicit deletion only, and require
  any later valid output to receive a new Artifact identity rather than promoting in place.
- Q: Which StageContext services must be real in v1, and which may be stubbed? → A: Provide a
  real isolated WorkspaceRef, memory/thread budgets, managed TemporaryStorageRef, read-only
  deterministic in-memory VectorAccessor, exact deterministic L2 DistanceFunction, in-memory
  MetricsSink, and cooperative CancellationToken; expose CheckpointStore only as an unavailable
  stub that advertises no checkpoint capability and causes checkpoint-dependent implementations to
  fail preflight.
- Q: Which Adapter conversions preserve representation, and which change algorithm semantics? →
  A: Permit only demonstrably lossless format/layout/endianness/range-safe width conversions and
  identity-preserving Local/Global relabeling from complete explicit maps; any change to logical
  nodes, replicas, adjacency content or order, candidates, distances, vectors, graph metadata, or
  algorithm decisions is a separate implementation rather than an Adapter.
- Q: What minimum structural properties and failure scenarios must the Fake Pipeline verify? → A:
  Use a fixed six-node, two-Partition fixture with two overlapping GlobalNodeIds, exact vectors,
  LocalToGlobalIdMaps, local adjacency, deterministic Fake Merge semantics, expected global
  adjacency, and stable checksums; cover mapping, neighbor, checksum, version, ID-space,
  capability, size/degree, quarantine, Registry, reuse, and cancellation failures at their proper
  validation gates with no unauthorized downstream invocation.

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `infrastructure_validation`. This Feature defines common contracts and
test-only behavior; it produces no Baseline, paper-reproduction, or cross-paper scientific result.

**Baseline Reference**: `microsoft/DiskANN`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`, reproduction mode `not-executed`.
The referenced source tree remains fixed and read-only.

**Replacement Scope**: Define public contracts for Partition, Build Local Graph, and Merge Graph.
Only test-specific Fake implementations may occupy these stages in this Feature. No real Baseline,
paper algorithm, or production Adapter is included.

**Implementation Identities**: The Fake Pipeline uses three distinct test-only identities:
`fake_partition_v1`, `fake_local_graph_v1`, and `fake_merge_v1`. Future implementations must
coexist under different stable identities and may be selected without replacing these identities.

**ID-Space Contract**: Partition output is one aggregate PartitionArtifact that defines a
LocalNodeId space per Partition and a complete LocalToGlobalIdMap. For each Local Build invocation,
the pipeline passes that validated aggregate ArtifactRef plus one explicit PartitionId. Local Graph
output uses LocalNodeId and records the owning PartitionId. Merge input accepts validated local
graphs and maps them into a GlobalNodeId output space.

**Artifact Flow**: Dataset reference → aggregate PartitionArtifact → one LocalGraphArtifact per
explicitly selected PartitionId → LocalGraphArtifact collection → GlobalGraphArtifact. Each data
Artifact is immutable and separately referenced from its BuildManifest and EvaluationReport.

**Validation Gate**: Before any downstream stage, reuse, or evaluation, the system validates
Artifact type, format version, checksum, dataset identity, ID space, structural invariants, and
the selected implementation's required capabilities.

**Experiment Protocol**: Validation uses a locally generated, deterministic, small-scale dataset
with a recorded checksum and complete Fake configuration. It runs no SIFT dataset, parameter
search, performance comparison, or repeated-run selection. Exactly two deterministic runs use
identical input and configuration and are both retained under `retain_all`; payload and aggregate
Artifact checksums and the deterministic EvaluationReport findings projection must match, while
run identities, report identities, timestamps, and provenance remain distinct.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Execute a Complete Modular Pipeline (Priority: P1)

As a pipeline contributor, I want to select compatible implementations for all three stages and
run them through one common execution path, so that future Baseline and paper implementations can
plug into a proven stage lifecycle without changing the pipeline core.

**Why this priority**: End-to-end stage composition is the minimum useful capability. Without it,
the contracts, Registry, validators, and Harness cannot prove that the shared pipeline works.

**Independent Test**: Register the three deterministic Fake implementations, execute a small
dataset from Partition through Merge, and verify that each stage emits a valid Artifact reference
and the final global graph satisfies node-count and degree invariants.

**Acceptance Scenarios**:

1. **Given** three selected Fake implementations and complete build configuration, **When** the
   build lifecycle runs, **Then** the implementations are made runnable without modifying the
   upstream tree and complete source, toolchain, build-flag, and configuration provenance is
   recorded.
2. **Given** a deterministic dataset and three compatible Fake implementations, **When** the
   pipeline is executed, **Then** Partition runs once, Local Build receives the validated aggregate
   PartitionArtifact and one explicit in-range PartitionId for each Partition, Merge runs after all
   local outputs validate, and every output passes contract validation before its consumer starts.
3. **Given** overlapping Partition membership for at least one GlobalNodeId, **When** local graphs
   are merged, **Then** every local node and local neighbor is resolved through its owning
   LocalToGlobalIdMap and the final graph contains only GlobalNodeIds.
4. **Given** a configured global node count and maximum degree, **When** evaluation completes,
   **Then** the final graph contains exactly the declared logical node count and no node exceeds
   the declared degree bound.

---

### User Story 2 - Reject Invalid Artifacts Before Downstream Work (Priority: P2)

As an experiment operator, I want invalid or incomplete stage output rejected at a hard boundary,
so that corrupt data cannot consume downstream resources or contaminate evaluations and results.

**Why this priority**: A modular pipeline is trustworthy only if its handoffs fail closed. This
story protects every later implementation and experiment from invalid upstream state.

**Independent Test**: Supply negative fixtures with a missing map entry, mismatched map length,
out-of-range local/global IDs, an unsupported version, an altered checksum, and a missing
capability; verify that each fixture is rejected before the downstream Fake implementation is
invoked.

**Acceptance Scenarios**:

1. **Given** a LocalGraphArtifact whose LocalToGlobalIdMap is missing, incomplete, or inconsistent
   with `local_node_count`, **When** it is validated, **Then** it is marked invalid and Merge is not
   invoked.
2. **Given** a local or global adjacency containing an out-of-range neighbor, **When** contract
   validation runs, **Then** the Artifact is rejected and no evaluation report treats it as valid.
3. **Given** an Artifact with an unsupported format version, checksum mismatch, wrong ID space, or
   unmet capability, **When** compatibility is checked, **Then** execution stops before downstream
   work and records a specific failure reason.

---

### User Story 3 - Select Compatible Implementations and Reuse Artifacts (Priority: P3)

As a reproduction engineer, I want Registry-based selection and validated Artifact reuse, so that
I can change one or more stages without overwriting existing implementations or rerunning unchanged
upstream work.

**Why this priority**: Safe selection and reuse deliver the extensibility and experiment efficiency
needed after the core pipeline and its safety gates are established.

**Independent Test**: Resolve a compatible Fake chain through the Registry, reuse a previously
validated PartitionArtifact for a new downstream run, and then attempt an incompatible chain;
verify successful reuse and pre-execution rejection respectively.

**Acceptance Scenarios**:

1. **Given** unique implementation identities with compatible `requires` and `provides`, **When**
   a pipeline configuration is resolved, **Then** the Registry returns one unambiguous
   implementation for each selected stage.
2. **Given** a validated immutable PartitionArtifact whose dataset identity and required
   capabilities match a new run, **When** reuse is requested, **Then** Local Build can consume it
   without executing Partition again and the new BuildManifest records the reused input.
3. **Given** a selected implementation whose requirements are not provided upstream, **When** the
   configuration is preflighted, **Then** it is rejected before any stage implementation runs.

### Edge Cases

- An Artifact reference points to a missing or partially written payload.
- Artifact metadata declares a supported type but an unsupported format version.
- The recorded checksum differs from the immutable payload checksum.
- A LocalToGlobalIdMap is absent, has the wrong length, or contains a GlobalNodeId outside the
  dataset range.
- A local graph references a LocalNodeId outside its owning Partition.
- A global graph contains a LocalNodeId, an unresolved replica, or a GlobalNodeId outside the
  dataset range.
- A local graph refers to a different source Partition checksum or dataset checksum.
- Local Build is invoked without a PartitionId, or with a PartitionId absent from or outside the
  declared range of its aggregate PartitionArtifact.
- Two Registry entries attempt to use the same implementation identity.
- An implementation declares the correct Artifact type but the wrong input or output ID space.
- An implementation's `requires` contains a capability not supplied by the preceding stage.
- An implementation requires checkpoint storage while StageContext v1 exposes only the unavailable
  CheckpointStore stub; compatibility preflight must reject the implementation before invocation.
- An executable run with zero nodes, zero Partitions, or an empty Partition is invalid. A valid
  node may have an empty adjacency list, but a required graph payload may not be absent.
- A graph declares a maximum degree lower than an observed adjacency list length.
- A validated Artifact is offered for reuse under a different dataset identity or incompatible
  configuration.
- Evaluation is requested for an Artifact that has not passed the contract validator.
- A quarantined INVALID_OUTPUT Artifact is presented to a normal resolver or is revalidated after
  its underlying defect is corrected; the resolver must keep it isolated and corrected output must
  receive a new identity.
- An Adapter encounters integer overflow, an incomplete or ambiguous ID map, multiple replica
  occurrences, or a requested physical reorder that would change observable logical record or
  adjacency order; it must reject the conversion rather than guess, merge, or normalize.
- Cancellation is requested before or between Fake stages, or during Fake Partition, Local Build,
  or Merge at stage start, after input resolution/validation, or before output publication; the
  current stage must return `CANCELLED`, no later stage may start, and the Harness must not
  fabricate a valid or INVALID_OUTPUT Artifact.

## Requirements *(mandatory)*

### Functional Requirements

#### Identifier Semantics

- **FR-001**: The system MUST define GlobalNodeId as a dataset-stable integer identifier with
  legal range `0 <= id < global_node_count`.
- **FR-002**: The system MUST define LocalNodeId as an identifier valid only inside one owning
  Partition, with legal range `0 <= id < local_node_count` for that Partition.
- **FR-003**: The system MUST define PartitionId as unique within one pipeline run, with legal
  range `0 <= id < partition_count`.
- **FR-004**: Every standard PartitionRecord shard payload and LocalGraphArtifact MUST reference a
  complete LocalToGlobalIdMap whose length equals `local_node_count` and whose values are legal
  GlobalNodeIds.
- **FR-005**: Every LocalGraphArtifact adjacency MUST contain only LocalNodeIds valid in its owning
  Partition; every GlobalGraphArtifact adjacency MUST contain only legal GlobalNodeIds.

#### Stage Artifacts and Provenance

- **FR-006**: The system MUST define a versioned PartitionArtifact that identifies the dataset,
  dataset checksum, global node count, Partition count, each Partition's local node count, and the
  references and checksums for each shard payload and complete LocalToGlobalIdMap, plus declared
  capabilities and an integrity checksum.
- **FR-007**: The system MUST define a versioned LocalGraphArtifact that identifies its owning
  Partition, declares `id_space: local`, references its graph and LocalToGlobalIdMap, records its
  explicit owning PartitionId and the aggregate checksum of the PartitionArtifact containing that
  Partition record, declares maximum and observed degree and capabilities, and carries an integrity
  checksum.
- **FR-008**: The system MUST define a versioned GlobalGraphArtifact that declares
  `id_space: global`, records global node count, references the global graph and source Artifacts,
  declares maximum and observed degree and capabilities, and carries an integrity checksum.
- **FR-009**: Every stage data Artifact MUST be immutable and addressed through an ArtifactRef that
  contains `artifact_type`, `format_version`, SHA-256 `aggregate_checksum`, and `metadata_path`.
  `ArtifactRef.metadata_path` MUST resolve to the immutable Artifact metadata document and MUST NOT
  identify a BuildManifest or EvaluationReport.
  Every stored payload MUST have a SHA-256 computed over its exact stored bytes. The aggregate
  checksum MUST be SHA-256 over a path-independent canonical serialization of Artifact type,
  format version, dataset checksum, declared ID space, interpretation-affecting structural fields,
  declared capabilities, source Artifact checksums, and an ordered list of each payload's role,
  byte length, and SHA-256. The contract MUST define and version the byte-for-byte canonical
  serialization. Absolute paths, timestamps, BuildManifest, EvaluationReport, runtime metrics, and
  the aggregate checksum field itself MUST be excluded.
- **FR-010**: Stage boundaries MUST pass ArtifactRefs rather than mutable graph or Partition
  objects shared between implementations.
- **FR-011**: Artifact, BuildManifest, and EvaluationReport MUST use separate contracts and stored
  references. No one object may substitute for another.
- **FR-012**: A BuildManifest MUST record the stage, implementation identity and version,
  repository/branch/full commit or equivalent source identity, workspace modification state,
  compiler and version, complete build flags, complete inputs and outputs, dataset identity,
  complete execution configuration, resource budget, execution status, and failure or reuse reason
  needed to trace and repeat the Fake run.
- **FR-013**: An EvaluationReport MUST record validation and graph-quality findings without
  mutating the evaluated Artifact or serving as its generation provenance.

#### Registry and Compatibility

- **FR-014**: Every implementation descriptor MUST declare a unique implementation identity,
  implementation version, stage, `requires`, `provides`, each required input's accepted ID space,
  the produced output ID space, and `parameter_schema_path`. `parameter_schema_path` MUST be a
  stable repository-relative path to the implementation parameter JSON Schema.
- **FR-015**: `requires` MUST contain an `input_artifacts` collection whose entries each identify
  one required Artifact type, accepted format versions, and input ID space; required capabilities
  and runtime services remain properties of the complete requirement. `provides` MUST identify one
  produced Artifact type, version, output ID space, and capabilities. A Merge descriptor MUST
  require both PartitionArtifact and LocalGraphArtifact inputs without adding cardinality,
  multiplicity, or optional-input fields.
- **FR-016**: The Implementation Registry MUST register, enumerate, and resolve implementations by
  stable identity and stage without overwriting an existing identity. It MUST have no default
  constructor and MUST be explicitly constructed with `RegistryAdmissionPolicy::kProduction` or
  `RegistryAdmissionPolicy::kTest`. Production admission MUST reject descriptors with
  `test_only == true`; test admission MUST accept both test-only and formal descriptors. Admission
  failure MUST leave Registry state unchanged, and both policies MUST enforce duplicate identity,
  declared-stage, sealing, no-overwrite, and no-fallback rules.
- **FR-017**: The Registry MUST reject duplicate implementation identities and selection of an
  implementation for a stage it does not declare.
- **FR-018**: Before execution, the pipeline MUST validate the complete selected chain so that
  every entry in each downstream `requires.input_artifacts` collection is satisfied by upstream
  `provides`, including Artifact version and per-input ID-space compatibility. Merge preflight MUST
  match both its PartitionArtifact and LocalGraphArtifact requirements. A missing required input,
  incompatible accepted version, or incompatible input ID space MUST reject preflight before any
  implementation is created or invoked.
- **FR-019**: An incompatible chain MUST be rejected before build or execution invokes any selected
  stage implementation.

#### Validation and Harness

- **FR-020**: A contract validator MUST validate Artifact type, format version, dataset identity,
  declared ID space, structural fields, ID ranges, degree bounds, and declared capabilities. It
  MUST recompute every payload SHA-256 from exact stored bytes and then recompute the aggregate
  SHA-256 from the contract's canonical, path-independent checksum input before accepting the
  Artifact.
- **FR-021**: A LocalToGlobalIdMap validator MUST detect a missing map, length mismatch, invalid
  GlobalNodeId, and inconsistency with the owning Partition and source checksum.
- **FR-022**: The GlobalGraphArtifact validator MUST verify that the logical node count equals the
  declared dataset node count and that observed degree never exceeds declared maximum degree.
- **FR-023**: Invalid or incomplete produced output MUST receive status `INVALID_OUTPUT`. Its
  immutable payload, Artifact metadata, failed BuildManifest, and validator evidence MUST be
  retained in a physically or logically separate quarantine namespace until explicit deletion.
  Normal Registry enumeration, Artifact cache lookup, ordinary Artifact resolution, downstream
  execution, evaluation, reuse, comparison, and aggregation MUST NOT discover or accept quarantine
  entries. Quarantined output MUST provide no capabilities and MUST be readable only through an
  explicit diagnostic path.
- **FR-024**: Validation failure MUST preserve a machine-readable failure category and a
  human-readable reason identifying the failed contract condition. A quarantined Artifact MUST NOT
  be promoted or mutated in place, even if later revalidation would succeed; corrected output MUST
  be written under a new Artifact identity and MAY retain a diagnostic reference to the quarantined
  identity.
- **FR-025**: The minimal Harness MUST expose distinct build, execute, validate, and evaluate
  lifecycle operations and enforce validation between every producer and consumer.
- **FR-026**: The build lifecycle MUST resolve implementation identities, make the selected Fake
  implementations runnable outside the upstream tree, and record the complete build configuration
  and provenance before execution begins.
- **FR-027**: The execute lifecycle MUST invoke only a preflight-compatible implementation and MUST
  stop the pipeline when a stage fails or produces invalid output.
- **FR-028**: The validate lifecycle MUST be independently callable for both newly produced and
  previously stored ArtifactRefs.
- **FR-029**: The evaluate lifecycle MUST accept only validated Artifacts and MUST report graph
  integrity, node count, invalid-neighbor count, and degree-bound findings required by this Feature.
- **FR-030**: The Harness MUST NOT modify or repair an implementation or Artifact automatically
  when validation fails.

#### Fake Pipeline and Reuse

- **FR-031**: The Feature MUST provide test-only Fake implementations for Partition, Local Build,
  and Merge, each registered under its own identity and declaring compatible contracts.
- **FR-032**: The Fake Pipeline MUST use a deterministic small dataset, deterministic Partition
  membership, deterministic local adjacency, and deterministic global output with recorded input
  and output checksums.
- **FR-033**: The Fake dataset MUST exercise at least two Partitions and at least one GlobalNodeId
  represented by different LocalNodeIds in more than one Partition.
- **FR-034**: The Fake Merge implementation MUST resolve local nodes and neighbors using each
  Artifact's LocalToGlobalIdMap and emit only GlobalNodeIds.
- **FR-035**: At a minimum, the Fake Pipeline negative-fixture categories MUST include incomplete
  mappings, invalid Local Build PartitionId selection, out-of-range local and global neighbors,
  checksum mismatch, unsupported version, and incompatible `requires`/`provides`. This is a
  high-level category summary; FR-048 is the authoritative complete negative fixture matrix.
- **FR-036**: A stored Artifact MAY be reused only after current validation succeeds and its
  dataset identity, format version, checksum, ID space, and capabilities satisfy the new consumer.
- **FR-037**: Reusing a valid Artifact MUST avoid rerunning its producer and MUST be recorded as
  reuse in the consuming run's BuildManifest.
- **FR-038**: All project-owned contracts, Fake implementations, Harness behavior, tests, and
  generated outputs for this Feature MUST remain outside `upstream/diskann-cpp-main`.
- **FR-039**: An executable Fake run MUST have `global_node_count > 0`, `partition_count > 0`, and
  `local_node_count > 0` for every Partition. A zero-neighbor adjacency list MAY be valid, but a
  missing required graph payload MUST be treated as incomplete output.
- **FR-040**: StageContext v1 MUST provide a functional isolated WorkspaceRef, positive configured
  memory and thread budgets, a managed TemporaryStorageRef, a read-only deterministic in-memory
  VectorAccessor over the Fake dataset, an exact deterministic L2 DistanceFunction, an in-memory
  MetricsSink that records stage lifecycle events and counters, and a cooperative CancellationToken
  observable by every Fake stage. Fake Partition, Local Build, and Merge MUST each check the token
  at stage start, after input resolution/validation, and before output publication.
- **FR-041**: StageContext v1 MUST expose CheckpointStore only as an explicitly unavailable stub.
  The stub MUST reject checkpoint operations, MUST NOT advertise a checkpoint capability, and MUST
  NOT satisfy an implementation's checkpoint requirement. Any implementation requiring checkpoint
  storage MUST be rejected during compatibility preflight.
- **FR-042**: An Adapter MAY perform only lossless serialization, container, storage-layout, or
  endianness conversion; range-safe integer-width conversion; physical record relocation that
  preserves observable logical record and adjacency order; and identity-preserving LocalNodeId to
  GlobalNodeId or GlobalNodeId to LocalNodeId relabeling driven by a complete explicit map. Each
  source node occurrence MUST resolve to exactly one declared target identity. Replica occurrences
  across Partitions MUST remain distinguishable and MUST NOT be aggregated by an Adapter.
- **FR-043**: An Adapter MUST reject conversion when a map is missing, incomplete, ambiguous, or
  out of range, or when integer conversion would overflow. It MUST NOT add, remove, deduplicate,
  sort, reorder, truncate, symmetrize, or otherwise alter adjacency; merge or split logical nodes;
  aggregate replicas; invent missing IDs; recompute distance; change the distance metric; quantize
  or mutate vectors; modify entry points, centroids, replica ownership, candidates, degree,
  parameters, randomness, pruning, refinement, or connectivity. Any such transformation MUST be a
  separately identified implementation with its own `requires`, `provides`, tests, and deviation
  record, not an Adapter.
- **FR-044**: Every Adapter MUST declare its accepted and produced format versions and ID spaces,
  preserve a complete source-to-target trace, and provide round-trip or canonical logical
  equivalence evidence appropriate to the conversion. A byte-level checksum MAY change when the
  physical representation changes; source and output checksums MUST both be retained.
- **FR-045**: The canonical positive Fake fixture MUST contain six two-dimensional vectors with
  `GlobalNodeId` values `0..5` and coordinates `[(0,0), (1,0), (2,0), (0,1), (1,1), (2,1)]`.
  Partition `0` MUST use `LocalToGlobalIdMap [0,1,2,3]`; Partition `1` MUST use
  `LocalToGlobalIdMap [2,3,4,5]`. Thus GlobalNodeIds `2` and `3` occur in both Partitions under
  different Partition-scoped LocalNodeIds, and the union of Partition membership covers all six
  dataset nodes.
- **FR-046**: In both Partitions, the canonical Fake local adjacency by LocalNodeId MUST be
  `0:[1,2]`, `1:[0,2]`, `2:[1,3]`, and `3:[]`, with declared local maximum degree `2`. The
  separately identified Fake Merge implementation MUST map these lists through their owning maps,
  visit Partitions in ascending PartitionId order, preserve first occurrence order, remove only
  exact duplicate GlobalNodeId candidates, and truncate at declared global maximum degree `3`.
  Its exact expected global adjacency MUST be `0:[1,2]`, `1:[0,2]`, `2:[1,3,4]`, `3:[2,4]`,
  `4:[3,5]`, and `5:[]`. These rules define only the Fake implementation and MUST NOT be attributed
  to the fixed cpp_main Baseline or to Adapter behavior.
- **FR-047**: The canonical fixture MUST record versioned expected dataset, payload, and aggregate
  checksums derived under the contracts in FR-009. Its positive test MUST verify the exact maps and
  adjacency lists, complete GlobalNodeId coverage, all local and global neighbor ranges, resolution
  of every emitted local reference, final node count `6`, local observed degree at most `2`, global
  observed degree at most `3`, acceptance of the valid zero-neighbor lists, separation of Artifact,
  BuildManifest, and EvaluationReport references; identical payload and aggregate Artifact
  checksums and deterministic EvaluationReport findings projections across two retained
  deterministic runs with identical input and configuration, both preserved under `retain_all`;
  distinct run identities, report identities, timestamps, and provenance; and validated Artifact
  reuse without reinvoking its producer.
- **FR-048**: This is the authoritative complete, normalized, executable negative fixture matrix.
  It MUST cover: absent, wrong-length, and out-of-range LocalToGlobalIdMaps; missing or out-of-range
  explicit Local Build PartitionId selection; out-of-range local and global neighbors; altered
  payload and aggregate checksums; unsupported format version; wrong ID space; unsatisfied
  capability including checkpoint storage; observed degree above its declaration; wrong final node
  count; missing required payload; duplicate Registry identity; reuse under a mismatched dataset
  identity or checksum; cooperative cancellation before a stage or between stages; and independent
  cooperative cancellation during Fake Partition, Local Build, and Merge at each checkpoint
  declared by FR-040.
- **FR-049**: Every negative fixture MUST assert the specific failure category and enforcement
  gate. Compatibility and Registry failures MUST cause zero selected-stage invocations; produced
  invalid or incomplete output MUST be quarantined and cause zero consumer, evaluation, reuse, or
  aggregation invocations; reuse mismatch MUST leave the cached Artifact unchanged and unconsumed;
  cancellation MUST prevent every later stage and MUST be reported as cancellation rather than as
  a fabricated Artifact validation result.

### Key Entities

- **GlobalNodeId**: Stable logical identity of one dataset vector across all stages.
- **LocalNodeId**: Partition-scoped physical identity used by a local shard and local graph.
- **PartitionId**: Identity of one Partition within a pipeline run.
- **LocalToGlobalIdMap**: Complete positional mapping from every LocalNodeId in one Partition to a
  legal GlobalNodeId.
- **ArtifactRef**: Immutable reference containing `artifact_type`, `format_version`,
  `aggregate_checksum`, and `metadata_path`; `metadata_path` points only to the immutable Artifact
  metadata document, never to a BuildManifest or EvaluationReport.
- **PartitionArtifact**: Dataset-bound description of all Partitions, their local sizes, shard
  references, and LocalToGlobal mappings.
- **LocalGraphArtifact**: Partition-bound local-ID graph plus its mapping, source identity, entry
  information, and degree declarations.
- **GlobalGraphArtifact**: Dataset-wide global-ID graph with source references and degree
  declarations.
- **BuildManifest**: Trace of how a stage output was produced or reused.
- **EvaluationReport**: Independent validation, integrity, and quality findings for an Artifact.
- **Quarantined Artifact**: Immutable INVALID_OUTPUT payload and evidence stored outside normal
  discovery paths for diagnosis and explicit deletion only; it is never a valid pipeline input.
- **ImplementationDescriptor**: Registry record containing identity, stage, version, a collection
  of required input Artifact contracts, one produced output contract, capabilities, runtime
  requirements, a stable repository-relative `parameter_schema_path`, and the existing `test_only`
  marker.
- **RegistryAdmissionPolicy**: Explicit Registry construction policy. `kProduction` rejects
  test-only descriptors; `kTest` accepts both test-only and formal descriptors. It is not stored in
  ImplementationDescriptor.
- **Implementation Registry**: Collection that resolves immutable implementation descriptors for a
  configured stage chain.
- **Capability**: Versioned feature or service named in `requires` or `provides`.
- **StageContext**: Per-run access to the isolated workspace, resource budgets, temporary storage,
  vector access, distance function, metrics, cancellation, and explicitly available optional
  runtime services.
- **Adapter**: Independently versioned, capability-declared representation converter that preserves
  logical algorithm output and records complete source-to-target traceability.
- **Fake Pipeline**: Test-only, deterministic three-stage composition used to prove contracts and
  lifecycle behavior without a real Baseline or paper algorithm.

## Out of Scope

- Integration with the real `cpp_main` Baseline or execution of upstream Baseline commands.
- Any real paper algorithm, official paper repository adaptation, or production Adapter.
- Loop Engine behavior, experiment matrices, scheduling, retry, checkpointing, or state persistence.
- SIFT100M or any other large-scale experiment; this Feature runs only deterministic small data.
- Online insertion, deletion, update, or dynamic-index behavior.
- Search-quality benchmarking, QPS/latency optimization, or paper-result comparison.
- Automatic mutation, repair, pruning, refinement, or connectivity repair of algorithm output.
- Silent fallback from an incompatible implementation to a different implementation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The deterministic Fake Pipeline completes all three stages end to end in 100% of
  valid test runs and produces one validated GlobalGraphArtifact.
- **SC-002**: Two repeated Fake Pipeline runs with identical input and configuration produce
  identical payload and aggregate Artifact checksums and identical deterministic evaluation
  findings, while retaining distinct run identities, report identities, timestamps, and
  provenance. Both runs are retained under `retain_all`, and EvaluationReport equality is assessed
  only through the deterministic findings projection rather than complete report-object equality.
- **SC-003**: The mapping validator rejects 100% of fixtures covering a missing map, wrong map
  length, out-of-range GlobalNodeId, and source-Partition mismatch.
- **SC-004**: The graph validators reject 100% of fixtures containing an out-of-range local or
  global neighbor before any downstream implementation is invoked.
- **SC-005**: Every valid Fake output verifies final logical node count equal to input node count
  and observed degree less than or equal to the declared maximum.
- **SC-006**: Every tested incompatible `requires`/`provides`, format-version, checksum, or ID-space
  combination, including a checkpoint requirement, is rejected during preflight, with zero stage
  implementation invocations.
- **SC-007**: Every invalid or incomplete Artifact fixture is retained with failure evidence in
  quarantine while remaining undiscoverable to normal resolution and excluded from downstream
  execution, evaluation, reuse, comparison, and aggregation.
- **SC-008**: A validated reusable PartitionArtifact can drive a downstream Fake run with zero new
  Partition executions while preserving an auditable reuse record.
- **SC-009**: Every successful stage run exposes separate Artifact and BuildManifest references,
  and every evaluation exposes a separate EvaluationReport reference.
- **SC-010**: Running the complete Feature validation suite leaves the pinned upstream tree and
  submodule identity unchanged and executes no dataset larger than the deterministic Fake dataset.
- **SC-011**: A contributor can select any registered compatible Fake stage combination solely by
  configuration, without editing or replacing an existing implementation identity.
- **SC-012**: The canonical six-node positive fixture passes every exact mapping, adjacency,
  coverage, ID-range, node-count, degree-bound, zero-neighbor, checksum, separation, determinism,
  and reuse assertion defined in FR-045 through FR-047.
- **SC-013**: Every case in the authoritative complete negative fixture matrix defined by FR-048 is
  rejected at its specified gate, records its expected failure category, and causes zero
  unauthorized downstream invocations; each produced invalid or incomplete output remains isolated
  in quarantine.

## Assumptions

- The first contract versions use one project-defined version family; the planning phase chooses
  its concrete serialization and language representation.
- The minimal Harness may run on one machine and in one process; distributed execution is not
  required by this Feature.
- Fake vectors and graph topology are generated locally from a fixed specification and receive a
  stable dataset checksum.
- Standard LocalGraphArtifacts use LocalNodeId; other ID spaces require future explicit Adapters.
- Artifact reuse means logical reuse through a validated ArtifactRef, not mutable sharing of an
  in-memory object.
- Evaluation in this Feature is limited to contract and graph-integrity outcomes, not ANN search
  quality or performance claims.
- Empty inputs and zero-neighbor nodes may be valid only when the corresponding contract explicitly
  permits them; missing required payloads are always incomplete.

## Dependencies

- Ratified project constitution v1.0.0 and root `AGENTS.md` governance.
- The fixed upstream submodule identity is available for provenance checks but is never executed or
  modified by this Feature.
- The planning phase must choose concrete contract serialization, implementation language, build
  system, and test runner without weakening this specification.
