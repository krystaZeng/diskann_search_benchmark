# Quickstart Plan: XN-Descent Validation

This file plans the executable validation sequence for the later implementation phase. **None of
the commands below exists as a guaranteed current entry point, and none was run during Plan.** Every
command marked `[PLANNED]` must be created or confirmed by later tasks before execution.

The sequence is intentionally gated. A later gate cannot convert a failed or unrun earlier gate
into a pass.

## 1. Planned build entry

`[PLANNED]` Add one opt-in CMake switch and out-of-tree build:

```bash
cmake -S . -B build/xn-descent-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON
cmake --build build/xn-descent-debug --parallel
```

The later task must verify actual target names before retaining these commands. The build must not
write into `upstream/diskann-cpp-main/`.

## 2. Parameter and Equation (2) unit gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_(parameters|equation)_test$'
```

Required cases:

- reject non-finite or `<1` `alpha`, zero `omega`, zero `lambda`, and zero cap before construction;
- enforce all three profile constraints in runtime semantic validation, not schema text alone;
- squared L2 uses `alpha^2`, never `alpha`;
- both comparisons are strict and equality in either comparison is non-occluding;
- zero, ordinary, very large exponent, invalid/non-finite, and direct-overflow cases have explicit
  expected outcomes.

## 3. TryInsert unit gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_try_insert_test$'
```

Required cases:

- duplicate insertion is idempotent;
- top `L` uses distance to the actual target center;
- equal distance retains lower LocalNodeId;
- `L` capacity is never exceeded;
- the two cross calls use centers `v` and `u`, respectively;
- a self or out-of-range candidate cannot enter logical state.

## 4. Small-graph round oracle gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_round_oracle_test$'
```

The fixture records every normalized boundary `C/R/G` state. It must prove:

- initial `G/R` empty and seeded `C` unique/non-self;
- `n-1 < omega` uses every available non-self node without padding;
- candidates derive only from boundary `C/R`;
- every candidate emits a reverse relation before acceptance;
- all cross-`TryInsert` effects occur even after its flag becomes false;
- earlier working-`G[i]` mutations affect later candidates for `i`;
- candidate-caused removals are not rolled back if capacity later evicts the candidate;
- `C<=omega`, `G<=lambda`, and `R` is not silently capped;
- only complete normalized `C/R/G` equality is convergence.

## 5. Thread-count and scheduling determinism gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_determinism_test$'
```

For the same validated input, parameters, seed, build, and numerical environment, repeat with
thread budgets 1, 2, and the fixture's admitted maximum plus an injected alternative work schedule.
Require identical:

- native graph payload SHA-256;
- normalized final-state digest;
- completed rounds and fixed-point evidence;
- selected local entry point.

The test also verifies each node's random stream is independent of worker assignment.

## 6. Failure and resource gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_(termination|resource)_test$'
```

Required cases:

- fixed point on the last permitted round succeeds;
- changed `C` or `R` with stable `G` does not converge;
- cap hit with any changed state returns `FAILED`, reason
  `iteration_cap_nonconverged`, evidence, and no candidate Artifact;
- checked-size overflow and known budget excess fail before major graph allocation/first iteration;
- round-time reverse-state exhaustion fails without truncating `R`;
- allocation failure and cancellation are controlled and publish no valid or reusable Artifact;
- effective worker count never exceeds the positive thread budget.

## 7. Artifact, Manifest, and validator contract gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_local_artifact_test$'
```

The accepted candidate must prove:

- LocalGraphArtifact/v1 and LocalNodeId;
- exact selected Partition ownership and complete copied LocalToGlobal map/checksum;
- no out-of-range, duplicate, or self neighbors;
- `declared_max_degree=lambda`;
- observed metadata = native header = actual payload maximum `<=lambda`;
- legal deterministic local-medoid entry point;
- existing Validator passes before publication;
- BuildManifest separately records implementation/source/toolchain/input identities, effective
  policies/seed, budgets, convergence and termination;
- no Recall, QPS, latency, or other EvaluationReport fields are placed in Artifact/Manifest.

Negative candidates with corrupted bytes, checksum, map, lineage, degree, entry point, self edge,
or duplicate edge must never enter normal publication.

## 8. Early M0 compatibility gate

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_pipeline_preflight_test$'
```

Submit exact current P0/B1/M0 requests with `pipeline-compatible lambda=11` and with a
`paper-style` profile. Both must be rejected before Registry/factory execution; instrumented P0,
B1, and M0 invocation counts must all remain zero. A valid `lambda<=10` request proceeds to normal
generic preflight. M0's code and degree limit remain unchanged.

## 9. Small P0 → B1 → M0 closure

`[PLANNED]`

```bash
ctest --test-dir build/xn-descent-debug --output-on-failure \
  -R '^xn_descent_small_pipeline_test$'
```

The closure uses one immutable request, the exact current P0 and M0 identities, B1 identity, and a
small native Dataset. It must show ordered validated P0 → each explicit B1 Partition → M0, complete
local set, M0-admitted native media/map, valid final GlobalNodeIds, and separate stage manifests
and evidence. The report labels the run a component-level faithful adaptation, not full XN-Graph.

## 10. Isolated `lambda=64` paper-style validation

`[PLANNED]` The later task must add and document the exact Feature-runner target and request schema
before enabling this command:

```bash
build/xn-descent-debug/src/papers/xn_graph/xn_descent/xn_descent_experiment_runner \
  --request /absolute/path/to/reviewed-paper-style-request.json
```

The request selects `paper-style`, `lambda=64`, an already validated Dataset/Partition, and no
Merge. Success may publish only the validated LocalGraphArtifact plus separate Manifest/evidence;
it must not publish a GlobalGraph, invoke M0, or claim Baseline-compatible/full-system results.

## 11. SIFT1M validation gate

Before execution, a human must review:

- semantic and small-pipeline test results;
- the implementation diff against `source-mapping.md`;
- the exact SIFT1M Dataset path/checksum and Partition/P0 parameters;
- `omega`, `lambda`, `alpha`, seed, cap, profile, memory/thread budget;
- compiler/build flags, hardware, OS, and the intended result/aggregation policy.

`[PLANNED]` After that review, run the verified Feature runner with a predeclared request:

```bash
XN_SIFT1M_REQUEST=/absolute/path/to/reviewed-sift1m-request.json
build/xn-descent-release/src/papers/xn_graph/xn_descent/xn_descent_experiment_runner \
  --request "$XN_SIFT1M_REQUEST"
```

Run the profile-specific validation and report separately. Missing or failed SIFT1M validation
means implementation validation remains incomplete. After the run, a human reviews the
implementation diff, validation evidence, failures, and SIFT1M results before the Feature can be
reported as fully validated. No SIFT100M or larger run is authorized.

## 12. Linux rebuild and replay gate

On the target Linux host, check out the same reviewed project commit and pinned upstream gitlink;
rebuild from source with the actual confirmed CMake targets. Do not copy the macOS binary as a
substitute.

`[PLANNED]`

```bash
cmake -S . -B build/xn-descent-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON
cmake --build build/xn-descent-linux --parallel
ctest --test-dir build/xn-descent-linux --output-on-failure \
  -R '^xn_descent_'
```

Repeat an identical Linux request at least twice and vary only a valid thread count; require exact
Linux-internal graph and termination replay. Record Linux compiler/flags/architecture/numerical
environment independently. Compare macOS and Linux checksums for disclosure only: a difference is
recorded and does not fail the agreed replay guarantee.

## 13. Completion report template

The later implementation report must list separately:

- every executed command and result;
- failures, cause, and impact;
- applicable but unexecuted gates and why;
- macOS and Linux replay results independently;
- SIFT1M and human-review status;
- unchanged upstream commit/worktree status;
- explicit confirmation that BBP, Secondary Index, IMF Search, SSD behavior, and end-to-end paper
  performance were not reproduced.
