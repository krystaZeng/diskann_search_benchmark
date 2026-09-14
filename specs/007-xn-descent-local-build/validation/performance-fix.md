# XN-Descent Performance Fix Validation

**Validated on**: 2026-09-14

**Project HEAD under test**: `3fc14732b57a1510b2136752cbf2d94847e63d14`

**Project worktree under test**: dirty (the reviewed performance fix and its test-first regression)

**Configured dirty-source checksum**: `3e0a21bae10596a798abc479ede0426875d4283655e861ef4e3e4cb7bc9ab7bc` (captured after the final code/test changes and before appending this evidence file)

**Fixed upstream**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823`

## Interrupted T040 attempt

The user explicitly authorized stopping only the running container `xn-sift1m-paper-v1-seed42-t040`. Before stopping, the container image, command, mounts, request SHA-256 `b71453ecad9360f4af9d33c02ff1a05e69f07c6ef3c63599097d104d8a0c1f23`, project HEAD, and upstream identity were checked against the approved T040 run.

Pre-stop observations and the stop outcome are retained under:

`build/xn-sift1m-paper-v1-seed42/diagnostics/`

The recorded attempt had been running for approximately 1 hour 52 minutes. The last pre-stop sample reported 585.78% container CPU, 6.885 GiB of 19.55 GiB memory, and 14 PIDs. Docker was given a 30-second normal stop grace period, then reported that it did not receive an exit event; the associated execution ended with code 137. Because the container used `--rm`, it was absent after the stop. No other container was stopped.

No `result.json` existed, and no B1 termination Manifest or convergence evidence existed. The attempt is therefore recorded as `user_authorized_abort`, with `sift1m_validation_complete=false` and `t040_completed=false`. No result, termination Manifest, success status, or convergence evidence was inferred or fabricated.

## Confirmed redundant work

The existing deterministic synchronous round implementation performed three kinds of provably redundant work:

- Candidate records and working G entries already carried exact squared L2 distances to the owner, but Equation (2), G deletion, and TryInsert recomputed those center distances.
- A candidate/current pair distance was recomputed independently for the two cross-TryInsert proposals and the occlusion decisions, and cross-C distance evaluation occurred while holding the target lock.
- State comparison constructed membership sets even for byte-for-byte equal normalized adjacency rows, and `run_algorithm` encoded and hashed the same normalized state again after `execute_round` had already done so.

## Test-first evidence and fix

A fixed single-thread round oracle was instrumented with a test-only counting L2 `DistanceFunction`. Its vectors, initial C/R/G state, parameters, candidate set, and expected complete normalized output are fixed in `xn_descent_round_oracle_test.cpp`.

Before the production change, the new assertion failed exactly as intended:

```text
XN_PERF_DISTANCE_CALLS optimized_expected=6 actual=34
assertion failed: counted_distance.evaluations() == std::uint64_t{6}
```

The minimum equivalent production change:

- reuses the candidate and working-G owner distances already stored in `NeighborRecord`;
- evaluates each encountered candidate/current squared L2 distance once and reuses it for both directional cross-TryInsert proposals and both Equation (2) roles;
- passes the exact precomputed pair distance into the target-owned cross-C reducer before acquiring its mutex;
- avoids sorting when a duplicate or a full-capacity worse proposal leaves the bounded top-omega result unchanged;
- reuses the cached pair distances during accepted-candidate G deletion and the candidate record during G/C insertion;
- performs membership-delta work only for normalized rows already proven unequal by direct logical comparison; and
- computes each round's normalized state digests once, carrying the already computed digest between `run_algorithm` rounds while retaining complete logical C/R/G comparison as the convergence criterion.

After the change, the same oracle reported:

```text
XN_PERF_DISTANCE_CALLS optimized_expected=6 actual=6
```

This is only a fixed micro-input count of eliminated redundant `DistanceFunction::evaluate` calls. It is not a measured SIFT1M speedup and is not used to claim any scale-level performance factor.

The exact complete C/R/G state, cross-C and reverse-R proposal counts, changed-node and changed-edge evidence, convergence condition, and before/after state digests remained equal to the pre-existing oracle expectations. Candidate generation, reverse-R, both cross-TryInsert directions, occluded-candidate cross effects, working-G visibility, insertion/removal order, strict Equation (2) comparisons, alpha-squared handling, capacity limits, and both convergence conditions were not changed.

## macOS Baseline-OFF validation

Environment:

- Darwin 22.3.0, x86_64.
- Apple Clang 14.0.0 (`14.0.0.14000029`).
- CMake 4.4.2; Debug; `BUILD_TESTING=ON`.
- `DISKANN_CPP_MAIN_BASELINE=OFF`; `DISKANN_XN_DESCENT=ON`.
- GP-ANN, HNSW-Merger, and Search Evaluation disabled.
- Existing nlohmann_json source cache supplied explicitly; no network access or dependency change.

Commands:

```sh
cmake -S . -B build/xn-descent-core-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_CPP_MAIN_BASELINE=OFF \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$PWD/build/xn-descent-core-debug/_deps/nlohmann_json-src"

cmake --build build/xn-descent-core-debug --parallel \
  --target \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test \
  xn_descent_determinism_test

ctest --test-dir build/xn-descent-core-debug \
  -V --no-tests=error \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|determinism|termination|resource)_test$'
```

Exactly eight tests ran and passed:

1. `xn_descent_parameters_test`
2. `xn_descent_equation_test`
3. `xn_descent_try_insert_test`
4. `xn_descent_round_oracle_test`
5. `xn_descent_local_artifact_test`
6. `xn_descent_determinism_test`
7. `xn_descent_termination_test`
8. `xn_descent_resource_test`

Result: **8/8 passed**.

## Docker Linux/amd64 Baseline-ON validation

Environment:

- Image `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`, `linux/amd64`.
- Repository mounted read-only; only `build/xn-descent-docker-linux-t023-r2` writable.
- Network disabled and image pulling disabled.
- GCC 11.4.0; CMake 3.22.1; Release.
- `DISKANN_CPP_MAIN_BASELINE=ON`; `DISKANN_XN_DESCENT=ON`.
- GP-ANN, HNSW-Merger, and Search Evaluation disabled.
- Existing nlohmann_json source cache supplied explicitly.

The existing r2 build directory was reconfigured so its generated build identity records the current HEAD, `dirty=true`, and the dirty-source checksum above. The runner, all XN test programs, and the three requested cpp_main regression programs were rebuilt. CTest first listed exactly 13 selected tests, then executed them.

Exactly 13 tests ran and passed:

1. `cpp_main_descriptors_preflight`
2. `cpp_main_m0`
3. `cpp_main_pipeline`
4. `xn_descent_parameters_test`
5. `xn_descent_equation_test`
6. `xn_descent_try_insert_test`
7. `xn_descent_round_oracle_test`
8. `xn_descent_local_artifact_test`
9. `xn_descent_determinism_test`
10. `xn_descent_termination_test`
11. `xn_descent_resource_test`
12. `xn_descent_pipeline_preflight_test`
13. `xn_descent_small_pipeline_test`

Result: **13/13 passed**.

## Equivalence and failure-path results

Both validated platforms retained the pre-existing deterministic evidence values:

- full normalized C/R/G SHA-256: `3827ea96b8c7d6bdc262ee7041f28b0be0b7c5bac93bf6492169bf55822b89d5`;
- native LocalGraph payload SHA-256: `46e4c8d3ca83254ca869a487d2722806dd8f4478fefb61179bbe05863cfb1386`;
- deterministic fixture completed rounds: `8`;
- convergence condition: `three_round_c_g_stability`;
- two-node full-fixed-point digest: `199f201b658da42b060dd3b4f1532298fac79870576ce219afac73f6ac315198`; and
- R-two-cycle cap-8 digest: `d308f7cef50a6411d49c6281c72316d674dea3ad5a3f7ffb3b1f4868839c6f3d`.

The allocation-failure injection still triggered `allocation_failed` on the real algorithm path under one worker. Cancellation, preflight budget rejection, exact reverse-R budget rejection without truncation, fixed-point termination, R-two-cycle termination, iteration-cap failure, thread/schedule determinism, Artifact validation, pipeline preflight, and P0 → B1×N → M0 regression all remained passing.

Artifact, BuildManifest, and EvaluationReport responsibilities remain separate. The state digest encoding and Manifest evidence meaning did not change; digest equality is still evidence only and never replaces complete normalized C/R/G comparison.

## Failures, unexecuted validation, and remaining risk

- The intended Red failure was observed before the implementation change: 34 distance evaluations versus the required 6.
- An initial Docker validation launch from the restricted execution sandbox was denied access to the Docker socket; the same fixed offline command was rerun with authorized Docker-daemon access. This was an execution-permission issue, not a compile or test failure.
- Final macOS and Docker builds and tests had no compile or test failures.
- The interrupted T040 run is not complete and does not validate this performance change at SIFT1M scale.
- No new SIFT1M run was authorized or started, and the frozen request was not modified.
- No external Linux migration, Search/Recall/QPS evaluation, T041 review, or larger-dataset experiment was performed.
- The fixed micro-oracle proves redundant work was removed without changing its results; it does not prove the remaining implementation is performance-optimal or that the prior T040 wall time will improve by a particular factor.
- No Git commit or push was performed.

## Round-result update exception-safety follow-up

This follow-up was validated on 2026-09-14 against project HEAD
`3fc14732b57a1510b2136752cbf2d94847e63d14` with the preceding performance fix and this
one-line follow-up present in the dirty worktree. Both platform configurations recorded
`dirty=true` and dirty-source checksum
`2cef23beabbf1ebde0dd8af566773b81cc8950e5230d47e17096a71bac738e6a`. The pinned upstream
remained `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`.

The update of a completed round now move-assigns `next.convergence` into the algorithm result
instead of copying it. A compile-time check outside the repository confirmed that
`NormalizedRoundState`, `ConvergenceEvidence`, and `StateDigests` are all nothrow
move-assignable under the validated C++17 standard-library implementations. Source inspection
confirmed that `next.convergence` is not read after the move. From the move of `next.state`
through the move of `execution.state_digests`, the remaining assignments update only fixed-size
round/resource counters and flags; no string or adjacency-state copy remains in that update
sequence.

The exact production-code change was:

```cpp
result.convergence = std::move(next.convergence);
```

No algorithm ordering, convergence rule, digest encoding, parameter, Artifact, Manifest, or
failure-policy semantics changed. This removes the identified string-copy allocation risk at
the round-result commit point; it does not claim to exhaustively prove every possible
out-of-memory path exception-safe.

The macOS Baseline-OFF directory was reconfigured with Debug, testing enabled, XN enabled,
Baseline/GP-ANN/HNSW-Merger/Search Evaluation disabled, and its existing nlohmann_json cache.
The five test executables covering the eight XN core CTests were rebuilt, then the exact eight-test
regex was executed with verbose output. Apple Clang `14.0.0.14000029` and CMake 4.4.2 produced
**8/8 passing tests**. The log is
`build/xn-descent-core-debug/round-exception-safety-ctest.log`.

The Docker Linux/amd64 directory was reconfigured offline using image
`sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`, Release,
Baseline and XN enabled, GP-ANN/HNSW-Merger/Search Evaluation disabled, the existing pinned
nlohmann_json cache, read-only repository mounting, and only the r2 build directory writable.
The runner, XN test executables, and requested cpp_main regression executables were rebuilt.
CTest listed exactly 13 matching tests and GCC 11.4.0/CMake 3.22.1 then produced **13/13 passing
tests**. The log is
`build/xn-descent-docker-linux-t023-r2/round-exception-safety-ctest.log`.

Both platforms reported the optimized micro-oracle count as exactly 6 distance evaluations and
retained all pre-follow-up deterministic evidence:

- full normalized C/R/G SHA-256:
  `3827ea96b8c7d6bdc262ee7041f28b0be0b7c5bac93bf6492169bf55822b89d5`;
- native LocalGraph payload SHA-256:
  `46e4c8d3ca83254ca869a487d2722806dd8f4478fefb61179bbe05863cfb1386`;
- completed rounds `8` with `three_round_c_g_stability`;
- full-fixed-point digest:
  `199f201b658da42b060dd3b4f1532298fac79870576ce219afac73f6ac315198`; and
- R-two-cycle digest:
  `d308f7cef50a6411d49c6281c72316d674dea3ad5a3f7ffb3b1f4868839c6f3d`.

The resource test continued to report `allocation_failed` and `cancelled`, with uncapped reverse-R
and bounded cross-C evidence intact. The Docker pipeline preflight and small-pipeline tests also
continued to pass, preserving invalid/partial LocalGraph rejection and downstream admission
behavior. There were no compile or test failures. GP-ANN/HNSW-Merger were not rerun because this
follow-up did not modify their code or shared contracts.

T040 remains incomplete after the previously recorded user-authorized abort. No new SIFT1M run
was authorized, resumed, or started, no task checkbox was changed, and no commit or push was
performed as part of this follow-up.
