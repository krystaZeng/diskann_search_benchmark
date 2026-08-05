# BuildManifest Provenance Review

**Date**: 2026-08-05  
**Reviewed set**: the terminal Harness build Manifest plus 16 stage BuildManifests from two retained
canonical runs and two retained Partition-reuse runs.

All reviewed documents validate against `build-manifest.schema.json`.

The Harness build Manifest records:

- repository path, branch `master`, full 40-character commit, dirty-worktree state, and a
  dirty-source checksum computed from exact tracked/untracked evidence;
- actual compiler path/version, CMake version, generator, build type, effective CMake cache
  definitions, and `PROJECT_COMPILE_OPTIONS`;
- the actual configure and build commands as two command records;
- an immutable build configuration with source/build roots, build type, and CMake cache digest;
- original YAML and normalized JSON digests; and
- real build start/end timestamps.

Feature 001 intentionally does **not** collect per-translation-unit compile commands or linker
commands. The schema fields remain arrays for compatibility; the build Manifest stores the two
actual configure/build commands in `compile_commands` and an empty `link_commands` array.

The 16 stage Manifests record the same Harness build toolchain provenance, complete normalized stage
parameters, original/normalized configuration digests, input/output ArtifactRefs, terminal status,
real stage start/end timestamps, validation evidence, implementation identity/version, dataset
identity, and resource budget. Reused Partition Manifests record `REUSED`, exact source Artifact
identity, reason, fresh validation evidence, and zero producer invocation.

The four EvaluationReports record real evaluation-generation timestamps and remain separate from
Artifact metadata, BuildManifests, runner results, and validation evidence. The deterministic
projection includes graph integrity, logical node count, invalid-neighbor count, declared/observed
maximum degree, degree-bound finding, and pass/fail; it excludes report/run identity, paths,
timestamps, provenance, and nondeterministic metrics.

No reviewed document claims that the pinned cpp_main Baseline ran.
