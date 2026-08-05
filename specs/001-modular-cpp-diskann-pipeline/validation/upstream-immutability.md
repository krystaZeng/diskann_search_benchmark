# Upstream Immutability Evidence

**Date**: 2026-08-05

Before Phase 3/4 implementation and after the complete build/test/quickstart sequence:

```text
git submodule status -- upstream/diskann-cpp-main
 78256bbab4685e1774e78d331e081a153be26823 upstream/diskann-cpp-main (0.7.0-36-g78256bb)

git status --short -- upstream/diskann-cpp-main
<empty>
```

All CMake output is below `build/feature-001/`; Fake workspaces are below that build directory or
pytest temporary directories. No command configured, built, formatted, tested, or generated a file
inside `upstream/diskann-cpp-main`. The pinned Baseline remains `not-executed`.

