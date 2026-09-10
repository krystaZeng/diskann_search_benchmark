# Feature Specification: XN-Descent Local Build

**Feature Branch**: `feature/xn-descent-local-build`

**Created**: 2026-09-04

**Status**: Draft

**Input**: User description: "Implement Algorithm 1, eXtended Neighborhood Descent (XN-Descent),
from the XN-Graph paper as a separately identifiable Local Build implementation in the existing
DiskANN pipeline, while preserving the fixed Baseline and clearly separating paper-stated behavior
from faithful adaptations required by the current contracts."

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `single_paper_reproduction` at the XN-Descent component level.

**Reproduction Mode**: `faithful_adaptation`. This Feature reproduces the graph-construction
behavior of Algorithm 1 within the repository's Local Build boundary. It is not an exact or complete
reproduction of the paper's disk-based system.

**Baseline Reference**: Microsoft DiskANN repository
`https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
`78256bbab4685e1774e78d331e081a153be26823`, with fixed read-only source at
`upstream/diskann-cpp-main`. The Baseline remains P0 → B0 → M0 in `upstream_faithful` mode.

**Paper Reference**: Cheng Zhang, Jianzhi Wang, Wan-Lei Zhao, and Shihai Xiao, "Highly Efficient
Disk-based Nearest Neighbor Search on Extended Neighborhood Graph," SIGIR 2025, DOI
`10.1145/3726302.3729996`. The reviewed local PDF has SHA-256
`75815cf0e447bb1445e8b142ef85c4fa55176c30f265a994e4fa66b67498bc0a`. Section 2.2,
Section 3.2, Algorithm 1 and its `TryInsert` function, Section 3.3, and the parameter statements in
Section 4 are the primary paper evidence for this Feature.

**Replacement Scope**: `Local Build` only. The adaptation path is P0 → one B1 XN-Descent
execution for every Partition → the reusable M0 → GlobalGraph. P0, B0, M0's pinned
`merge_shards` algorithm, search evaluation, and the fixed Baseline runner remain unchanged. The
M0 wrapper exposes its existing merge degree as explicit `merge.R`; the frozen Baseline runner
continues to pass `R=16`, while its B0 continues to build with local degree 10. Mapping Algorithm
1's dataset `S` to one P0 Partition and selecting the required local graph entry point are explicit
contract adaptations, not paper-stated XN-Descent behavior.

**Implementation Identities**: Existing P0 remains
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1`; existing B0 remains
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1`; existing M0 remains
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1`. B1 MUST use a distinct,
stable identity, provisionally `xn_graph_xn_descent_local_build_v1`, and MUST be reported in
`faithful_adaptation` mode.

**Algorithm Basis**: For each local node `i`, Algorithm 1 maintains candidate neighborhood `C[i]`,
reverse neighborhood `R[i]`, and extended neighborhood `G[i]`. Candidates from `C[i] ∪ R[i]`
are checked against `G[i]` using the paper's Equation (2). Every candidate/neighbor encounter also
cross-inserts the two encountered points into their candidate neighborhoods. `G` is the returned
XN-Graph; `C` is an evolving approximate omega-nearest-neighbor graph and `R` carries reverse
relationships from the previous iteration.

**Composition Classification**: P0 is the fixed DiskANN k-means-based Partition rather than the
paper's Boundary-adaptive Balanced Partition (BBP). Therefore P0 → B1 × Partition count → M0
→ GlobalGraph is a controlled component replacement and MUST NOT be presented as a reproduction
of BBP, the paper's Secondary Index, IMF Search, SSD page layout, or its complete end-to-end
retrieval system.

**ID-Space Contract**: B1 consumes one validated aggregate `PartitionArtifact/v1`, an explicit
owning `PartitionId`, and access to the corresponding Dataset vectors. It interprets the selected
Partition in Partition-scoped LocalNodeId space and emits `LocalGraphArtifact/v1` in `id_space:
local`. Every output MUST preserve the selected Partition's complete LocalToGlobalIdMap, have the
same local node count as that map, contain only valid LocalNodeIds, and include a valid deterministic
local entry point required by the existing native local-graph representation.

**Artifact Flow**: One validated DatasetArtifact → one validated aggregate PartitionArtifact →
exactly one validated B1 LocalGraphArtifact for every PartitionId → reusable existing M0 → one
existing GlobalGraphArtifact. Artifact, BuildManifest, validation evidence, and EvaluationReport
remain separate objects and references.

**Validation Gate**: B1 MUST NOT start unless Dataset and Partition inputs validate for type,
format version, checksum, Dataset identity, Partition ownership, LocalToGlobalIdMap completeness,
ID space, lineage, and declared capabilities. A B1 output MUST pass the existing
`LocalGraphArtifact/v1` validator before M0, evaluation, cache reuse, comparison, or aggregation.
The P0 → B1 × Partition count → M0 composition MUST require positive explicit `merge.R` and a
complete validated set of B1 outputs before M0 starts. `lambda` and `merge.R` are independent;
neither is derived from the other.

**Experiment Protocol**: Validation proceeds through deterministic semantic fixtures, unit and
contract checks, small-graph property checks, a small real P0 → B1 → M0 closure, and a bounded
SIFT1M validation. No parameter sweep, automatic tuning, best-run selection, or dataset larger than
SIFT1M is in scope. Any formal B0/B1 comparison MUST reuse the exact same validated Dataset and
Partition Artifact identities and hold query set, ground truth, distance metric, resource budget,
cache condition, timing boundary, repetition count, and aggregation rule fixed or explicitly record
the difference. XN execution in this Feature always follows the partitioned-and-merged path; the
paper's all-memory configuration remains parameter evidence, not a separate execution path.

### Paper-Stated Behavior and Adaptation Boundary

The following behavior is directly supported by Algorithm 1 and the surrounding paper text:

- `G[i]` and `R[i]` start empty, while `C[i]` starts from `omega` random points from `S`.
- `C[i]` retains at most `omega` closest intermediate neighbors; `G[i]` retains at most `lambda`
  diversified intermediate neighbors; `R[i]` contains reverse neighbors from the previous
  iteration.
- Each iteration evaluates `C[i] ∪ R[i]`, produces reverse relationships, applies cross-`TryInsert`
  operations between every encountered candidate `u` and existing `v` in `G[i]`, and tests the
  candidate with Equation (2).
- A candidate that is not occluded removes current `G[i]` members that it occludes and is then
  subject to capacity-bounded insertion into `G[i]`.
- `TryInsert(y, N, L)` performs set insertion and removes maximum-distance members until the size is
  at most `L`.
- The paper states that a small `omega`, typically 32–40, is sufficient; this is guidance rather
  than a universal validity bound.
- Section 3.3 states that Algorithm 1 requires its input dataset to be held in memory and introduces
  BBP for large-scale construction. Section 4.3 sets the maximum out-edges of all compared
  all-memory methods to 64, corresponding to `lambda = 64` for that experiment.

The following behaviors are necessary faithful adaptations because the paper does not uniquely
specify them:

- mapping `S` to one current P0 Partition and preserving its LocalToGlobalIdMap;
- excluding self and duplicate nodes during random initialization and defining undersized-Partition
  behavior;
- choosing and recording a pseudo-random generator, seed default, and per-node seed derivation;
- choosing candidate iteration order and distance-tie ordering;
- resolving concurrent writes to `R`, `C[u]`, and `C[v]` without a data race or hidden result drift;
- defining convergence, distinguishing it from an iteration safety cap, and reporting the actual
  termination reason;
- selecting a deterministic local entry point for the existing local graph representation; and
- binding every successful local build to the complete Partition collection and supplying an
  explicit, independently validated `merge.R` to the reusable M0.

Clarify fixes the observable policies recorded below. Deferred to Plan are the concrete PRNG and
seed default/derivation, candidate and distance-tie ordering, implementation of deterministic
conflict outcomes, normalized-state comparison mechanism, deterministic local-entry-point
selection mechanism, and other non-observable implementation choices. Plan MUST freeze one choice
for each of these remaining adaptations before implementation. All selected choices MUST be
recorded in the parameter contract, Algorithm/source mapping or deviation record, and BuildManifest
as applicable; none may be described as paper-prescribed behavior.

## Clarifications

### Session 2026-09-04

- Q: What semantics do the LocalGraphArtifact degree fields have? → A:
  `structural.declared_max_degree` equals the configured `lambda` exactly;
  `structural.observed_max_degree` equals the actual maximum out-degree in the graph payload; the
  observed value cannot exceed the declared value; and the native local-graph header's
  `maximum_observed_degree` equals both the payload's actual maximum and
  `structural.observed_max_degree`.
- Q: What observable semantics govern a parallel iteration? → A: Each round derives its candidate
  sequences from the `C` and `R` state visible at the round boundary. Reverse relationships and
  cross-`C` updates produced during that round become visible in the following round. Within one
  node's deterministic candidate sequence, earlier changes to its working `G[i]` are visible to
  later candidates in the same round. Conflicting logical updates have one deterministic outcome,
  and thread count or scheduling cannot change the graph under otherwise identical declared
  conditions.
- Q: What exact condition means that XN-Descent has converged? → A: Convergence is reached only
  when one complete deterministic round leaves every node's normalized logical `C`, `R`, and `G`
  state unchanged from the round boundary at which it began. A stable `G` alone or a below-threshold
  change count is insufficient.
- Q: What is the result policy when the iteration cap is reached before convergence? → A: The run
  ends unsuccessfully with an explicit non-converged termination status and publishes no valid
  LocalGraphArtifact for M0, formal evaluation, cache reuse, comparison, or aggregation. Its
  BuildManifest retains the cap, completed-round count, termination reason, and final fixed-point
  check evidence. Any retained diagnostic data remains quarantined and cannot be represented as a
  valid Artifact.
- Q: Does exact replay have to produce the same graph across macOS and Linux? → A: No. Exact replay
  is required within the same declared numerical execution environment, including platform,
  architecture, compiler and flags, distance behavior, implementation identity, inputs,
  parameters, and seed. Valid thread-count or scheduling differences remain result-invariant.
  macOS and Linux runs must each be internally replayable, but cross-platform graph equality is not
  guaranteed and any observed difference must be recorded.

### Session 2026-09-09

- Q: How should convergence handle a deterministic reverse-neighborhood `R` two-cycle after `C`
  and `G` have stabilized? → A: This clarification supersedes only the 2026-09-04 answer that
  required an adjacent-round full `C/R/G` fixed point as the sole success condition. The full-state
  fixed point remains the immediate and explicitly reported condition. In addition, three
  consecutive complete rounds with both normalized `C` and normalized `G` unchanged qualify as a
  proved two-phase closure: with fixed `C`, `R_(t+1)=reverse(C union R_t)` can have at most a
  two-cycle, so those three stable `C/G` transitions cover both candidate phases. Any intervening
  `C` or `G` change resets the count. Stable `G` alone is still insufficient. Algorithm 1 says only
  `until Converged` and does not uniquely define this rule, so the additional condition is a
  necessary faithful adaptation based on the observed deterministic `R` two-cycle, not a
  paper-stated or approximate-threshold rule.

### Session 2026-09-10

- Q: Is the fixed Baseline's B0 local degree 10 a generic input-degree limit of M0? → A: No. This
  clarification supersedes all Feature 007 statements that required `lambda<=10`, restricted
  `lambda=64` to a local-only path, or prohibited a paper-parameter example from entering Merge.
  The pinned `merge_shards` operation remains the single reusable M0 algorithm and takes its output
  maximum degree from explicit positive `merge.R`. XN `lambda` independently declares each B1
  LocalGraph's maximum degree; valid B1 outputs may therefore declare or observe degrees above 10.
  Feature 002's `R=16` and B0 local degree 10 remain frozen values of the Baseline Profile and its
  runner, not universal M0 capability limits. XN runs in this Feature use only the complete
  partitioned path P0 → B1 × Partition count → M0 → GlobalGraph.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Build an Algorithm-1 Local Graph (Priority: P1)

As a paper-reproduction engineer, I need XN-Descent to transform one validated Partition into a
valid local extended-neighborhood graph, so I can inspect and test the paper's core construction
mechanism independently of BBP and IMF Search.

**Why this priority**: Correct Algorithm 1 behavior is the minimum useful result and the foundation
for every framework integration or experiment.

**Independent Test**: Run B1 on a deterministic small Partition whose vectors and seed are known;
verify the iteration trace, Equation (2) decisions, capacity bounds, and final local adjacency
against an independently prepared expected result.

**Acceptance Scenarios**:

1. **Given** a validated Partition with legal vectors and parameters, **When** B1 executes, **Then**
   it evolves `C`, `R`, and `G` according to Algorithm 1 and emits one structurally valid local
   graph whose declared maximum degree equals `lambda`, whose observed maximum degree equals the
   payload's actual maximum out-degree, and whose observed maximum does not exceed `lambda`.
2. **Given** a candidate that is occluded by an existing `G[i]` member, **When** the candidate is
   evaluated, **Then** it does not enter `G[i]`, while all paper-required cross-`TryInsert` effects
   from its encounters still occur.
3. **Given** a candidate that is not occluded, **When** it is accepted, **Then** members of `G[i]`
   occluded by that candidate are removed before the accepted candidate is capacity-bounded into
   `G[i]`.
4. **Given** a one-node or otherwise undersized Partition, **When** initialization runs, **Then**
   each `C[i]` contains all available distinct non-self candidates without duplication or self
   padding, and a legal graph is still produced.

---

### User Story 2 - Compose B1 Safely with Existing P0 and M0 (Priority: P2)

As an experiment operator, I need B1 to satisfy the existing Local Build contract and be selected
without changing P0, B0, or M0's merge algorithm, so I can perform a controlled Local Build
replacement and obtain one complete GlobalGraph.

**Why this priority**: The repository's value comes from a traceable stage-level comparison, which
requires compatibility to be established before expensive work starts.

**Independent Test**: Starting from one validated small Dataset and P0 PartitionArtifact, run one
P0 → B1 × Partition count → M0 → GlobalGraph closure with `lambda > 10` and an independently
specified positive `merge.R`; confirm all local handoffs validate, the complete local collection
enters the existing M0, and the fixed P0/B0 identities, M0 algorithm identity, frozen Baseline
runner, and upstream tree remain unchanged.

**Acceptance Scenarios**:

1. **Given** a compatible validated Partition collection and any legal `lambda`, including a value
   above 10, **When** B1 is selected, **Then** it emits exactly one validated LocalGraphArtifact for
   every Partition and existing M0 consumes the complete collection with explicit positive
   `merge.R`.
2. **Given** `lambda` and `merge.R`, **When** compatibility preflight runs, **Then** each parameter
   is validated independently; neither is derived from the other and a legal `lambda > 10` is not
   rejected solely because it exceeds the frozen Baseline's B0 local degree.
3. **Given** B0 and B1 are both available, **When** either implementation is selected, **Then** they
   coexist under distinct identities and the frozen Baseline path continues to select B0.
4. **Given** any invalid input or B1 output, **When** a downstream action is requested, **Then** the
   validator gate prevents M0, evaluation, reuse, comparison, and aggregation from starting.

---

### User Story 3 - Reproduce a Declared B1 Run (Priority: P3)

As a research maintainer, I need every result-affecting adaptation and resource condition to be
declared and retained, so another operator can replay the same B1 run and distinguish normal
convergence from bounded failure.

**Why this priority**: The paper leaves random, concurrent, and convergence details open; without
declared choices, an apparently successful graph cannot support a scientific claim.

**Independent Test**: Execute the same small input twice with identical implementation identity,
parameters, seed, distance behavior, toolchain, platform, architecture, and declared numerical
execution conditions; verify identical accepted output and termination evidence, including when
only valid thread count or scheduling differs. Treat a macOS/Linux comparison as two independently
replayable environments rather than an exact cross-platform replay, and record any graph
difference. Then change one result-affecting input and verify that the BuildManifest identity
changes accordingly.

**Acceptance Scenarios**:

1. **Given** the same validated inputs, implementation identity, parameters, seed, distance
   behavior, compiler and flags, platform, architecture, and numerical execution conditions,
   **When** B1 is repeated under different valid thread counts or schedules, **Then** its output and
   termination evidence are identical.
2. **Given** an iteration cap reached before either declared convergence condition, **When** B1 stops,
   **Then** the run records an unsuccessful non-converged terminal reason, retains the cap,
   completed-round count, and final convergence-check evidence, and publishes no valid or reusable
   LocalGraphArtifact for any formal downstream action.
3. **Given** a completed round whose normalized logical `C`, `R`, and `G` states all equal their
   states at the start of that round, **When** termination is evaluated, **Then** B1 reports exact
   fixed-point convergence immediately. **Given** three consecutive complete rounds whose
   normalized `C` and `G` states each remain unchanged while `R` alternates, **Then** B1 reports
   `three_round_c_g_stability` with `fixed_point=false`; unchanged `G` with changed `C` is not
   convergence.
4. **Given** a mandatory memory requirement known to exceed the declared budget, **When** admission
   runs, **Then** B1 fails before its major graph allocation and first iteration.
5. **Given** an allocation or resource failure during execution, **When** B1 terminates, **Then** it
   retains truthful failure evidence and publishes no valid or reusable partial LocalGraphArtifact.

---

### User Story 4 - Validate the Partitioned Merge Without Overstating Paper Coverage (Priority: P4)

As a reviewer, I need the complete partitioned XN build and reusable M0 result to remain correctly
classified, so a useful XN-Descent component study is not mistaken for the paper's complete
disk-based system.

**Why this priority**: The current P0/M0 composition intentionally differs from BBP and the paper's
search stack; truthful classification is required before interpreting any result.

**Independent Test**: Run one complete P0 → B1 × Partition count → M0 → GlobalGraph request with
`lambda > 10` and explicit `merge.R`; verify every LocalGraph validates before M0, the complete
collection is merged, and the result retains component-level faithful-adaptation claim boundaries.

**Acceptance Scenarios**:

1. **Given** a `semantic-small` profile, **When** it is reported, **Then** only algorithm-semantic and
   small-graph evidence is claimed.
2. **Given** a `pipeline-compatible` profile, **When** it enters P0 → B1 → M0, **Then** it enforces
   independent legal values for `lambda` and `merge.R` and is identified as a component adaptation
   using the reusable M0 rather than as the frozen Baseline Profile.
3. **Given** a paper-reported `lambda = 64` example, **When** the XN pipeline validates it, **Then**
   it follows the same partitioned P0 → B1 × Partition count → M0 → GlobalGraph path and does
   not use a local-only or full-memory execution lifecycle.
4. **Given** that BBP, Secondary Index, IMF Search, and SSD layout are absent, **When** results are
   summarized, **Then** no complete-system or paper end-to-end SSD performance claim is made.

### Edge Cases

- The selected Partition contains one node, fewer than `omega + 1` nodes, or `lambda` exceeds the
  number of available non-self nodes.
- Random sampling would otherwise select the center node, repeat a node, or depend on thread
  scheduling.
- `C[i] ∪ R[i]` contains duplicates, contains `i`, or a reverse list is much larger than `omega`.
- The same candidate is encountered from multiple local neighborhoods during one iteration.
- A candidate and an existing `G[i]` member have equal center distance or make either Equation (2)
  comparison exactly equal.
- The supplied vectors produce a non-finite distance, have an inconsistent dimension, or cannot be
  read through the declared vector service.
- `alpha` is non-finite or below 1; `omega`, `lambda`, or the iteration cap is zero.
- `alpha` is finite but a naive squared-distance expression would overflow or lose the strict
  inequality semantics.
- The declared convergence condition is never met before the iteration cap.
- An execution is cancelled or runs out of memory after temporary state exists but before a valid
  Artifact can be published.
- The input Artifact has an unsupported version, checksum mismatch, incomplete map, wrong Dataset
  lineage, unknown PartitionId, or incompatible capability.
- A LocalGraph claims a degree different from its payload, contains a self edge or duplicate, uses
  an out-of-range LocalNodeId, or has an illegal entry point.
- `lambda` and `merge.R` differ substantially, including `lambda > 10`, and an implementation
  mistakenly derives one from the other or rejects the composition using the frozen B0 degree.
- `merge.R` is zero, missing, non-integral, or otherwise invalid.
- One or more expected Partition-local graphs are absent, duplicated, partial, or invalid when M0
  admission is attempted.
- A partitioned P0/B1/M0 run is presented as if it used BBP or the paper's complete disk-search
  stack.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The Feature MUST preserve the fixed read-only DiskANN Baseline at repository
  `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
  `78256bbab4685e1774e78d331e081a153be26823`, and MUST NOT modify its source tree or gitlink.
- **FR-002**: The work MUST remain a component-level `single_paper_reproduction` with
  `reproduction_mode: faithful_adaptation` and `replacement_scope: local_build`; it MUST NOT be
  reported as exact reproduction or as reproduction of the complete XN-Graph system.
- **FR-003**: B1 MUST have a stable identity distinct from P0, B0, and M0 and MUST coexist with B0
  without overwriting, modifying, unregistering, or redefining any Baseline implementation.
- **FR-004**: B1 MUST consume the selected validated Partition and its corresponding Dataset vectors
  and MUST produce the existing `LocalGraphArtifact/v1` in LocalNodeId space with a complete,
  unchanged LocalToGlobalIdMap and valid local entry point.
- **FR-005**: For every local node `i`, B1 MUST maintain the logical states `C[i]`, `R[i]`, and
  `G[i]` with the paper-defined roles: closest intermediate candidates, previous-iteration reverse
  neighbors, and diversified extended neighbors returned as the final graph, respectively.
- **FR-006**: B1 initialization MUST begin with empty `G` and `R`. Its seeded `C[i]` initialization
  MUST contain at most `omega` distinct non-self local nodes and MUST use every available distinct
  non-self node when fewer than `omega` exist.
- **FR-007**: Each iteration MUST use a deterministic synchronous round boundary. The candidate
  sequence for node `i` MUST be derived from the `C[i]` and prior-round `R[i]` state visible at the
  start of that round. Reverse relationships and cross-`C` updates produced during the round MUST
  become visible in the following round, not alter any current-round candidate sequence. Within
  one node's deterministic candidate sequence, earlier changes to its working `G[i]` MUST be
  visible to later candidates in that same round.
- **FR-008**: For every evaluated candidate `u` and encountered `v` in `G[i]` where `u != v`, B1
  MUST preserve the equivalent effects of `TryInsert(u, C[v], omega)` and
  `TryInsert(v, C[u], omega)`, including when `u` is ultimately occluded. Concurrent logical
  updates MUST produce one deterministic next-round result without dropping a required insertion
  effect; the concrete conflict-resolution mechanism is deferred to Plan.
- **FR-009**: A candidate occluded by any relevant `G[i]` member MUST NOT be inserted into `G[i]`.
  A non-occluded candidate MUST remove current `G[i]` members it occludes before the candidate is
  subjected to the `lambda` capacity bound.
- **FR-010**: Every `TryInsert(y, N, L)` effect MUST be set insertion followed, when necessary, by
  removal of members farthest from the node that owns neighborhood `N` until `N` contains at most
  `L` members; duplicate insertion MUST NOT consume capacity.
- **FR-011**: B1 MUST implement Equation (2) with both strict comparisons. Candidate `u` is occluded
  by existing member `v` only when `dist(i,v) < dist(i,u)` and
  `alpha * dist(v,u) < dist(i,u)` are both true; equality in either comparison MUST NOT be treated
  as satisfying that comparison.
- **FR-012**: When the admitted distance service returns squared L2, B1 MUST preserve mathematical
  equivalence to Equation (2) by evaluating `d2(i,v) < d2(i,u)` and
  `alpha^2 * d2(v,u) < d2(i,u)`. It MUST NOT directly multiply squared distance by `alpha`, and
  numerical handling MUST preserve the strict predicate for valid finite inputs.
- **FR-013**: B1 MUST reject non-finite `alpha` and `alpha < 1`, zero `omega`, zero `lambda`, and a
  zero iteration cap before construction begins. Vector dimension or non-finite distance failures
  MUST terminate in a controlled, truthfully recorded failure.
- **FR-014**: `C[i]` MUST never exceed `omega`; `G[i]` and final local out-degree MUST never exceed
  `lambda`; final adjacency MUST contain no self edge, duplicate neighbor, or out-of-range
  LocalNodeId. Every accepted output MUST set `structural.declared_max_degree` exactly to the
  configured `lambda` and `structural.observed_max_degree` exactly to the actual maximum out-degree
  in the graph payload; the observed value MUST NOT exceed the declared value. The native
  local-graph header's `maximum_observed_degree` MUST equal both the payload's actual maximum and
  `structural.observed_max_degree`.
- **FR-015**: The XN-Descent algorithm identity MUST support explicit `lambda`; it MUST NOT encode 10
  as an inherent algorithm limit. The paper's typical `omega = 32–40` and all-memory
  `lambda = 64` setting are parameter guidance or examples, not universal bounds or a separate
  execution profile.
- **FR-016**: Every XN pipeline request MUST provide explicit positive `lambda` for B1 and explicit
  positive `merge.R` for M0. The two parameters MUST be validated independently and MUST NOT be
  derived from one another. The reusable M0 MUST accept structurally valid LocalGraphArtifact/v1
  inputs whose declared or observed degree exceeds 10 and MUST continue to call the pinned
  `merge_shards` algorithm with the requested `merge.R`.
- **FR-017**: The Feature MUST distinguish `semantic-small`, `pipeline-compatible`, and
  `paper-style` profiles. `semantic-small` remains limited to core, oracle, and contract
  validation. Both full-pipeline profiles use one B1 per Partition and the reusable M0.
  Neither profile adds a local-only or all-memory runner path.
- **FR-018**: B1 MUST report convergence immediately when one complete deterministic round leaves
  every node's normalized logical `C`, `R`, and `G` state unchanged, with
  `convergence_condition=full_state_fixed_point` and `fixed_point=true`. It MUST also report
  convergence when three consecutive complete rounds leave every node's normalized `C` and `G`
  unchanged, with `convergence_condition=three_round_c_g_stability`, `fixed_point=false`, and
  `stable_c_g_rounds>=3`; any `C` or `G` change MUST reset that count. Stability of `G` alone and
  any non-zero approximate threshold MUST NOT qualify. These declared conditions MUST remain
  distinct from the iteration safety cap; reaching the cap without either condition MUST record a
  non-converged termination reason and
  MUST NOT be reported as successful or converged. Such a run MUST publish no valid
  LocalGraphArtifact for M0, formal evaluation, comparison, cache reuse, or aggregation. Its
  BuildManifest MUST retain the cap, completed-round count, termination reason, and final
  fixed-point check evidence. Any retained diagnostic data MUST remain quarantined and MUST NOT be
  represented as a valid Artifact.
- **FR-019**: Plan MUST select exactly one formal policy for the pseudo-random generator, seed
  default and derivation, candidate ordering, distance ties, and local entry point before
  implementation begins. It MUST realize the synchronous visibility, deterministic
  conflict-outcome, and exact fixed-point semantics fixed by FR-007, FR-008, and FR-018 without
  changing them.
- **FR-020**: Every result-affecting policy selected under FR-019 MUST be deterministic under its
  declared execution conditions, free of undeclared data races, and recorded as a faithful
  adaptation rather than paper-stated behavior.
- **FR-021**: A replay with identical validated inputs, implementation identity, parameters, seed,
  distance behavior, compiler and flags, platform, architecture, and declared numerical execution
  conditions MUST produce identical accepted graph and termination evidence. Thread scheduling and
  any valid thread count within the declared budget MUST NOT change that result. macOS and Linux
  runs MUST each satisfy this guarantee within their own declared environment, but exact graph
  equality between those environments is not required; any observed cross-platform difference
  MUST be recorded rather than silently classified as an exact replay.
- **FR-022**: B1 MUST accept and obey positive declared memory and thread budgets. Any mandatory
  memory requirement that can be determined before major allocation MUST be checked with
  overflow-safe arithmetic and rejected before the first iteration when it exceeds the budget.
- **FR-023**: B1 MUST NOT silently cap `R` or another paper-unbounded logical state to satisfy a
  resource budget. Cancellation, allocation failure, or budget exhaustion MUST produce controlled
  failure evidence, MUST stay within the declared thread budget, and MUST publish no valid partial
  LocalGraphArtifact.
- **FR-024**: B1 MUST declare `requires`, `provides`, accepted Artifact versions, input/output ID
  spaces, capabilities, required runtime services, and its parameter contract before registration
  or execution.
- **FR-025**: Every input MUST pass its corresponding validator before B1 starts, and every B1
  output MUST pass the existing `LocalGraphArtifact/v1` validator before downstream execution,
  evaluation, comparison, cache reuse, or aggregation.
- **FR-026**: Artifact, BuildManifest, validation evidence, and EvaluationReport MUST remain separate.
  Graph data and ID mapping belong to the Artifact; implementation, parameters, source, toolchain,
  resources, termination, outputs, and failures belong to generation evidence; quality and
  performance findings belong to evaluation evidence.
- **FR-027**: Build provenance MUST record at least `omega`, `lambda`, `alpha`, seed, selected
  convergence policy, iteration cap, actual termination reason, thread and memory budgets, distance
  behavior, implementation identity, reproduction mode, validated inputs, source state, compiler,
  build flags, platform, architecture, declared numerical execution environment, output identity,
  and failure reason when applicable. A cap-limited attempt MUST also record its completed-round
  count and final fixed-point check evidence.
- **FR-028**: The Feature MUST maintain an Algorithm/source mapping and deviation record that
  separately identify paper-supported behavior, current source/contract facts, faithful adaptation
  decisions, and any remaining unsupported claim.
- **FR-029**: A formal B0/B1 comparison MUST use the exact same validated Dataset and Partition
  Artifact identities. Separately rebuilt inputs with equal-looking configuration MUST NOT qualify
  as same-input evidence.
- **FR-030**: Validation MUST proceed through applicable unit, contract, deterministic small-fixture,
  small-graph property, and integration checks before SIFT1M. SIFT1M evidence and human diff review
  are required before the implementation is described as fully validated; datasets larger than
  SIFT1M require separate explicit approval.
- **FR-031**: The Feature MUST NOT perform parameter sweep, automatic tuning, changed-configuration
  retry, best-result selection, or a dataset larger than SIFT1M. Any future repeated experiment
  MUST predeclare and retain its configurations, repetition count, exclusions, stopping rule, and
  aggregation method.
- **FR-032**: Results produced without BBP, Secondary Index, IMF Search, or the paper's SSD layout
  MUST NOT claim the paper's complete system, its end-to-end SSD performance, or causal performance
  improvement beyond the evidence directly measured by this Feature.

### Key Entities *(include if feature involves data)*

- **B1 XN-Descent Implementation**: The independently selectable Local Build implementation with a
  stable identity, parameter contract, declared capabilities, and faithful-adaptation status.
- **XN State**: Per-local-node logical neighborhoods `C`, `R`, and `G`, with capacities, iteration
  visibility, and convergence behavior determined by paper rules plus declared adaptations.
- **XN Parameters**: `omega`, `lambda`, `alpha`, seed, convergence policy, iteration cap, distance
  behavior, and declared execution/resource conditions that can affect the graph or termination.
- **PartitionArtifact**: The validated aggregate P0 output that owns Partition membership and every
  complete LocalToGlobalIdMap consumed by B1.
- **LocalGraphArtifact**: The immutable B1 output for one owning Partition, containing its unchanged
  LocalToGlobalIdMap and native local adjacency in LocalNodeId space. Its declared maximum degree
  records the configured `lambda`, while its observed maximum degree and the native graph header
  record the payload's actual maximum out-degree.
- **BuildManifest**: Complete evidence of how one B1 Artifact was attempted or produced, including
  inputs, policies, resources, environment, termination, output, and failure.
- **EvaluationReport**: Separate structural, graph-quality, search-quality, performance, and
  comparison findings for one explicitly classified study.
- **Validation Profile**: `semantic-small` for core/oracle/contract checks; `pipeline-compatible`
  or `paper-style` for the same complete partitioned P0/B1/M0 execution and claim boundary.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of accepted B1 LocalGraph outputs pass the existing local graph validator, match
  their owning Partition node count and complete map, use only valid LocalNodeIds, contain no self
  or duplicate edges, have a legal entry point, declare maximum degree exactly equal to `lambda`,
  report observed maximum degree exactly equal to the payload's actual maximum and native header,
  and have observed maximum degree at most the declared maximum.
- **SC-002**: Deterministic semantic fixtures exercise accepted, occluded, reverse-neighbor,
  cross-`TryInsert`, removal, capacity, duplicate, self, equal-distance, and undersized-Partition
  cases with all expected outcomes passing.
- **SC-003**: 100% of squared-L2 Equation (2) conformance cases use the `alpha^2` predicate, retain
  both strict inequalities, and reject the known incorrect `alpha * squared_distance` behavior.
- **SC-004**: Repeating a validated semantic-small run with identical declared inputs and execution
  conditions produces identical graph and termination evidence in every required repetition;
  varying only the valid thread count or schedule also produces that identical result. Every run
  reported as converged has evidence of either an adjacent-round full normalized `C/R/G` fixed
  point or three consecutive complete rounds with normalized `C/G` unchanged across both possible
  `R` phases. No run is reported as converged merely because `G` is stable or a change count is
  below a non-zero threshold. macOS and Linux each meet this criterion within
  their own declared numerical environment; equality between their graphs is not required, and any
  observed difference is recorded.
- **SC-005**: 100% of valid P0 → B1 × Partition count → M0 → GlobalGraph requests, including
  requests with `lambda > 10`, validate `lambda` and positive explicit `merge.R` independently,
  validate every B1 output before M0, and pass the complete Partition-local graph collection to
  the reusable M0. Invalid `merge.R` or an invalid/partial collection starts no M0 execution.
- **SC-006**: 100% of invalid parameter, input, capability, ID-space, checksum, lineage, known
  pre-allocation budget, cancellation, runtime resource-failure, and cap-limited non-convergence
  cases publish no valid Artifact and cannot enter downstream execution or aggregation.
- **SC-007**: Every attempted B1 run retains a separate BuildManifest with complete result-affecting
  parameters and termination status; every accepted Artifact and EvaluationReport remains a
  separate object with independently verifiable references.
- **SC-008**: Unit, contract, small-fixture, small-graph, integration, SIFT1M, and human-review gates
  are reported separately; missing SIFT1M or human review prevents a "fully validated" claim, and
  no dataset larger than SIFT1M runs without separately recorded approval.
- **SC-009**: 100% of reports label P0 → B1 → M0 as a component-level faithful adaptation and make
  no claim that BBP, Secondary Index, IMF Search, SSD layout, or complete paper-system performance
  was reproduced.

## Assumptions

- The fixed P0 PartitionArtifact provides complete, validated, ascending LocalToGlobalIdMaps and
  the existing Dataset vector service can resolve their GlobalNodeIds.
- The admitted distance metric for the first B1 Feature is L2, and the current distance service
  returns squared L2; support for additional metrics is outside this Feature unless later approved.
- LocalGraphArtifact/v1 and the reviewed native local-graph payload can represent B1 output without
  a new Artifact version or ID-space Adapter.
- The pinned `merge_shards` implementation can serve as reusable M0 with explicit positive
  `merge.R`. The fixed Baseline Profile and runner continue to use global `R=16` and B0 local
  degree 10; those values do not constrain XN's `lambda`.
- The paper does not provide enough information to uniquely determine PRNG, seed derivation,
  candidate tie order, race-free parallel semantics, convergence, iteration cap, or local entry
  point. This Clarify session fixes their required observable semantics where applicable; Plan
  freezes the remaining implementation choices as faithful adaptations before implementation.
- The paper's all-memory parameter examples are provenance for parameter selection only. This
  Feature produces project results through the partitioned P0/B1/M0 path and does not implement an
  all-memory or local-only execution route.
- No performance advantage is assumed. Recall, hops, latency, QPS, construction time, and resource
  findings require separate measured EvaluationReports under a predeclared protocol.
