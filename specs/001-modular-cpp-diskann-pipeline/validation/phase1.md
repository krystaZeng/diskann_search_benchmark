# Phase 1 Validation Record

**Date**: 2026-08-05  
**Scope**: T001–T025 only  
**Platform**: macOS, AppleClang 14.0.0, CMake 4.3.3, Python 3.12.12

## Executed checks

1. Configure the out-of-tree C++17 build:

   ```bash
   cmake -S . -B build/feature-001 \
     -DCMAKE_BUILD_TYPE=Debug \
     -DBUILD_TESTING=ON \
     -DCMAKE_CXX_COMPILER=/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
   ```

   Result: passed. CMake found OpenSSL 3.6.0 and materialized the pinned
   `nlohmann/json` v3.11.3 dependency outside `upstream/`.

2. Build all currently defined Phase 1 targets:

   ```bash
   cmake --build build/feature-001 --parallel
   ```

   Result: passed. The C++17 core library, non-operational Phase 1 runner skeleton, and four test
   executables compiled successfully.

3. Run the Phase 1 CTest selection:

   ```bash
   ctest --test-dir build/feature-001 --output-on-failure -L phase1
   ```

   Result: **4 passed, 0 failed**. Passing tests were `public_api_cxx17`,
   `checksum_canonicalization`, `document_codecs`, and `stage_context_and_workspace`.

4. Run the Python contract selection with a supported Python 3.12 environment:

   ```bash
   build/feature-001/pyvenv/bin/python -m pytest \
     tests/python/test_contract_schemas.py \
     tests/python/test_yaml_safety.py \
     tests/python/test_checksum_canonicalization.py \
     tests/python/test_phase1_config_and_workspace.py
   ```

   Result: **23 passed, 0 failed**.

5. Verify `contracts/v1/SOURCES.json` ordering, source SHA-256 values, and byte identity for all
   design/runtime copies.

   Result: passed for **16 entries**: all approved schemas plus
   `checksum-canonicalization.md` and `adapter-policy.md`.

6. Verify the public headers contain neither `std::span` nor `nlohmann` and run
   `git diff --check`.

   Result: passed.

7. Verify the fixed upstream identity and path status:

   ```bash
   git submodule status -- upstream/diskann-cpp-main
   git status --short -- upstream/diskann-cpp-main
   ```

   Result: gitlink remains `78256bbab4685e1774e78d331e081a153be26823`; the upstream path has
   no worktree changes.

## Failed commands

- The first sandboxed CMake configure attempt could not resolve GitHub while materializing the
  pinned nlohmann/json dependency. After dependency-download permission was granted, the same
  configure command completed successfully.
- A Python 3.10 virtual-environment pytest attempt exited with status `139` before collection while
  importing that host interpreter's `readline` extension. This was an interpreter/environment
  crash rather than a project assertion failure. The complete selection subsequently passed under
  Python 3.12.12, which is within the declared Python 3.10+ support range. Python 3.10 execution on
  this host remains unverified.

## Applicable checks not executed

- Linux/GCC validation was not executed; macOS/AppleClang is the Feature-supported development
  platform used for this Phase 1 run.
- Phase 2 Registry, compatibility checker, and Validator tests were not executed because their
  implementations are outside this request and remain unbuilt.
- Fake Pipeline, runner lifecycle, Adapter policy implementation, integration, reuse, quarantine,
  Assembly isolation, and end-to-end tests were not executed because they belong to later phases.
- Baseline parity, SIFT1M, SIFT100M, and performance experiments are not applicable: no real
  DiskANN implementation or algorithm-semantic change was introduced or executed.

## Scope statement

No Registry behavior, capability checker, contract Validator, Fake stage, real DiskANN Adapter,
or experiment execution was implemented. The Registry surface added in Phase 1 is declaration-only.
No dataset was executed, and no file under `upstream/diskann-cpp-main` was changed.

