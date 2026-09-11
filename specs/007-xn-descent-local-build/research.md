# Phase 0 Research: XN-Descent Local Build

**Feature**: `007-xn-descent-local-build`

**Date**: 2026-09-07

**Study class**: `single_paper_reproduction`

**Reproduction mode**: `faithful_adaptation`

This document resolves the implementation choices left open by the paper and by the five accepted
clarifications in `spec.md`. The paper is authoritative for Algorithm 1, `TryInsert`, and Equation
(2). The current repository contracts and pinned source mapping are authoritative for P0, B0, M0,
Artifact, Manifest, validation, and pipeline behavior.

## R1. Parameter contract and validation profiles

**Decision**

The effective B1 parameter document uses `xn-descent-parameters/v1` and records:

- `validation_profile`: `semantic-small`, `pipeline-compatible`, or `paper-style`;
- `omega`, `lambda`, `alpha`, `seed`, and `iteration_cap`;
- fixed policy identities for PRNG/seed derivation, sampling, candidate/tie ordering, synchronous
  rounds, fixed-point-or-three-round-`C/G` convergence, squared-L2 behavior, and local entry-point
  selection;
- `reproduction_mode: faithful_adaptation`.

`omega`, `lambda`, and `iteration_cap` are positive integers. `alpha` is finite and at least 1.
`seed` is an unsigned 64-bit integer. Runtime parsing repeats all semantic checks because the
current generic parameter validator checks object shape and primitive types but does not enforce
JSON Schema numeric bounds, `enum`, or conditionals. The runner performs composition preflight;
the builder repeats B1-local validation as defense in depth before allocating graph state.

The profile boundaries are:

| Profile | Intended use | Composition rule |
|---|---|---|
| `semantic-small` | unit, oracle, and hand-checkable small-graph validation | B1 core only; no claim about M0 or paper scale |
| `pipeline-compatible` | P0, one B1 per Partition, reusable M0, and GlobalGraph | positive `lambda` and positive `merge.R` are explicit and independent; every Partition-local output validates before M0 |
| `paper-style` | Paper-derived parameters such as `lambda=64` | Same complete partitioned pipeline with explicit `merge.R`; no local-only or all-memory lifecycle |

The relaxed parameter contract is revision `1.1.0`, independently identified by exact versioned
schema paths and contract `$id` values rather than by changing the normalized document format:

- M0 runtime schema:
  `contracts/baseline-v1/schemas/cpp-main-m0-parameters-v1.1.0.schema.json`;
- XN design source:
  `specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json`;
- XN runtime copy:
  `contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters-v1.1.0.schema.json`.

The M0 `$id` is
`https://diskann-repro.local/contracts/baseline-v1/cpp-main-m0-parameters-v1.1.0.schema.json`.
Both XN copies use
`https://diskann-methods.local/specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json`;
that shared `$id` is required by their byte identity. Both values are distinct from the respective
historical contract `$id`. The XN copies' real SHA-256/source/target relation is recorded in
`SOURCES.json`. Existing schema paths remain preserved as the pre-relaxation design baseline. The
M0 revision retains exactly `R` and `use_filters`; the XN revision retains
`schema_version: xn-descent-parameters/v1` and all existing fields/policies but removes the
`pipeline-compatible` `lambda` maximum 10 and `paper-style` `lambda` constant 64. The existing
M0 and B1 descriptor identities and implementation versions do not change; their exact
`parameter_schema_path` selects revision `1.1.0`. No request version or conversion path is added.

The default seed for authoring a request is decimal `42`, but the normalized effective parameter
document passed to B1 and retained in its BuildManifest always contains `seed` explicitly. `alpha`,
`omega`, `lambda`, and `iteration_cap` remain explicit; the paper's `omega = 32–40` and
`lambda = 64` observations are not universal algorithm bounds.

Termination evidence records completed rounds, the configured cap, per-state change counts and
digests for the final comparison, `fixed_point`, consecutive stable-`C/G` rounds, the selected
convergence condition, `converged`, and a stable termination reason.

**Rationale**

This makes every result-affecting policy reviewable without turning examples from the paper into
hidden defaults. Two validation layers are required because the current parameter-schema reader is
not a complete JSON Schema implementation. The execution design preserves the algorithm's general
`lambda` while independently exposing the existing merge output bound as `merge.R`.

**Alternatives considered**

- Hard-code `lambda = 10`: rejected because 10 is the frozen Baseline B0 local degree, not an
  Algorithm 1 or generic M0 limit.
- Treat `omega = 32–40` as a legal range: rejected because the paper presents it as practical
  guidance, not a validity condition.
- Rely only on schema keywords for numeric/profile validation: rejected because the current
  validator does not implement those keywords.
- Add a paper-style local-only path: rejected because the project goal is a complete partitioned
  graph and the reusable M0 can merge validated XN local graphs with explicit `merge.R`.
- Change the implementation ID/version or add descriptor-version routing: rejected because this is
  a compatible parameter-contract revision selected by the exact descriptor schema path and
  recorded project source identity, not a new algorithm or Artifact format.
- Overwrite the old schema paths and relabel old evidence: rejected because the constitution
  requires the revised contract and historical baseline to remain distinguishable.

**Relation to sources**

Algorithm 1 supplies `omega` and `lambda`; Equation (2) supplies `alpha >= 1`. Section 3.2 gives
typical `omega`, and the evaluation uses maximum degree 64. FR-013 and FR-015–FR-019 define the
repository-facing validation and profile semantics.

## R2. Deterministic random initialization

**Decision**

Initialization uses `std::mt19937_64`. Node `i` receives an independent seed produced by the
following versioned SplitMix64 derivation over unsigned 64-bit wraparound arithmetic:

```text
z = base_seed + 0x9E3779B97F4A7C15 * (uint64(i) + 1)
z = (z xor (z >> 30)) * 0xBF58476D1CE4E5B9
z = (z xor (z >> 27)) * 0x94D049BB133111EB
node_seed = z xor (z >> 31)
```

For a partition with `n` nodes, the effective initial capacity is
`min(omega, n - 1)`. If `n - 1 <= omega`, initialization takes every non-self LocalNodeId exactly
once and consumes no random draws. Otherwise, raw engine output is mapped to `[0, n - 2]` by a
specified unbiased rejection mapper (`threshold = (-bound) mod bound`; accept `x >= threshold`,
then use `x mod bound`), mapped around `i`, and accepted only if not already selected. The resulting
membership is normalized by the ordering in R3.

Each node consumes only its own engine stream. Initialization therefore does not depend on worker
assignment, thread count, or scheduling. This policy is required to replay within one declared
numerical/build environment. Cross-platform graph equality is not promised.

**Rationale**

An explicitly versioned generator, derivation, and bounded mapping eliminates implementation-
defined draw ordering and shared-RNG races. Taking all available non-self nodes handles undersized
partitions without duplicate sampling or self edges.

**Alternatives considered**

- One shared generator guarded by a lock: rejected because scheduling would assign draws to nodes.
- `std::uniform_int_distribution`: rejected because the distribution algorithm need not be stable
  across standard-library implementations.
- Shuffle all LocalNodeIds for every node: rejected because it requires unnecessary `O(n)`
  temporary memory per active node.
- Platform-independent graph equality: rejected by the fifth clarification.

**Relation to sources**

Algorithm 1 says only that `C[i]` is initialized with `omega` random points. PRNG, seed, sampling,
and undersized-partition behavior are faithful adaptations required by FR-006 and FR-019–FR-021.

## R3. Canonical state and ordering

**Decision**

A logical neighbor is `(LocalNodeId id, double squared_distance_to_owner)`. For fixed owner `i`,
the total order is ascending `(squared_distance_to_i, id)`. `C[i]` and `G[i]` are duplicate-free,
self-free vectors in that order. `R[i]` is a duplicate-free ascending LocalNodeId vector; its
distance is derived only when forming owner `i`'s candidate sequence.

At the start of a round, `C[i] union R[i]` is de-duplicated, self candidates are rejected, distances
to `i` are obtained once, and candidates are traversed in ascending
`(squared_distance_to_i, LocalNodeId)` order. `TryInsert(y, N, L)` performs set insertion and retains
the first `L` members under the target owner's total order. At equal distance, the lower
LocalNodeId is retained; the farthest removable member is therefore the largest `(distance, id)`.

Ordering and tie rules never participate in the Equation (2) truth value. Occlusion compares only
the two strict distance predicates; equality in either predicate remains non-occluding.

**Rationale**

A single owner-relative total order makes candidate traversal, capacity reduction, proposal
reduction, serialization, and fixed-point comparison deterministic. Treating distance as derived
owner-relative data prevents accidental use of the wrong center in cross-`TryInsert`.

**Alternatives considered**

- Preserve arrival order: rejected because parallel proposal arrival is schedule-dependent.
- Use LocalNodeId alone: rejected because `TryInsert` must keep the closest `L` nodes.
- Use non-strict Equation (2) at distance ties: rejected because it changes the paper algorithm.

**Relation to sources**

Section 3.2 and `TryInsert` require distance-ordered bounded neighborhoods. Tie handling and a
canonical representation are faithful adaptations fixed by FR-010, FR-011, and FR-019.

## R4. Deterministic synchronous round model

**Decision**

Round `t` begins with immutable snapshots `C_t` and `R_t`; each node's working graph begins as a
copy of `G_t[i]`. Candidate sequences are derived only from `C_t[i] union R_t[i]`. For candidate
`u`, reverse proposal `(target=u, neighbor=i)` is emitted before its acceptance decision. While
scanning every current `v` in working `G[i]` where `v != u`, both cross proposals are emitted:

- `(target=v, neighbor=u)`, equivalent to `TryInsert(u, C[v], omega)` with center `v`;
- `(target=u, neighbor=v)`, equivalent to `TryInsert(v, C[u], omega)` with center `u`.

The scan never stops merely because `u` has been found occluded. The flag is local to `(i,u)`.
Within node `i`, the working `G[i]` is mutated in candidate order, so earlier removals/insertions are
visible to later candidates in that same sequence. A candidate accepted after all comparisons
first removes every current member it occludes, then undergoes `TryInsert` into `G[i]`; removals are
not rolled back if capacity reduction immediately evicts that candidate.

After all nodes finish:

- `C_(t+1)[x]` is the closest `omega` under R3 from `C_t[x]` plus every cross proposal targeting
  `x`;
- `R_(t+1)[x]` is the unique ascending set of every reverse proposal targeting `x`;
- `G_(t+1)` is the collection of the completed per-node working graphs.

Target-owned bounded reducers may discard only proposals provably outside the top `omega` under
the total order; every proposal is logically submitted. Reverse proposals are never capacity
capped. The three next states become visible atomically at the next round boundary. Parallel work
uses only snapshot reads, node-owned working `G`, and deterministic reductions.

**Rationale**

This is a bulk-synchronous realization of the accepted clarification. It preserves Algorithm 1's
within-node `G` effects and all cross-`TryInsert` effects while removing data races from cross-node
`C` and `R` writes.

**Alternatives considered**

- In-place cross-node `C` mutation: rejected because scheduling changes candidates and results.
- Make all current-round `G` changes next-round-only: rejected because Algorithm 1's earlier
  `G[i]` changes are visible to later candidates for the same owner.
- Break on the first occluding `v`: rejected because later cross-`TryInsert` effects would be lost.
- Cap or sample reverse proposals: rejected because the paper does not bound `R`.

**Relation to sources**

The loop body follows Algorithm 1. The snapshot/commit boundary and deterministic reduction are the
second clarification and FR-007–FR-009's faithful adaptation.

## R5. Full-state fixed point, `R` two-cycle closure, and bounded termination

**Decision**

After a complete round, all `C`, `R`, and `G` lists are normalized using R3. Logical equality is
exact ordered membership equality for every node; cached distance values are derived integrity
data and must match the recomputation for their `(owner,id)`. The implementation records, for each
state class, changed-node and changed-edge counts plus a SHA-256 digest over a versioned canonical
encoding of node boundaries, LocalNodeIds, and IEEE-754 squared-distance bits for `C` and `G`.

The run converges immediately if `C_(t+1) == C_t`, `R_(t+1) == R_t`, and
`G_(t+1) == G_t` for every node after the full commit. This reports
`convergence_condition=full_state_fixed_point` and `fixed_point=true`.

The run also converges after three consecutive complete rounds in which both normalized `C` and
normalized `G` equal their respective round-start states. Any change to either resets the count.
This reports `convergence_condition=three_round_c_g_stability`, `fixed_point=false`, and
`stable_c_g_rounds>=3`. It does not require adjacent `R` equality: under fixed `C`, the recurrence
`R_(t+1)=reverse(C union R_t)` can form at most a two-cycle, and three stable `C/G` transitions
exercise both candidate phases before repeating. This is an exact finite-state closure, not a
change threshold; stable `G` without stable `C` remains insufficient.

The replay-compared termination evidence is this canonical algorithm evidence; run IDs,
timestamps, absolute paths, and other lifecycle metadata are intentionally not part of its
equality projection. Digest equality is evidence, not the equality algorithm; state arrays are
compared directly.

The cap is checked after each complete round. Either convergence condition on the last permitted
round succeeds. If neither condition holds, the stage returns existing `FAILED` status, emits no
output candidate, and its stage-owned BuildManifest records
`termination_reason: iteration_cap_nonconverged`, cap, completed rounds, final digests, and change
counts. No partially built graph is represented or published as an Artifact. By default no raw
partial graph diagnostic is retained; any future opt-in trace must remain in non-publication
temporary/quarantine storage and is outside formal results.

**Rationale**

This realizes the superseding 2026-09-09 clarification and the cap policy without extending the
public `StageStatus` contract. Direct comparison avoids treating a digest collision as
convergence. The three-round rule prevents a deterministic reverse-neighborhood two-cycle from
masking stable `C/G`, while retaining the stronger fixed-point label only for adjacent full-state
equality.

**Alternatives considered**

- Stop when `G` alone is stable: rejected because `C` can still change future candidates.
- Require adjacent `R` equality forever: rejected because a proved two-phase reverse-neighborhood
  closure can otherwise prevent termination after `C/G` stabilize.
- Use a below-threshold change ratio: rejected by clarification.
- Publish the last `G` with a warning: rejected because a cap-limited run is unsuccessful.
- Add a public `NON_CONVERGED` enum: rejected as unnecessary contract expansion; the stage-owned
  termination reason is precise.

**Relation to sources**

The paper leaves `Converged` undefined. The immediate full-state fixed point, three-round `C/G`
two-phase closure, and cap policy are faithful adaptations embodied in the superseding
clarification, FR-018, and SC-004/SC-006.

## R6. Overflow-safe squared-L2 Equation (2)

**Decision**

Only finite, non-negative squared-L2 distances are admitted. Candidate `u` is occluded by `v` only
when both of these strict predicates are true:

```text
d2(i,v) < d2(i,u)
alpha^2 * d2(v,u) < d2(i,u)
```

The first comparison is a direct strict `double` comparison. The second uses a versioned
scale-safe positive-product comparator: decompose the positive operands with `frexp`, form the
mantissa product in `long double`, normalize it, combine exponents in a wide signed integer, then
compare exponent and normalized mantissa against `d2(i,u)`. This evaluates the `alpha * alpha *
d2(v,u)` relationship without materializing an overflowing or underflowing product. Zero is handled
explicitly. Non-finite `alpha`, non-finite distance, negative distance, or an unrepresentable
numeric state fails the stage; it is never converted into an occlusion decision.

The comparator uses `<` at the final significand comparison. It neither substitutes `alpha` for
`alpha^2` nor changes equality to `<=`. Boundary/equality cases are mandatory tests. The numerical
environment and policy version enter the BuildManifest; exact cross-platform equality is not a
guarantee.

**Rationale**

Naively computing `alpha * alpha` in `double` can overflow even when the comparison has a definite
answer. Exponent-scaled comparison uses only C++17 standard facilities and needs no new numeric
dependency while preserving the required strict predicate in the implementation's declared
floating environment.

When an accepted candidate `u` is used to remove an existing member `v`, the same predicate is
applied in the opposite role direction: `d2(i,u) < d2(i,v)` and
`alpha^2 * d2(u,v) < d2(i,v)`. This is not the candidate-rejection predicate with labels left
unchanged.

**Alternatives considered**

- Compute `alpha * d2` on squared distances: rejected as mathematically wrong.
- Compute `alpha * alpha * d2` directly in `double`: rejected because intermediate overflow can
  force false decisions.
- Divide the right side twice by `alpha`: rejected because underflow at small right-side values can
  lose a representable strict comparison.
- Add a multiprecision dependency: rejected because exponent scaling is sufficient and smaller.

**Relation to sources**

Equation (2) is stated for ordinary distances. Squaring both positive sides gives the required
`alpha^2` form. FR-011–FR-013 freeze strictness, validity, and failure behavior.

## R7. Deterministic local entry point

**Decision**

The output entry point is the selected Partition's deterministic local medoid:

1. traverse vectors in ascending LocalNodeId order;
2. accumulate each coordinate in `long double` and divide by local node count;
3. convert the finite centroid to the float-vector input required by the admitted distance service;
4. choose the local vector minimizing squared L2 to that centroid;
5. break distance ties with the lower LocalNodeId.

A one-node partition yields LocalNodeId 0. An empty partition is rejected because no valid entry
point or Algorithm-1 node state can be produced. Centroid working memory is `O(dimension)` and is
included in budget checks. The exact rule and chosen ID are retained in the stage-owned Manifest.

**Rationale**

This is legal for LocalGraphArtifact/v1 and M0 and is conceptually aligned with B0's local medoid.
It is more meaningful than choosing LocalNodeId 0 and does not introduce a dependency on P0's
sampled k-means centroid payload, which is not defined as a local entry point and does not
necessarily represent the replicated Partition membership.

**Alternatives considered**

- Use the P0 centroid payload: rejected because it is optional in the public Partition contract,
  derived from sampled k-means pivots, and not an entry-point contract.
- Always use LocalNodeId 0: valid but rejected because it discards available deterministic
  centrality information.
- Reproduce B0's exact floating accumulator: rejected as an unnecessary bit-level coupling; this
  B1 rule is explicitly an adaptation and has its own replay environment.

**Relation to sources**

The paper does not specify an entry point. Pinned B0 selects a local point closest to the local-data
centroid, and M0 only requires a valid LocalNodeId that it can map to GlobalNodeId. This is a
faithful adaptation under FR-014 and FR-019.

## R8. Memory and thread budgets

**Decision**

All size calculations use checked unsigned addition/multiplication and fail on overflow. Let
`n` be local nodes, `d` dimension, `w = min(omega, n-1)`, `l = min(lambda, n-1)`, `p` effective
workers, and `r_t` the current reverse-edge count. The preflight lower bound accounts for:

- the selected Partition map and vector-access working set declared/owned by the stage;
- two bounded `C` generations: at most `2 * n * w` neighbor records;
- two `G` generations: at most `2 * n * l` neighbor records;
- offsets and metadata for current/next `R`;
- target-owned bounded cross-`C` reducers: at most `n * w` records plus synchronization metadata;
- per-worker candidate, working-`G`, distance, and serialization scratch;
- the `O(d)` entry-point centroid;
- the maximum native payload and copied LocalToGlobal map sizes.

No all-pairs distance cache is used. Stored neighbor records cache only their owner-relative
distance. Before the first major allocation, all known mandatory terms are checked against
`StageContext.memory_budget_bytes`. At each round boundary, exact candidate/reverse proposal counts
are obtained with checked arithmetic before allocating the next CSR-style `R`; if the unbounded
logical `R` cannot fit, the run fails rather than truncating it. All allocation failures and
additional runtime budget exhaustion are controlled failures with no output candidate.

Cross-`C` proposals are reduced online into a target-owned top-`w` set under R3. This bounds storage
without dropping any logical `TryInsert`: membership outside the top `w` can never affect the
final `TryInsert` result under a total order. Reverse `R` has no analogous cap.

Worker count is `min(thread_budget, n)` for non-empty partitions and never exceeds the positive
declared budget. Work partitioning uses `std::thread` with snapshot reads, node-owned `G`, and
deterministic reducers. One and many valid worker counts must produce identical results.

**Rationale**

This meets the resource contract without requiring continuous byte accounting for every container.
It preserves the paper's unbounded reverse relation and makes failure occur before downstream can
observe a partial Artifact. Standard C++ threads avoid a macOS-specific OpenMP requirement.

**Alternatives considered**

- Cap `R`: rejected because it silently changes Algorithm 1.
- Buffer every cross-`C` proposal: correct but rejected as avoidable peak memory.
- Use a global all-pairs distance cache: rejected as unnecessary `O(n^2)` memory.
- Ignore extra thread budget and remain permanently single-threaded: budget-safe but rejected
  because deterministic bounded parallelism is feasible within the chosen round model.

**Relation to sources**

Section 3.3 says the base Algorithm 1 construction is all-memory. FR-022–FR-023 add the engineering
safety contract; the synchronous visibility rule comes from clarification 2.

## R9. Framework integration and native output

**Decision**

B1 is an isolated paper module with stable identity
`xn_graph_xn_descent_local_build_v1`, version `1.0.0`. It implements the existing
`LocalGraphBuilder` and directly uses:

- `StageContext` memory/thread budgets, cancellation, and metrics;
- `VectorAccessor`, resolving each LocalNodeId through the selected complete LocalToGlobal map;
- `DistanceFunction` capability `distance_function/l2-v1`;
- existing Dataset and Partition validators before construction;
- existing LocalGraphArtifact/v1, `id_space: local`, complete copied map, validation evidence, and
  stage-owned BuildManifest.

Its descriptor requires the current Dataset and Partition formats, the native float32 Dataset and
complete LocalToGlobal map capabilities, and the listed runtime services. It provides only
`local-graph-artifact/v1`, LocalNodeId, and `graph.local_adjacency/v1`. It is registered only by a
Feature-local runner; `production_registry` remains unchanged.

B1 serializes the reviewed cpp_main native local-graph byte layout because current M0 requires that
exact media type. The B1 Manifest records both the project implementation source and the pinned
DiskANN format-source identity. A Feature-local encoding contract binds B1's descriptor, payload
role/media, and the pinned layout mapping; producer tests add the self-edge and duplicate checks
that the existing native structural reader does not perform. The payload header is actual maximum
out-degree, Artifact `observed_max_degree` is that same value, and
`declared_max_degree` is exactly `lambda`.

For `pipeline-compatible`, the Feature-local runner parses the complete request and checks the
exact P0/B1/M0 identities, positive `lambda`, and independently positive `merge.R` before invoking
the generic `PipelineExecutor`. It produces and validates one B1 LocalGraph for every Partition,
then supplies only the complete validated collection to the existing M0. `lambda > 10` is legal.
Invalid `merge.R`, incomplete Partition coverage, or invalid/partial LocalGraph output prevents M0.

Both `pipeline-compatible` and `paper-style` use that same complete runner lifecycle. The latter
may select `lambda=64` as paper-derived input, but 64 is not a profile constraint. Consequently,
`parameters.cpp` removes only its two old profile-degree restrictions while retaining positive
uint32, alpha, seed, policy, strict-field, and other core checks used by both direct Local Build and
canonical parameter normalization.

The shared `ParameterValidator` recursively checks object shape, required/additional properties,
primitive `type`, and `const`; it does not execute JSON Schema `minimum`, `maximum`, `enum`, or
conditional branches. Therefore the XN runner performs a Feature-local, pre-dispatch `merge.R`
parse before Registry/factory construction, expensive allocation, workspace/runtime-service
creation, or P0/B1/M0 invocation. It accepts only a signed-or-unsigned JSON integer in
`1..UINT32_MAX`, never converts through `double`, and reports a `merge.R`-specific reason for
missing, boolean, string, fractional, negative, zero, or oversized values. M0's T031 runtime range
check remains defense in depth. No public validator, dependency, or validation abstraction changes.

M0 remains the single reusable wrapper around the pinned `merge_shards` algorithm. Its parameter
schema and implementation accept explicit positive `merge.R`; generic M0 admission has no
`declared_max_degree <= 10` rule. Feature 002's frozen Baseline runner still supplies `R=16`, and
its B0 still uses local degree 10. Those frozen Profile values do not derive or constrain XN
`lambda`. No XN Merge, M1, fake Merge, local-only lifecycle, or all-memory execution path is added.

After the generic M0 descriptor points to revision `1.1.0`, the Baseline runner owns an independent
early guard in `src/baseline/cpp_main/cpp_main_baseline_runner.cpp`: a
`baseline_validation` request must contain integer merge `R=16` and `use_filters=false` before
workspace, vector accessor, Registry, or stage execution. The actual executable-entry regression
belongs in `tests/cpp/baseline/test_cpp_main_pipeline.cpp`; the existing `cpp_main_pipeline` target
receives `$<TARGET_FILE:cpp_main_baseline_runner>` through the minimal dependency/path wiring in
`tests/cpp/baseline/CMakeLists.txt` and invokes it with an otherwise valid `R=7` request. The
existing Python configuration test continues to verify profile normalization, but its mocked
runner invocation is not evidence that the C++ runner rejects drift.

The new revision-1.1.0 Feature-local parameter schema is copied byte-for-byte during T033 to its
versioned runtime path under `contracts/papers/xn-graph-xn-descent-v1/`, with the established
`SOURCES.json` provenance pattern. Existing schema files remain intact; no public Artifact or
BuildManifest schema is redefined.

**Rationale**

The public interfaces already carry every required input, service, budget, and output field.
Feature-local request validation and parameterized reusable M0 close the composition gap without
changing the public pipeline. Native serialization is required for M0 compatibility and can be
bounded by an explicit format-source contract instead of an Artifact version or ID Adapter.

**Alternatives considered**

- Add a generic pipeline relation for `lambda`: rejected because `lambda` belongs to B1, while M0
  needs its own explicit `merge.R` and validated local inputs.
- Keep M0 fixed to Baseline parameter values: rejected because those values define the frozen
  Baseline Profile rather than M0's reusable merge capability.
- Add a public or Feature-local local-only mode: rejected because XN execution must produce a
  complete partitioned GlobalGraph through the existing executor and reusable M0.
- Rely on the revised M0 schema to freeze Baseline `R=16`: rejected because reusable M0 accepts
  other positive values, so Baseline profile admission must remain independently explicit.
- Extend `ParameterValidator` with numeric-range support: rejected for this Feature because the
  narrow runner preflight plus M0 defense-in-depth check covers the required boundary without a
  public validation change.
- Use a fake or XN-specific Merge: rejected because the pinned `merge_shards` M0 already provides
  the required merge semantics when parameterized by `merge.R`.
- Change LocalGraphArtifact/v1: rejected because the existing contract can carry B1 output above
  degree 10 without a schema version change.
- Add an ID-space Adapter: rejected because B1 already operates in LocalNodeId.

**Relation to sources**

The decision follows current `LocalGraphBuilder`, `StageContext`, descriptor, validator, runner, and
M0 code. FR-024–FR-028 and the Baseline native-format/source mapping govern the integration.

## R10. macOS development and Linux migration

**Decision**

The implementation uses the repository's C++17 level, fixed-width integers, little-endian native
writer helpers, standard-library threading, and existing dependencies only. It contains no
macOS-only APIs. Development proceeds on macOS through parameter/Equation (2), `TryInsert`, oracle,
determinism, failure, contract, and small pipeline tests. Linux rebuilds the same source from
scratch and records a separate toolchain/numerical environment in every BuildManifest.

Within each platform/environment, exact replay requires identical inputs, parameters, seed,
implementation, compiler/flags, distance behavior, and numerical conditions; any valid worker
count or scheduling must leave the graph unchanged. macOS and Linux graph checksums are compared
and recorded, but equality is not a pass condition. Native cpp_main payloads used for formal M0
closure are the reviewed little-endian format; platform eligibility is checked explicitly.

SIFT1M is the largest permitted validation scale without new approval. Its Dataset checksum,
query/ground-truth identity when search quality is later evaluated, build flags, resources, and
complete parameters must be frozen before execution. No performance or complete-system claim is
derived from component construction.

**Rationale**

This matches the agreed development workflow and keeps replay claims within the numerical
environment that can actually be controlled. Standard C++17 and the existing dependency graph
minimize migration risk.

**Alternatives considered**

- Copy a macOS binary or Artifact to Linux as validation: rejected because Linux must have its own
  recorded build and replay.
- Require cross-platform edge-for-edge equality: rejected by clarification 5.
- Introduce platform-specific fast paths before semantic validation: rejected because they expand
  the replay surface without evidence of need.

**Relation to sources**

This is an engineering adaptation governed by FR-021 and the constitution's provenance,
validation-order, and scale gates.

## Resolution status

All specification-level and implementation-policy decisions required for Phase 0 are resolved.
Implementation details below the frozen observable policies—private container layout, work
chunking, and helper decomposition—may be chosen during task execution only if they preserve these
contracts.
