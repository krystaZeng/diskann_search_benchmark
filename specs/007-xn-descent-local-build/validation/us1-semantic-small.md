# US1 Semantic-Small Validation Evidence

**Feature**: `007-xn-descent-local-build`

**Validation date**: 2026-09-08

**Scope**: T003–T017 only. This evidence covers the macOS core-only US1 MVP. It does not cover
US2 pipeline closure, US3 multi-worker/resource behavior, US4 paper-style execution, Linux,
SIFT1M, or complete Feature validation.

## Source and build identity

- Project HEAD: `5f0429d93388bdcf1e0ac159a5d63f9bde7ff2c3`
- Project branch: `feature/xn-descent-local-build`
- Configure-time source state: dirty worktree, recorded compile-definition checksum
  `0db1241a14259293276debd3743a0baff8ac237a67e84faac9595d9d1d6c5929`
- Pinned native-layout source: `microsoft/DiskANN`, branch `cpp_main`, commit
  `78256bbab4685e1774e78d331e081a153be26823`
- Upstream worktree during validation: clean; upstream gitlink unchanged
- Host: macOS 13.2.1 (22D68), x86_64
- Compiler: `/usr/bin/c++`, Apple Clang 14.0.0 (`clang-1400.0.29.202`)
- CMake: 4.4.2, generator `Unix Makefiles`
- Build type and flags: `Debug`; `CMAKE_CXX_FLAGS` empty;
  `CMAKE_CXX_FLAGS_DEBUG=-g`
- Feature switches: `BUILD_TESTING=ON`, `DISKANN_XN_DESCENT=ON`,
  `DISKANN_CPP_MAIN_BASELINE=OFF`
- XN compile definition: `XN_DESCENT_CPP_MAIN_BASELINE_ENABLED=0`

The configure-time dirty checksum describes the source state compiled by the fresh configure.
Subsequent task-checkbox and evidence-file edits are documentation changes and are not represented
by that compile definition.

## Formal commands and results

Fresh configure:

```bash
cmake --fresh -S . -B build/xn-descent-core-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_CPP_MAIN_BASELINE=OFF
```

Result: PASS. Configuration completed in the existing dedicated out-of-tree directory with a
fresh cache.

Full core-only build:

```bash
cmake --build build/xn-descent-core-debug --parallel
```

Result: PASS. The build reached 100%, including `diskann_xn_descent` and all four US1 test
executables.

Strict US1 test selection and execution:

```bash
ctest --test-dir build/xn-descent-core-debug \
  --output-on-failure \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact)_test$'
```

Result: PASS, exactly 5/5 tests:

| CTest name | Result | Formal-run time |
|---|---:|---:|
| `xn_descent_parameters_test` | PASS | 1.05 s |
| `xn_descent_equation_test` | PASS | 0.50 s |
| `xn_descent_try_insert_test` | PASS | 0.01 s |
| `xn_descent_round_oracle_test` | PASS | 0.47 s |
| `xn_descent_local_artifact_test` | PASS | 0.63 s |

Discovery was separately checked with the same strict regex and reported `Total Tests: 5`.
The layout remains four test source files: Equation (2) and TryInsert share
`xn_descent_equation_try_insert_test.cpp`, which is invoked through two CTest names.

## Semantic fixtures and parameters

- Parameter tests use normalized `xn-descent-parameters/v1` documents and cover default seed 42,
  required policy identities, unsigned bounds, invalid/non-finite alpha, positive capacities/cap,
  positive StageContext budgets, all three profile constraints, and byte identity of the authored
  and runtime schema copies. No profile runner was executed.
- Equation (2) uses one-dimensional hand-checkable vectors and covers ordinary acceptance,
  equality in each strict predicate, the known alpha-versus-alpha-squared boundary, non-finite and
  negative numeric rejection, and exponent-scaled extreme values.
- TryInsert uses hand-checkable one-dimensional centers to verify target-owned distances,
  duplicate idempotence, distance/LocalNodeId ties, capacity, self rejection, and range rejection.
- The complete-round oracle uses vectors `[0, 10, 1, 3, -10]` with `omega=4`, `lambda=3`,
  `alpha=1`, seed 42, and cap 8. Its exact next state is asserted for every node's normalized
  `C`, `R`, and `G`, including eight cross-C proposals, two reverse proposals, immediate
  within-owner working-G visibility, removal before insertion, and cross proposals retained for an
  occluded candidate.
- A second exact round uses `alpha=2` and `lambda=1`; the candidate is accepted and then evicted by
  capacity, while both prior cross-TryInsert proposals remain in next-round C. G is unchanged but
  C/R change, so the round is correctly non-converged.
- The exact fixed-point oracle uses vectors `[0, 1]`, `omega=1`, `lambda=1`, and verifies the full
  final normalized C/R/G state, two completed rounds, direct equality, zero changed C/R/G nodes,
  and convergence.
- Direct Local Build uses a native float32 Dataset with 3 rows and dimension 1, Partition map
  `[2, 1, 0]`, `omega=5`, `lambda=3`, `alpha=1.2`, seed 42, and cap 16. It also checks a small
  Partition with `omega=1000` under a 4096-byte declared preflight budget, a cap-1 non-converged
  attempt, a known one-byte memory-budget failure, and rejection of a non-native accessor before a
  valid Dataset/run identity can be established.

## Verified outcomes

- Paper Algorithm 1 ordering is preserved: round-boundary C/R candidates, reverse proposal before
  acceptance, both cross-TryInsert effects for every encountered `v`, continued cross updates after
  occlusion, immediate node-owned G visibility, removal before bounded insertion, and no rollback
  of earlier cross effects after capacity eviction.
- Equation (2) uses two strict `<` comparisons and a scale-safe `alpha^2` comparison over squared
  L2. Equality in either predicate is non-occluding.
- Initialization has empty G/R and deterministic unique non-self C; undersized Partitions use all
  available non-self nodes. C and G remain bounded; R was observed larger than omega without
  truncation in the round oracle.
- Fixed point is determined by direct equality of complete normalized C/R/G, not stable G alone or
  a digest/threshold substitute. A cap hit with changed state returns failure, records completed
  rounds and fixed-point evidence, and emits no candidate Artifact.
- Accepted output uses LocalNodeId adjacency, copies the complete map byte-for-byte, has deterministic
  local medoid 1 for the hand-checkable fixture, contains no self/duplicate/out-of-range edges, sets
  declared degree to lambda, and sets observed metadata/native header to the payload maximum.
- The candidate passes the existing LocalGraphArtifact/v1 Validator. Artifact data, stage-owned
  BuildManifest, and metrics remain separate. The successful stage-owned Manifest records the
  candidate output identity; the budget and cap failures retain failure/termination evidence and
  have empty output lists.
- Known preallocation uses effective `min(configured_capacity, n-1)` C/G bounds, so an undersized
  Partition is not rejected for memory it cannot use. A known required-memory excess is rejected
  before vector loading/Algorithm-1 iteration and publishes no candidate.
- Core-only boundary checks found exactly one compile rule for
  `cpp_main_vector_accessor.cpp`; the XN library and US1 tests link neither
  `diskann_cpp_main_stages` nor `diskann_s`. No upstream DiskANN target or upstream build directory
  was generated.
- `git diff --check` passed before evidence publication. Upstream remained at the pinned commit,
  with a clean worktree and no gitlink diff.

## Test-driven failures resolved before the formal run

1. The first strengthened five-test run produced 4/5 passes and exposed an empty successful
   stage-owned Manifest output list. The test's unguarded follow-up access also aborted the process.
   The assertion was made safe, and the validated candidate identity is now recorded before the
   successful Manifest is persisted.
2. The immediate rerun was blocked by the stale temporary stage directory left by that abnormal
   abort. The generated directory was moved outside the repository to a named failed-TDD quarantine
   path; no source or workspace data was deleted.
3. The new undersized-Partition budget case then failed because preflight used configured omega and
   lambda rather than effective `min(capacity,n-1)` bounds. The estimator was corrected, after
   which the focused Artifact test and the final fresh 5/5 run passed.

No unresolved US1 test failure remains.

## Explicitly unverified scope

- The allocation-failure, cancellation, scheduling, and stage-call-count fixtures required by T005
  are present and compile through the US1 test targets, but their future behaviors were not executed
  or claimed here.
- T018–T042 are unexecuted. In particular, descriptor/capability exactness (T020), P0 → B1 → M0,
  multi-worker/schedule replay, dynamic allocation/cancellation/R-exhaustion suites, complete runner
  provenance, paper-style `lambda=64` local-only execution, Linux rebuild, and SIFT1M remain open.
- No pipeline-compatible or paper-style runner was built or run. Profile parsing/validation only was
  tested in this phase.
- No Linux command, SIFT1M run, larger dataset, commit, amend, or push occurred.

Accordingly, US1 semantic-small is validated for the recorded macOS core-only environment, but the
complete XN-Descent Feature remains **未完成验证**.
