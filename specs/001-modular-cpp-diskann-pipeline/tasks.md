# Tasks: Modular cpp_main DiskANN Pipeline

**Input**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `contracts/`, and `quickstart.md`
from `specs/001-modular-cpp-diskann-pipeline/`

**Tests**: This Feature changes code, contracts, validators, and Harness logic, so C++ unit,
contract, small-graph property, integration, CTest, and Python pytest coverage is mandatory. Real
DiskANN parity, SIFT1M/SIFT100M, performance experiments, Loop Engine, retries, SQLite, and
checkpoint scheduling are explicitly not applicable.

**Boundary**: Every write/build/test target is outside `upstream/diskann-cpp-main/`; Fake stages are
test-only and never registered as formal experiment implementations.

## Phase 1：项目骨架与核心数据契约

**Goal**: Establish the C++17/CMake and Python 3.10+ skeleton, versioned JSON contracts, immutable
data types, checksum rules, StageContext interfaces, and separated provenance objects.

### Setup and contract tests

- [X] T001 Verify the actual Feature branch identity against branch metadata in `specs/001-modular-cpp-diskann-pipeline/spec.md` and `specs/001-modular-cpp-diskann-pipeline/plan.md`; if a concrete mismatch exists, update only the identified fields and record the old/new values, otherwise mark the task complete without changing metadata
- [X] T002 Create root C++17 library/runner targets and out-of-tree build structure in `CMakeLists.txt`, `include/diskann_pipeline/CMakeLists.txt`, and `src/CMakeLists.txt`
- [X] T003 [P] Configure private nlohmann_json and OpenSSL::Crypto dependencies, strict C++17 properties, warnings, and compile-command export in `cmake/Dependencies.cmake` and `cmake/CompilerOptions.cmake`
- [X] T004 [P] Define the Python 3.10+ Harness package, standard build backend, runtime dependencies, test extra, pytest configuration, and console entry point in `pyproject.toml` and `harness/python/diskann_harness/__init__.py`
- [X] T005 [P] Copy every approved schema, `checksum-canonicalization.md`, and `adapter-policy.md` byte-identically from the authoritative Feature design contracts into `contracts/v1/schemas/`, `contracts/v1/checksum-canonicalization.md`, and `contracts/v1/adapter-policy.md`; generate (do not copy) `contracts/v1/SOURCES.json` with `format_version` and stable-by-`target` entries containing repository-relative POSIX `source`/`target` paths and the SHA-256 of each source file's exact bytes
- [X] T006 [P] Create CTest/pytest discovery and the project-owned C++ assertion helper in `tests/CMakeLists.txt`, `tests/cpp/CMakeLists.txt`, `tests/cpp/support/test_assert.hpp`, and `tests/python/conftest.py`
- [X] T007 Add build, virtual-environment, generated-workspace, and test-output exclusions while preserving experiment records and the upstream gitlink in `.gitignore`
- [X] T008 [P] Add failing Draft 2020-12 metaschema, local `$ref`, strict-field, and design/runtime schema parity tests in `tests/python/test_contract_schemas.py`; before the YAML loader implementation, add `tests/python/test_yaml_safety.py` contract tests for duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation: accept an ordinary top-level mapping containing list, string, boolean, integer, finite float, and null values; reject a duplicate key, top-level sequence, non-string mapping key, Python object tag, custom tag, timestamp/date/datetime, binary/bytes, set, NaN, Infinity, unsafe constructor, and every other non-JSON result type
- [X] T009 [P] Add a failing public-header compile contract that explicitly includes and compiles `include/diskann_pipeline/registry.hpp` alongside the other public headers under C++17; verify `const std::vector<ArtifactRef>&` where applicable, `ArtifactRequirement`, collection-valued `ContractRequirement.input_artifacts`, single-output `ContractProvide`, `RegistryAdmissionPolicy`, explicit non-default `ImplementationRegistry` construction, and no exposed C++20 or nlohmann_json types in `tests/cpp/contract/test_public_api_cxx17.cpp`
- [X] T010 [P] Add failing C++ golden tests for restricted canonical JSON, payload SHA-256, exclusions, and all five aggregate checksums in `tests/cpp/contract/test_checksum_canonicalization.cpp`
- [X] T011 [P] Add failing Python golden tests for byte-identical Artifact identity canonical JSON/SHA-256; reject duplicate keys, invalid UTF-8, and any null/float values that enter the Artifact checksum identity projection, while allowing schema-permitted float/null values in BuildManifest, EvaluationReport, ValidationEvidence, and QuarantineRecord, in `tests/python/test_checksum_canonicalization.py`
- [X] T012 [P] Add failing codec/schema tests that keep Artifact, BuildManifest, EvaluationReport, ValidationEvidence, and QuarantineRecord distinct in `tests/cpp/contract/test_document_codecs.cpp`
- [X] T013 [P] Add failing path/service contract tests for workspace confinement, temporary roots, positive budgets, and unavailable checkpoint semantics in `tests/cpp/unit/test_stage_context_and_workspace.cpp`

### Core contract implementation

- [X] T014 Define GlobalNodeId, LocalNodeId, PartitionId, count bounds, Artifact/ID/stage enums, lifecycle states, stable error categories, and range helpers in `include/diskann_pipeline/identifiers.hpp` and `include/diskann_pipeline/errors.hpp`
- [X] T015 Define immutable ArtifactRef, PayloadRef, DatasetIdentity, source identity, and Dataset/Partition/LocalGraph/GlobalGraph metadata structs in `include/diskann_pipeline/artifact_ref.hpp` and `include/diskann_pipeline/artifacts.hpp`
- [X] T016 Define separate BuildManifest, EvaluationReport, ValidationEvidence, QuarantineRecord, and lifecycle data structures in `include/diskann_pipeline/provenance.hpp` and `include/diskann_pipeline/evaluation.hpp`
- [X] T017 Define C++17-only WorkspaceRef, TemporaryStorageRef, VectorAccessor, DistanceFunction, MetricsSink, CancellationToken, unavailable CheckpointStore, and StageContext interfaces in `include/diskann_pipeline/stage_context.hpp`; validate positive memory/thread budgets and expose them for BuildManifest recording, without implementing OS-level hard resource enforcement in this Feature
- [X] T018 Define PartitionStrategy, LocalGraphBuilder, GraphMerger, and StageResult interfaces using only ArtifactRefs, stage Manifest paths, and `const std::vector<ArtifactRef>&` where applicable; define LocalGraphBuilder to receive an aggregate PartitionArtifact `ArtifactRef`, an explicit `PartitionId`, `LocalBuildConfig`, and `StageContext` in `include/diskann_pipeline/stage_interfaces.hpp`
- [X] T019 Define `ArtifactRequirement { artifact_type, accepted_format_versions, input_id_space }`, collection-valued `ContractRequirement.input_artifacts` with requirement-level capabilities/services, single-output `ContractProvide`, stable repository-relative `parameter_schema_path`, implementation descriptors, and static factory types in `include/diskann_pipeline/implementation.hpp`; declare `RegistryAdmissionPolicy { kProduction, kTest }` and an explicit non-default `ImplementationRegistry(RegistryAdmissionPolicy)` interface in `include/diskann_pipeline/registry.hpp`
- [X] T020 Implement strict JSON codecs with private nlohmann_json usage and explicit unknown-field rejection, including the nested `requires.input_artifacts[]` representation and `parameter_schema_path`, in `src/core/json_codec.cpp` and `src/core/document_codecs.cpp`
- [X] T021 Implement `diskann-artifact-c14n-v1`, streaming payload SHA-256, aggregate projection construction, and golden-vector helpers in `src/validation/canonical_json.cpp` and `src/validation/sha256.cpp`
- [X] T022 Implement isolated normal, temporary, manifest, report, and quarantine roots plus safe relative-path and atomic same-filesystem publication primitives in `src/core/workspace.cpp` and `src/core/artifact_resolver.cpp`
- [X] T023 Implement terminal BuildManifest writing and independent EvaluationReport, ValidationEvidence, and QuarantineRecord persistence in `src/core/provenance_store.cpp` and `src/core/evaluation_store.cpp`
- [X] T024 Implement duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation in `harness/python/diskann_harness/config.py`: use one project subclass of `yaml.SafeLoader` with a custom mapping constructor, require a top-level mapping and string keys, recursively permit only `dict`, `list`, `str`, `bool`, `int`, finite `float`, and `None`, and reject Python object/custom tags, unsafe constructors, NaN, Infinity, date/datetime, bytes, set, and other non-JSON types without a second YAML parser; then perform RunRequest normalization, JSON Schema validation, and immutable JSON request writing through `config.py` and `harness/python/diskann_harness/manifests.py`, leaving field-specific null/float permission to JSON Schema; create `harness/python/diskann_harness/workspace.py` to normalize the workspace root, construct separate normal/temporary/manifest/report/quarantine roots, confine paths and reject path escapes, and write the normalized workspace paths into the normalized request
- [X] T025 Run Phase 1 CTest/pytest contract selections after targets exist, explicitly including every success/failure case in `tests/python/test_yaml_safety.py`, and record commands, totals, failures, and applicable unexecuted checks in `specs/001-modular-cpp-diskann-pipeline/validation/phase1.md`

**Checkpoint**: Public contracts compile as C++17; schema/canonicalization goldens pass; Artifact,
BuildManifest, and EvaluationReport remain separate; no implementation is registered.

---

## Phase 2：Registry、capability checker 和 Validator

**Goal**: Implement immutable static registration, complete-chain preflight, Artifact validation,
quarantine, and validated reuse before any Fake stage or Harness orchestration is added.

### Tests first

- [X] T026 [P] [US3] Add failing Registry-level policy tests in `tests/cpp/unit/test_registry_selection.cpp`: `kProduction` rejects `test_only: true`, `kTest` accepts test-only and formal descriptors, no default construction exists, failed admission leaves state unchanged, and both policies enforce compatible resolution, enumeration, duplicate/stage rejection, sealing, no overwrite, and no fallback; do not test final runner target or installation Assembly here
- [X] T027 [P] [US3] Add failing preflight tests for matching every `requires.input_artifacts` entry by type, accepted version, and per-input ID space, including a Merge descriptor that requires both PartitionArtifact and LocalGraphArtifact; reject a missing required input, incompatible version or ID space, capability/runtime-service mismatch, and checkpoint requirement with zero factory and stage invocations in `tests/cpp/integration/test_compatibility_preflight.cpp`
- [X] T028 [P] [US1] Add failing positive validator tests for Dataset, aggregate Partition, LocalGraph, and GlobalGraph schema/checksum/map/ID/count/degree/capability rules, including that an explicit `partition_id` is present and in range, the selected Partition record exists, and its shard payload and LocalToGlobal map agree with the selected PartitionId, in `tests/cpp/contract/test_valid_artifacts.cpp`
- [X] T029 [P] [US2] Add failing negative metadata tests for missing payloads, length/hash/aggregate mismatches, unsupported versions, wrong type/ID space, and source dataset/checksum mismatches in `tests/cpp/contract/test_invalid_artifact_metadata.cpp`
- [X] T030 [P] [US2] Add failing small-graph property tests for absent/wrong-length/out-of-range maps, invalid neighbors, unresolved replicas, wrong node counts, and degree overruns in `tests/cpp/property/test_invalid_graph_properties.cpp`
- [X] T031 [P] [US2] Add failing quarantine tests for evidence retention, zero capabilities, ordinary resolver invisibility, diagnostic reads/deletion, and prohibition on in-place promotion in `tests/cpp/integration/test_quarantine_isolation.cpp`
- [X] T032 [P] [US3] Add failing reuse tests for fresh validation, producer non-invocation, mismatch reasons, immutable cache preservation, and provenance lineage in `tests/cpp/integration/test_artifact_reuse_core.cpp`
- [X] T033 [P] Add failing Adapter policy tests in `tests/cpp/contract/test_adapter_policy.cpp` that allow lossless format/layout/endianness conversion, range-safe integer-width conversion, explicit Local/Global ID relabeling, and physical record relocation preserving both logical record order and adjacency order; require a complete source-to-target trace, round-trip or canonical logical-equivalence evidence, retained source and output checksums, and accepted/produced format versions and ID spaces; reject missing or incomplete trace/equivalence evidence, observable record-order changes, adjacency-order or adjacency-content changes, output-only checksum retention, overflow, ambiguous maps, replica aggregation, and algorithm-semantic changes

### Registry and validation implementation

- [X] T034 [US3] Implement the public `ImplementationRegistry` interface declared by `include/diskann_pipeline/registry.hpp`: require explicit `kProduction` or `kTest` construction, reject `test_only: true` only under `kProduction`, admit both descriptor classes under `kTest`, preserve Registry state on admission failure, and enforce exact-identity registration, immutable sealing, enumeration, duplicate/stage rejection, no overwrite, and no-fallback resolution under both policies in `src/registry/implementation_registry.cpp`
- [X] T035 [US3] Implement whole-chain matching for every `requires.input_artifacts` entry against upstream `provides`, including all Merge PartitionArtifact and LocalGraphArtifact requirements, exact Artifact versions, per-input ID spaces, requirement-level capabilities, StageContext services, and checkpoint unavailability; reject before factory creation when any required input is missing or incompatible in `src/registry/compatibility_checker.cpp`
- [X] T036 Implement schema catalog/version dispatch and strict ArtifactRef/metadata document admission in `src/validation/schema_catalog.cpp` and `src/validation/validation_gate.cpp`
- [X] T037 [US2] Implement metadata/payload/checksum/map/ID/source/count/degree/capability validators with stable failure categories, including checks that `partition_id` is present and in range, the selected Partition record exists, and its shard payload and LocalToGlobal map agree with the selected PartitionId, in `src/validation/artifact_validator.cpp`, `src/validation/mapping_validator.cpp`, and `src/validation/graph_validator.cpp`
- [X] T038 [US2] Implement invalid-candidate quarantine publication, immutable evidence bundling, normal-resolver exclusion, diagnostic resolver, explicit deletion, and no promotion in `src/core/quarantine_manager.cpp` and `src/core/diagnostic_quarantine_resolver.cpp`
- [X] T039 [US3] Implement reusable Artifact resolution with fresh validation, consumer compatibility checks, immutable cache handling, and REUSED provenance inputs in `src/pipeline/artifact_reuse.cpp`
- [X] T040 Implement a validator façade that returns typed ValidationEvidence and grants downstream capabilities only to exact validated normal ArtifactRefs in `include/diskann_pipeline/validation.hpp` and `src/validation/validator.cpp`
- [X] T041 Implement Adapter policy classification and evidence validation in `src/validation/adapter_policy.cpp`: allow lossless format/layout/endianness conversion, range-safe integer-width conversion, explicit Local/Global ID relabeling, and physical relocation only when logical record and adjacency order are preserved; require a complete source-to-target trace, round-trip or canonical logical-equivalence evidence, retained source/output checksums, and accepted/produced format versions and ID spaces; reject missing/incomplete trace or equivalence evidence, record-order changes, adjacency-order/content changes, output-only checksum retention, overflow, ambiguous maps, replica aggregation, and algorithm-semantic changes, without implementing a production Adapter
- [X] T042 Run Phase 2 Registry/contract/property/quarantine/reuse test selections and record commands, totals, failures, failure categories, invocation counts, and unexecuted checks in `specs/001-modular-cpp-diskann-pipeline/validation/phase2.md`; record both `kProduction` rejection and `kTest` admission behavior for the existing `test_only` marker, and mark final production/Fake target and installation Assembly isolation as not yet applicable until T059/T064/T071 rather than claiming it is verified

**Checkpoint**: Incompatible chains fail before factory invocation; invalid or unvalidated reused
Artifacts cannot cross the validation gate; compatible reused Artifacts may proceed only after
fresh validation and must be recorded with REUSED provenance; quarantine is isolated; no Fake
pipeline exists yet.

---

## Phase 3：测试专用 Fake Pipeline 与最小 Harness

**Goal**: Add the exact six-node Fake stages, real v1 Fake StageContext services, three-stage
orchestrator, evaluator, C++ runner, and Python build/execute/validate/evaluate Harness.

### Tests first

- [X] T043 [P] [US1] Add failing fixture tests for exact vector/map/adjacency bytes, payload role order, lengths, dataset identity, and all golden checksums in `tests/python/test_fake_fixture_golden.py`
- [X] T044 [P] [US1] Add failing unit tests for isolated WorkspaceRef, in-memory vectors, deterministic L2, metrics, unavailable CheckpointStore, and deterministically injected and observed cooperative-cancellation checkpoints without unstable thread timing in `tests/cpp/unit/test_fake_stage_context.cpp`
- [X] T045 [P] [US1] Add failing per-stage contract tests for the three stable test-only identities and compatible descriptors with stable repository-relative `parameter_schema_path` values; verify that the Fake LocalGraphBuilder descriptor and interface explicitly receive a `PartitionId`, produce LocalNodeId output with a complete map, and record the corresponding owning PartitionId in the LocalGraphArtifact, while the Fake Merge descriptor declares both PartitionArtifact and LocalGraphArtifact in `requires.input_artifacts` and produces one GlobalNodeId GlobalGraphArtifact, in `tests/cpp/contract/test_fake_stage_contracts.cpp`
- [X] T046 [P] [US1] Add failing small-graph properties for overlapping IDs, owning-map resolution, ascending Partition order, first-occurrence deduplication, degree-3 truncation, and zero-neighbor lists in `tests/cpp/property/test_fake_merge_properties.cpp`
- [X] T047 [P] [US1] Add failing orchestrator tests for preflight, stage order, per-handoff validation, temporary candidates, normal publication, and no in-memory graph handoff; verify that the orchestrator passes explicit PartitionIds for P0 and P1 separately and never infers them from list position, path names, or an implicit current Partition, in `tests/cpp/integration/test_pipeline_orchestration.cpp`
- [X] T048 [P] [US2] Add failing orchestrator tests for implementation failure, INVALID_OUTPUT, cancellation before/between stages, and distinct terminal states, plus independent during-stage cancellation cases for Partition, Local Build, and Merge; each cancellation case must assert `CANCELLED`, no normal Artifact publication, no misclassification as `INVALID_OUTPUT`, and zero subsequent-stage invocation count, in `tests/cpp/integration/test_pipeline_failure_gates.cpp`
- [X] T049 [P] [US1] Add failing Python unit tests for build/execute/validate/evaluate command construction, argument-array subprocess use, JSON result validation, stable exit mapping, workspace-root normalization, normal/quarantine/report root separation, relative-path resolution, and path-escape rejection in `tests/python/test_harness_cli.py`; for errors from duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation and later normalization, verify only stable propagation through the Harness CLI because the YAML Loader's first contract tests belong to T008
- [X] T050 [P] [US3] Add failing Python unit tests for configured identities, reuse request normalization, mismatch refusal, and retained complete parameters in `tests/python/test_harness_reuse_config.py`

### Fake stages and Harness implementation

- [X] T051 [US1] Create canonical test payloads, checksum inventory, and deterministic fixture materializer in `tests/fixtures/fake_pipeline/golden.json` and `tests/cpp/fakes/fake_fixture.cpp`
- [X] T052 [US1] Implement real v1 Fake WorkspaceRef/vector/L2/metrics/cancellation services and unavailable CheckpointStore in `tests/cpp/fakes/fake_stage_context.cpp`; make the Fake CancellationToken support deterministic checkpoint injection and observation without relying on unstable thread timing
- [X] T053 [P] [US1] Implement test-only `fake_partition_v1` with maps `[0,1,2,3]` and `[2,3,4,5]`, exact payload roles, and declared capabilities; check cancellation at stage start, after input resolution/validation, and before output publication in `tests/cpp/fakes/fake_partition.cpp`
- [X] T054 [P] [US1] Implement test-only `fake_local_graph_v1` with an explicitly supplied PartitionId, LocalNodeId adjacency, corresponding owning PartitionId and map, source Partition checksum, degree declarations, and capabilities; check cancellation at stage start, after input resolution/validation, and before output publication in `tests/cpp/fakes/fake_local_graph.cpp`
- [X] T055 [US1] Implement test-only `fake_merge_v1` with map resolution, ordered exact deduplication, degree truncation, and exact GlobalNodeId adjacency; check cancellation at stage start, after input resolution/validation, and before output publication in `tests/cpp/fakes/fake_merge.cpp`
- [X] T056 [US1] Implement the three-stage executor with preflight, validated inputs/candidates, failure/cancellation/INVALID_OUTPUT transitions, quarantine publication, reuse entry, and invocation counters in `src/pipeline/pipeline_executor.cpp`
- [X] T057 [US1] Implement validated GlobalGraph evaluation for integrity, node count, invalid-neighbor count, and degree findings in `src/pipeline/evaluator.cpp`
- [X] T058 [US1] Implement runner request/result parsing, atomic result writing, common dispatcher, and stable exit codes in `src/cli/runner_protocol.cpp` and `src/cli/runner_main.cpp`
- [X] T059 [US1] Build separate runner targets and registration assemblies: the installed production runner constructs `ImplementationRegistry(RegistryAdmissionPolicy::kProduction)`, links no Fake library/object target, contains no Fake registration source, and installs no Fake-only files; the independent non-installed Fake runner constructs `ImplementationRegistry(RegistryAdmissionPolicy::kTest)` and alone registers Fake factories, in `src/cli/production_registry.cpp`, `tests/cpp/fakes/fake_runner_main.cpp`, and `tests/cpp/fakes/CMakeLists.txt`
- [X] T060 [US1] Implement Python Harness build/execute/validate/evaluate commands, compiler/build provenance capture, C++ subprocess driving, JSON response handling, and cancellation in `harness/python/diskann_harness/cli.py` and `harness/python/diskann_harness/runner.py`; use `harness/python/diskann_harness/workspace.py` for workspace creation, path resolution, lifecycle output placement, and cleanup-boundary management
- [X] T061 [US1] Add the frozen positive configuration and expected workspace model for two retained deterministic runs under `retain_all` with distinct run provenance in `tests/fixtures/fake_pipeline/experiment.yaml` and `tests/fixtures/fake_pipeline/expected_workspace.json`
- [X] T062 [US2] Create the complete machine-readable FR-048 negative case inventory and corrupt-candidate materializers in `tests/fixtures/fake_pipeline/negative/cases.json` and `tests/cpp/fakes/invalid_fixture_factory.cpp`
- [X] T063 [US3] Add valid and mismatched Partition reuse configurations in `tests/fixtures/fake_pipeline/reuse-partition.yaml` and `tests/fixtures/fake_pipeline/reuse-mismatch.yaml`
- [X] T064 Wire production/Fake runner, unit, contract, property, and Harness targets with Phase 3 labels in `src/cli/CMakeLists.txt`, `tests/cpp/fakes/CMakeLists.txt`, and `tests/CMakeLists.txt`; add a simple configure-time assertion, CTest CMake script, or equivalent target-property check under `tests/cmake/` that the production runner links no Fake target, includes no Fake registration source, and keeps the Fake runner independent, and wire standard pytest discovery plus temporary-install support for `tests/python/test_production_assembly.py`
- [X] T065 Run Phase 3 unit/contract/property/Harness selections and record commands, totals, failures, invocation counts, and unexecuted checks in `specs/001-modular-cpp-diskann-pipeline/validation/phase3.md`

**Checkpoint**: The minimal Harness and test-only Fake stages exist and the separate runner
assemblies have been created; final production/Fake link and registration isolation remains for
T071, and no real DiskANN code has been integrated or executed.

---

## Phase 4：端到端测试、文档和清理

**Goal**: Prove each user story end to end, run the complete CTest/pytest suites, reconcile
documentation with actual commands, and produce an evidence-bounded final report.

### End-to-end and cross-cutting tests

- [X] T066 [P] [US1] Add the C++ end-to-end test for exact three-stage order, per-handoff validation, exact global graph/checksum, separate documents, and two retained deterministic runs with distinct run provenance; compare the deterministic evaluation projection rather than bytewise equality of complete EvaluationReport documents, excluding `report_id`, `generated_at`, `run_id`, absolute paths, and run-specific provenance while retaining distinct run provenance, in `tests/cpp/integration/test_fake_pipeline_e2e.cpp`
- [X] T067 [P] [US1] Add the Python end-to-end test for valid ordinary YAML accepted through duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation, normalization, Fake runner invocation, build/execute/validate/evaluate lifecycles, expected workspace, and separate references in `tests/python/test_harness_fake_pipeline_e2e.py`
- [X] T068 [P] [US2] Add the end-to-end negative matrix test for stable categories, correct enforcement gates, quarantine isolation, cancellation, and zero unauthorized invocations in `tests/cpp/integration/test_negative_matrix_e2e.cpp`
- [X] T069 [P] [US3] Add the end-to-end Registry/reuse test for exact configured selection, zero Partition reinvocation, REUSED manifest lineage, and immutable mismatch rejection in `tests/python/test_harness_reuse_e2e.py`
- [X] T070 [P] Add design/runtime byte-sync coverage in `tests/python/test_contract_source_sync.py` for every source/target copy listed in `contracts/v1/SOURCES.json`; verify the inventory completely covers every approved schema plus `checksum-canonicalization.md` and `adapter-policy.md`, and validate its minimum format, repository-relative POSIX paths, exact-source-byte SHA-256 values, stable `target` ordering, and target byte identity without treating generated `SOURCES.json` as a self-copied source
- [X] T071 [P] Complete final Assembly evidence across three surfaces: in `tests/cpp/contract/test_production_surface.cpp`, include `registry.hpp`, verify the C++17 public Registry interface, construct `kProduction`, assert no Fake identity is visible, and assert no production Adapter registration; execute the T064 CMake target/source isolation check proving the production runner links no Fake target, contains no Fake registration source, and keeps the Fake runner independent; in `tests/python/test_production_assembly.py`, install to a temporary prefix and assert the production runner is installed while the Fake runner, Fake library, Fake fixture, and Fake-only files are absent. Registry enumeration must not be treated as proof of binary linkage, and no link map or symbol-scanning tool is introduced
- [X] T072 [P] Add governance coverage for `study_class: infrastructure_validation`, current repository source identity, frozen Fake parameters, `retain_all`, all-run retention, and rejection of parameter-search or non-Fake dataset configuration in `tests/python/test_feature_governance.py`

### Documentation, full validation, and cleanup

- [X] T073 Update actual command names, dependencies, workspace outputs, checksums, failure paths, and scope exclusions in `specs/001-modular-cpp-diskann-pipeline/quickstart.md` and `specs/001-modular-cpp-diskann-pipeline/plan.md`
- [X] T074 Run the complete C++ suite with `ctest --test-dir build/feature-001 --output-on-failure` and record totals/failures in `specs/001-modular-cpp-diskann-pipeline/validation/final.md`
- [X] T075 Run the complete Python suite with `.venv/bin/python -m pytest`, naturally including `tests/python/test_production_assembly.py`, and append totals/failures to `specs/001-modular-cpp-diskann-pipeline/validation/final.md`
- [X] T076 Run the canonical Fake and reuse quickstart commands and record Artifact/Manifest/Report paths, checksums, adjacency, and invocation counts in `specs/001-modular-cpp-diskann-pipeline/validation/quickstart.md`
- [X] T077 Capture the upstream gitlink and clean upstream path before/after validation, ensure every build/workspace path is outside upstream, and record evidence in `specs/001-modular-cpp-diskann-pipeline/validation/upstream-immutability.md`
- [X] T078 Review actual BuildManifests for complete repository/branch/full commit/dirty state/compiler/build flags/dataset/configuration/budget/status provenance and record findings in `specs/001-modular-cpp-diskann-pipeline/validation/provenance-review.md`
- [X] T079 Finalize `specs/001-modular-cpp-diskann-pipeline/validation/final.md` with separately listed executed, failed, and applicable unexecuted checks; document why Baseline parity, SIFT1M/SIFT100M, performance experiments, Loop Engine, retries, SQLite, and checkpoint scheduling are not applicable, and do not claim success if applicable validation is incomplete

**Checkpoint**: All three stories have independent end-to-end evidence, every applicable repository
test is accurately reported, and the upstream gitlink/tree remains unchanged.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1** starts immediately. T003–T006 and T008–T013 contain parallel work after T002 fixes
  target/layout names. Tests are authored before T014–T024 and run at T025.
- **Phase 2** depends on Phase 1 and blocks all stage execution. Its tests T026–T033 precede
  T034–T041; T042 is the gate to Phase 3.
- **Phase 3** depends on Phase 2. T043–T050 are authored before T051–T064; T065 is the gate to
  end-to-end work.
- **Phase 4** depends on the complete Fake/Harness increment from Phase 3 and finishes with the full
  evidence set.

### User Story Dependency Graph

```text
Phase 1 contracts
       ↓
Phase 2 Registry / capability checker / Validator
       ↓
Phase 3 Fake Pipeline / Harness
       ├──────────────┬──────────────┐
       ↓              ↓              ↓
Phase 4 US1 E2E   Phase 4 US2 E2E   Phase 4 US3 E2E
       └──────────────┴──────────────┘
                       ↓
             Full validation and handoff
```

### Independent Test Criteria

- **US1**: The six-node Fake pipeline produces the exact Global graph and frozen checksum after
  validation at every handoff, with separate Artifact/BuildManifest/EvaluationReport references.
- **US2**: Every FR-048 invalid/cancelled case fails at the declared gate, records its stable
  category, quarantines produced invalid output, and invokes no unauthorized consumer.
- **US3**: Exact Registry selection and valid Partition reuse succeed with zero Partition
  reinvocation; incompatible chains and reuse mismatches fail before consumption without mutation.

## Parallel Opportunities

- **Phase 1**: CMake dependencies (T003), Python packaging (T004), contract materialization (T005),
  and test support (T006) touch independent files; T008–T013 can be authored in parallel.
- **Phase 2**: T026–T033 cover independent Registry, compatibility, valid/invalid Artifact,
  quarantine, reuse, and Adapter-policy surfaces.
- **Phase 3**: T043–T050 can be authored in parallel; T053 and T054 are separate Fake stages;
  T061–T063 are independent positive, negative, and reuse fixture configuration tasks.
- **Phase 4**: T066–T072 are independent end-to-end/cross-cutting test files; T074 and T075 run
  different test systems after those tests are registered.

## Parallel Examples

### Phase 2

```text
T026: Registry selection requirements in tests/cpp/unit/test_registry_selection.cpp
T028: valid Artifact contracts in tests/cpp/contract/test_valid_artifacts.cpp
T030: invalid graph properties in tests/cpp/property/test_invalid_graph_properties.cpp
T031: quarantine isolation in tests/cpp/integration/test_quarantine_isolation.cpp
```

### Phase 3

```text
T043: golden Fake fixture in tests/python/test_fake_fixture_golden.py
T044: Fake StageContext services in tests/cpp/unit/test_fake_stage_context.cpp
T045: Fake stage contracts in tests/cpp/contract/test_fake_stage_contracts.cpp
T046: Fake Merge properties in tests/cpp/property/test_fake_merge_properties.cpp
```

### Phase 4

```text
T066: US1 C++ end-to-end in tests/cpp/integration/test_fake_pipeline_e2e.cpp
T068: US2 negative matrix in tests/cpp/integration/test_negative_matrix_e2e.cpp
T069: US3 reuse end-to-end in tests/python/test_harness_reuse_e2e.py
T072: governance requirements in tests/python/test_feature_governance.py
```

## Implementation Strategy

### MVP First

1. Complete Phase 1 contracts and Phase 2 hard gates.
2. Complete the US1-labeled positive Fake/Harness tasks in Phase 3.
3. Run T065, then complete T066–T067 for the US1 MVP.
4. Stop and review exact output/provenance before completing US2/US3 end-to-end paths.

### Incremental Delivery

1. **Contracts**: C++17 types, schemas, hashing, provenance separation, workspace, StageContext.
2. **Hard gates**: Registry, compatibility, Validator, quarantine, reuse.
3. **Fake/Harness**: test-only stages and CLI-driven orchestration.
4. **Evidence**: US1 positive E2E, US2 negative E2E, US3 reuse E2E, then full validation.

### Scope and Approval Boundaries

- Never add, edit, format, build into, or generate files under `upstream/diskann-cpp-main/`.
- Do not integrate or execute real cpp_main, a paper implementation, or a production Adapter.
- Do not implement Loop Engine, SQLite state, parameter matrices, retry, or checkpoint scheduling.
- Do not run SIFT1M, SIFT100M, or any dataset beyond the canonical Fake fixture in this Feature.
- Do not label infrastructure or Fake output as a Baseline, paper reproduction, or scientific result.

## Notes

- `[P]` means the task touches independent files and has no incomplete dependency at that point.
- `[US1]`, `[US2]`, and `[US3]` map to the prioritized stories in `spec.md` even though the user
  requested organization by four technical phases.
- Check off validation tasks only after recording executed, failed, and applicable unexecuted tests.
- Commit after each task or coherent task group without resetting unrelated user changes.

## Phase 5: Convergence

- [X] T080 CRITICAL: Enforce the fixed upstream write boundary before any filesystem mutation by rejecting Harness build source/build directories and normalized workspace roots inside `upstream/diskann-cpp-main`, applying the equivalent guard to direct production/Fake runner requests before workspace or dataset materialization, and adding Python/C++ regression tests that assert rejection and zero files created under upstream per Constitution I, FR-038, and US1/AC1 (missing)
- [X] T081 CRITICAL: Add an actual handoff compatibility gate in `src/pipeline/pipeline_executor.cpp` that compares every freshly validated candidate's Artifact type, format version, ID space, and capabilities with the selected producer `provides` and the next consumer `requires` before publication or invocation; quarantine mismatching produced output with stable evidence and assert zero consumer/evaluation/reuse invocations in integration tests per Constitution V and FR-018/FR-020 (partial)
- [X] T082 CRITICAL: Before factory creation, parse the selected descriptor's repository-relative `parameter_schema_path`, require the referenced schema to exist, and validate each normalized stage-parameter object against the corresponding definition in `contracts/v1/schemas/fake-parameters.schema.json`; reject invalid parameters before factory creation with factory and stage invocation counts both equal to 0, and cover one valid configuration for each Fake stage plus missing required field, additional property, wrong value type, and missing or invalid schema path cases per Constitution VI, the Feature Experiment Protocol, and T072 (partial)
- [X] T083 CRITICAL: After T080-T082 and T084-T091 are complete, rerun every applicable CTest, pytest, canonical/reuse/negative quickstart, schema, upstream-immutability, provenance, and Assembly check; update the Feature validation records to separately list executed, failed, and applicable unexecuted checks, remove unsupported completion claims, and restore a success conclusion only if all applicable checks pass per Constitution VII and T078-T079 (contradicts)
- [X] T084 Materialize and execute every case in `tests/fixtures/fake_pipeline/negative/cases.json` through case-specific helpers in `tests/cpp/fakes/invalid_fixture_factory.cpp` and the end-to-end matrix tests; for each case assert its declared stable category and enforcement gate, candidate quarantine where applicable, immutable reuse behavior, cancellation classification, and zero unauthorized factory/stage/evaluation/reuse/aggregation invocations per FR-048, FR-049, SC-013, T062, and T068 (partial)
- [X] T085 Make the C++ and Python deterministic evaluation projections include the complete deterministic Fake findings, including declared and observed maximum degree, while continuing to exclude report/run identities, paths, timestamps, provenance, and nondeterministic metrics; make the Harness retain both runs but return a stable failure when Artifact payload/aggregate checksums, the projection, or distinct provenance requirements do not hold, with mismatch-focused C++/Python tests per SC-002, FR-047, T066, and T067 (partial)
- [X] T086 Replace the ad hoc Harness build record with a schema-valid terminal BuildManifest that records repository, branch, full commit, dirty state, actual compiler path/version, CMake version, generator, build type, effective CMake definitions, project compile options, actual configure/build commands, and immutable build configuration; record original YAML and normalized JSON digests, capture real stage start/end and evaluation-generation timestamps at their lifecycle boundaries, and validate all resulting documents in Python E2E/provenance tests without requiring per-translation-unit compile commands or linker commands in Feature 001 per FR-012, FR-026, US1/AC1, T060, and T078 (partial)
- [X] T087 Complete the C++ runner protocol by strictly validating request shape and Feature governance before workspace mutation, requiring the command-line operation to equal the request operation, rejecting unknown operations instead of falling through to execute, implementing the documented request-free `registry --result` command, and aligning cancellation/validation/evaluation exit codes and Python mappings with the single CLI contract; add direct production/Fake runner protocol tests per the plan's one-C++-runner decision and T058 (contradicts)
- [X] T088 Handle Harness interruption by requesting normal termination of the runner subprocess, waiting for a bounded period, and forcing termination only if necessary; record stable cancellation/termination evidence, report the run as CANCELLED, publish neither a normal nor an INVALID_OUTPUT Artifact, and start no later stage. Add deterministic Harness integration coverage without introducing a Harness-to-runner IPC protocol or any mechanism for remotely mutating the runner's internal CancellationToken; Fake-stage cooperative cancellation remains covered by T044, T048, and T052-T055 per the FR-040 cancellation edge case and T060 (partial)
- [X] T089 Correct the independent validate lifecycle so the Harness derives the canonical type, format, ID space, and capability contract for DatasetArtifact, PartitionArtifact, LocalGraphArtifact, and GlobalGraphArtifact, and the C++ dispatcher enforces every `expected_contract` field including `artifact_type`; add newly produced, stored, mismatch, unvalidated, and quarantine-path CLI tests per FR-020, FR-028, and T060 (partial)
- [X] T090 Treat a stage result that reports success without an output candidate, or otherwise violates the StageResult terminal-output contract, as incomplete/invalid output rather than a generic failure; persist schema-valid failure evidence and BuildManifest state, publish no normal Artifact, and assert zero downstream invocations in Partition, Local Build, and Merge tests per FR-023, FR-027, FR-039, and T056 (contradicts)
- [X] T091 Exercise the actual production registration assembly in `tests/cpp/contract/test_production_surface.cpp` by invoking `register_production_implementations()` before sealing/enumeration, while retaining the existing CMake target/source and temporary-install checks; assert that the assembled production Registry exposes neither Fake identities nor a production Adapter registration per the plan's Assembly-isolation decision and T071 (partial)

## Phase 6: Convergence

- [ ] T092 CRITICAL: After T093-T098 are complete, rerun every applicable CTest, pytest, canonical/reuse/negative quickstart, schema/source-sync, provenance, Assembly, and upstream-immutability check; update `specs/001-modular-cpp-diskann-pipeline/validation/` so executed, failed, and applicable unexecuted checks remain separate, remove the current unsupported all-clear conclusion until the new checks pass, and restore it only with evidence for every new regression per Constitution VII and T083 (contradicts)
- [ ] T093 CRITICAL: Make the documented Harness build lifecycle produce and bind a schema-valid terminal BuildManifest before canonical/reuse execution, and reject execution rather than silently falling back to stage manifests with missing actual configure/build commands; ensure a clean out-of-tree build records repository/branch/full commit/dirty state, actual compiler/CMake/generator/build type, effective definitions/project compile options, actual configure/build commands, immutable build configuration, and YAML/normalized-JSON digests, then add clean-build E2E/provenance coverage and align the Quickstart command sequence per FR-012, FR-026, US1/AC1, and T086 (partial)
- [ ] T094 CRITICAL: Validate each GlobalGraph structural `source_partition_artifact` and `source_local_graph_artifacts` entry against the complete validated source identity tuple of Artifact type, format version, and aggregate checksum, not checksum alone; add contract tests that mutate structural source type and version, recompute the aggregate identity consistently, and still receive `source_mismatch` before evaluation or reuse per Constitution V, FR-008, FR-020, and T037 (partial)
- [ ] T095 Add executor-level cooperative-cancellation gates before factory creation/invocation for the first stage and between completed stages; make `cancel_before_stage`, `cancel_between_stages`, and each during-stage checkpoint independently observable, require zero invocation of the not-yet-started stage and every later stage, publish no normal or INVALID_OUTPUT Artifact, and persist a CANCELLED manifest/result with a stable cancellation reason while retaining the existing Fake-stage checkpoints per FR-012, FR-040, FR-048, FR-049, T048, and T084 (partial)
- [ ] T096 Correct the case-specific FR-048 helpers so `reuse_dataset_mismatch` actually presents a cached PartitionArtifact under a mismatched dataset identity or checksum rather than only an unsupported format version, and so `partition_id_missing`/`partition_id_out_of_range` exercise the explicit Local Build selection input or its aggregate Partition record rather than merely corrupting a produced LocalGraph owner field; preserve frozen positive payloads/checksums and assert the declared category/gate, cache immutability, quarantine rule, and zero unauthorized invocations in the authoritative matrix per FR-035, FR-048, FR-049, SC-013, and T084 (partial)
- [ ] T097 Complete C++ runner request validation before `create_workspace()` or temporary-storage creation by enforcing the runtime RunRequest contract for stable IDs, implementation-selection value types, repetition values, expected-contract members, evaluator configuration, source-config SHA-256 fields, and every other nested field consumed after admission; add direct production/Fake runner malformed-request tests that assert stable usage/schema failure, zero stage/factory invocations, and no workspace mutation per FR-025, FR-027, T058, and T087 (partial)
- [ ] T098 Extend the existing Adapter declaration/evidence contract only as needed to prove that every explicit Local/Global ID relabeling target lies in its declared legal ID range, reject missing or out-of-range evidence without implementing a production Adapter, and add positive boundary plus negative out-of-range policy tests while preserving all already-approved Adapter semantics per FR-042, FR-043, FR-044, T033, and T041 (partial)
