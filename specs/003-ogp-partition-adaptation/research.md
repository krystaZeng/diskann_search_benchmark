# Research: OGP Partition Adaptation

This document records design conclusions. Exact symbol and line facts are maintained in
[source-mapping.md](source-mapping.md). No source was modified and no GP-ANN or SIFT1M execution was
performed during planning.

## Evidence status

The official repository is `https://github.com/larsgottesbueren/gp-ann.git`, branch `main`. A
read-only remote audit identified commit `df5efb4a6b303011c795666ed267f29d994b558c` and recursive
gitlinks for ParlayLib, KaMinPar, hnswlib and message-queue. That commit is the selected GP-ANN
SourceIdentity for this Feature.

The local candidate at
`/Users/krystal/Desktop/入职材料/Mirage 复现/内存实验代码/OGP/gp-ann` is not an independent
Git checkout: it has no `.git`, its `git -C` lookup resolves to an unrelated parent repository, and
its submodule directories are empty. Audited files match the selected official commit except that
candidate `partition.cpp` contains an extra diagnostic `WRITE clusters.size()` print absent from
official history. The candidate is therefore research-only and cannot be a formal build or run
source.

Evidence labels used below:

- **official_source_verified**: checked against the selected official commit or its gitlinks;
- **candidate_snapshot_verified**: checked only in the unauthenticated local snapshot;
- **implementation_probe_required**: must be established by a clean pinned configure/build or a
  small real run before implementation can be accepted;
- **not_observable**: the selected official CLI provides no direct observation surface.

## Decision 1: Reuse the existing PipelineExecutor

**Decision**: Register OGP as one additional `PartitionStrategy` and select OGP, existing cpp_main
B0 and existing cpp_main M0 in the current `PipelineExecutionRequest`. Do not create another stage
loop.

**Rationale**: The executor already performs Dataset validation, exact Registry resolution,
requires/provides preflight and parameter schema checks before factory creation. It publishes a
Partition only after Validator and B0 handoff checks, sorts explicit PartitionIds, runs B0 once per
ID, validates each LocalGraph before M0, then invokes M0 once and evaluates the validated global
graph. Invalid Partition output returns before B0 with invocation count zero.

**Alternatives considered**:

- A paper-specific executor was rejected because it duplicates already-correct composition and
  would change B0/M0 execution semantics.
- Invoking B0/M0 from the thin runner was rejected because orchestration belongs to the executor.

## Decision 2: Preserve the existing B0 native-map contract

**Decision**: OGP emits one cpp_main native uint32 LocalToGlobal map for every contiguous
PartitionId. Each file is little-endian `uint32 row_count`, `uint32 dim=1`, followed by exactly
`row_count` uint32 GlobalNodeIds in ascending order, with no trailing bytes.

**Rationale**: B0 fresh-validates the aggregate PartitionArtifact, resolves the explicit
PartitionId, requires the reviewed native map media type, checks range/uniqueness and additionally
requires ascending Dataset order. `retrieve_shard_data_from_ids` performs a monotonic scan, so the
ascending rule is an actual native precondition, not presentation-only canonicalization.

**Alternatives considered**:

- JSON maps were rejected because the existing cpp_main B0 accepts only its reviewed native media
  type.
- Changing B0 to tolerate unsorted OGP output was rejected because it changes an existing Baseline
  binding and adds an OGP-specific branch.

## Decision 3: Existing M0 supports overlap

**Decision**: Pass the ordinary aggregate PartitionArtifact and complete LocalGraphArtifact set to
unchanged M0.

**Rationale**: M0 requires one LocalGraph per Partition and exact map lineage but does not require a
P0 producer identity or cross-map uniqueness. The pinned `merge_shards` path intentionally builds a
GlobalNodeId-to-shards relation, converts local neighbors for every replica, combines candidates,
deduplicates, shuffles and truncates. The existing M0 test already succeeds with fully overlapping
maps. Cross-shard duplicate membership is therefore a supported input, not an Adapter exception.

**Alternatives considered**:

- An OGP-specific merge branch was rejected because overlap is already a native M0 semantic.
- A pre-merge deduplication of memberships was rejected because it would destroy OGP overlap.

## Decision 4: Split common and OGP-specific validation

**Decision**: Before creating a candidate Artifact, OGP validates raw cluster output in memory in
this order: parse, non-empty, range, same-shard duplicate detection, global coverage, then ascending
canonicalization. It writes maps once. The existing common Validator remains the only publication
grant.

**Rationale**: The common Validator already verifies metadata/schema, payload length/SHA-256,
aggregate checksum, Dataset lineage, Partition count/contiguous IDs, nonzero local counts, native
map header/length/range/same-map uniqueness and payload ordering. It does not verify native-map
ascending order, union coverage, requested/effective values or worker provenance. B0's ascending
check is too late for the requirement that invalid OGP leave B0 invocation count at zero.

**Alternatives considered**:

- Extending the generic Validator with OGP semantics was rejected as a paper-specific redesign.
- Sorting and deduplicating together was rejected because it would hide invalid official output.
- Writing maps and then reimplementing a second full validator was rejected; the stage checks only
  pre-publication OGP invariants and delegates the formal Artifact grant to the existing Validator.

## Decision 5: Keep the fixed Baseline runner isolated

**Decision**: Add a non-installed OGP experiment runner. Do not change
`cpp_main_baseline_runner`.

**Rationale**: The Baseline runner admits `baseline_validation`, the fixed DiskANN commit, and
exact P0/B0/M0 identities, then registers only those bindings. That is the correct immutability
gate. The OGP runner instead registers P0/B0/M0 plus the OGP Partition factory in a Production
Registry, selects OGP+B0+M0 and calls the existing executor once.

**Alternatives considered**:

- Relaxing the Baseline runner identity check was rejected because it would let a paper method
  masquerade as the fixed Baseline.
- Registering OGP globally in the installed production assembly was rejected because only the
  Feature runner needs it.

## Decision 6: Add one typed, optional stage-source set

**Decision**: Add `std::optional<StageSourceIdentities>` to `PipelineExecutionRequest`, containing
required Partition, Local Build and Merge SourceIdentity values when present. An internal
`source_for(stage)` chooses the stage value; absence falls back to the current `request.source`.

**Rationale**: Audit found no equivalent mechanism. The request has one SourceIdentity and
`base_manifest()` currently copies it into every stage manifest. `source_config_json` is only
untyped parameter provenance. Each BuildManifest already stores one SourceIdentity, so choosing the
correct one per stage needs no BuildManifest schema or codec change.

**Alternatives considered**:

- Encoding sources in `source_config_json` was rejected because it would not bind the manifest's
  authoritative SourceIdentity.
- A map, Registry, plugin or partial source list was rejected as unnecessary generic machinery.
- Making stage sources mandatory was rejected because it would break existing callers.

## Decision 7: Use the official CLI subprocess only

**Decision**: Invoke the pinned official `Partition` executable using argv execution without a
shell. The formal method token is explicitly `OGP`.

**Rationale**: The official CMake project defines `Partition`, links it to Parlay and
`kaminpar_shm`, and that entry calls the official OGP callable. The CLI boundary keeps official
C++20 types and dependencies private, provides a direct binary checksum and permits exact argv,
stdout, stderr and exit status retention. The callable exists, but the project exposes no library
target; assembling one would require selecting and relinking official sources ourselves.

**Alternatives considered**:

- Direct callable was rejected for this Feature because it increases code and build provenance
  without improving fidelity.
- A generic callable/subprocess backend hierarchy was rejected because only one formal path is
  allowed.
- Invoking `GP` with nonzero overlap and relying on the CLI's implicit rewrite to `OGP` was rejected
  as misleading; the argv says `OGP`.

## Decision 8: Reflect the exact CLI parameter surface

**Decision**: The parameter schema contains only:

- `requested_num_clusters`: integer, at least one; semantic admission also requires it not exceed
  Dataset node count;
- `requested_overlap`: finite number, at least zero, with overflow-safe `overlap*N` arithmetic;
- `partitioning_mode`: `default` or `strong`.

There is no epsilon field. Evidence records `effective_epsilon=0.05` and
`effective_epsilon_source=upstream_cli_constant`.

**Rationale**: The CLI parses cluster count with `stoi`, mode as exactly `default|strong`, overlap
with `stod`, and fixes epsilon in source. Official source does not declare an overlap upper bound;
the adaptation must not invent one. Negative overlap is unsafe because conversion to `size_t` is
pathological, while non-finite/overflowing arithmetic is not replayable.

**Alternatives considered**:

- A configurable epsilon was rejected because the selected CLI cannot consume it.
- `epsilon: const 0.05` was considered but omitted to avoid advertising a nonexistent control.
- An invented `overlap <= 1` rule was rejected because the source has no such contract.

## Decision 9: Treat requested, effective and actual values separately

**Decision**: Preserve the requested cluster count and overlap exactly as input. Derive from the
official output:

- effective Partition count = number of non-empty output cluster records after validating that no
  empty record exists;
- total assignments = sum of raw cluster sizes;
- actual extra assignments = total assignments minus Dataset node count;
- assignment amplification = total assignments divided by Dataset node count;
- shard size projection and cross-shard overlap count.

**Rationale**: Official OGP recomputes an internal cluster count from capacity and requested extra
assignments. Its overlap loop may stop when no positive-affinity move remains, while the distance
fallback is compiled out. Requested overlap can therefore exceed actual extra assignments without
being an upstream failure.

**Alternatives considered**:

- Treating argv cluster count as output count was rejected because source recalculates it.
- Claiming the overlap budget was fulfilled was rejected because the official loop can retain a
  nonzero remainder.

## Decision 10: Stage an exact-byte `.fbin` view

**Decision**: Resolve the already validated DiskANN float32 Dataset payload and create a confined
stage-temporary `.fbin` hard-link when supported, otherwise an exact byte copy. Verify staged length
and SHA-256 equal the admitted Dataset before invoking the CLI.

**Rationale**: The DiskANN Dataset and GP-ANN float input have the same audited two-uint32 header and
float32 payload layout, but official `ReadPoints` dispatches solely on `.fbin`, `.u8bin` or `.i8bin`
suffix. Passing the current `.bin` path fails even though bytes are compatible.

**Alternatives considered**:

- Renaming or modifying the Dataset was rejected.
- Re-encoding vectors was rejected because it could change bytes/order and is not needed.
- Changing official suffix dispatch was rejected because official source is read-only.

## Decision 11: Record configured threads without a worker state model

**Decision**: Set `PARLAY_NUM_THREADS` in the child environment before spawning the official CLI,
using the requested StageContext thread budget. Record that configured value in existing
provenance; do not add observed-worker fields or a worker-completeness lifecycle.

**Rationale**: Pinned Parlay commit `ea671f6fbae17e6d6cd25fe9b48134d3c0601787` reads
`PARLAY_NUM_THREADS` on first scheduler initialization. OGP passes
`min(32, parlay::num_workers())` to `PartitionAdjListGraph`, which constructs
`kaminpar::KaMinPar(num_threads, context)`. The exact value passed is source-derived from the pinned
runtime contract and configured environment; it is not a direct worker observation. The CLI emits
neither Parlay worker count nor active KaMinPar worker count.

The Feature therefore records only the requested budget, configured environment value and the
pinned-source fact that OGP computes its KaMinPar argument from `parlay::num_workers()`. It does not
label a source-derived or configured value as observed. The absence of an observation is stated in
documentation and creates neither a new Artifact validity rule nor a new run status. No StageResult
metrics field is added.

**Alternatives considered**:

- Labeling the configured budget as observed was rejected as false provenance.
- Adding instrumentation to GP-ANN/KaMinPar was rejected because official control flow is fixed.
- Extending StageResult with runtime metrics was rejected as out of scope.

## Decision 12: Defer Partition structural reporting

**Decision**: Add no Feature-local structural report in the MVP. Preserve requested parameters in
existing provenance and the effective Partition/shard sizes in the existing validated
`PartitionArtifact/v1`. Total assignments and amplification remain derivable from those values if
a later evaluation Feature needs them.

**Rationale**: Current EvaluationReport is a closed GlobalGraph model and ValidationEvidence is the
common Artifact grant. Adding a one-consumer report now would expand the evidence lifecycle before
the real OGP→B0→M0 path is proven. The existing Artifact plus provenance is sufficient for the
Feature's structural correctness claims; quality comparison can define a dedicated evaluator only
when it has a concrete consumer.

**Alternatives considered**:

- Reusing GlobalGraph EvaluationReport fields was rejected because it would misstate the evaluated
  Artifact.
- Expanding the public evaluator or adding a local report now was deferred until a real comparison
  requirement exists.
- Replacing ValidationEvidence was rejected because it remains the contract-validity authority.

## Decision 13: Build only from a clean recursive official checkout

**Decision**: Formal source is GP-ANN commit
`df5efb4a6b303011c795666ed267f29d994b558c`, branch `main`, with exact gitlinks:

| Dependency | Commit |
|---|---|
| ParlayLib | `ea671f6fbae17e6d6cd25fe9b48134d3c0601787` |
| KaMinPar | `a9e4b1f6d92b4110d4d4a31ee2e7dee5a0761b05` |
| hnswlib | `359b2ba87358224963986f709e593d799064ace6` |
| message-queue | `f9b0856dde02248cc91a63077f7b6d5267004f25` |

Configure and build only target `Partition` out of tree with `MIPS_DISTANCE=OFF`. Record C++20,
KaMinPar 64-bit weight/edge settings, timers setting, Release flags, actual compiler/generator,
TBB/transitive dependencies and optional tcmalloc selection.

**Rationale**: The current snapshot cannot build because its submodules are empty. The official
README additionally suggests manually removing an hnswlib metrics block; doing so would dirty a
pinned checkout. The first probe must therefore try the clean pinned tree. If that exact build
fails because of the suggested edit, source/build admission fails closed and requests an explicit
reviewed policy instead of silently patching official dependencies.

**Alternatives considered**:

- Building the candidate snapshot was rejected because it lacks identity and differs from official
  source.
- Applying the README edit automatically was rejected because it violates clean-source admission.
- Vendoring algorithm sources into this repository was rejected.

## Decision 14: Freeze one structural SIFT1M configuration

**Decision**: After source/build admission and all small gates, freeze one SIFT1M config selecting
OGP+B0+M0. Use one source-backed OGP candidate (`requested_num_clusters=40`,
`requested_overlap=0.2`, `partitioning_mode=default`) from the official experiment script and the
unchanged Feature 002 B0/M0 profile. Retain every attempt; permit a new-run-ID retry only for a
recorded infrastructure or external execution failure with the frozen configuration unchanged.

**Rationale**: The official experiment surface uses 40 shards, default mode and overlap values
including 0.2. Selecting one predeclared value avoids a parameter sweep while actually exercising
overlap. The config must also bind the exact GP-ANN/DiskANN commits, dependency identities, Dataset
checksum, child environment, thread budget and output policy before execution.

**Alternatives considered**:

- Picking a cluster count to match P0 was rejected as an unsupported equivalence goal.
- Running both official overlap values or changing any field on retry was rejected as a sweep.
- Hiding an infrastructure failure or reusing its run ID was rejected as unauditable.
- Query evaluation was rejected because the Feature ends at structural graph closure.

## Remaining implementation probes, not design ambiguities

- Obtain the clean recursive official checkout locally and verify remote/branch/HEAD/dirty state.
- Run a clean out-of-tree `Partition` configure/build; record the actual compiler, CMake,
  generator, TBB and tcmalloc identities, or fail closed on the README hnswlib issue.
- Confirm the built binary checksum and exact CLI behavior on a small float32 fixture.
- Because direct Parlay/KaMinPar active-worker observations are unavailable, record only configured
  values and do not invent an observation or completeness status.

These probes gate implementation/formal execution. They do not authorize a second backend,
algorithm rewrite, public evaluator redesign or parameter guess.
