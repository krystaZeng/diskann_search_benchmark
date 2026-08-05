# Phase 2 Validation Record

**Date**: 2026-08-05  
**Scope**: T026–T042 only; static Registry, compatibility preflight, Artifact validation,
quarantine, validated reuse, and Adapter policy evidence validation.

## Test-first evidence

After T026–T033 were registered and before T034–T041 existed,
`cmake --build build/feature-001 --parallel` failed as expected because the Phase 2 public headers
and implementations were absent. The failure was limited to the new Phase 2 targets: missing
`validation.hpp`, `compatibility.hpp`, `quarantine.hpp`, `reuse.hpp`, and `adapter_policy.hpp`,
plus the not-yet-implemented Registry operations. Existing Phase 1 targets still built. This was
the intentional red state, not a final validation failure.

## Executed validation

```text
cmake --build build/feature-001 --parallel
```

Result: passed; the core library, runner stub, four Phase 1 tests, and eight Phase 2 tests built.

```text
ctest --test-dir build/feature-001 --output-on-failure -L phase2
```

Result: **8/8 passed, 0 failed**.

```text
ctest --test-dir build/feature-001 --output-on-failure
```

Result: **12/12 passed, 0 failed**: 4 Phase 1 regression tests and 8 Phase 2 tests.

## Gate and failure evidence

- Registry policy: `kProduction` rejected a `test_only` descriptor and retained size zero;
  `kTest` admitted both formal and test-only descriptors. Exact stage/identity resolution,
  enumeration, duplicate rejection, sealing, no overwrite, and no fallback passed.
- Compatibility preflight: Merge declared both PartitionArtifact and LocalGraphArtifact inputs.
  Missing input, unsupported version, wrong ID space, missing capability/service, and unavailable
  checkpoint storage were rejected. For the exercised rejected chain, factory invocation count was
  **0** and stage invocation count was **0**.
- Validator: valid Dataset, aggregate Partition, both owning PartitionId LocalGraphs, and Global
  graph received typed grants. Missing/invalid metadata, payload, checksum, source, ID, map, node
  count, and degree cases received no grant.
- Stable negative categories exercised include `schema_invalid`, `unsupported_version`,
  `path_escape`, `payload_missing`, `payload_length_mismatch`, `checksum_mismatch`,
  `source_mismatch`, `id_space_mismatch`, `id_range_invalid`, `mapping_incomplete`,
  `degree_exceeded`, `node_count_mismatch`, `capability_unsatisfied`,
  `duplicate_implementation`, `implementation_not_found`, `registry_sealed`,
  `reuse_mismatch`, and `checkpoint_unavailable`.
- Quarantine: the candidate was removed from temporary storage, retained with immutable Manifest
  and evidence copies, granted zero capabilities, remained invisible to the normal resolver, and
  was available only through the diagnostic read/delete surface. There is no promotion API.
- Reuse: compatible cached data proceeded only after fresh validation with `REUSED` provenance;
  configuration mismatch and post-validation payload mutation were rejected without mutating the
  cache or invoking a producer.
- Adapter policy: representation-preserving cases and required evidence passed; incomplete trace,
  missing equivalence, missing source checksum, order/content change, overflow, ambiguous mapping,
  replica aggregation, and semantic-change cases were rejected. No Adapter factory, implementation,
  or production registration was created.

## Not executed / not yet applicable

- Phase 3 Fake stage implementations, Fake runner, Harness orchestration, cancellation behavior,
  and stage execution do not exist yet and were not started.
- Final production/Fake CMake target and installation Assembly isolation are **not yet applicable**;
  they remain assigned to T059, T064, and T071.
- Real cpp_main Baseline integration, any real paper implementation, and any production Adapter are
  outside Feature 001 Phase 2 and were not executed.
- SIFT1M/SIFT100M, performance experiments, Loop Engine, retries, SQLite, and checkpoint scheduling
  are outside this phase and were not executed.

No claim about final production/Fake Assembly isolation is made by the Registry enumeration test.
