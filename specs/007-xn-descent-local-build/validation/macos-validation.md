# XN-Descent macOS Validation Archive

**Archived on**: 2026-09-11

**Execution reused**: T034 macOS Baseline-OFF comprehensive regression, executed and recorded on
2026-09-11 in `us4-partitioned-merge.md`

**Project HEAD at configuration and execution**:
`175d2bc0899a8040a21862c2c4fbf2f4fbbe6206`

**Project worktree at configuration**: dirty (`dirty_worktree=true`)

**Configured dirty-source checksum**:
`345070572bc6999daebf0b7c6e329b4ad83de6c272e83026327227f459746064`

**Fixed upstream**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`

## Archive decision

T036 reuses the real T034 macOS execution rather than rerunning it. The T034 evidence records the
exact command, environment, complete 33-test list, and result. The retained build's XN compile
definitions still bind the same project HEAD, dirty state, configured checksum, Darwin platform,
and Baseline-OFF boundary. Start-of-T035–T038 content hashes confirmed that runtime source, tests,
CMake, schemas, and the T034 evidence had not changed since that regression; the only subsequent
changes in this work item are documentation, this archive, and task status. A new checksum after
those documentation changes is not substituted for the identity of the earlier test execution.

## Environment and configuration

- macOS 13.2.1 / Darwin 22.3.0, x86_64.
- AppleClang 14.0.0 (`14.0.0.14000029`), CMake 4.4.2, Unix Makefiles.
- Debug (`-g`), C++17, `-Wall -Wextra -Wpedantic`.
- `BUILD_TESTING=ON`.
- `DISKANN_CPP_MAIN_BASELINE=OFF`.
- `DISKANN_XN_DESCENT=ON`.
- `DISKANN_GP_ANN_OGP=OFF`, `DISKANN_HNSW_MERGER=OFF`, and
  `DISKANN_SEARCH_EVALUATION=OFF`.
- Existing clean nlohmann_json FetchContent source; no dependency version change.

Commands executed by T034:

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

## Results

CTest listed and executed exactly 33 tests. Result: **33/33 passed**.

The run included all eight Baseline-OFF XN CTests:

1. `xn_descent_parameters_test`
2. `xn_descent_equation_test`
3. `xn_descent_try_insert_test`
4. `xn_descent_round_oracle_test`
5. `xn_descent_local_artifact_test`
6. `xn_descent_determinism_test`
7. `xn_descent_termination_test`
8. `xn_descent_resource_test`

It also included `production_surface` and all 24 other registered core tests listed in
`us4-partitioned-merge.md`. Build failures: none. Test failures or skips: none.

The build cache records `DISKANN_CPP_MAIN_BASELINE=OFF` and `DISKANN_XN_DESCENT=ON`; therefore the
Baseline-dependent XN runner, the two XN pipeline CTests, and cpp_main pipeline tests were neither
available nor required in this macOS core-only run. No upstream DiskANN target was configured or
built by this path.

## Boundaries and remaining gates

- This archive closes the macOS T036 requirement only; it does not create a new execution result.
- T034's Docker Linux/amd64 regression is separate and does not satisfy T037's required
  user-executed external Linux migration record.
- No SIFT1M, paper-scale experiment, search evaluation, SIFT100M, commit, or push was performed.
- The macOS result is not evidence that macOS and Linux graphs or digests must match.
- The pinned upstream worktree and gitlink were verified clean and unchanged at T034 and again when
  this archive was prepared.

Authoritative full execution evidence:
`specs/007-xn-descent-local-build/validation/us4-partitioned-merge.md`.
