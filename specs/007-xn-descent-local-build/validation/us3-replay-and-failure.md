# US3 Deterministic Replay and Failure Validation

**Validated on**: 2026-09-09

**Implementation**: `xn_graph_xn_descent_local_build_v1@1.0.0`

**Reproduction mode**: `faithful_adaptation`

**Project HEAD**: `939c98a058a2621db241627f04a8a58406d97a16`

**Project worktree**: dirty (the reviewed T024–T029 implementation under validation)

**Configured dirty-source checksum**: `d3091852f46e1bb6ef16aff0d9ca91a89e8b74690775747594be8fd9d2513a93` (captured before appending this evidence file and task checkmarks)

**Fixed upstream**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823`

## Scope and acceptance boundary

This evidence covers T024–T029 only: deterministic worker/schedule replay, the two declared convergence conditions, iteration-cap failure, checked-size and memory-budget handling, uncapped reverse-neighbor storage, allocation failure, cancellation, and stage-owned BuildManifest evidence. It does not validate paper-style execution, SIFT1M, an external Linux host, or T030–T042.

Replay is required within a platform and declared numerical environment. The macOS and Docker Linux results are reported separately; equality between the two platforms is not an acceptance condition, even though these fixtures happened to produce the same graph and state hashes.

## macOS Baseline-OFF validation

Environment:

- macOS 13.2.1 (22D68), Darwin 22.3.0, x86_64.
- Apple Clang 14.0.0 (`14.0.0.14000029`).
- CMake 4.4.2; Debug; `BUILD_TESTING=ON`.
- `DISKANN_CPP_MAIN_BASELINE=OFF`; `DISKANN_XN_DESCENT=ON`.
- Existing local nlohmann_json FetchContent source was supplied explicitly; no dependency version changed.

Commands:

```sh
cmake -S . -B build/xn-descent-core-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_CPP_MAIN_BASELINE=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$PWD/build/xn-descent-core-debug/_deps/nlohmann_json-src"

cmake --build build/xn-descent-core-debug --parallel \
  --target \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test \
  xn_descent_determinism_test

ctest --test-dir build/xn-descent-core-debug \
  --output-on-failure --no-tests=error -V \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|determinism|termination|resource)_test$'
```

Exactly eight tests were selected and passed:

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

- Image `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`, inspected as `linux/amd64`.
- Reused the previously configured `build/xn-descent-docker-linux-t023-r2` directory and reconfigured it to refresh source identity.
- Network disabled with `--network=none`; image pulling disabled with `--pull=never`.
- Cached nlohmann_json source commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`, clean.
- GCC 11.4.0; CMake 3.22.1; Release.
- `DISKANN_CPP_MAIN_BASELINE=ON`; `DISKANN_XN_DESCENT=ON`.

Commands executed inside the specified Docker image:

```sh
cmake -S . -B build/xn-descent-docker-linux-t023-r2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src

cmake --build build/xn-descent-docker-linux-t023-r2 --parallel \
  --target \
  xn_descent_experiment_runner \
  xn_descent_pipeline_test \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test \
  xn_descent_determinism_test

ctest --test-dir build/xn-descent-docker-linux-t023-r2 -N \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|determinism|termination|resource|pipeline_preflight|small_pipeline)_test$'

ctest --test-dir build/xn-descent-docker-linux-t023-r2 \
  --output-on-failure --no-tests=error -V \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|determinism|termination|resource|pipeline_preflight|small_pipeline)_test$'
```

CTest listed exactly ten tests: the eight macOS-listed tests plus `xn_descent_pipeline_preflight_test` and `xn_descent_small_pipeline_test`. Result: **10/10 passed**. The two pipeline regressions additionally reconfirmed pre-dispatch rejection, Validator admission of each B1 candidate, invalid/partial candidate exclusion, and the legal fixed P0 → XN B1 → fixed M0 path.

## Replay evidence

The deterministic fixture uses 24 nodes and runs the same private production algorithm/reducer with thread budgets 1, 2, and 33, yielding effective worker counts 1, 2, and 24. It also uses ascending, reverse, and even-then-odd owner schedules. Within each platform all variants and a repeated two-worker run produced:

- identical complete normalized C/R/G states;
- identical component and full-state digests;
- identical completed-round counts and convergence condition;
- identical deterministic local medoid entry point;
- byte-identical native LocalGraph payloads for stage runs with effective worker counts 1, 2, and 24.

Observed replay values on both independently validated platforms:

- final full C/R/G SHA-256: `3827ea96b8c7d6bdc262ee7041f28b0be0b7c5bac93bf6492169bf55822b89d5`;
- native LocalGraph payload SHA-256: `46e4c8d3ca83254ca869a487d2722806dd8f4478fefb61179bbe05863cfb1386`;
- completed rounds: `8`;
- convergence condition: `three_round_c_g_stability`.

Changing the declared seed from 42 to 43 changed the recorded Manifest `seed` while Dataset and Partition input identities/checksums remained fixed. This is the result-related evidence change; run ID, timestamps, and absolute paths were not used to establish replay.

## Termination and resource evidence

- Full-state fixed point: the two-node case converged immediately at round 2 with `fixed_point=true`, condition `full_state_fixed_point`, and full-state SHA-256 `199f201b658da42b060dd3b4f1532298fac79870576ce219afac73f6ac315198`.
- R two-cycle: cap 7 returned `iteration_cap_nonconverged`; cap 8 succeeded on the last allowed round with `fixed_point=false`, `stable_c_g_rounds=3`, condition `three_round_c_g_stability`, and SHA-256 `d308f7cef50a6411d49c6281c72316d674dea3ad5a3f7ffb3b1f4868839c6f3d`.
- Checked-size overflow was rejected by checked unsigned arithmetic.
- Known required memory greater than budget failed before the first round with `memory_budget_preflight_exceeded`.
- A budget equal to the bounded preflight requirement failed at the exact next-R boundary with `reverse_state_memory_budget_exceeded`; R was not truncated.
- The uncapped-R oracle retained 4 reverse records with `omega=2`.
- The bounded cross-C reducer processed all 8 logical proposals, retained 6 canonical target-owned proposals, and produced the same top-omega C result as complete TryInsert reduction.
- The test-only DistanceFunction threw `std::bad_alloc` at a deterministic evaluation under `thread_budget=1`; the worker caught it, all threads joined, the coordinating path returned `allocation_failed`, and no Artifact was published.
- Cancellation was polled only by the coordinating thread at deterministic safe points; workers did not poll the public token, all threads joined, termination was `cancelled`, and no Artifact was published.
- Effective workers were exactly `min(thread_budget, local_node_count)` and never exceeded the declared budget.
- macOS and Linux conservative preflight estimates were 11292 and 11004 bytes respectively for the resource fixture; the difference reflects platform type/runtime sizes and is expected.

All cap, budget, allocation, and cancellation stage failures returned no output candidate and recorded no output Artifact.

## Manifest evidence and separation

The stage-owned BuildManifest was validated to record implementation/version and faithful-adaptation mode; project source commit plus dirty state/checksum; fixed upstream repository/branch/commit; Dataset and Partition identities/checksums; compiler/version, CMake generator/build type, configuration flags, actual compile commands, and link command; platform/OS/architecture/numerical environment; all XN parameters and policy identifiers; requested/effective threads and memory evidence; completed rounds, direct state comparison/change counts, convergence condition, component/final digests, termination status/reason; and success output identity/checksum or failure information.

Replay comparisons excluded run ID, timestamps, absolute paths, and other lifecycle fields. Artifact, BuildManifest, and EvaluationReport responsibilities remain separate: no Recall, QPS, or latency value was written into the Artifact or BuildManifest, and `metrics_json` remained `{}`.

## Failures and unexecuted validation

- Test/build failures in this validation run: none.
- Not executed: T030–T042, paper-style local-only execution, SIFT1M or larger datasets, external Linux migration, and cross-platform byte-equality acceptance.
- No Git commit or push was performed as part of this validation.
