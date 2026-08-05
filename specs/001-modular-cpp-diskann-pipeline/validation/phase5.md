# Phase 5 Convergence Validation Record

**Date**: 2026-08-05  
**Scope**: T080–T091 only.

## Executed checks

```bash
cmake -S . -B build/feature-001 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/feature-001 --parallel 4
ctest --test-dir build/feature-001 --output-on-failure
```

Result: **23/23 CTests passed**.

```bash
.venv/bin/python -m pytest -q
```

Result: **49/49 pytest tests passed**.

Canonical and valid-reuse Harness executions both exited 0 in fresh temporary workspaces. The
focused negative selection passed 4/4 CTests; `negative_matrix_e2e` materialized and executed every
one of the 29 frozen cases. The Harness build command exited 0 and generated a schema-valid terminal
BuildManifest with actual configure/build command provenance.

The contract-source/schema/Assembly/E2E selection passed 8/8. `git diff --check` passed. The upstream
gitlink remained `78256bbab4685e1774e78d331e081a153be26823`, and
`git status --short -- upstream/diskann-cpp-main` remained empty.

## Failed and resolved checks

- Targeted `pipeline_failure_gates` initially failed because the old test injected an unapproved
  `mode` field. It now uses an explicit test-only invalid factory, preserving the frozen parameter
  schema; the rerun passed.
- The first build of the new production Registry test header failed on an include path. The internal
  include was corrected; the full build passed.
- The host `python3` lacked pytest. All required Python validation was executed successfully through
  the repository `.venv`.

No final check is failed.

## Applicable checks not executed

- Linux/GCC was not available on this macOS development host.

## Not applicable

Real cpp_main, a paper implementation, a production Adapter, SIFT1M/SIFT100M, performance/Recall,
Loop Engine, retries, SQLite, parameter matrices, and online updates remain outside Feature 001.
