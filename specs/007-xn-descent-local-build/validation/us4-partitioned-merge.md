# US4 Partitioned Merge Comprehensive Regression

**Validated on**: 2026-09-11

**Implementation**: `xn_graph_xn_descent_local_build_v1@1.0.0`

**Reproduction mode**: `faithful_adaptation`

**Project HEAD**: `175d2bc0899a8040a21862c2c4fbf2f4fbbe6206`

**Project worktree**: dirty (`dirty_worktree=true`; the reviewed T030–T033 implementation and design changes were already present before this validation)

**Configured dirty-source checksum**: `345070572bc6999daebf0b7c6e329b4ad83de6c272e83026327227f459746064` (captured by both macOS and Docker configurations before this evidence file and the T034 checkbox were added)

**Fixed upstream**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823`

## Scope and acceptance boundary

This evidence covers T034 only: the complete macOS Baseline-OFF core/XN regression and the Docker Linux/amd64 Baseline-ON regression spanning Feature 002, XN-Descent, GP-ANN OGP, HNSW-Merger, and `production_surface`. The paper-style case is an existing small partitioned-pipeline test; no standalone paper experiment was launched.

No SIFT1M or larger dataset, external Linux migration, performance evaluation, parameter search, or T035–T042 work was executed. Cross-platform graph-byte equality is not an acceptance condition.

## Source and dependency admission

- Docker image: `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`, inspected as `linux/amd64`.
- nlohmann_json cache commit: `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`; both macOS and Docker cache worktrees were clean before execution.
- GP-ANN repository: `https://github.com/larsgottesbueren/gp-ann.git`, branch `main`, commit `df5efb4a6b303011c795666ed267f29d994b558c`; the repository and every recursive submodule were clean before and after execution.
- GP-ANN pinned direct dependencies: parlaylib `ea671f6fbae17e6d6cd25fe9b48134d3c0601787`, KaMinPar `a9e4b1f6d92b4110d4d4a31ee2e7dee5a0761b05`, hnswlib `359b2ba87358224963986f709e593d799064ace6`, and message-queue `f9b0856dde02248cc91a63077f7b6d5267004f25`.
- The isolated build produced the official GP-ANN `Partition` executable with SHA-256 `d50ae6100f5a35057f5cc00b2cc2ff9e372592f6f9f1c2d2040310330c6ce99`.
- The fixed DiskANN upstream worktree and gitlink remained clean and unchanged.

## macOS Baseline-OFF regression

Environment:

- macOS 13.2.1 / Darwin 22.3.0, x86_64.
- AppleClang 14.0.0 (`14.0.0.14000029`), CMake 4.4.2, Unix Makefiles.
- Debug flags include `-g`; project targets use C++17 with `-Wall -Wextra -Wpedantic`.
- `BUILD_TESTING=ON`, `DISKANN_CPP_MAIN_BASELINE=OFF`, `DISKANN_XN_DESCENT=ON`, `DISKANN_GP_ANN_OGP=OFF`, `DISKANN_HNSW_MERGER=OFF`, and `DISKANN_SEARCH_EVALUATION=OFF`.

Commands:

```sh
cmake -S . -B build/xn-descent-core-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=OFF \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$PWD/build/xn-descent-core-debug/_deps/nlohmann_json-src"

cmake --build build/xn-descent-core-debug --parallel

ctest --test-dir build/xn-descent-core-debug -N

ctest --test-dir build/xn-descent-core-debug \
  --output-on-failure \
  --no-tests=error
```

CTest listed and ran exactly 33 tests. All passed:

```text
production_target_isolation
public_api_cxx17
checksum_canonicalization
document_codecs
stage_context_and_workspace
registry_selection
compatibility_preflight
valid_artifacts
artifact_contract_amendment
invalid_artifact_metadata
invalid_graph_properties
quarantine_isolation
artifact_reuse_core
adapter_policy
fake_stage_context
fake_stage_contracts
fake_merge_properties
pipeline_orchestration
stage_source_provenance
pipeline_failure_gates
fake_pipeline_e2e
negative_matrix_e2e
parameter_schema_gate
terminal_output_contract
production_surface
xn_descent_parameters_test
xn_descent_equation_test
xn_descent_try_insert_test
xn_descent_round_oracle_test
xn_descent_local_artifact_test
xn_descent_determinism_test
xn_descent_termination_test
xn_descent_resource_test
```

Result: **33/33 passed**, including all eight registered XN tests and `production_surface`.

## Docker Linux/amd64 comprehensive regression

Environment:

- Linux 5.15.49-linuxkit, x86_64, in the pinned `linux/amd64` image above.
- GCC 11.4.0, CMake 3.22.1, Unix Makefiles.
- Release flags include `-O3 -DNDEBUG`; project targets use C++17 with the repository warning flags.
- GP-ANN official Partition uses C++20 with `MIPS_DISTANCE=OFF`, 64-bit KaMinPar weights/edge IDs, and timers disabled.
- `BUILD_TESTING=ON`, `DISKANN_CPP_MAIN_BASELINE=ON`, `DISKANN_XN_DESCENT=ON`, `DISKANN_GP_ANN_OGP=ON`, `DISKANN_HNSW_MERGER=ON`, and `DISKANN_SEARCH_EVALUATION=OFF`.
- Docker network access and image pulls were disabled.

The new build directory was `build/xn-descent-docker-linux-t034`. Configuration used:

```sh
cmake -S . -B build/xn-descent-docker-linux-t034 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=ON \
  -DDISKANN_HNSW_MERGER=ON \
  -DDISKANN_SEARCH_EVALUATION=OFF \
  -DGPANN_SOURCE_DIR=/workspace/build/ogp-partition-adaptation/gp-ann-formal-source \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src
```

The corrected target build executed:

```sh
cmake --build build/xn-descent-docker-linux-t034 --parallel --target \
  xn_descent_experiment_runner \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test \
  xn_descent_determinism_test \
  xn_descent_pipeline_test \
  cpp_main_native_formats \
  cpp_main_source_build_admission \
  cpp_main_link_smoke \
  cpp_main_descriptors_preflight \
  cpp_main_p0 \
  cpp_main_b0 \
  cpp_main_m0 \
  cpp_main_pipeline \
  gp_ann_ogp_partition \
  gp_ann_ogp_pipeline \
  hnsw_merger_m1_semantics \
  hnsw_merger_m1_native_output \
  hnsw_merger_merge_only_execution \
  hnsw_merger_m1_pipeline \
  production_surface
```

Tests were listed and run with:

```sh
ctest --test-dir build/xn-descent-docker-linux-t034 \
  -N \
  -R '^(xn_descent_.*|cpp_main_.*|gp_ann_ogp_.*|hnsw_merger_.*|production_surface)$'

ctest --test-dir build/xn-descent-docker-linux-t034 \
  --output-on-failure \
  --no-tests=error \
  -R '^(xn_descent_.*|cpp_main_.*|gp_ann_ogp_.*|hnsw_merger_.*|production_surface)$'
```

CTest listed and ran exactly 25 tests. All passed:

```text
production_surface
cpp_main_native_formats
cpp_main_source_build_admission
cpp_main_link_smoke
cpp_main_descriptors_preflight
cpp_main_p0
cpp_main_b0
cpp_main_m0
cpp_main_pipeline
gp_ann_ogp_partition
gp_ann_ogp_pipeline
hnsw_merger_m1_semantics
hnsw_merger_m1_native_output
hnsw_merger_merge_only_execution
hnsw_merger_m1_pipeline
xn_descent_parameters_test
xn_descent_equation_test
xn_descent_try_insert_test
xn_descent_round_oracle_test
xn_descent_local_artifact_test
xn_descent_determinism_test
xn_descent_termination_test
xn_descent_resource_test
xn_descent_pipeline_preflight_test
xn_descent_small_pipeline_test
```

Result: **25/25 passed**: XN 10/10, cpp_main 8/8, GP-ANN 2/2, HNSW-Merger 4/4, and `production_surface` 1/1.

## Behavioral acceptance evidence

- The fixed Baseline runner rejected `merge.R=7` before workspace/stage creation, rejected `use_filters=true`, and retained `R=16` with `use_filters=false`. Its normal P0 → B0 → M0 pipeline passed. The B0 regression retained the frozen local-degree bound of 10.
- Reusable M0 accepted `R=7` with a legal degree-10 input and `R=16` with a validated degree-11 input. The `cpp_main.m0.merge_shards` event proved that the fixed upstream `merge_shards` path ran; the output declared the requested global degree and passed the existing Validator.
- The pipeline-compatible XN case ran the complete multi-Partition P0 → B1×N → M0 flow with `lambda=12` and `merge.R=7`.
- The paper-style XN case used the same complete flow with `lambda=64` and `merge.R=9`; it did not use a local-only or fake Merge path.
- In both XN success cases, each produced Partition had exactly one B1 invocation and one validated LocalGraph; every local graph declared `lambda` and observed no more than `lambda`; the validated GlobalGraph declared `merge.R` and observed no more than `merge.R`.
- Missing, boolean, string, fractional, negative, zero, and greater-than-`UINT32_MAX` `merge.R` values were rejected with a `merge.R` reason before factory creation or P0/B1/M0 invocation; all factory and stage counts remained zero.
- A missing output after one successful B1 left a partial LocalGraph set and did not invoke M0. A corrupted LocalGraph was quarantined and did not invoke M0. Neither failure published a GlobalGraph.
- Successful XN results remained scoped to `component-level_faithful_adaptation` and explicitly did not claim complete XN-Graph system reproduction.
- GP-ANN tests admitted and executed the pinned official `Partition` binary, then passed its real small composed pipeline. HNSW-Merger's semantics, native output, merge-only execution, and composed pipeline regressions all passed. `production_surface` also passed.

## Failures, retries, and unexecuted validation

- The first Docker configuration succeeded and the build successfully reached the GP-ANN pipeline target, but the submitted target list accidentally contained a nonexistent `reti` token. `gmake` stopped with `No rule to make target 'reti'` before the HNSW targets and before any test execution. No source or build configuration was changed.
- One attempted follow-up host command was malformed with a `Nana?` shell token and was rejected by zsh before Docker started.
- The correct target list was then built in the same configured T034 directory. All requested targets completed, after which the exact 25-test listing and execution above passed. These command-entry failures were operational, not source, dependency, compile, or test failures.
- Not executed: T035–T042, SIFT1M or larger datasets, external Linux migration, paper-scale experiments, search evaluation, cross-platform payload comparison, commit, or push.
