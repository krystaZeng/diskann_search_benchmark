# BuildManifest Provenance Review

**Date**: 2026-08-05  
**Reviewed set**: 16 stage BuildManifests from the two retained canonical runs and two retained
Partition-reuse runs.

All 16 documents validate against `build-manifest.schema.json`. The review found:

- repository path, branch `master`, full 40-character commit
  `aad560e23b6edde9c7b1d4493930c7b463b48475`, dirty-worktree state, and a complete dirty-source
  checksum are present;
- OS `Darwin`, AppleClang path/version, CMake 4.3.3, Unix Makefiles, Debug build type, C++17
  definitions, warning flags, 33 expanded source compile commands, and both runner link commands are
  retained;
- dataset ID, payload checksum, global node count, dimension, scalar type, and L2 metric are bound;
- each stage retains its complete normalized parameters and the memory/thread budget;
- input/output ArtifactRefs, terminal status, start/end timestamps, validation evidence path, and
  implementation identity/version are present;
- reused Partition manifests record `REUSED`, exact source Artifact identity, reason, and fresh
  validation evidence; producer invocation count is zero in the runner result.

The four EvaluationReports, 16 BuildManifests, Artifact metadata/payloads, and validation evidence
remain independent documents. No document claims that the pinned cpp_main Baseline ran.

Because the worktree is intentionally dirty during implementation, the configure step computes the
dirty-source checksum from the tracked binary diff plus exact SHA-256 values of untracked files.
Any later source edit requires reconfiguration before generating new manifests.

