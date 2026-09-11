# XN JSON-only Request Provenance Validation

**Validated on**: 2026-09-11

**Scope**: Feature-local request protocol compatibility only; no XN algorithm, public pipeline,
Artifact, BuildManifest schema, M0, Registry, or upstream change

**Project HEAD during configure and tests**:
`9d606eeab7617c8081dc4a345d9e5716360c696a`

**Project worktree during configure**: dirty (`dirty_worktree=true`; only the protocol runner and
pipeline-test changes existed before this evidence/document update)

**Configured dirty-source checksum**:
`2f1765d2aa94a88c2bd3b00951c34888da34fa0b096bf282df8e3be12d28a788`

**Fixed upstream**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`

## Environment

- Docker image:
  `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`.
- Image platform and execution platform: Linux/amd64.
- Kernel reported by the container: Linux 5.15.49-linuxkit, x86_64.
- GCC 11.4.0, CMake 3.22.1, Unix Makefiles, Release (`-O3 -DNDEBUG`).
- `BUILD_TESTING=ON`, `DISKANN_CPP_MAIN_BASELINE=ON`, `DISKANN_XN_DESCENT=ON`.
- GP-ANN, HNSW-Merger, and Search Evaluation were OFF.
- nlohmann_json came from the existing offline r2 cache at commit
  `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`.
- Docker network access and image pulls were disabled. The repository was mounted read-only; only
  `build/xn-descent-docker-linux-t023-r2` was writable.

The existing r2 cache was refreshed and the affected programs were built offline with:

```sh
cmake -S . -B build/xn-descent-docker-linux-t023-r2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src

cmake --build build/xn-descent-docker-linux-t023-r2 --parallel \
  --target \
    xn_descent_experiment_runner \
    xn_descent_parameters_test \
    xn_descent_equation_try_insert_test \
    xn_descent_round_oracle_test \
    xn_descent_local_artifact_test \
    xn_descent_determinism_test \
    xn_descent_pipeline_test \
    cpp_main_m0 \
    cpp_main_descriptors_preflight \
    cpp_main_pipeline
```

## Test-first Red

The protocol assertions were added first to the existing
`tests/cpp/papers/xn_graph/xn_descent_pipeline_test.cpp`; no target or CTest name was added. The
existing r2 directory was reconfigured with the environment above and the runner and pipeline test
were built. The Red command was:

```sh
ctest --test-dir build/xn-descent-docker-linux-t023-r2 \
  --output-on-failure --no-tests=error \
  -R '^xn_descent_(pipeline_preflight|small_pipeline)_test$'
```

CTest found both tests and returned status 8, as expected before implementation:

- `xn_descent_pipeline_preflight_test` failed because a `1.1.0` request carrying caller
  `source_config` was rejected only as an unsupported request version, not for the required
  version-specific field violation.
- `xn_descent_small_pipeline_test` reached the new JSON-only cases and reported
  `missing required field: source_config`; the new real P0/B1/M0 executions therefore could not
  succeed under the old parser.

The old `1.0.0` partitioned cases still executed before the new failing cases. The Red failures
were protocol-specific rather than configuration, dependency, registration, fixture, or algorithm
failures.

## Minimal implementation

The runner now selects an exact request shape from `request_version`:

- `1.0.0` requires and preserves the historical two-digest caller `source_config`.
- `1.1.0` rejects caller `source_config` and generates a Feature-local source object from the
  actual request text.

For `1.1.0`, the generated object contains `source_format=json`, SHA-256 of the exact input bytes,
SHA-256 of the strictly parsed ordered-key compact `dump()` bytes, and
`normalization_policy=xn-json-compact-v1`. It is passed through the existing
`PipelineExecutionRequest.source_config_json`; the existing executor records it in every parent
stage BuildManifest.

## Green and behavioral evidence

An incremental build followed by the corrected two-test command passed 2/2. The final verification
then reconfigured r2 to refresh the dirty-source identity, built the runner, all XN test programs,
and the three affected cpp_main programs, listed the selected tests, and ran:

```sh
ctest --test-dir build/xn-descent-docker-linux-t023-r2 \
  -N \
  -R '^(xn_descent_.*|cpp_main_(m0|descriptors_preflight|pipeline))$'

ctest --test-dir build/xn-descent-docker-linux-t023-r2 \
  --output-on-failure --no-tests=error \
  -R '^(xn_descent_.*|cpp_main_(m0|descriptors_preflight|pipeline))$'
```

CTest found exactly 13 tests. Result: **13/13 passed**.

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

The two pipeline CTests additionally proved:

- an old `1.0.0` request completes and every parent Partition/LocalBuild/Merge BuildManifest
  retains exactly its caller-supplied two-digest source object;
- a `1.1.0` request omitting `source_config` completes the real small multi-Partition
  P0 → B1×N → M0 path;
- every parent stage Manifest contains the exact generated object and its raw and normalized
  hashes equal independent test calculations;
- compact, whitespace-changed, and top-level-key-reordered encodings of the same request have
  different raw-byte hashes where their bytes differ and the same normalized hash;
- changing the effective XN seed changes the normalized hash;
- `1.1.0` with caller `source_config`, an unsupported version, a duplicate key, and invalid JSON
  are rejected before workspace/factory/stage activity;
- existing invalid `merge.R`, partial/corrupt LocalGraph, lambda-above-10, paper-style, Validator,
  and component-level claim-boundary assertions remain green.

## Failures and unexecuted checks

- One incremental Green invocation contained a malformed test-selection regular expression. CTest
  selected zero tests and returned status 8 under `--no-tests=error`; the source had built
  successfully. The command was corrected immediately and the intended two tests passed 2/2.
  This was a command-entry error, not a code or test failure.
- The macOS Baseline-OFF configuration intentionally does not create this runner or its pipeline
  tests, so it was not claimed as macOS compilation evidence for this patch.
- GP-ANN and HNSW-Merger were not rerun because this patch does not change their public pipeline,
  M0, contracts, or build inputs.
- External Linux migration, T037/T038 completion, T039–T042, SIFT1M, larger datasets, parameter
  search, commit-time rebuild, and push were not part of this validation.

The test identity above intentionally records the dirty source that was actually configured and
executed. The later commit containing this evidence is not presented as if it had been rebuilt.
