# T037 native Linux validation

Recorded at 2026-09-16T10:01:58+08:00. Reuses the actual tests executed on 2026-09-15; no tests rerun here.
Source commit: 687017cc3cefefea913267f8eb976c572a13d841; branch codex/xn-remote-linux-prep; source_dirty=0.
Transfer: user-approved GitHub push/fetch into the existing remote repository.
Upstream: https://github.com/microsoft/DiskANN.git, cpp_main,
78256bbab4685e1774e78d331e081a153be26823; clean and unchanged.
Native Release, Baseline=ON, XN=ON. No imported macOS/Docker binary.
Runner SHA-256: 3f59b5e2fe89ddc6731b4863f6a5b755efcefebc0e1fde0ccc19090513f69e28

## Commands and environment

Actual configure/build/test commands, compiler/CMake versions, OS/kernel/architecture,
CPU/RAM and resource samples are retained in the copied session.log.
CMakeCache, complete compile commands, compiler identification, XN flags, runner link command
and runtime dependencies are preserved below with hashes.
Original environment: env -i, LANG=LC_ALL=C.UTF-8, TZ=UTC,
OMP_NUM_THREADS=1, MKL_NUM_THREADS=1; repository .venv/bin plus system executable paths.
Build parallelism: 6. This is distinct from the approved SIFT1M thread budget of 16.

## Executed results

14/14 selected tests passed (5.78 seconds).
Both additional deterministic replays passed (0.09 seconds each).
No selected tests failed or were skipped.
All three executions reported:

XN_US3_DETERMINISM full_state_sha256=3827ea96b8c7d6bdc262ee7041f28b0be0b7c5bac93bf6492169bf55822b89d5 native_payload_sha256=46e4c8d3ca83254ca869a487d2722806dd8f4478fefb61179bbe05863cfb1386 completed_rounds=8 convergence_condition=three_round_c_g_stability effective_workers=1,2,24 source_dirty=0

Actual selected test inventory:

Internal ctest changing into directory: /ssd1/kk/diskann_search_benchmark/build/xn-descent-linux
Test project /ssd1/kk/diskann_search_benchmark/build/xn-descent-linux
  Test #25: production_surface
  Test #26: cpp_main_native_formats
  Test #30: cpp_main_descriptors_preflight
  Test #34: cpp_main_pipeline
  Test #35: xn_descent_parameters_test
  Test #36: xn_descent_equation_test
  Test #37: xn_descent_try_insert_test
  Test #38: xn_descent_round_oracle_test
  Test #39: xn_descent_local_artifact_test
  Test #40: xn_descent_determinism_test
  Test #41: xn_descent_termination_test
  Test #42: xn_descent_resource_test
  Test #43: xn_descent_pipeline_preflight_test
  Test #44: xn_descent_small_pipeline_test

Total Tests: 14

The pinned determinism test asserts normalized C/R/G state, rounds/convergence and
production B1 payload equality across worker variants. Its production fixture is
24 x 4 float32 with one identity Partition map 0..23.
Entry-point equality is asserted for the 1/2-worker production stages; no numeric value
was printed. Fixture checksums were asserted but not separately exported.
The selected XN pipeline tests exercise request_version 1.1.0 small-partition execution
and runner-generated source_config in parent Manifests.
The clean-source determinism path asserts absence of dirty_source_checksum.

## Boundaries

T037 only is complete. Final material-commit rebuild, remote SIFT1M, T041 and T042 are pending.
These results must not be relabeled as tests of a later commit.
macOS evidence remains separate in validation/macos-validation.md:
AppleClang/Debug/Baseline-OFF differs from native Linux Release/Baseline-ON.
No new cross-platform replay comparison was performed or claimed; byte equality is not a gate.
No SIFT1M success or full-paper validation is implied.

## Preserved external evidence and SHA-256

The following ignored files remain on remote Linux. Originals were preserved;
future reconfiguration must not overwrite this snapshot.

- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/session.log: 32a0b49fc5e48ff1cb7ffb73cd0a1cacd48be73b5d51b7fd563bc48fb6abae6b
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/runner-ldd.log: aa5d58e6336de49cc06c7ca2dfcf305ef1130d87be62f387ed84887debdae157
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/test-list.log: 4ca39952ee58525f75fd9336d24c15a4b396b6fc61e72892c391fbe00bfb656d
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/ctest-14.log: e9adef6a2cd9499af2c0cb49dcb8163ddc9ee02d3de83ee0539c37ea0cffecaa
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/replay-1.log: aec1fac7546de9c4c42af34639ca574494df8889bc88b5b6148fc32cd18dcdd2
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/diagnostics/replay-2.log: aec1fac7546de9c4c42af34639ca574494df8889bc88b5b6148fc32cd18dcdd2
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/CMakeCache.txt: 90115b323d80d3355e6da5bc233f7faa3a84c0a81bb172d2c08d3846f7838525
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/compile_commands.json: dad7cec440441bf92f18c5a002d3393546f3882f94e6ea5b55b5312f22fd2dbf
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/src/papers/xn_graph/xn_descent/CMakeFiles/diskann_xn_descent.dir/flags.make: 1e355d46a1d0026627f1adbef948646fe3ec649c721af337151ff3c3467504b5
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/src/papers/xn_graph/xn_descent/CMakeFiles/xn_descent_experiment_runner.dir/link.txt: a015684cc34d0f39ed61ea8f6c61ff43e19210630f6b5b53ad7be1a90d10220d
- build/xn-descent-linux/diagnostics/t037-before-remote-approval/CMakeFiles/3.22.1/CMakeCXXCompiler.cmake: 2b45b213952e8ff33366da3750a3a8ccdc914d32dde4b4cbb2051e959ab192ee
