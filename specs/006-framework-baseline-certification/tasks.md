# Tasks: Framework Baseline Certification

**Input**: Approved Phase 0/1 design in `specs/006-framework-baseline-certification/`.

**Authority**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `quickstart.md`,
`contracts/`, `.specify/memory/constitution.md`, and repository `AGENTS.md`.

**Test-first rule**: Every behavior, contract, and integration test task must be completed as a red
test before its coupled implementation task. Only deterministic or small native fixtures are
permitted during implementation. A compile result alone does not complete a task.

**Scope guard**: All writes and builds remain outside `upstream/diskann-cpp-main/` and leave its
fixed `cpp_main` commit `78256bbab4685e1774e78d331e081a153be26823` clean. GlobalGraphArtifact/v1,
Feature 002's frozen Policy/helper/report, Feature 005's search/evaluation contracts and metric
semantics, and DiskLayoutArtifact/v1's schema/version remain unchanged. No task adds a Pipeline
stage, Registry entry, Adapter, backend, MetricPlugin, public certification system, or performance
certification.

## Format: `[ID] [P?] [Story] Description`

- **[P]** marks only work with no unfinished dependency and no overlapping files.
- **[US1]**, **[US2]**, and **[US3]** are the only User Stories from `spec.md`.
- Setup, Foundational, and Final tasks intentionally have no User Story label.

---

## Phase 1: Setup — Identity and Existing Entry Points

**Purpose**: Establish the real build, test, contract, and read-only Baseline surfaces without
creating implementation placeholders.

- [X] T001 Verify the active Feature, Project/upstream identity, protected workspace files, and out-of-tree build/test entry points in `.specify/feature.json`, `CMakeLists.txt`, `cmake/CppMainBaseline.cmake`, `src/baseline/cpp_main/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/cpp/CMakeLists.txt`, `tests/cpp/baseline/CMakeLists.txt`, and `tests/cpp/search/CMakeLists.txt`
  - **Work**: require branch `feature/framework-baseline-certification`; record the actual Project
    commit/dirty state; require upstream branch `cpp_main`, exact commit
    `78256bbab4685e1774e78d331e081a153be26823`, and clean state; confirm all future builds use
    `build/framework-baseline-certification/`; confirm `README.md` and
    `docs/architecture/codebase-structure-and-design.md` are user-owned and out of scope. Inspect,
    but do not modify, the fixed upstream source and the existing Feature 002/005 entry points.
  - **Complete when**: the active Feature path and exact writable/test files are recorded, the
    upstream source is proven read-only/clean, and no source, build, test, or data operation has run.

---

## Phase 2: Foundational — Bounded Feature-Local Document Primitives

**Purpose**: Establish only the shared Feature 006-local schema admission and immutable output
behavior used by all three stories.

**CRITICAL**: T002 must expose missing behavior before T003 implements it. These helpers remain in
one Feature-local module and are not a public certification or validation framework.

- [X] T002 Add failing tests for Feature 006-local schema loading, canonical checksum verification, path confinement, exclusive-create persistence, and the six-operation CLI boundary in `tests/python/test_contract_schemas.py` and `tests/python/test_framework_baseline_certification.py`
  - **Test first**: cover Draft 2020-12 legality for all five Feature 006 design schemas, of which
    GlobalGraphArtifact/v2 is the future public authoritative source and the other four are
    Feature-local schemas; cover strict unknown-field rejection; re-read plus schema/checksum
    verification; rejection of path escape or pre-existing output; and exactly
    `reference-preflight`, `reference-execute`,
    `derive-structural`, `freeze-quality-policy`, `compare-quality`, and `decide`, with no generic
    `validate`, `certify`, `protocol`, or Pipeline-stage command.
  - **Complete when**: the tests fail only because the bounded Feature 006 module does not yet
    provide these primitives; no implementation is added in this task.

- [X] T003 Implement the bounded shared document/CLI primitives in `harness/python/diskann_harness/framework_baseline_certification.py`
  - **Depends on**: T002 red tests.
  - **Implementation**: load only the schemas in
    `specs/006-framework-baseline-certification/contracts/`; reuse existing canonical SHA-256,
    schema, path-confinement, source-identity, and immutable-write helpers where available; expose
    only the six approved operations; and keep Artifact, BuildManifest, ValidationEvidence,
    structural report, quality evidence, and decision as independent documents.
  - **Complete when**: T002 passes, invalid or existing outputs fail closed, and no public schema,
    Registry, generic validator, certification ID, or claim-eligibility abstraction is introduced.

**Checkpoint**: The Feature-local document boundary is ready; it does not yet capture, compare, or
decide anything.

---

## Phase 3: User Story 1 — Capture a Complete Native Reference Graph (Priority: P1) — MVP

**Goal**: Represent and capture the fixed native sharded final graph truthfully as a validated
GlobalGraphArtifact/v2, without modifying v1, upstream, or requiring invented intermediate lineage.

**Independent Test**: A deterministic Linux-native child fixture creates the exact final graph and
medoids paths; the project-owned observer retains the final graph with a same-filesystem hard link,
publishes separate v2 Artifact/BuildManifest/ValidationEvidence, validates both native payloads,
and allows DiskLayoutArtifact/v1 materialization/load while a v1 Framework fixture remains valid.

### Tests for User Story 1

- [X] T004 [P] [US1] Add failing additive source-authority and schema tests for GlobalGraphArtifact/v2 in `tests/python/test_contract_source_sync.py` and `tests/python/test_contract_schemas.py`
  - **Test first**: require one new target
    `contracts/v1/schemas/global-graph-artifact-v2.schema.json` whose sole authoritative source is
    `specs/006-framework-baseline-certification/contracts/global-graph-artifact-v2.schema.json`;
    preserve the existing v1 source mapping byte-for-byte; preserve unique targets, path safety,
    file existence, byte identity, and source SHA-256 checks; validate the v2 schema and reject
    wrong version, lineage mode, source cardinality/type, payload roles/media types, and additional
    fields.
  - **Complete when**: the new cases fail because the v2 runtime target/source mapping is absent,
    while existing v1 and Feature 005 source-authority assertions remain unchanged and passing.

- [X] T005 [P] [US1] Add failing version-dispatch, v1 regression, v2 semantic, and native graph/medoids validator cases in `tests/cpp/contract/test_valid_artifacts.cpp`, `tests/cpp/contract/test_invalid_artifact_metadata.cpp`, and `tests/cpp/baseline/test_cpp_main_native_formats.cpp`
  - **Test first**: retain the existing valid v1 Partition plus LocalGraph lineage path; accept one
    validated Dataset source for `direct_native_final` v2; and reject missing/extra/wrong sources,
    Dataset mismatch, payload byte-length/SHA/aggregate mismatch, node-count mismatch, out-of-range
    IDs, self edges, degree overflow, truncated/trailing graph records, observed-degree mismatch,
    malformed medoids, wrong medoid count, and entry point absent from medoids. V1-as-v2,
    v2-as-v1, and unsupported versions must fail closed.
  - **Complete when**: valid v1 still passes, v2 positive and negative tests fail only for missing
    version-aware runtime support, and no duplicate native parser is added.

- [X] T006 [P] [US1] Add failing native Reference request/capture lifecycle tests in `tests/cpp/baseline/test_native_reference_capture.cpp`, `tests/cpp/baseline/CMakeLists.txt`, and `tests/python/test_framework_baseline_certification.py`
  - **Test first**: cover clean Project/upstream/Dataset/profile/binary/toolchain/resource admission;
    an immutable exact request; observer-before-launch ordering; exact `_mem.index` event; exclusive
    same-filesystem hard-link retention; no copy or symlink fallback; child unlink followed by
    retained-inode validation; medoids retention; actual `started_at`/`ended_at`; and separate
    Artifact, terminal BuildManifest, and ValidationEvidence. Record source payload existence,
    byte length, and SHA-256 before/after capture and validation.
  - **Failure cases**: nonzero/cancelled child, missed or unexpected event, existing capture path,
    hard-link failure, invalid graph/medoids/checksum, and publication failure retain terminal
    evidence, publish no normal Artifact, clean only operation-owned partial output, never modify
    the source Artifact, and never retry. The exact-filename observer test is Linux-only; no
    non-Linux substitute may claim native capture acceptance.
  - **Complete when**: the test target is registered, its behavior cases fail for the missing
    capture owner/orchestrator, and no upstream file is created or modified.

- [X] T007 [P] [US1] Add failing DiskLayoutArtifact/v1 compatibility tests for validated GlobalGraph v1 and v2 inputs in `tests/cpp/search/test_post_graph_materialization.cpp` and `tests/cpp/search/search_artifact_test_support.hpp`
  - **Test first**: prove the fixed descriptor admits exactly validated `global-graph-artifact/v1`
    and `global-graph-artifact/v2`; v2 Dataset/graph/medoids/entry identities remain intact; both
    paths produce distinct valid DiskLayoutArtifact/v1 and pass the existing PQFlash load probe;
    and unsupported, unvalidated, or lineage-mismatched graph versions fail before materialization.
    Preserve float32, `pq_disk_bytes=0`, `reorder=false`, labels-disabled, and `end_to_end` semantics.
  - **Complete when**: v1 remains green and v2 fails only at the current fixed-descriptor version
    admission; no DiskLayout schema or format version is changed.

### Implementation for User Story 1

- [X] T008 [US1] Publish and admit GlobalGraphArtifact/v2 through the existing source-sync, schema catalog, structural-shape, and semantic validation path in `contracts/v1/SOURCES.json`, `contracts/v1/schemas/global-graph-artifact-v2.schema.json`, `src/validation/schema_catalog.cpp`, `src/validation/validation_internal.hpp`, `src/validation/validation_gate.cpp`, and `src/validation/artifact_validator.cpp`
  - **Depends on**: T004 and T005 red tests.
  - **Implementation**: copy the Feature 006 source byte-identically to the new runtime target; add
    one source entry and one version-aware catalog rule; dispatch v1 to its unchanged
    Partition/LocalGraph branch and v2 to an explicit `direct_native_final` branch. Reuse
    `src/baseline/cpp_main/native_formats.cpp` helpers for the complete graph and medoids scans,
    verify exact Dataset source/identity, entry-point membership, canonical aggregate checksum,
    capabilities, and all schema/native constraints. Do not modify
    `contracts/v1/schemas/global-graph-artifact.schema.json` or its existing source mapping.
  - **Complete when**: all T004/T005 positive/negative cases pass, v1 behavior is byte- and
    semantics-compatible, and v2 cannot enter downstream use without normal Validator and
    ValidationEvidence admission.

- [X] T009 [P] [US1] Extend only the fixed DiskLayout descriptor's accepted GlobalGraph versions in `src/search/diskann/fixed_descriptors.cpp`
  - **Depends on**: T007 red tests and T008 validated v2 support.
  - **Implementation**: accept validated `global-graph-artifact/v1` and
    `global-graph-artifact/v2` for the existing `global_graph` source slot; preserve ordered
    Dataset/GlobalGraph sources and every existing materialization, entry, PQFlash, and output-v1
    rule.
  - **Complete when**: T007 passes for both graph versions, unsupported/unvalidated inputs fail
    closed, and no DiskLayoutArtifact/v2, Adapter, Registry, or alternate materializer is added.

- [X] T010 [P] [US1] Implement the Linux-only project-owned native final-graph capture owner in `src/baseline/cpp_main/native_reference_capture.hpp`, `src/baseline/cpp_main/native_reference_capture.cpp`, `src/baseline/cpp_main/native_reference_capture_runner.cpp`, and `src/baseline/cpp_main/CMakeLists.txt`
  - **Depends on**: T006 red tests and T008 validated v2 support.
  - **Implementation**: parse the immutable request; bind the fixed upstream role and binary;
    pre-arm exact-filename inotify; launch shell-free argv once; exclusive-create the hard link;
    retain medoids; wait for child/observer; validate and publish v2 only after success; and write the
    sole terminal BuildManifest/v1 (`stage=build`) plus separate ValidationEvidence. Preserve stdout,
    stderr, exit/cancellation, actual timestamps, source/binary/toolchain/profile/Dataset/resources,
    request SHA, output references, and failure reason. Compile/register this owner only for its
    supported Linux surface; no non-Linux capture fallback is permitted.
  - **Complete when**: T006's native lifecycle/failure/cleanup/source-immutability cases pass and
    the owner never changes upstream, invokes Framework P0/B0/M0, reconstructs intermediate
    Artifacts, falls back to copy/symlink, or retries.

- [X] T011 [US1] Implement `reference-preflight` and `reference-execute` orchestration in `harness/python/diskann_harness/framework_baseline_certification.py`
  - **Depends on**: T003, T008, and T010.
  - **Implementation**: normalize one exact
    `specs/006-framework-baseline-certification/contracts/native-reference-build-request.schema.json`
    request with `execution_authorized=false`; fresh-validate Dataset/Evidence; admit exact clean
    Project/upstream/source/profile/native-binary/toolchain/resources; prove confined same-filesystem
    hard-link capability; reject all pre-existing request/output/log/evidence paths; exclusive-create,
    re-read, schema-check, and hash the request; and stop. `reference-execute` requires external
    approval of those exact bytes and dispatches the C++ capture owner once with shell-free argv; it
    does not duplicate the owner's terminal Manifest or Evidence.
  - **Complete when**: request/lifecycle tests pass for success and every fail-closed case, fixture
    execution produces a validated v2 Artifact with independent terminal records, and no formal
    SIFT1M request or operation is generated or executed by `/speckit-implement`.

- [X] T012 [US1] Complete the independent small-fixture US1 validation using `build/framework-baseline-certification/`, `tests/cpp/baseline/test_native_reference_capture.cpp`, `tests/cpp/baseline/test_cpp_main_native_formats.cpp`, `tests/cpp/contract/test_valid_artifacts.cpp`, `tests/cpp/contract/test_invalid_artifact_metadata.cpp`, `tests/cpp/search/test_post_graph_materialization.cpp`, `tests/python/test_contract_schemas.py`, `tests/python/test_contract_source_sync.py`, and `tests/python/test_framework_baseline_certification.py`
  - **Depends on**: T008–T011.
  - **Minimum validation**: build only the affected out-of-tree targets; run the v1/v2 schema,
    source-sync, catalog/validator/native-format, Linux capture, and DiskLayout compatibility tests.
    The Linux capture test must actually PASS on an applicable Linux environment for native-capture
    acceptance; otherwise record it as pending and do not claim US1 native acceptance.
  - **Complete when**: the small Reference graph is captured and validated through v2, v1 remains
    fully compatible, both graph versions can feed DiskLayout/v1, source payloads are unchanged,
    all failed outputs remain unpublished with terminal evidence, and no formal SIFT1M build ran.

**Checkpoint**: US1 is an independently testable MVP: the project can truthfully retain a native
final graph without fabricating P0/B0/M0 intermediates.

---

## Phase 4: User Story 2 — Re-Derive the Frozen Structural Gate (Priority: P2)

**Goal**: Feed admitted Reference-v2 and Framework-v1 evidence to the unchanged Feature 002 Policy
and helper and retain a new Feature 006-owned report snapshot.

**Independent Test**: Deterministic Reference-v2 and Framework-v1 evidence exercises PASS, FAIL,
INCONCLUSIVE, and NOT_REACHED through Feature 002's existing functions and schemas, without writing
Feature 002 or widening the allowed claim.

### Tests for User Story 2

- [X] T013 [US2] Add failing frozen-helper reuse and four-state structural derivation tests in `tests/python/test_framework_baseline_certification.py`
  - **Test first**: import and invoke `load_policy`, `policy_checksum`, `derive_report`, and
    `validate_report` from
    `harness/python/diskann_harness/cpp_main_structural_parity.py`; bind the existing policy at
    `specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.json`; admit exact
    Reference-v2/Framework-v1 Artifact, Manifest, Evidence, role/source/Dataset/profile/control
    identities; cover all four Gate states and invalid/missing/mismatched evidence; and assert that
    PASS means only structural-and-contract parity.
  - **Complete when**: cases fail only because Feature 006 has no `derive-structural` orchestration;
    no Feature 002 file, helper, Policy, schema, threshold, report, or four-state rule changes.

### Implementation for User Story 2

- [X] T014 [US2] Implement Feature 006 `derive-structural` as a thin immutable snapshot owner in `harness/python/diskann_harness/framework_baseline_certification.py`
  - **Depends on**: T013 red tests and T012 admitted graph evidence behavior.
  - **Implementation**: validate exact input references, call the four existing Feature 002 helper
    functions unchanged, exclusive-create the new report outside Feature 002, re-read/validate/hash
    it, retain exclusions/reasons, and expose no graph-equality, search, quality, certification, or
    performance claim.
  - **Complete when**: all T013 states pass with the frozen Policy checksum and exact roles, the
    retained Feature 002 `NOT_REACHED` report is unchanged, and Structural PASS cannot be mistaken
    for Search Runner Parity, Quality Parity, or final certification.

- [X] T015 [US2] Complete the independent US2 checkpoint with `tests/python/test_framework_baseline_certification.py` and `tests/python/test_cpp_main_structural_parity.py`
  - **Depends on**: T014.
  - **Minimum validation**: run only the Feature 006 structural cases plus the existing Feature 002
    regression suite; compare the frozen Policy/helper/schema file hashes before and after.
  - **Complete when**: PASS, FAIL, INCONCLUSIVE, and NOT_REACHED are derived identically by the
    existing helper, all outputs are Feature 006-owned fixture evidence, Feature 002 files are
    unchanged, and no formal Gate derivation or SIFT1M operation ran.

**Checkpoint**: US2 independently proves exact reuse of the frozen structural Gate and its limited
claim boundary.

---

## Phase 5: User Story 3 — Decide Fixed-Scope Framework Certification (Priority: P3)

**Goal**: Freeze the Feature-local quality rule, admit two graph-specific Feature 005 report sets,
derive all Quality Parity findings, and map complete evidence to CERTIFIED, NOT_CERTIFIED, or
INCONCLUSIVE without a second Recall evaluator.

**Independent Test**: Deterministic two-graph Feature 005-compatible fixtures bind separate graph,
layout, protocol, observation, and report identities; shared byte-identical PQ, QuerySet, and exact
GroundTruth; exact runner-parity identity; all cache/L aggregates; exclusions; and pure decision
precedence.

### Tests for User Story 3

- [X] T016 [US3] Add strict schema tests for the Feature-local Quality Parity Policy, Quality Parity Evidence, and Certification Decision in `tests/python/test_contract_schemas.py`
  - **Depends on**: T012 and T015, so all earlier edits to the shared contract and Feature 006 test
    files are complete before US3 test editing starts.
  - **Test first**: validate
    `specs/006-framework-baseline-certification/contracts/quality-parity-policy.schema.json`,
    `quality-parity-evidence.schema.json`, and
    `framework-baseline-certification-decision.schema.json`; reject unknown fields, invalid enums,
    stale/missing identity references, invalid checksum shapes, absent exclusions/reasons, and any
    attempt to encode certification in a public Artifact. Confirm these schemas remain Feature-local
    and absent from `contracts/v1/SOURCES.json`.
  - **Complete when**: strict positive/negative schema fixtures exist before comparator/decision
    implementation and no public certification contract is created.

- [X] T017 [US3] Add failing controlled two-graph admission, Quality Parity, runner-identity, and final-decision tests in `tests/python/test_framework_baseline_certification.py`
  - **Depends on**: T016; US3 shared-test edits proceed in task order.
  - **Test first — controlled inputs**: require distinct Reference-v2/Framework-v1 graph,
    DiskLayout, Protocol, Observation, and SearchEvaluationReport identities; the same byte-identical
    PQArtifact, QuerySetReference, exact GroundTruthReference, K/L/beamwidth/threads/IO limit/cache/
    repetitions/query-order/timing/evaluator controls; and complete audit references. Do not compare
    graph bytes, adjacency, ordered result IDs, or distances across the two graphs.
  - **Test first — real Feature 005 fixture path**: use only small fixtures and actually reuse the
    existing Feature 005 fixture `preflight --mode formal-search`, SearchObservation, and evaluator
    paths for both roles. Reference and Framework must each produce one zero-cache and one
    fixed-cache SearchProtocol plus one zero-cache and one fixed-cache SearchEvaluationReport: four
    independent Protocols and four independent reports in total. Both roles share the byte-identical
    PQArtifact, QuerySetReference, and GroundTruthReference and retain identical search controls.
    Feature 006 may consume only those generated and validated reports; it must not substitute
    hand-authored reports, calculate Recall itself, or run formal SIFT1M.
  - **Test first — exact parity admission**: PASS is reusable only for Project commit
    `10f8520d8617fdaf8de3abf8a87842346ae4c96f`, runner SHA-256
    `97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`,
    implementation `cpp_main_pqflash_search_instrumented_v1`, and DiskANN commit
    `78256bbab4685e1774e78d331e081a153be26823`; any source, binary, implementation, or upstream
    mismatch is stale/missing evidence unless an exact-current conclusive FAIL is supplied.
  - **Test first — quality and decision**: convert each Feature 005 Recall from `[0,100]` to `[0,1]`;
    compare Recall@10 and Recall@100 independently at every declared zero/fixed-cache × L point;
    pass equality, Framework improvement, and exactly `0.010` shortfall; fail greater shortfall; and
    require all-point conjunction with no averaging, compensation, best-point selection, or
    replacement repetition. Cover missing aggregate, insufficient eligible repetitions, exclusions,
    invalid/mismatched reports, exact-current parity/admission FAIL, all three final states, and
    Structural Gate precedence. Informational QPS/latency/Mean IOs never affect a finding.
  - **Complete when**: tests fail only because `freeze-quality-policy`, `compare-quality`, and
    `decide` are not implemented; Feature 005 remains the sole Recall/evaluator owner.

### Implementation for User Story 3

- [X] T018 [US3] Implement `freeze-quality-policy`, `compare-quality`, and `decide` in `harness/python/diskann_harness/framework_baseline_certification.py`
  - **Depends on**: T016 and T017 red tests; T014 supplies the structural input boundary.
  - **Implementation**: freeze exact graph/layout/shared-input/protocol/control/evaluator and runner
    identities before fixture search; admit existing Feature 005 SearchEvaluationReports and every
    attempted Observation/audit exclusion; use only existing quality-eligible/median-eligible Recall
    aggregates; divide percent values by 100; derive every point/metric finding with
    `reference - framework <= 0.010`; conjoin all findings; and exclusive-create the local evidence.
    Apply the frozen final precedence: Structural FAIL or exact-current mandatory negative evidence
    yields NOT_CERTIFIED; Structural INCONCLUSIVE/NOT_REACHED or missing/stale/mismatched/invalid/
    incomplete/ambiguous mandatory evidence yields INCONCLUSIVE; Structural PASS plus every other
    mandatory PASS yields CERTIFIED. Preserve all exclusions and explicit non-claims.
  - **Complete when**: T016/T017 pass, output checksums are reproducible, informational performance
    is ignored, no Recall is recomputed from raw results, and no state is written into a graph, PQ,
    DiskLayout, Feature 005 report, or public subsystem.

- [X] T019 [US3] Complete the independent US3 fixture checkpoint with `tests/python/test_framework_baseline_certification.py`, `tests/python/test_diskann_search_evaluation.py`, and `tests/python/test_contract_schemas.py`
  - **Depends on**: T018.
  - **Minimum validation**: run the deterministic small two-graph integration through the existing
    Feature 005 fixture formal-search preflight, SearchObservation, and evaluator paths; verify four
    independent Protocols and four independently generated/validated EvaluationReports; then run
    the Feature 006 comparison/decision fixtures, applicable Feature 005 evaluator/report regression
    suite with formal SIFT1M paths skipped, and local schema tests. Verify Feature 005
    source/schema/helper file hashes do not change.
  - **Complete when**: every threshold boundary, all-point rule, missing/excluded evidence case,
    exact parity identity, and three-state precedence passes; Reference and Framework identities stay
    separate except for shared byte-identical PQ/QuerySet/GroundTruth; the Feature 006 comparator
    consumes only the four generated Feature 005 reports and never manually constructs a report or
    computes Recall; and no formal SIFT1M search or certification derivation ran.

**Checkpoint**: US3 independently derives the fixed-scope decision from admitted fixture evidence;
it does not certify any real SIFT1M candidate during implementation.

---

## Phase 6: Final Validation and Documentation Synchronization

**Purpose**: Re-run all directly affected fixture/regression surfaces, audit boundaries, and leave
the implementation at the formal manual-execution boundary.

- [X] T020 Run the complete applicable Feature 006 static, unit, contract, native-fixture, integration, and regression validation using `build/framework-baseline-certification/`, `tests/cpp/baseline/test_native_reference_capture.cpp`, `tests/cpp/baseline/test_cpp_main_native_formats.cpp`, `tests/cpp/contract/test_valid_artifacts.cpp`, `tests/cpp/contract/test_invalid_artifact_metadata.cpp`, `tests/cpp/search/test_post_graph_materialization.cpp`, `tests/python/test_contract_schemas.py`, `tests/python/test_contract_source_sync.py`, `tests/python/test_cpp_main_structural_parity.py`, `tests/python/test_diskann_search_evaluation.py`, and `tests/python/test_framework_baseline_certification.py`
  - **Depends on**: T012, T015, and T019.
  - **Validation**: configure/build out of tree; run only the affected CTest targets and Python
    suites; run `PYTHONDONTWRITEBYTECODE=1 .venv/bin/python -m py_compile
    harness/python/diskann_harness/framework_baseline_certification.py
    tests/python/test_framework_baseline_certification.py`; validate every Feature 006 and new public
    JSON Schema with Draft 2020-12; run source-sync; and run `git diff --check`. Do not enable any
    formal SIFT1M or performance opt-in path.
  - **Complete when**: every applicable test passes; any Linux-only test not executable on the
    current platform is explicitly pending and prevents the corresponding Linux native-acceptance
    claim; v1/Feature 002/Feature 005 regressions remain green; and no formal operation occurred.

- [X] T021 Synchronize implementation-only usage notes and complete the final provenance/scope audit in `specs/006-framework-baseline-certification/quickstart.md` and `specs/006-framework-baseline-certification/tasks.md`
  - **Depends on**: T020.
  - **Work**: update only commands or paths proven by implementation; record executed, failed, and
    applicable-unexecuted validation; audit the additive v2 source mapping, v1 byte/source identity,
    exact Search Runner Parity admission tuple, Feature 002/005 unchanged hashes, Artifact/Manifest/
    Evidence/report/decision separation, Project status, upstream branch/commit/clean state, and
    absence of production P/B/M or formal outputs. Do not add results to `spec.md`, `plan.md`,
    `research.md`, Feature 001–005, `README.md`, or
    `docs/architecture/codebase-structure-and-design.md`.
  - **Complete when**: every completed T001–T021 checkbox is evidence-backed, `git diff --check`
    passes, all remaining Linux/formal prerequisites are reported truthfully, and
    `/speckit-implement` stops before the boundary below.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (T001)**: starts immediately.
- **Foundational (T002–T003)**: depends on T001 and blocks all story implementation.
- **US1 (T004–T012)**: depends on T003; T004–T007 are test-first tasks; T008–T011 implement their
  coupled behavior; T012 closes the small-fixture MVP.
- **US2 (T013–T015)**: depends on T003 for its local document boundary; its fixture can be authored
  independently, but completion requires US1's validated Reference-v2 evidence shape.
- **US3 (T016–T019)**: depends on T012 and T015; shared test-file edits and the controlled two-graph
  fixture proceed serially as T016 -> T017 -> T018 -> T019.
- **Final (T020–T021)**: depends on all three story checkpoints.

### Dependency Mainline

```text
T001 -> T002 -> T003
T003 -> T004 + T005 -> T008
T003 -> T007
T007 + T008 -> T009
T003 -> T006
T006 + T008 -> T010
T003 + T008 + T010 -> T011
T008 + T009 + T010 + T011 -> T012
T003 -> T013
T013 + T012 -> T014 -> T015
T012 + T015 -> T016 -> T017 -> T018 -> T019
T012 + T015 + T019 -> T020 -> T021 -> non-task manual boundary
```

The T012 join represents its explicit dependency on every task from T008 through T011. T010 also
requires T006, T011 also requires T003, and T009 requires both T007 and T008, as shown above.

### Parallel Opportunities

- T004, T005, T006, and T007 affect distinct test surfaces and may run in parallel after T003.
- T009 and T010 affect distinct production files and may run in parallel after their explicit
  v2/test dependencies are complete.
- US3 test edits are deliberately serialized after T012 and T015 because T016/T017 share test files
  with the completed US1/US2 work.
- No other task is marked `[P]`; in particular, US3 tests and shared edits to
  `framework_baseline_certification.py`, public validation files, or completion records are
  serialized.

---

## Independent Completion Criteria

### US1 — Native Reference capture (MVP)

A small native fixture produces a valid GlobalGraphArtifact/v2 with exact final graph/medoids,
separate terminal Manifest/Evidence, complete direct-native lineage, unchanged source payloads, and
DiskLayoutArtifact/v1 compatibility; v1 remains unchanged and valid. No formal SIFT1M native build
is required or permitted.

### US2 — Frozen Structural Gate reuse

Reference-v2 and Framework-v1 fixture evidence drives Feature 002's unchanged helper through all
four states, stores a new Feature 006 report snapshot, and limits PASS to structural-and-contract
parity only.

### US3 — Fixed-scope certification decision

Two controlled graph-role fixtures share PQ/QuerySet/GroundTruth and controls, admit the exact
Search Runner Parity identity, apply 0.010 to both normalized Recall metrics at every cache/L point,
retain exclusions, ignore informational performance, and deterministically produce the correct
CERTIFIED/NOT_CERTIFIED/INCONCLUSIVE fixture decision.

### Suggested MVP

Complete T001–T012 only. This delivers the truthful native final-graph retention and validation
boundary needed to unblock later structural and quality evidence without running formal SIFT1M.

---

## Non-Task Manual Execution Boundary

**STOP**: No checkbox task exists after this boundary. `/speckit-implement` must stop here even when
all T001–T021 tasks and fixture validations pass. A successful preflight, test, or pure fixture
decision is not execution approval.

All ten items remain outside `/speckit-implement` and cannot be automatically executed or marked
complete. The formal data operations in items 1, 3–8, and 10 require exact immutable requests and
separate human approval. The offline evidence derivations in items 2 and 9 must instead be
explicitly triggered by a human after this boundary and bind exact immutable evidence through their
existing Feature-local input/output contracts; they do not require, and must not introduce, a new
execution-request schema or authorization subsystem.

1. Formal SIFT1M native Reference build and final GlobalGraph/v2 capture.
2. Formal re-derivation of the Structural/Contract Gate from the Reference-v2 and Framework-v1
   evidence with the frozen Feature 002 Policy/helper.
3. Formal Reference DiskLayoutArtifact/v1 materialization.
4. Any Framework materialization not admitted for exact reuse.

**Mandatory sequencing gate before items 5–8**: After items 1–4 establish or exactly admit the
Reference/Framework inputs, the Structural/Contract Gate, and both graph-specific DiskLayouts, the
existing Feature 005 `preflight --mode formal-search` must run separately for the Reference and
Framework roles. It must generate and re-validate four exact immutable protocols: Reference
zero-cache, Reference fixed-cache, Framework zero-cache, and Framework fixed-cache. Before any of
items 5–8 starts, a human must explicitly trigger the existing Feature 006
`freeze-quality-policy` offline evidence operation. The frozen QualityParityPolicy must bind all
four Protocol paths, file SHAs, and protocol identities; both graphs and both DiskLayouts; the shared
byte-identical PQ, QuerySet, and GroundTruth; and the evaluator, cache/L declarations, and every
frozen search control. The policy must be exclusive-created, immediately re-read, and validated; it
must not be created, replaced, or modified after any certification search starts. This operation
uses only the existing Feature-local QualityParityPolicy contract, adds no checkbox task, request
schema, or authorization subsystem, does not itself approve Search, and does not replace the
separate exact-request human approval required for each of items 5–8. Only a successfully frozen
policy permits those four searches to seek execution approval.

5. Formal Reference zero-cache search.
6. Formal Reference fixed-cache search.
7. Formal Framework zero-cache search.
8. Formal Framework fixed-cache search.
9. Formal Quality Parity evidence and final certification-decision derivation from the admitted
   formal reports.
10. Any Linux formal performance experiment, which remains non-certifying and outside Feature 006.

If the formal certification searches do not use Project commit
`10f8520d8617fdaf8de3abf8a87842346ae4c96f`, runner SHA-256
`97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`, implementation
`cpp_main_pqflash_search_instrumented_v1`, and DiskANN commit
`78256bbab4685e1774e78d331e081a153be26823` exactly, the retained Search Runner Parity PASS is not
reusable. That later formal execution must stop with missing/stale parity evidence unless a new,
separately approved exact-current parity result is supplied; these tasks do not modify or rerun the
Search Runner.

---

## Phase 7: Convergence — Formal Reference Binary Provenance Closure

**Purpose**: Close the one implementation-only US1 provenance gap found after T021. This append-only
phase temporarily reopens implementation before returning to the unchanged Non-Task Manual
Execution Boundary above; it authorizes none of that boundary's ten formal operations.

- [ ] T022 [US1] Produce and admit two independent target-specific BuildManifest/v1 documents during the next real clean build lifecycle per US1, FR-010–FR-012, SC-001–SC-002, and the Plan's Compiler/Build Identity and pinned-provenance decisions (partial, HIGH)
  - **Depends on**: T021. The audited `native_reference_capture_runner` SHA-256
    `763ba2ddac4ce1cbcc427d58192146da65a243ddc8b25390a83548f0b01fa57f` and
    `build_disk_index` SHA-256
    `92ec5128f9de48d62d24ec34fb5c70808ac69d8d83a877e1b008d08635400883`
    remain old clean-build audit evidence only and MUST NOT be reused as the formal identities after
    this task changes source.
  - **Test first**: extend `tests/python/test_framework_baseline_certification.py` with a bounded
    clean-build fixture that proves the two fixed targets produce separate schema-valid
    `contracts/v1/schemas/build-manifest.schema.json` documents. Cover the positive
    `reference_preflight()` admission and fail-closed source repository/branch/commit/dirty-state,
    binary path/byte-length/SHA, Manifest file SHA, compiler/CMake/Release/compile/link evidence,
    and toolchain mismatch cases. Prove no Manifest is published before its target reaches a
    terminal build status and that failed target builds retain terminal `FAILED` evidence with
    actual runtime timestamps. Keep the existing six-operation CLI boundary unchanged.
  - **Implement**: add only a Feature 006-private, two-target build-closure helper in
    `harness/python/diskann_harness/framework_baseline_certification.py`. It MUST own the actual
    configure plus individually timed `native_reference_capture_runner` and `build_disk_index`
    target builds, capture real `started_at`/`ended_at`, terminal status, configure/build/link
    commands, compiler/CMake/generator/Release and applicable `-O3`/`-DNDEBUG`/C++17/AVX/OpenMP
    evidence, and actual binary absolute path, byte length, and SHA-256. The capture-owner Manifest
    MUST bind the clean Project repository/branch/commit/dirty state; the `build_disk_index`
    Manifest MUST independently bind the fixed upstream repository/`cpp_main`/commit/clean state.
    Reuse the existing BuildManifest/v1 codec, schema, path confinement, and exclusive-create
    persistence; do not add a schema, public CLI operation, Registry, Pipeline stage, public build
    identity subsystem, or third Manifest for `diskann_pipeline_runner`.
  - **Isolation**: do not change
    `harness/python/diskann_harness/runner.py::build_project()`, upstream targets, or existing callers
    of `src/baseline/cpp_main/source_build_identity.*`. If an additive shared helper becomes strictly
    necessary, preserve every existing API and behavior and add regression coverage in the same
    directly affected test suite before using it. Do not affect Feature 001–005, OGP, HNSW-Merger,
    or other build paths.
  - **Admission**: strengthen only Feature 006 `reference_preflight()` in
    `harness/python/diskann_harness/framework_baseline_certification.py` so each request binary is
    admitted against its own validated Manifest content, exact source role, actual binary identity,
    toolchain/build evidence, terminal `SUCCEEDED` status, and document reference SHA. Missing,
    stale, cross-bound, mismatched, failed, or manually reconstructed evidence MUST fail closed.
  - **Clean-build closure**: after human review and a new clean Project commit, use a new exclusive
    out-of-tree Linux build directory to run only the directly relevant clean configure, two target
    builds, Manifest validation, positive/negative preflight fixture tests, applicable existing
    Feature 006 regressions, `py_compile`, source/schema validation, `git diff --check`, and
    Project/upstream identity audit. The two newly generated binary identities and Manifests must
    come from that same real clean-build lifecycle; do not backfill this task from the audited
    `eba0c2fd8846f0486a0ba2fdee8de9b6acee6ea8` build.
  - **Complete when**: both independent terminal Manifests bind their actual distinct source and
    binary identities plus complete real build evidence, pass the existing schema and Feature 006
    admission, every listed mismatch fails closed, existing build paths remain unchanged, and the
    post-commit clean-build closure passes. No formal Reference request, Reference capture, SIFT1M,
    DiskLayout, Search, Structural/Quality Gate, certification decision, or production P/B/M has
    run. `/speckit-implement` then stops again at the unchanged manual boundary.
