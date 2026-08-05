# Feature 001 Prototype Validation Record

**Date**: 2026-08-05  
**Study class**: `infrastructure_validation`  
**Environment**: macOS, AppleClang 14.0.0, CMake 4.3.3, Python 3.12.12

## Executed and passing checks

The results below are the recorded Phase 5 validation evidence from 2026-08-05. They establish the
tested scope of the infrastructure prototype; they do not include or imply execution of T092–T098.

```bash
cmake -S . -B build/feature-001 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/feature-001 --parallel 4
ctest --test-dir build/feature-001 --output-on-failure
```

Result: build passed; **23/23 CTests passed, 0 failed**. The suite includes all Phase 1–4
regressions plus the Phase 5 parameter-Schema gate, terminal-output contract, 29 executed negative
fixtures, actual producer/consumer handoff checks, real production registration assembly, CMake
target isolation, and C++17 public surface.

```bash
.venv/bin/python -m pytest -q
```

Result: **49/49 pytest tests passed, 0 failed**. Coverage includes YAML safety, runtime/design
contract byte identity, build/run document schemas, upstream path guards, all Harness lifecycles,
direct production/Fake runner protocol, graceful and forced subprocess cancellation, four Artifact
validate lifecycles, deterministic mismatch fail-closed behavior, reuse, and temporary-prefix
production installation.

The canonical Fake and valid-reuse quickstarts both exited **0** in fresh workspaces under
`/private/tmp/diskann-feature001-quickstart.BJiKlF/`. The focused negative command passed **4/4**
CTest targets, including all 29 entries in `negative/cases.json`. The Harness build lifecycle also
exited **0** and produced `build/feature-001/build-manifest.json`, validated against the runtime
BuildManifest schema.

Contract source-sync/schema/Assembly/E2E selection: **8/8 passed**. `git diff --check` passed.

## Phase 5 gate evidence

- Workspace, output, source, and build targets inside `upstream/diskann-cpp-main` are rejected before
  mutation by both Harness and direct runner surfaces.
- Every selected parameter schema is resolved from its repository-relative
  `parameter_schema_path`; missing/invalid schemas and invalid stage parameters produce zero factory
  and stage invocations.
- Freshly validated candidates are compared with producer `provides` and next-consumer `requires`
  before publication/invocation; mismatches are quarantined with zero downstream invocations.
- All 29 frozen negative cases execute through case-specific helpers and match their declared
  category and enforcement gate. Applicable candidate failures are quarantined, cancellation never
  masquerades as `INVALID_OUTPUT`, and reuse remains immutable.
- Two successful runs compare payload/aggregate checksums and the complete deterministic findings
  projection, including declared and observed maximum degree. Identity/provenance-only fields remain
  excluded; checksum, projection, or distinct-provenance mismatches retain both runs and fail closed.
- The request-free `registry --result` command works for production and Fake runners. Strict shape,
  governance, unknown-operation, and argv/request mismatch checks happen before workspace mutation.
- `SUCCEEDED` without a candidate is classified as `INVALID_OUTPUT` for all three stages, with
  parseable terminal Manifest/evidence/quarantine records, no normal Artifact, and no downstream
  invocation.

## Failed commands during this convergence run

- One initial targeted CTest exposed a stale test-only `mode: invalid_output` parameter that is
  correctly rejected by the new parameter-Schema gate. The negative tests were moved to explicit
  invalid factories; the rerun and all final suites passed.
- One initial full-build attempt failed because the new internal production Registry header was not
  on the runner include path. The include was corrected to its local path; the complete build and
  final suites passed.
- `python3 -m pytest` used a system interpreter without pytest. The repository's supported `.venv`
  was then used; all pytest selections and the final suite passed.

After the listed corrections, no command in the recorded Phase 5 validation selection remained
failed. No Phase 6 hardening validation was executed.

## Applicable checks not executed

- Linux/GCC was not run on this macOS workstation. Linux remains the normative experiment
  environment; macOS/AppleClang is explicitly supported for Feature 001 development and Fake tests.

## Explicitly not applicable

- Real cpp_main Baseline parity: the pinned Baseline remains `not-executed`; no Baseline Adapter or
  real algorithm is integrated.
- SIFT1M/SIFT100M, ANN performance/Recall, paper reproduction, production Adapter, Loop Engine,
  retries, SQLite state, parameter matrices, checkpoint scheduling, and online mutation are outside
  this Feature and were not executed.

## Known limitations and deferred hardening

- Feature 001 is an infrastructure prototype / contract MVP, not a production-ready scientific
  pipeline.
- The real `cpp_main` Baseline is not integrated or executed. The installed production runner has
  no formal Partition, Local Build, or Merge implementation.
- T092–T098 remain unimplemented and unvalidated. Their gaps and reassessment triggers are recorded
  in [../deferred-hardening.md](../deferred-hardening.md), and none may be reported as complete.
- T092–T098 are no longer prerequisites for starting Feature B. Baseline-related contract and
  lifecycle gaps will be reassessed from the real Baseline integration evidence in Feature B.
- The complete cancellation matrix, additional Fake negative-fixture hardening, and expanded
  Adapter evidence are deferred until a real Baseline or paper implementation demonstrates the
  corresponding need.
- Existing implementation and test assets are retained; this freeze does not delete them or turn
  Fake-only evidence into production evidence.
- Feature 001 produces no Baseline, ANN performance, Recall, paper-reproduction, or comparative
  scientific conclusion.

## Boundary conclusion

The recorded Phase 5 checks support delivery of Feature 001 as a contract MVP for ID, Artifact,
Registry, Validator, Fake Pipeline, and minimum Harness behavior. They do not establish complete
convergence, production readiness, or scientific-pipeline readiness. Development now proceeds to
Feature B without making T092–T098 a launch gate; any selected hardening will be reconsidered using
evidence from real Baseline integration. The pinned upstream Baseline remains read-only and
not-executed in Feature 001.
