# Data Model: XN-Descent Local Build

**Feature**: `007-xn-descent-local-build`

**Model version**: `xn-descent-state/v1`

This is the logical design model. Concrete C++ containers are private implementation choices. All
node identifiers below are scoped to exactly one validated Partition unless explicitly named
GlobalNodeId.

## 1. XNParameters

| Field | Type | Validation/invariant |
|---|---|---|
| `validation_profile` | enum | `semantic-small`, `pipeline-compatible`, or `paper-style` |
| `omega` | positive uint32 | Capacity of each `C[i]`; effective maximum is `min(omega,n-1)` |
| `lambda` | positive uint32 | Capacity of each `G[i]` and declared LocalGraph maximum degree |
| `alpha` | finite double | `alpha >= 1`; squared-L2 predicate uses `alpha^2` |
| `seed` | uint64 | Explicit effective base seed; authoring default is 42 |
| `iteration_cap` | positive uint32 | Maximum number of complete rounds; not a convergence condition |
| `prng_policy` | stable ID | `mt19937_64-splitmix64-per-node-v1` |
| `sampling_policy` | stable ID | `unbiased-rejection-unique-nonself-v1` |
| `tie_break_policy` | stable ID | `squared-distance-then-local-id-v1` |
| `round_model` | stable ID | `deterministic-synchronous-round-v1` |
| `convergence_rule` | stable ID | `normalized-c-r-g-fixed-point-v1` |
| `entry_point_rule` | stable ID | `local-centroid-nearest-v1` |
| `distance_implementation` | stable ID | `squared-l2/v1` backed by the admitted L2 service |
| `reproduction_mode` | enum | exactly `faithful_adaptation` |

Profile invariants:

- `pipeline-compatible` requires `lambda <= 10` and exact current P0/B1/M0 composition.
- `paper-style` requires `lambda = 64` and no Merge stage.
- `semantic-small` is isolated from formal M0 and paper-scale claims.
- Profile rules never replace the core validity of positive `lambda`.

Thread and memory budgets are not duplicated in this value object. They come from `StageContext`
and are recorded in the BuildManifest `resource_budget` together with requested/effective worker
evidence.

## 2. PartitionBinding

| Field | Type | Invariant |
|---|---|---|
| `partition_artifact` | validated ArtifactRef | `partition-artifact/v1`, Local ID space, complete map capability |
| `partition_id` | PartitionId | Selects exactly one record and one map payload |
| `local_node_count` | NodeCount `n` | Positive and equal to the selected map row count |
| `local_to_global` | ordered vector of GlobalNodeId | Length `n`; item `i` is the vector-service key for LocalNodeId `i` |
| `source_partition_checksum` | SHA-256 identity | Equals the owning aggregate Partition Artifact checksum |
| `dataset` | DatasetIdentity | Equals validated Dataset identity and vector-service binding |

The selected map order is immutable. A local graph copies its exact bytes/checksum rather than
reconstructing or sorting it.

## 3. NeighborRecord

```text
NeighborRecord {
  id: LocalNodeId
  squared_distance_to_owner: finite non-negative double
}
```

For owner `i`:

- `id < n` and `id != i`;
- there is at most one record for an `id` in a logical set;
- `squared_distance_to_owner == d2(i,id)` under the admitted distance service;
- canonical order is ascending `(squared_distance_to_owner, id)`;
- distance is owner-relative. A record cannot be reused under another owner without recomputation.

## 4. PerNodeState

### 4.1 CState

`C[i]` is a canonical vector of at most `omega` NeighborRecords. It retains the closest records
under the total order. Initialization contains unique non-self random neighbors; subsequent rounds
retain prior membership plus all logical cross-`TryInsert` proposals reduced to the same bound.

### 4.2 RState

`R[i]` is a canonical ascending vector of unique non-self LocalNodeIds. It contains all reverse
neighbors proposed by the preceding round. It has no algorithmic capacity bound; resource failure
must not be converted into truncation.

### 4.3 GState

`G[i]` is a canonical vector of at most `lambda` NeighborRecords. It is the cross-round accumulated
diversified neighborhood and the source of final outgoing edges. Candidate processing may remove
occluded members and then `TryInsert` the candidate. All entries are unique, in range, and non-self.

## 5. RoundSnapshot

```text
RoundSnapshot {
  round_index: uint32
  C: immutable array<CState>[n]
  R: immutable array<RState>[n]
  G: immutable normalized array<GState>[n]
  canonical_state_digest: SHA-256
}
```

The snapshot is the only source for current-round candidate membership. `G` is copied into
node-owned working state; no worker mutates another node's snapshot.

## 6. CandidateSequence

For owner `i`, the sequence is the unique union of snapshot `C[i]` and `R[i]`, excluding `i`, with
fresh or validated owner-relative distance. It is sorted by `(d2(i,u), u)`. Its traversal order is
part of the deterministic adaptation.

Every traversed candidate emits a reverse proposal before the acceptance outcome. Candidate
membership in `G[i]` does not suppress this behavior.

## 7. Proposals

### 7.1 CProposal

```text
CProposal {
  target_owner: LocalNodeId
  proposed_neighbor: LocalNodeId
  squared_distance_to_target: finite non-negative double
  cause: cross_try_insert
}
```

For every encountered `v != u` in working `G[i]`, Algorithm 1 logically emits both:

- target `v`, neighbor `u`, distance `d2(v,u)`;
- target `u`, neighbor `v`, distance `d2(u,v)`.

The target center determines the distance and `TryInsert` order. Proposals are still emitted after
the candidate's local occlusion flag becomes false. The deterministic reducer computes exactly
the closest `omega` of prior `C[target]` plus every proposal; buffering all proposals is not a
logical requirement.

### 7.2 RProposal

```text
RProposal {
  target_owner: LocalNodeId = candidate_u
  proposed_reverse_neighbor: LocalNodeId = source_i
}
```

All proposals are de-duplicated per target only after logical emission. There is no capacity limit.
They become `R` at the next round boundary.

## 8. WorkingG

Each owner has exactly one logical `WorkingG[i]`, initialized from snapshot `G[i]` and processed by
its canonical candidate sequence.

For candidate `u`:

1. initialize local `not_occluded = true`;
2. inspect every current `v` in `WorkingG[i]` where `v != u`;
3. emit both cross-`C` proposals;
4. if both strict Equation (2) predicates hold, set `not_occluded = false` but continue the scan;
5. if still true after the complete scan, remove every current `v` occluded by `u`;
6. apply `TryInsert(u, WorkingG[i], lambda)`.

Changes at steps 5–6 are visible to later candidates for the same `i`. They are not visible to
another owner's candidate sequence. Capacity eviction does not roll back earlier removals.

## 9. NormalizedRoundState

```text
NormalizedRoundState {
  round_index: uint32
  C: canonical array<CState>[n]
  R: canonical array<RState>[n]
  G: canonical array<GState>[n]
  state_digest: SHA-256
  c_digest: SHA-256
  r_digest: SHA-256
  g_digest: SHA-256
}
```

It is constructed only after all current-round owners and proposal reductions complete. It is
committed atomically as the next snapshot. Canonical encoding includes a version marker, node
count, node boundaries, IDs, and the IEEE-754 bits of cached `C/G` distances in a declared byte
order. Digests are replay evidence; direct normalized state equality determines convergence.

## 10. ConvergenceEvidence

| Field | Meaning |
|---|---|
| `round_index` | The complete round that was compared |
| `before_state_digest` / `after_state_digest` | Canonical boundary evidence |
| `changed_c_nodes`, `changed_r_nodes`, `changed_g_nodes` | Exact node-level change counts |
| `changed_c_edges`, `changed_r_edges`, `changed_g_edges` | Exact membership delta counts |
| `direct_equality_checked` | Always true for a valid fixed-point decision |
| `fixed_point` | True only when every normalized node state is equal |

`fixed_point=true` requires all state change counts to be zero. Digest equality alone is
insufficient. A stable `G` with changed `C` or `R` is not convergence. The replay projection covers
these canonical algorithm fields and excludes run IDs, timestamps, absolute paths, and other
expected-to-vary lifecycle metadata.

## 11. TerminationResult

```text
TerminationResult {
  kind: converged | invalid_input | cancelled | resource_exhausted |
        numeric_failure | iteration_cap_nonconverged | internal_failure
  stage_status: SUCCEEDED | FAILED | CANCELLED | INVALID_OUTPUT
  completed_rounds: uint32
  iteration_cap: uint32
  convergence_evidence: ConvergenceEvidence?
  failure_reason: string?
  graph: normalized G?       # present only for converged core execution
}
```

State transitions:

```text
unvalidated
  -> validated/preflighted
  -> initialized
  -> round snapshot
  -> working G + proposals
  -> normalized next state
       -> converged -> output candidate -> validated -> published
       -> next round (below cap)
       -> iteration_cap_nonconverged -> FAILED, no candidate
       -> resource/numeric/cancelled failure -> no candidate
```

Only `converged` may progress to output creation. A candidate rejected by producer checks or the
Artifact validator becomes `INVALID_OUTPUT`/`FAILED` according to the existing lifecycle and is
never published or admitted downstream.

## 12. LocalGraphOutputMetadata

The output reuses LocalGraphArtifact/v1 and contains no XN-private schema fields.

| Existing field/claim | B1 value/invariant |
|---|---|
| `id_space` | `local` |
| `partition_id` | Exact selected PartitionId |
| `local_node_count` | `n` |
| `source_partition_checksum` | Exact owning Partition aggregate checksum |
| `declared_max_degree` | Exactly configured `lambda` |
| `observed_max_degree` | Actual maximum row length in serialized `G` |
| native header maximum | Equals actual maximum and `observed_max_degree` |
| native local entry point | Deterministic local medoid and `< n` |
| `local_to_global_map` payload | Exact selected P0 map bytes/checksum |
| `local_graph` payload | `G` rows in LocalNodeId space; no self/duplicate/out-of-range ID |
| capabilities | Exactly descriptor-provided `graph.local_adjacency/v1` |

The Artifact answers only what immutable graph data was produced. XN policies, PRNG, convergence,
resources, implementation/source/toolchain/input identities, and termination live in the separate
BuildManifest. Structural, graph/search quality, and performance findings live in separate
EvaluationReports.

## 13. Global invariants

1. No state or output contains an out-of-range LocalNodeId, duplicate neighbor, or self neighbor.
2. `C[i].size <= omega`; `G[i].size <= lambda`; `R[i]` is not silently capped.
3. Equation (2) uses two strict comparisons and scale-safe `alpha^2` under squared L2.
4. Cross-`TryInsert` effects use the proposal target as distance center and occur even for an
   eventually occluded candidate.
5. Current-round cross-`C` and reverse-`R` changes become visible only in the next round;
   node-owned working-`G` changes are visible later in that node's current candidate sequence.
6. Every complete state transition is independent of valid worker count and schedule.
7. Fixed point means complete normalized `C`, `R`, and `G` equality.
8. Failure, including cap-limited non-convergence, produces no valid LocalGraphArtifact.
9. `pipeline-compatible` cannot begin P0 or B1 when `lambda>10`; `paper-style` cannot invoke M0.
10. macOS and Linux each require internal exact replay evidence; their graphs need not be equal.
