# Upstream Immutability Evidence

**Date**: 2026-08-05

Before convergence and after the complete Phase 5 build/test/quickstart sequence:

```text
git submodule status -- upstream/diskann-cpp-main
 78256bbab4685e1774e78d331e081a153be26823 upstream/diskann-cpp-main (0.7.0-36-g78256bb)

git status --short -- upstream/diskann-cpp-main
<empty>
```

All CMake output is below `build/feature-001/`; convergence Fake workspaces are below
`/private/tmp/diskann-feature001-quickstart.BJiKlF/` or pytest temporary directories. Harness and
direct-runner regression tests also proved that source/build/workspace/result targets inside the
upstream tree are rejected before mutation. No command configured, built, formatted, tested, or
generated a file inside `upstream/diskann-cpp-main`. The pinned Baseline remains `not-executed`.
