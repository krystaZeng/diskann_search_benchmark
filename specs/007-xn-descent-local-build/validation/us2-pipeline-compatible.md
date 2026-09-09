# US2 Pipeline-Compatible Validation Evidence

**Feature**: `007-xn-descent-local-build`

**Validation date**: 2026-09-09

**Scope**: T018–T023 only. This evidence covers the component-level P0 → XN B1 → M0 path in
the local Docker Linux/amd64 environment. It does not cover US3, the paper-style local-only path,
external Linux migration, SIFT1M, or complete Feature validation.

## Source, image, and toolchain identity

- Project branch: `feature/xn-descent-local-build`
- Project HEAD: `2d6a01ce2eb2e41ab618da75d90eac8bb498f2fa`
- Configure-time source state: `dirty_worktree=true`
- Configure-time dirty source checksum:
  `b71f4bc40129a056d42a2bd701055e21ae1b3376974e5720ce6005c9e74c3e59`
- Pinned upstream: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
  `78256bbab4685e1774e78d331e081a153be26823`; worktree clean and gitlink unchanged
- Docker image: `diskann-cpp-main-baseline-builder:jammy`, immutable image ID
  `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`
- Container platform: `linux/amd64`; network disabled
- Reused configured build directory: `build/xn-descent-docker-linux-t023-r2`
- Dependency source: existing clean nlohmann checkout at
  `build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src`, commit
  `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`
- CMake 3.22.1 with `Unix Makefiles`; `/usr/bin/c++` GCC 11.4.0
- Build type and effective XN compile flags: Release, `-O3 -DNDEBUG -Wall -Wextra -Wpedantic
  -std=c++17`
- Feature switches: `BUILD_TESTING=ON`, `DISKANN_XN_DESCENT=ON`,
  `DISKANN_CPP_MAIN_BASELINE=ON`; compile definition
  `XN_DESCENT_CPP_MAIN_BASELINE_ENABLED=1`

The existing r2 directory was reconfigured after the convergence and GCC test-source changes so
the compile-time source identity reflects the dirty source that was actually built. The checksum
above predates the later task-checkbox and evidence-file edits.

## Commands

The repository was mounted read-only and only the existing r2 build directory was writable:

```bash
docker run --rm \
  --platform linux/amd64 \
  --pull=never \
  --network=none \
  --mount "type=bind,src=${xn_repo},dst=/workspace,readonly" \
  --mount "type=bind,src=${xn_repo}/build/xn-descent-docker-linux-t023-r2,dst=/workspace/build/xn-descent-docker-linux-t023-r2" \
  --workdir /workspace \
  sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf \
  bash -lc '<configure, build, discovery, and CTest commands below>'
```

Offline reconfigure:

```bash
cmake -S . -B build/xn-descent-docker-linux-t023-r2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src
```

Targeted build:

```bash
cmake --build build/xn-descent-docker-linux-t023-r2 --parallel \
  --target \
  xn_descent_experiment_runner \
  xn_descent_pipeline_test \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test
```

Discovery and execution used the same exact seven-test expression:

```bash
ctest --test-dir build/xn-descent-docker-linux-t023-r2 -N \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|pipeline_preflight|small_pipeline)_test$'

ctest --test-dir build/xn-descent-docker-linux-t023-r2 \
  --output-on-failure \
  --no-tests=error \
  -R '^xn_descent_(parameters|equation|try_insert|round_oracle|local_artifact|pipeline_preflight|small_pipeline)_test$'
```

## Results

Discovery reported exactly seven tests, and execution passed 7/7:

| CTest name | Result | Time |
|---|---:|---:|
| `xn_descent_parameters_test` | PASS | 0.16 s |
| `xn_descent_equation_test` | PASS | 0.04 s |
| `xn_descent_try_insert_test` | PASS | 0.02 s |
| `xn_descent_round_oracle_test` | PASS | 0.03 s |
| `xn_descent_local_artifact_test` | PASS | 0.21 s |
| `xn_descent_pipeline_preflight_test` | PASS | 0.23 s |
| `xn_descent_small_pipeline_test` | PASS | 17.13 s |

The Baseline-ON build compiled both `xn_descent_experiment_runner` and
`xn_descent_pipeline_test` successfully.

## Pipeline and admission evidence

- The descriptor test admitted exactly the fixed P0, XN B1, and fixed M0 identities into the
  Feature-local production-policy Registry and confirmed B0 was absent.
- A pipeline-compatible request with `lambda=11` and a paper-style request attempting Merge were
  both rejected before workspace creation, factory selection, or stage execution. For each request,
  the returned execution counters, serialized result counters, and injected factory counters were
  all `partition=0`, `local_build=0`, `merge=0`.
- A valid pipeline-compatible request with `lambda=4` completed fixed P0 → XN B1 → fixed M0:
  partition ran once, local build ran for every produced partition, and merge ran once.
- The validation evidence contained one valid LocalGraphArtifact/v1 result for every B1 candidate
  before M0. A successful-but-missing candidate and a payload-corrupted candidate both ended as
  `INVALID_OUTPUT`, produced no GlobalGraph Artifact, and kept merge invocations at zero; the
  corrupted candidate was quarantined.
- The serialized successful result asserted `claim_scope=component-level_faithful_adaptation`,
  `replacement_scope=local_build_only`, and `full_xn_graph_system_reproduction=false`.

This evidence does not claim complete XN-Graph, BBP, Secondary Index, IMF Search, SSD layout, or
end-to-end paper reproduction. T024–T042 remain unexecuted, so the complete Feature remains
**未完成验证**.
