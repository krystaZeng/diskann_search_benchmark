# Tasks: XN-Descent Local Build

**Input**: Design documents in `specs/007-xn-descent-local-build/`
**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/`, `quickstart.md`, and `source-mapping.md`

**Tests**: Tests are mandatory because the specification requires algorithm-oracle, determinism, contract, failure, and pipeline evidence before any scale validation.

**Organization**: Tasks are grouped by user story. Within every story, tests are authored before implementation and the story ends with an executable validation task.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add only the opt-in, Feature-local build and contract-copy surface approved by the plan.

- [X] T001 [P] Copy `specs/007-xn-descent-local-build/contracts/xn-descent-parameters.schema.json` byte-for-byte to `contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters.schema.json` and record its source path plus SHA-256 in `contracts/papers/xn-graph-xn-descent-v1/SOURCES.json` without changing any public schema.
- [X] T002 [P] Add the default-OFF `DISKANN_XN_DESCENT` option and wire only the isolated module/tests through `CMakeLists.txt`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/papers/xn_graph/xn_descent/CMakeLists.txt`, and `tests/cpp/papers/xn_graph/CMakeLists.txt` using C++17 and the existing dependency set; core/semantic-small must build with `DISKANN_CPP_MAIN_BASELINE=OFF`, while pipeline-compatible requires `DISKANN_CPP_MAIN_BASELINE=ON`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish private interfaces and deterministic test support used by all stories.

**Critical**: No user-story implementation begins until this phase is complete.

- [X] T003 [P] Define the stable implementation identity, existing `LocalGraphBuilder` binding, profile/parameter model, termination model, and Feature-local execution request declarations in `src/papers/xn_graph/xn_descent/stage_binding.hpp` and `src/papers/xn_graph/xn_descent/parameters.hpp` without changing public pipeline interfaces.
- [X] T004 [P] Define private neighbor/tie keys, normalized C/R/G state, round snapshot/proposals, working-G state, convergence evidence, and algorithm result interfaces in `src/papers/xn_graph/xn_descent/xn_descent_algorithm.hpp` with the invariants from `data-model.md`.
- [X] T005 Create deterministic vector, PartitionArtifact, LocalToGlobal map, distance, workspace, allocation-failure, cancellation, scheduling, and stage-call-count fixtures in `tests/cpp/papers/xn_graph/xn_descent_test_support.hpp` for reuse by all XN-Descent tests.

**Checkpoint**: The Feature has isolated declarations and reusable fixtures, with no production Registry or public-contract change.

---

## Phase 3: User Story 1 — Build an Algorithm-1 Local Graph (Priority: P1) 🎯 MVP

**Goal**: Given one validated Partition and vectors, produce a structurally valid local XN graph with the paper's Algorithm 1 ordering and Equation (2) semantics.

**Independent Test**: Invoke B1 directly on semantic-small fixtures and verify seeded initialization, every normalized round boundary, cross-`TryInsert`, strict occlusion, fixed-point termination, deterministic medoid, and a validator-admitted LocalGraphArtifact/v1 without invoking P0 or M0.

### Tests for User Story 1

- [X] T006 [P] [US1] Add parameter-contract tests in `tests/cpp/papers/xn_graph/xn_descent_parameters_test.cpp` for all required fields, default seed 42, finite `alpha >= 1`, positive `omega`/`lambda`/cap/thread and memory budgets, semantic-small/pipeline-compatible/paper-style profile rules, and byte identity of the runtime schema copy.
- [X] T007 [P] [US1] Add Equation (2) and `TryInsert` tests in `tests/cpp/papers/xn_graph/xn_descent_equation_try_insert_test.cpp` covering both strict inequalities, equality as non-occluding, squared-L2 `alpha^2`, overflow-safe extreme values, actual target centers `v` and `u`, duplicate idempotence, `(distance, LocalNodeId)` ties, capacity, self, and range rejection.
- [X] T008 [P] [US1] Add a paper-trace micro-oracle in `tests/cpp/papers/xn_graph/xn_descent_round_oracle_test.cpp` that asserts seeded unique/non-self C initialization, `n-1 < omega`, boundary C/R candidates, reverse proposals, all cross-`TryInsert` effects even after occlusion, within-node working-G visibility, removal-before-insertion, uncapped R, C/G capacities, immediate complete normalized C/R/G fixed-point convergence, and the three-round stable-C/G R-two-cycle condition.
- [X] T009 [P] [US1] Add direct-builder output tests in `tests/cpp/papers/xn_graph/xn_descent_local_artifact_test.cpp` for LocalNodeId payloads, complete copied mapping/checksum, deterministic local medoid, no self/duplicate/out-of-range edges, `declared_max_degree=lambda`, and native header/observed degree equal to the actual payload maximum.
- [X] T010 [US1] Register `xn_descent_parameters_test`, `xn_descent_equation_test`, `xn_descent_try_insert_test`, `xn_descent_round_oracle_test`, and `xn_descent_local_artifact_test` with explicit labels and arguments in `tests/cpp/papers/xn_graph/CMakeLists.txt` so the quickstart regexes select real tests.

### Implementation for User Story 1

- [X] T011 [P] [US1] Implement JSON parsing, canonical parameter emission, default seed 42, semantic validation, reproduction-profile validation, and stable failure reasons in `src/papers/xn_graph/xn_descent/parameters.cpp`; leave thread count and memory budget as declared execution conditions rather than algorithm constants.
- [X] T012 [P] [US1] Implement SplitMix64 per-node seed derivation, `std::mt19937_64`, custom unbiased bounded mapping, unique non-self sampling, small-Partition behavior, neighbor distance keys, and canonical C/R/G normalization in `src/papers/xn_graph/xn_descent/xn_descent_algorithm.cpp`.
- [X] T013 [US1] Implement overflow-safe squared-L2 Equation (2) with `alpha^2` and strict `<`, plus center-correct top-L `TryInsert` and removal/insertion primitives in `src/papers/xn_graph/xn_descent/xn_descent_algorithm.cpp` without converting equality or tie-breaking into occlusion.
- [X] T014 [US1] Implement the deterministic synchronous Algorithm 1 round and convergence loop in `src/papers/xn_graph/xn_descent/xn_descent_algorithm.cpp`: boundary C/R feed candidates, reverse-R and cross-C proposals become next-round visible, every required cross insertion is emitted, per-node working G is immediately visible to later candidates, full-state fixed points terminate immediately, three consecutive stable-C/G rounds close a possible R two-cycle, and cap exhaustion remains distinct from convergence.
- [X] T015 [US1] Implement deterministic local-medoid selection and current native local graph/map/metadata candidate serialization in `src/papers/xn_graph/xn_descent/xn_descent_local_build.cpp`, preserving LocalGraphArtifact/v1 and setting declared, observed, and native-header degree fields to their specified values.
- [X] T016 [US1] Complete the B1 `LocalGraphBuilder` path in `src/papers/xn_graph/xn_descent/xn_descent_local_build.cpp`: validate Dataset/Partition/map inputs before access, use `StageContext` vector/distance/runtime services, enforce checked preallocation/thread budgets, create a separate BuildManifest, validate the candidate output, and return no publishable Artifact on any failure.

### Validation for User Story 1

- [X] T017 [US1] Configure and build the macOS core-only out-of-tree `build/xn-descent-core-debug` targets with `DISKANN_CPP_MAIN_BASELINE=OFF`, run the five US1 CTest names from `tests/cpp/papers/xn_graph/CMakeLists.txt`, and record every command/result in `specs/007-xn-descent-local-build/validation/us1-semantic-small.md`; do not proceed on a failed oracle, contract, or artifact test.

**Checkpoint**: US1 is independently usable as a direct, semantic-small Local Build and is the MVP boundary.

---

## Phase 4: User Story 2 — Compose B1 Safely with P0 and M0 (Priority: P2)

**Goal**: Preserve the already completed interim P0/B1/M0 integration checkpoint; its old degree/profile gate is historical evidence and is superseded by T030-T034.

**Independent Test**: The existing US2 evidence records an interim gate and is superseded by T030-T034.

### Tests for User Story 2

- [X] T018 [US2] Add pipeline preflight and small-closure tests in `tests/cpp/papers/xn_graph/xn_descent_pipeline_test.cpp` for descriptor `requires`/`provides`, coexistence with fixed P0/M0 identities, zero stage invocations on `lambda>10` or paper-style merge requests, valid `lambda<=10` P0 → B1 → M0 execution, per-candidate validation, and downstream rejection of invalid/partial local output.
- [X] T019 [US2] Register separate `xn_descent_pipeline_preflight_test` and `xn_descent_small_pipeline_test` invocations from `tests/cpp/papers/xn_graph/xn_descent_pipeline_test.cpp` in `tests/cpp/papers/xn_graph/CMakeLists.txt`.

### Implementation for User Story 2

- [X] T020 [P] [US2] Implement `xn_graph_xn_descent_local_build_v1@1.0.0` with exact existing Artifact/version/ID-space/runtime-service `requires` and LocalGraphArtifact/v1/local-ID `provides` in `src/papers/xn_graph/xn_descent/descriptor.cpp`, without registering it in `src/cli/production_registry.cpp`.
- [X] T021 [P] [US2] Implement Feature-local registry assembly, request normalization, contract validation, execution-condition validation, and the pre-dispatch `pipeline-compatible lambda<=10`/no-paper-style-merge gate in `src/papers/xn_graph/xn_descent/xn_descent_experiment_runner.cpp` so rejection precedes P0, B1, M0, Registry factory selection, and expensive allocation.
- [X] T022 [US2] Implement the runner's pipeline-compatible path in `src/papers/xn_graph/xn_descent/xn_descent_experiment_runner.cpp` using the existing fixed P0 and M0 stage bindings and generic pipeline behavior without modifying P0, M0, `PipelineExecutor`, `CompatibilityChecker`, Harness, Loop Engine, or production Registry; label results component-level faithful adaptation.

### Validation for User Story 2

- [X] T023 [US2] Build the runner and pipeline tests, run `xn_descent_pipeline_preflight_test` and `xn_descent_small_pipeline_test`, and record commands, stage-call evidence, validator evidence, and results in `specs/007-xn-descent-local-build/validation/us2-pipeline-compatible.md`.

**Checkpoint**: T018-T023 preserve the interim implementation record only; T030-T034 define the current reusable-M0 acceptance boundary.

---

## Phase 5: User Story 3 — Reproduce a Declared B1 Run (Priority: P3)

**Goal**: Make identical declared runs exactly replayable within one platform/numerical environment and make non-convergence or resource failure explicit and non-publishable.

**Independent Test**: Repeat identical fixtures under thread budgets 1, 2, and the admitted maximum plus alternate injected schedules; within the same platform require identical native bytes, final-state digest, completed rounds, convergence evidence, and entry point, while every cap/resource/cancellation failure emits diagnostics and no reusable Artifact.

### Tests for User Story 3

- [X] T024 [US3] Add deterministic replay, termination, and resource suites in `tests/cpp/papers/xn_graph/xn_descent_determinism_test.cpp` covering worker-independent per-node streams, alternate schedules, immediate complete C/R/G fixed-point convergence, three-round stable-C/G convergence across an R two-cycle, success on the last permitted round, cap-nonconverged failure, checked-size overflow, known budget excess before major allocation, uncapped-R exhaustion, allocation failure, cancellation, and effective worker-count limits.
- [X] T025 [US3] Register `xn_descent_determinism_test`, `xn_descent_termination_test`, and `xn_descent_resource_test` as explicit grouped invocations of `tests/cpp/papers/xn_graph/xn_descent_determinism_test.cpp` in `tests/cpp/papers/xn_graph/CMakeLists.txt`.

### Implementation for User Story 3

- [X] T026 [P] [US3] Add deterministic proposal reduction independent of worker/schedule assignment, normalized C/R/G state digests, completed-round counts, full-state fixed-point evidence, stable-C/G round counts, and convergence-condition evidence in `src/papers/xn_graph/xn_descent/xn_descent_algorithm.cpp` while preserving the synchronous visibility rules from US1.
- [X] T027 [P] [US3] Add overflow-checked C/G/current-next-R/proposal/cache/scratch memory estimation, controlled dynamic-allocation failure, cancellation polling, and positive thread-budget enforcement in `src/papers/xn_graph/xn_descent/xn_descent_local_build.cpp`; never truncate R or publish a candidate to satisfy budget.
- [X] T028 [US3] Emit complete stage-owned BuildManifest source/input/checksum/toolchain/platform/build-flag/parameter/seed/policy/resource/convergence/termination evidence and isolated failure diagnostics from `src/papers/xn_graph/xn_descent/xn_descent_experiment_runner.cpp`, with no Artifact on `iteration_cap_nonconverged`, resource, allocation, or cancellation failure and no EvaluationReport metrics in Artifact/Manifest.

### Validation for User Story 3

- [X] T029 [US3] Run `xn_descent_determinism_test`, `xn_descent_termination_test`, and `xn_descent_resource_test` with their planned schedule/thread variants and record exact hashes, evidence, failures, and commands in `specs/007-xn-descent-local-build/validation/us3-replay-and-failure.md` without requiring macOS/Linux cross-platform equality.

**Checkpoint**: US3 proves within-platform replay and truthful terminal states; cross-platform byte equality remains explicitly outside the guarantee.

---

## Phase 6: User Story 4 — Parameterize and Reuse M0 for Partitioned XN (Priority: P4)

**Goal**: Complete P0 → B1 × Partition count → reusable M0 → GlobalGraph with independent
`lambda` and `merge.R`, while preserving the fixed Baseline Profile and claim boundary.

**Independent Test**: A request with `lambda > 10` and explicit positive `merge.R` produces and
validates one LocalGraphArtifact/v1 per Partition, supplies the complete set to the existing M0,
and publishes a valid GlobalGraph. Invalid `merge.R` or invalid/partial local output does not enter
M0, while the frozen Baseline runner still uses `R=16` and B0 local degree 10.

### M0 tests and parameterization

- [X] T030 [US4] Add failing reusable-M0 parameterization tests in `tests/cpp/baseline/test_cpp_main_m0.cpp` covering configurable `merge.R`, structurally valid LocalGraph inputs with `declared_max_degree > 10`, payloads whose actual degree exceeds 10, missing/zero/non-integral/otherwise invalid `merge.R`, and unchanged frozen Baseline behavior with `R=16` and B0 local degree 10.
- [X] T031 [US4] Parameterize the existing reusable M0 schema and wrapper in `contracts/baseline-v1/schemas/cpp-main-m0-parameters.schema.json` and `src/baseline/cpp_main/m0_merge.cpp`: accept explicit positive `merge.R`, remove the generic `declared_max_degree <= 10` and `R == 16` hard-codes, continue to call the pinned upstream `merge_shards`, and preserve the fixed Baseline runner's `R=16` and B0 local degree 10.
- [X] T032 [US4] Test first without adding a test target: update `tests/cpp/papers/xn_graph/xn_descent_parameters_test.cpp`, `tests/cpp/papers/xn_graph/xn_descent_pipeline_test.cpp`, `tests/cpp/papers/xn_graph/CMakeLists.txt`, `tests/cpp/baseline/test_cpp_main_descriptors_preflight.cpp`, `tests/cpp/baseline/test_cpp_main_pipeline.cpp`, and `tests/cpp/baseline/CMakeLists.txt`, and create an actually-read `tests/fixtures/xn_descent/pipeline-compatible-request.json`, to require parameter-contract revision `1.1.0`, exact M0/B1 descriptor schema paths and `$id` values, byte-identical revised XN schema copies, and matching `SOURCES.json` SHA-256; prove both `pipeline-compatible` with `lambda>10` and `paper-style` with `lambda=64` use the same P0 -> exactly one B1 per Partition -> existing M0 -> GlobalGraph path, every B1 output passes the existing Validator before M0, and explicit independent and different `lambda`/`merge.R` yield LocalGraph `declared_max_degree=lambda` with observed degree at most `lambda` and GlobalGraph `declared_max_degree=merge.R` with observed degree at most `merge.R`; cover missing/bool/string/fractional/negative/zero/greater-than-`UINT32_MAX` `merge.R` with a merge-specific failure and zero factory plus P0/B1/M0 calls; prove an earlier successful B1 followed by missing/failed output and a corrupted LocalGraph both prevent M0; and pass the actual `cpp_main_baseline_runner` executable path to the existing `cpp_main_pipeline` test to prove an otherwise valid Baseline request with merge `R=7` is rejected before workspace/stage creation while frozen `R=16` remains accepted.

### Implementation for User Story 4

- [X] T033 [US4] Implement the tested revision without changing implementation IDs/versions: create `contracts/baseline-v1/schemas/cpp-main-m0-parameters-v1.1.0.schema.json`, `specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json`, and byte-identical `contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters-v1.1.0.schema.json`; give M0 and XN their documented versioned `$id` values distinct from the historical contracts while both XN copies share the same `$id`, and retain XN `schema_version=xn-descent-parameters/v1`; restore `contracts/baseline-v1/schemas/cpp-main-m0-parameters.schema.json` as the frozen pre-relaxation baseline; update `contracts/papers/xn-graph-xn-descent-v1/SOURCES.json`, `src/baseline/cpp_main/descriptors.cpp`, and `src/papers/xn_graph/xn_descent/descriptor.cpp` to select and trace the revised schemas; remove only the old profile-degree checks from `src/papers/xn_graph/xn_descent/parameters.cpp`; update `src/papers/xn_graph/xn_descent/xn_descent_experiment_runner.cpp` to admit both full-pipeline profiles, range-check signed/unsigned integer `merge.R` in `1..UINT32_MAX` before workspace/Registry/factory/expensive allocation/stage execution without changing public `ParameterValidator`, and delegate Partition iteration, Validator/publication gates, complete B1 collection, and Merge scheduling to the existing `PipelineExecutor`; and add the independent pre-workspace integer-`R=16`/`use_filters=false` freeze guard to `src/baseline/cpp_main/cpp_main_baseline_runner.cpp`; retain T031's M0 runtime defense, pinned `merge_shards`, and the single normal PipelineExecutor lifecycle, with no XN Merge, M1, fake Merge, local-only path, descriptor-version routing, request conversion, Artifact/schema upgrade, or historical-evidence rewrite.

### Validation for User Story 4

- [X] T034 [US4] Run macOS Baseline-OFF XN/core plus `production_surface` regressions and Docker Linux/amd64 Baseline-ON Feature 002, XN, GP-ANN, HNSW-Merger, and `production_surface` regressions using their actual registered tests; verify frozen Baseline R=16 behavior, configurable `merge.R`, lambda-above-10 partitioned closure, and validator/downstream gates, then record commands and results in `specs/007-xn-descent-local-build/validation/us4-partitioned-merge.md`.

**Checkpoint**: All four stories are independently verified at small scale; no SIFT1M or full-paper claim is implied.

---

## Phase 7: Validation, Migration, and Human Gates

**Purpose**: Reconcile evidence, validate both supported platforms independently, then enforce two distinct human gates around the only permitted scale run.

- [X] T035 [P] Reconcile actual implementation/test paths, CTest names, Algorithm 1 line mapping, Equation (2), `TryInsert`, local `flag`, cross-update visibility, entry point, PRNG/ties/round/convergence adaptations, and M0 compatibility boundary in `specs/007-xn-descent-local-build/source-mapping.md` and `specs/007-xn-descent-local-build/quickstart.md` without changing settled specification semantics.
- [X] T036 [P] On macOS, perform the Baseline-OFF out-of-tree XN core configure/build, run all actually registered XN/core tests plus `production_surface`, verify the pinned upstream worktree, and record exact commands, toolchain/flags, passes, failures, unrun gates, and impact in `specs/007-xn-descent-local-build/validation/macos-validation.md`; do not require unavailable cpp_main pipeline tests from this core-only build.
- [ ] T037 The user must execute this task after T041 approval in an actual remote Linux environment using the exact reviewed clean project commit and pinned upstream gitlink; rebuild with `DISKANN_CPP_MAIN_BASELINE=ON` rather than copying a macOS or Docker binary, run all ten `^xn_descent_` tests plus `cpp_main_native_formats`, `cpp_main_descriptors_preflight`, `cpp_main_pipeline`, and `production_surface`, perform two Linux-internal deterministic replays, and record the actual commands, test results, compiler, flags, architecture, OS, numerical environment, replay hashes, and disclosure-only macOS differences in `specs/007-xn-descent-local-build/validation/linux-validation.md`. Local Docker evidence cannot satisfy T037, and any later remote-Linux SIFT1M run requires a separately frozen request and separate approval.
- [X] T038 Freeze the local Docker Linux/amd64 SIFT1M construction-scale request in `experiments/configs/xn-descent-local-build/sift1m-validation.json`, including exact Dataset path/checksum/shape, P0 identities/parameters, `omega`/`lambda`/`alpha`/seed/cap/profile, independent `merge.R`, memory/thread budgets, and workspace/request identity; bind its SHA-256 to the Docker image/toolchain/resources, three-mount execution layout, one-attempt failure-retention/no-selection policy, and result locations in `specs/007-xn-descent-local-build/quickstart.md`. This is only P0 → B1×Partition → M0 construction, Artifact, Manifest, resource, and termination validation, not Recall, QPS, IMF Search, or end-to-end paper-system validation.
- [X] T039 HUMAN APPROVAL GATE — after T035, T036, and T038, review all unit/contract/oracle/determinism/small-integration evidence, implementation diff against `specs/007-xn-descent-local-build/source-mapping.md`, the exact frozen SIFT1M request and SHA-256, Dataset identity/checksums, parameters, budgets, actual Docker resources/headroom, build conditions, and result/retention policy, then record explicit approval in `specs/007-xn-descent-local-build/validation/sift1m-pre-execution-approval.md`; `$speckit-implement` MUST STOP here and MUST NOT run SIFT1M without that approval.
- [ ] T040 Only after T039 is explicitly approved and both T038 material and the T039 approval record are committed, confirm a clean worktree, reconfigure and build the runner from that exact clean commit in local Docker Linux/amd64, verify clean source identity, then execute the single frozen JSON request `experiments/configs/xn-descent-local-build/sift1m-validation.json` exactly once; retain every success/failure without parameter-changing retry or best-run selection, and write immutable external workspace result/Artifact/Manifest/log references plus failure impact to `experiments/results/xn-descent-local-build/sift1m-validation.json`. The first attempt was user-authorized aborted and remains incomplete; its approval is consumed. The separately approved performance-fix second attempt uses `experiments/configs/xn-descent-local-build/sift1m-validation-perf-r2.json` (SHA-256 `b63805c6392338bd8798c8e950728443b0e75e3e7a56600e2781cfe9bc949dc5`), approval `specs/007-xn-descent-local-build/validation/sift1m-pre-execution-approval-perf-r2.md`, new host output `build/xn-sift1m-paper-v1-seed42-perf-r2`, container `xn-sift1m-paper-v1-seed42-perf-r2-t040`, and independent result index `experiments/results/xn-descent-local-build/sift1m-validation-perf-r2.json`. Commit this supplement, freeze, and approval together; from that unchanged clean commit reconfigure/build and pass the 13-test gate in quickstart Section 11.5 before executing the second request once. Preserve both attempts; failures/incomplete output do not complete this task, exit 137 alone is not OOM evidence, and no further attempt is implicitly authorized. Do not add a conversion flow or run SIFT100M or larger.
- [ ] T041 HUMAN REVIEW GATE — after T040, review the frozen request, implementation diff, source mapping, existing macOS/small-scale evidence, actual local Docker SIFT1M Artifact/Manifest/logs, failures, exclusions, and complete result set, then record the explicit decision and the exact clean commit selected for remote Linux migration in `specs/007-xn-descent-local-build/validation/sift1m-post-run-review.md`; do not imply that external Linux validation is already complete.
- [ ] T042 After successful T041 and T037, audit the final repository/upstream status, Artifact/Manifest/EvaluationReport separation, implementation identity/capabilities, validation commands/results/failures/unrun gates, both human decisions, local Docker SIFT1M status, external Linux status, and claim boundaries in `specs/007-xn-descent-local-build/validation/completion-report.md`, stating “未完成验证” whenever any required gate is absent or unsuccessful.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Starts immediately; T001 and T002 are parallel-safe.
- **Foundational (Phase 2)**: Depends on Phase 1 and blocks all user stories.
- **US1 (Phase 3)**: Depends on Phase 2 and is the MVP.
- **US2 (Phase 4)**: Depends on US1 because it composes the validated B1 output with fixed P0/M0.
- **US3 (Phase 5)**: Semantically depends only on US1, but follows US2 in the default sequence because both update the Feature test registration file.
- **US4 (Phase 6)**: Depends on US2 for operation/profile preflight and US3 for publish/failure semantics.
- **Validation and Human Gates (Phase 7)**: Depend on all stories; T038 freezes the local Docker request, T039 depends on T035, T036, and T038, T040 depends on explicit T039 approval, T041 depends on T040, T037 depends on successful T041 review, and T042 depends on both T041 and T037.

### User Story Dependencies

```text
Setup → Foundational → US1 (MVP)
                         ├──→ US2 ──┐
                         └──→ US3 ──┴──→ US4 → Local freeze → Pre-run approval → Local Docker SIFT1M → Post-run review → Remote Linux validation
```

### Within Each User Story

1. Author the complete failing tests and oracle expectations.
2. Register real CTest invocations before relying on quickstart commands.
3. Implement private algorithm/stage/runner responsibilities in dependency order.
4. Run and record the story's independent validation last.

### Parallel Opportunities

- T001 and T002 can proceed in parallel.
- T003 and T004 can proceed in parallel; T005 follows their stable test-facing declarations.
- T006–T009 can proceed in parallel against the foundational interfaces.
- T011 and T012 can proceed in parallel after test registration; T013–T016 then serialize shared implementation files and semantic dependencies.
- T020 and T021 can proceed in parallel after the US2 tests are registered.
- T026 and T027 can proceed in parallel after the US3 tests are registered; T028 consumes both result surfaces.
- T030 precedes T031 so reusable-M0 behavior is specified by failing tests before its schema and
  wrapper are parameterized; T032 similarly precedes T033, and T034 waits for T030–T033.
- T035, T036, and T038 may proceed after US4; T039 waits for all three outputs, T040 waits for explicit T039 approval, and T041 waits for T040. T037 is deliberately not parallel: the user performs the remote Linux rebuild/replay only after T041 selects the reviewed clean migration commit. T042 waits for both T041 and T037.

---

## Implementation Strategy

### MVP First

1. Complete Phase 1 and Phase 2.
2. Complete US1 through T017.
3. Stop and review the Algorithm 1 oracle, strict Equation (2), Artifact validity, and source boundaries before composing or scaling.

### Incremental Delivery

1. **MVP**: Direct semantic-small B1 Local Build (US1).
2. **Composition**: Early compatibility gate and small P0 → B1 → M0 closure (US2).
3. **Operational trust**: Deterministic replay and controlled terminal/resource failures (US3).
4. **Reusable M0 composition**: Parameterize the existing M0 and validate the full partitioned
   pipeline with independent `lambda` and `merge.R` (US4).
5. **Scale then migration evidence**: Freeze and approve one local Docker SIFT1M construction run, review it, then rebuild and replay the selected clean commit on remote Linux before final audit.

### Scope Guardrails

- Do not modify `upstream/diskann-cpp-main/`, its gitlink, public pipeline interfaces,
  LocalGraphArtifact/v1, BuildManifest schema, P0/B0 algorithm semantics, the pinned
  `merge_shards` semantics or M0 identity, `PipelineExecutor`, `CompatibilityChecker`, Harness,
  Loop Engine, or `src/cli/production_registry.cpp`; reusable-M0 algorithm changes remain limited
  to the wrapper admission completed by T030–T031, while T032–T033 may only add the reviewed
  versioned parameter-schema/descriptor path and fixed Baseline runner guard.
- Do not add an Adapter, Artifact v2, fake Merge, BBP, Secondary Index, IMF Search, SSD layout, parameter sweep, or any run larger than SIFT1M.
- Do not treat macOS/Linux graph-byte differences as failure; exact replay is required independently within each declared platform/numerical environment.
- Do not claim full XN-Graph-system reproduction or completed Feature validation from unit tests, compilation, or SIFT1M execution without the required human reviews.
