# Tasks: cpp_main Upstream P0+B0+M0 Graph Closure

**Input**: specs/002-cpp-main-upstream-baseline/spec.md, plan.md, research.md,
source-mapping.md, data-model.md, quickstart.md, contract-gap.md, and contracts/.

**Implementation boundary**: This task list has exactly three implementation phases. It implements
only fixed-commit native-format/build support, P0/B0/M0 bindings, and one SIFT1M Unified sharded
structural closure. It does not freeze a Profile, execute Graph Parity, register
validated_graph_baseline, evaluate Recall/QPS/latency/build time/memory/Artifact size, add
cross-Partition concurrency, implement Loop Engine, introduce a generic Adapter or automatic
tuning, or resume Feature 001 deferred hardening.

**Read-only boundary**: upstream/diskann-cpp-main is an input only and is never a task write target.
All configure, build, temporary, test, Artifact, Manifest, evidence, report, and log paths are
outside upstream/.

**Task record format**: Every task states its inputs, outputs, dependencies, minimum test, and
completion condition. Test-definition tasks intentionally run first and must demonstrate the
expected failure before their corresponding implementation task can be completed.

## Phase 1: Native Format Support and Build Integration

**Story mapping**: US1 — Establish a trustworthy Native Graph Reference.

**Goal**: Admit only the pinned clean source/build, link the real upstream library out of tree, and
validate every cpp_main-native byte format needed by the later bindings.

**Independent test**: Configure build/cpp-main-baseline without writing under upstream, link the
actual diskann_s target and all required callable symbols, then pass the native-format and
source/build-admission tests using only small test-generated files.

- [X] T001 [US1] Create the minimal non-implementation CMake/CTest scaffold in CMakeLists.txt, tests/CMakeLists.txt, and tests/cpp/baseline/CMakeLists.txt, then add failing native-format coverage in tests/cpp/baseline/test_cpp_main_native_formats.cpp
  - **输入**: contracts/native-formats.md, source-mapping.md entries SM-017, SM-023, SM-025–SM-027 and SM-033, public identifier/count limits.
  - **输出**: basic `DISKANN_CPP_MAIN_BASELINE` switch and conditional tests/cpp/baseline inclusion; an existing `cpp_main_native_formats` target and same-name CTest; tests that generate small temporary Dataset, ID-map, centroids, local-graph, local .data, merged-graph, and medoids byte fixtures; positive cases plus truncation, trailing bytes, overflow, wrong dimension/count, non-finite float, invalid local/global ID, degree overflow, invalid entry point, and malformed medoid cases. This scaffold contains no native reader implementation and no upstream linkage.
  - **依赖**: None.
  - **最小测试**: Execute `cmake -S . -B build/cpp-main-baseline -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON`, `ctest --test-dir build/cpp-main-baseline -N -R '^cpp_main_native_formats$'`, and `cmake --build build/cpp-main-baseline --target cpp_main_native_formats`; record the intended failure caused by the missing native reader declarations/implementation, never by an unknown target or missing CTest registration.
  - **完成条件**: The named target and CTest are discoverable before the red build, every Phase format has at least one positive and meaningful negative assertion, no fixture writes under upstream, and the expected reader-related failure is recorded.

- [X] T002 [US1] Create and register the source/build-admission red-test target in tests/cpp/baseline/CMakeLists.txt, tests/cpp/baseline/test_cpp_main_source_build_admission.cpp, and tests/cmake/check_cpp_main_baseline_build.cmake
  - **输入**: pinned repository/branch/commit/gitlink, plan Source/Build Identity fields, AGENTS.md upstream boundary.
  - **输出**: an existing `cpp_main_source_build_admission` target and same-name CTest plus tests/cmake/check_cpp_main_baseline_build.cmake; positive read-only checks against the real submodule plus negative synthetic-git fixtures outside upstream for wrong gitlink/HEAD/commit, dirty source, upstream-targeted output path, and incomplete build identity; assertions for actual configure/build commands, compiler, CMake, generator, build type, effective definitions/options, dependency and binary identities.
  - **依赖**: T001.
  - **最小测试**: Execute `cmake -S . -B build/cpp-main-baseline -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON`, `ctest --test-dir build/cpp-main-baseline -N -R '^cpp_main_source_build_admission$'`, `cmake -P tests/cmake/check_cpp_main_baseline_build.cmake`, and `cmake --build build/cpp-main-baseline --target cpp_main_source_build_admission`; record the intended source/build-guard implementation failure without dirtying the real upstream tree and never accept an unknown target as the red result.
  - **完成条件**: The named target and CTest are discoverable; tests prove that identity mismatch or an upstream write target is rejected before work; dirty-state negatives use only disposable test repositories; the red result is caused by the missing admission implementation.

- [X] T003 [US1] Create and register a non-executing upstream callable link smoke in tests/cpp/baseline/CMakeLists.txt and tests/cpp/baseline/test_cpp_main_link_smoke.cpp
  - **输入**: fixed headers partition.h, disk_utils.h, index.h and the source-mapped float instantiations.
  - **输出**: `cpp_main_link_smoke` target and same-name CTest; a C++17 executable that forces the linker to resolve partition_with_ram_budget<float>, retrieve_shard_data_from_ids<float>, merge_shards, and the Index<float> build/save path without running graph construction.
  - **依赖**: T002.
  - **最小测试**: Execute `cmake -S . -B build/cpp-main-baseline -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON`, `ctest --test-dir build/cpp-main-baseline -N -R '^cpp_main_link_smoke$'`, and `cmake --build build/cpp-main-baseline --target cpp_main_link_smoke`; record the expected unresolved-symbol link failure before diskann_s integration, never an unknown-target failure.
  - **完成条件**: The target and same-name CTest are discoverable, the red build reaches the linker and fails on the required upstream callable symbols, the test uses no nm/readelf or copied source, and later success can only come from linking the actual pinned upstream target.

- [X] T004 [P] [US1] Add failing draft-profile and baseline-local configuration tests in tests/python/test_cpp_main_baseline_config.py
  - **输入**: plan Technical Context, fixed SIFT1M identity, three implementation IDs, safe YAML rules already implemented by Feature 001, and baseline_validation governance.
  - **输出**: tests requiring experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml to declare status: draft, source identity, Dataset identity, R/L/M/T and disabled modes, with no frozen status, parity protocol, repetitions, performance evaluation, automatic retry, or registration claim.
  - **依赖**: None.
  - **最小测试**: Execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q` and record the expected failure because the baseline schemas/Profile do not yet exist.
  - **完成条件**: Positive ordinary safe YAML and negative wrong study class/source/commit/Dataset/implementation/parameter/status cases are represented, and Feature 001's Fake request rules are not weakened.

- [X] T005 [US1] Implement read-only wrapper build integration in cmake/CppMainBaseline.cmake, src/baseline/cpp_main/CMakeLists.txt, tests/cpp/baseline/CMakeLists.txt, CMakeLists.txt, and tests/CMakeLists.txt
  - **输入**: T001–T003 scaffold/red tests, pinned upstream source path, actual upstream target diskann_s, existing C++17 compiler options and dependency discovery.
  - **输出**: successful baseline CMake configuration extending the existing basic `DISKANN_CPP_MAIN_BASELINE` scaffold with an explicit upstream binary directory under build/cpp-main-baseline, baseline wrapper/test targets outside upstream, real diskann_s linkage that turns cpp_main_link_smoke green, and a registered/discoverable cpp_main_source_build_admission target and same-name CTest. The admission test may remain at its expected red state until T006 implements the guard.
  - **依赖**: T003.
  - **最小测试**: Execute `cmake -S . -B build/cpp-main-baseline -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON`, `cmake --build build/cpp-main-baseline --target cpp_main_link_smoke --parallel`, and `ctest --test-dir build/cpp-main-baseline -N -R '^cpp_main_source_build_admission$'`; do not require the admission CTest to pass in T005.
  - **完成条件**: Baseline configure succeeds; real upstream diskann_s links; cpp_main_link_smoke is green; cpp_main_source_build_admission target/CTest is registered and discoverable but may remain the expected red test; upstream remains at the pinned commit and clean; and every build/generated path remains outside upstream/diskann-cpp-main.

- [X] T006 [US1] Implement fixed source/build admission and build-stage provenance in src/baseline/cpp_main/source_build_identity.hpp and src/baseline/cpp_main/source_build_identity.cpp
  - **输入**: T002 tests; parent gitlink, submodule identity, configured compiler/CMake/build values, actual commands and linked binary identities from T005.
  - **输出**: fail-closed guard for repository/branch/full commit/gitlink/HEAD/clean state and path confinement, plus one schema-valid build-stage BuildManifest carrying actual Source/Build Identity without per-translation-unit compile or linker command collection.
  - **依赖**: T002, T005.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_source_build_admission` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_source_build_admission$' --output-on-failure`.
  - **完成条件**: The admission guard, complete Build Identity, and provenance are implemented; T005's registered cpp_main_source_build_admission CTest turns green for every positive/negative case; admission failure produces no wrapper work; and actual before/after upstream status is empty.

- [X] T007 [US1] Implement the fixed native readers in src/baseline/cpp_main/native_formats.hpp and src/baseline/cpp_main/native_formats.cpp
  - **输入**: T001 tests and contracts/native-formats.md; expected Dataset/map/centroid/local graph/.data/global graph/medoid counts and ID bounds supplied by callers.
  - **输出**: overflow-safe little-endian parsers and structural findings for all seven physical uses, with no normalization, sorting, repair, conversion, publication, or generic format registry.
  - **依赖**: T001, T005.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_native_formats` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_native_formats$' --output-on-failure`.
  - **完成条件**: All T001 cases pass; each parser consumes exact bytes, rejects trailing/truncated/invalid data, and exposes only private cpp_main types.

- [X] T008 [US1] Add narrow cpp_main semantic dispatch and Dataset metadata support in src/validation/schema_catalog.cpp, src/validation/artifact_validator.cpp, and contracts/baseline-v1/schemas/cpp-main-dataset-artifact.schema.json
  - **输入**: T007 native findings, existing Feature 001 metadata/checksum/lineage validator, reviewed media types in contracts/native-formats.md.
  - **输出**: fixed dispatch by Artifact type/format version/payload role/media type/source identity for the cpp_main Dataset, map, centroids, local graph, global graph, and medoids, while preserving existing Fake formats and public v1 schemas.
  - **依赖**: T006, T007.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_native_formats valid_artifacts artifact_contract_amendment invalid_artifact_metadata invalid_graph_properties` and `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_native_formats|valid_artifacts|artifact_contract_amendment|invalid_artifact_metadata|invalid_graph_properties)$' --output-on-failure`.
  - **完成条件**: Valid cpp_main payloads reach their exact reader; unknown/mismatched media types fail closed; existing Feature 001 tests and frozen Fake checksums remain unchanged.

- [X] T009 [US1] Create baseline-local request/schema skeletons and the unfrozen draft profile in contracts/baseline-v1/schemas/cpp-main-run-request.schema.json and experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml
  - **输入**: T004 tests, fixed source/Dataset identity, current Gate A decision, SIFT1M values R=16/L=32/M=0.6/T=6, and exact implementation IDs.
  - **输出**: baseline_validation request skeleton and YAML profile marked draft, containing source, Dataset, resource, stage-selection, and parameter placeholders needed by the three phases; no Graph Parity, frozen status, repeats, metrics evaluation, registration, retry, or larger-dataset authorization.
  - **依赖**: T004.
  - **最小测试**: Execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q`; Phase 1 Profile/schema cases must pass while explicitly marked execution cases remain skipped or expected-failing until Phase 3.
  - **完成条件**: The draft validates as safe YAML/JSON-compatible data, is distinguishable from Feature 001's request contract, and cannot be interpreted as a frozen Baseline Profile.

- [X] T010 [US1] Complete the Phase 1 test gate and retain logs under build/cpp-main-baseline/test-results/phase1/
  - **输入**: T005–T009 implementations and all Phase 1 test targets.
  - **输出**: configure/build, targeted CTest, targeted pytest, git diff --check, and upstream-before/after logs; separate lists of executed, failed, and applicable unexecuted tests.
  - **依赖**: T005, T006, T007, T008, T009.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_link_smoke cpp_main_source_build_admission cpp_main_native_formats artifact_contract_amendment`, `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_link_smoke|cpp_main_source_build_admission|cpp_main_native_formats|artifact_contract_amendment)$' --output-on-failure`, and `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q`.
  - **完成条件**: All Phase 1 tests pass, git diff --check is clean, upstream HEAD equals the pinned commit with empty porcelain status, and no Stage Binding has been implemented.

**Phase 1 checkpoint**: Native files and fixed build identity are independently testable; no P0,
B0, or M0 factory exists yet.

## Phase 2: P0/B0/M0 Stage Bindings

**Story mapping**: US2 — Implement the approved P0, B0, and M0 Stage Bindings independently.

**Goal**: Implement three thin, separately testable bindings over the pinned callables, with
complete descriptors and hard Artifact gates. B0 execution is sequential by explicit PartitionId;
no cross-Partition concurrency is introduced.

**Independent test**: On small native fixtures, invoke each binding independently and prove its
callable path, output structure, ID space, lineage, checksum, failure behavior, and lack of
downstream publication for invalid output.

- [ ] T011 [P] [US2] Add failing descriptor, parameter-schema, Registry, and preflight tests in tests/cpp/baseline/test_cpp_main_descriptors_preflight.cpp
  - **输入**: contracts/stage-bindings.md descriptor declarations and existing C++17 Registry/capability checker.
  - **输出**: `cpp_main_descriptors_preflight` target and same-name CTest; tests for exact stable IDs, test_only=false, parameter_schema_path, P0 Dataset requires/Partition provides, B0 Dataset+Partition requires/LocalGraph provides, M0 Partition+LocalGraph requires/GlobalGraph+medoids provides, ID spaces, runtime services, kProduction admission, and zero factory calls on incompatibility.
  - **依赖**: T010.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_descriptors_preflight` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_descriptors_preflight$' --output-on-failure`; record the intended pre-implementation failure before descriptors/schemas exist.
  - **完成条件**: Tests include missing input, wrong version, wrong ID space, missing capability/service, invalid parameters, duplicate identity, and prohibited fallback cases.

- [ ] T012 [P] [US2] Add failing P0 callable and Artifact tests in tests/cpp/baseline/test_cpp_main_p0.cpp
  - **输入**: contracts/stage-bindings.md P0 boundary, native format readers, small temporary float32 Dataset, source-derived parameters.
  - **输出**: `cpp_main_p0` target and same-name CTest; tests proving the actual partition_with_ram_budget<float> path returns Partition count/maps/centroids, publishes a map-only aggregate PartitionArtifact, preserves Dataset ownership/lineage, and rejects missing centroids/maps, invalid IDs/counts, shard-vector claims, cancellation, or invalid source/Profile.
  - **依赖**: T010.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_p0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_p0$' --output-on-failure`; record the intended pre-implementation failure before p0_partition.cpp exists.
  - **完成条件**: Assertions are structural only, do not freeze random Partition bytes/count, do not set a seed, and confirm no B0 invocation after invalid P0 output.

- [ ] T013 [P] [US2] Add failing B0 callable, local-ID, degree, and lineage tests in tests/cpp/baseline/test_cpp_main_b0.cpp
  - **输入**: contracts/b0-callable-wrapper.md, one small native Dataset, aggregate map-only PartitionArtifact, explicit valid/invalid PartitionIds, R/L/T profile values.
  - **输出**: `cpp_main_b0` target and same-name CTest; tests proving retrieve_shard_data_from_ids<float> row order, native parameter construction, Index<float>::build/save, .data lifecycle validation, LocalGraph node count/LocalNodeId/degree bounds, exact Dataset/Partition/map lineage, cancellation gates, and no normal output on failure.
  - **依赖**: T010.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_b0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_b0$' --output-on-failure`; record the intended pre-implementation failure before the native-backed accessor and B0 binding exist.
  - **完成条件**: Tests reject absent/wrong native Dataset binding, invalid PartitionId, incomplete/non-ascending/out-of-range map, shard row mismatch, invalid neighbor/count/degree/checksum/lineage, and assert no byte-identical Native-composite expectation.

- [ ] T014 [P] [US2] Add failing M0 complete-set, global-ID, medoid, and lineage tests in tests/cpp/baseline/test_cpp_main_m0.cpp
  - **输入**: contracts/stage-bindings.md M0 boundary, small aggregate PartitionArtifact, native local graphs and maps.
  - **输出**: `cpp_main_m0` target and same-name CTest; tests proving byte-preserving input staging, actual merge_shards call, GlobalNodeId range/degree/node count, required medoids and graph+medoids aggregate checksum, plus missing/duplicate/extra/wrong-owner/wrong-map/wrong-Dataset/wrong-source/version/ID-space/capability negatives.
  - **依赖**: T010.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_m0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_m0$' --output-on-failure`; record the intended pre-implementation failure before m0_merge.cpp exists.
  - **完成条件**: Tests do not assert deterministic adjacency, never describe M0 as pruning/refinement/connectivity repair, and ensure invalid input prevents merge invocation.

- [ ] T015 [US2] Implement the three formal descriptors and parameter contracts in src/baseline/cpp_main/descriptors.cpp and contracts/baseline-v1/schemas/cpp-main-p0-parameters.schema.json, cpp-main-b0-parameters.schema.json, and cpp-main-m0-parameters.schema.json
  - **输入**: T011 tests, exact declarations in contracts/stage-bindings.md, fixed profile modes and derived/default parameter rules.
  - **输出**: three static factories with exact implementation identities, multi-input requires, single-output provides, ID spaces, capabilities, services, repository-relative schema paths, and test_only=false; schemas reject missing/additional/wrong-type/unsupported fields before factory creation.
  - **依赖**: T011, T010.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_descriptors_preflight registry_selection compatibility_preflight` and `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_descriptors_preflight|registry_selection|compatibility_preflight)$' --output-on-failure`.
  - **完成条件**: All descriptors/schema cases pass in kProduction, no factory is created on admission failure, and nothing is registered in the installed Feature 001 production runner or validated_graph_baseline catalog.

- [ ] T016 [P] [US2] Implement P0 as a thin pinned callable in src/baseline/cpp_main/p0_partition.cpp
  - **输入**: T012 tests, validated native Dataset ArtifactRef, M, derived sample probability/local degree, replication factor 2, StageContext temporary storage and cancellation.
  - **输出**: one partition_with_ram_budget<float> invocation; validated returned count, centroids, and contiguous ID maps; immutable aggregate map-only PartitionArtifact with Dataset/ownership/map/centroid lineage and no shard_vectors_role.
  - **依赖**: T012, T015, T008.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_p0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_p0$' --output-on-failure`.
  - **完成条件**: All P0 positive/negative tests pass, invalid candidates do not reach B0, upstream randomness/control flow remain unchanged, and no partitioning algorithm is copied or rewritten.

- [ ] T017 [P] [US2] Implement the private native Dataset binding in src/baseline/cpp_main/cpp_main_vector_accessor.cpp
  - **输入**: T013 tests, validated cpp_main Dataset metadata/payload/checksum, existing public VectorAccessor interface and workspace confinement.
  - **输出**: Feature-local float32 accessor retaining the exact confined read-only native payload identity for B0, without a public API change, parameter path, process-global lookup, normalization, or Dataset rewrite.
  - **依赖**: T013, T008.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_b0 cpp_main_native_formats` and `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_b0|cpp_main_native_formats)$' --output-on-failure`.
  - **完成条件**: Correct Dataset rows/identity are exposed; mismatched/generic accessors fail before retrieval; no native-path method leaks into include/diskann_pipeline/.

- [ ] T018 [US2] Implement B0 retrieval, native parameter construction, build/save, and LocalGraph publication in src/baseline/cpp_main/b0_local_build.cpp
  - **输入**: T013 tests, T015 descriptor/schema, T017 accessor, aggregate PartitionArtifact, explicit PartitionId, LocalToGlobalIdMap, R/L/T.
  - **输出**: sequential single-Partition binding that calls retrieve_shard_data_from_ids<float>, validates the temporary shard, constructs upstream parameters with L, floor(2R/3), saturation=false, T and unchanged defaults, calls Index<float>::build/save, validates graph/.data, and publishes one LocalGraphArtifact.
  - **依赖**: T013, T015, T017, T008.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_b0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_b0$' --output-on-failure`.
  - **完成条件**: Every B0 test passes; LocalNodeId/count/degree/checksum and Dataset/Partition/map lineage are exact; temporary shard/.data lifecycle is recorded; no Vamana code is copied; no cross-Partition thread pool exists.

- [ ] T019 [P] [US2] Implement M0 complete-set staging, merge, and GlobalGraph publication in src/baseline/cpp_main/m0_merge.cpp
  - **输入**: T014 tests, T015 descriptor/schema, one validated aggregate PartitionArtifact, complete validated LocalGraphArtifact collection, R.
  - **输出**: exact PartitionId coverage validation, byte-preserving native map/local-graph staging, one merge_shards invocation, validated merged graph and required medoids, and one GlobalGraphArtifact whose aggregate identity covers both payloads.
  - **依赖**: T014, T015, T008.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_m0` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_m0$' --output-on-failure`.
  - **完成条件**: All M0 structural/negative tests pass, missing or invalid medoids are rejected, no invalid input reaches merge_shards, and no project-owned merge/prune/refinement/connectivity algorithm is added.

- [ ] T020 [P] [US2] Record B0's declared Dataset and Partition inputs without changing public APIs in src/pipeline/pipeline_executor.cpp
  - **输入**: B0 multi-input descriptor, existing PipelineExecutionRequest Dataset ArtifactRef, aggregate Partition ArtifactRef, BuildManifest v1.
  - **输出**: each B0 BuildManifest lists both actual input ArtifactRefs while LocalGraphArtifact source lineage remains the exact aggregate Partition plus DatasetIdentity; execution remains PartitionId-sequential.
  - **依赖**: T013, T015.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_b0 pipeline_orchestration document_codecs` and `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_b0|pipeline_orchestration|document_codecs)$' --output-on-failure`.
  - **完成条件**: Manifest inputs match declared requirements, existing Fake pipeline behavior remains valid, no terminal-manifest redesign or other Feature 001 deferred hardening is introduced.

- [ ] T021 [US2] Complete the Phase 2 stage-binding gate and retain logs under build/cpp-main-baseline/test-results/phase2/
  - **输入**: T015–T020 and all Phase 2 tests.
  - **输出**: build/CTest logs for descriptors, P0, B0, M0, relevant existing Registry/compatibility/Artifact/pipeline tests, git diff --check, upstream before/after state, and a diff review confirming no upstream algorithm copy.
  - **依赖**: T015, T016, T017, T018, T019, T020.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_descriptors_preflight cpp_main_p0 cpp_main_b0 cpp_main_m0 registry_selection compatibility_preflight artifact_contract_amendment pipeline_orchestration` and `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_descriptors_preflight|cpp_main_p0|cpp_main_b0|cpp_main_m0|registry_selection|compatibility_preflight|artifact_contract_amendment|pipeline_orchestration)$' --output-on-failure`.
  - **完成条件**: All targeted tests pass, failures/unexecuted tests are reported, upstream is pinned and clean, and no Phase 3 runner or SIFT1M execution has started.

**Phase 2 checkpoint**: Each stage is independently callable and structurally valid; execution is
still limited to small fixtures and Formal Graph Parity remains outside the current delivery scope.

## Phase 3: SIFT1M Unified Sharded Closure

**Story mapping**: US3 — Execute the complete Unified sharded structural closure without executing
Formal Graph Parity.

**Goal**: Use the still-draft Profile to execute exactly one ordered P0 → B0 × N → M0 SIFT1M
structural closure, validate every handoff, generate specs/002-cpp-main-upstream-baseline/unified-sharded-report.json, and retain a clean
upstream state.

**Independent test**: The non-installed cpp_main runner executes the public Pipeline on the fixed
SIFT1M Dataset, proves the sharded admission and Partition count at least two, publishes only valid
Artifacts, and produces a report with formal_graph_parity: false and
validated_graph_baseline_registered: false.

- [ ] T022 [P] [US3] Add failing sequential small-Pipeline and failure-gate tests in tests/cpp/baseline/test_cpp_main_pipeline.cpp
  - **输入**: completed P0/B0/M0 factories, existing PipelineExecutor and Fake JSON evaluator path, small native Dataset, a validator-approved native GlobalGraphArtifact with corresponding ValidationEvidence, and StageContext services.
  - **输出**: `cpp_main_pipeline` target and same-name CTest; tests asserting exact stage order P0 then ascending explicit PartitionIds then M0, one B0 per Partition, no cross-Partition concurrency, Artifact validation before every handoff, complete M0 input set, final graph+medoids, cancellation/failure short-circuit, and downstream invocation count zero after invalid output. The red suite also sends an already validated native GlobalGraph through the existing public internal evaluator implementation and requires a separate EvaluationReport whose evaluator object is exactly `{implementation_id: cpp_main_native_graph_integrity_evaluator, version: 1.0.0}`, with findings sourced from its ValidatedArtifact claims and ValidationEvidence. It verifies GlobalNodeId, the required capability, the exact reviewed cpp_main global-graph media type, unchanged `fake_graph_integrity_evaluator` identity/version/JSON behavior for reviewed Fake input, and fail-closed rejection for unvalidated or unknown/mismatched media types.
  - **依赖**: T021.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_pipeline` and `ctest --test-dir build/cpp-main-baseline -R '^cpp_main_pipeline$' --output-on-failure`; record the intended pre-implementation failure before the baseline runner/assembly exists.
  - **完成条件**: The native binary case fails before T024 because the existing Fake JSON evaluator cannot consume it; all expectations are structural/contractual, do not compare native graph bytes or add parity/repetition/performance logic, and include invalid P0, one invalid B0, invalid M0, missing ValidationEvidence, and unknown-media cases.

- [ ] T023 [P] [US3] Extend failing baseline configuration and report tests in tests/python/test_cpp_main_baseline_config.py
  - **输入**: experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml skeleton, cpp-main-run-request schema, exact normalized request path build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json, expected runner/result references, and specs/002-cpp-main-upstream-baseline/unified-sharded-report.json requirements.
  - **输出**: tests for safe normalization, repository-relative Dataset/Profile paths, and two explicit CLI modes. Preflight tests require Profile and Source/Build validation, Dataset admission plus ValidationEvidence, strict sharded predicate, immutable request at the exact path, a terminal Dataset ArtifactRef/request-path response, no runner/factory creation, and P0/B0/M0 invocation counts all zero. Execute tests require `--request`, reject implicit `--config` admission, preserve the immutable request, invoke the runner once, and retain separated Artifact/BuildManifest/ValidationEvidence/EvaluationReport references. Report tests require run_id, run_class=unified_sharded_closure, formal_graph_parity=false, validated_graph_baseline_registered=false, exact profile_ref/profile checksum/profile_status=draft, upstream commit, Dataset checksum, three implementation identities, Partition count, stage results, Artifact references, structural_validation=PASS|FAIL, upstream clean before/after, and a separate cpp_main native graph-integrity EvaluationReport whose evaluator object is exactly `{implementation_id: cpp_main_native_graph_integrity_evaluator, version: 1.0.0}`, with no parity result, registration, repeats, tuning, or performance evaluation.
  - **依赖**: T021.
  - **最小测试**: Execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q` and record the intended pre-implementation failure before cpp_main_baseline.py and the final draft fields exist.
  - **完成条件**: Preflight and execute validation/errors propagate stably; failed admission creates no executable request or stage invocation; a failed execute retains failure evidence; the exact request is never rewritten; and tests do not modify Feature 001's Harness request semantics.

- [ ] T024 [US3] Assemble the non-installed sequential baseline runner and add the single closed cpp_main condition to the existing public internal evaluator implementation in src/baseline/cpp_main/cpp_main_baseline_runner.cpp, src/baseline/cpp_main/CMakeLists.txt, and src/pipeline/evaluator.cpp
  - **输入**: T022 tests, three formal factories, RegistryAdmissionPolicy::kProduction, existing PipelineExecutor/Fake JSON evaluator, the existing `fake_pipeline_e2e` C++ regression and `tests/python/test_harness_fake_pipeline_e2e.py` EvaluationReport-lifecycle regression, the reviewed cpp_main global-graph media type, and validated StageContext. Because this task modifies the common internal `src/pipeline/evaluator.cpp`, both existing Fake evaluator regressions are mandatory.
  - **输出**: cpp_main_baseline_runner target that registers only the three cpp_main identities, resolves one validated Dataset, binds the private accessor, executes P0 then each explicit PartitionId in order then M0, and returns stable status/Artifact/Manifest/evidence/report references. In the existing public internal `src/pipeline/evaluator.cpp` implementation—not runner-private code—add exactly one closed cpp_main condition whose evaluator object is `{implementation_id: cpp_main_native_graph_integrity_evaluator, version: 1.0.0}`. Only the Feature-local cpp_main runner's successful path produces matching input. The condition accepts only an Artifact-validator-approved GlobalGraphArtifact with GlobalNodeId, required global-adjacency capability, exact reviewed cpp_main global-graph media type, and corresponding ValidationEvidence; fixed source identity is guaranteed by pre-execution source admission, normalized request identity, and BuildManifest. It derives findings from ValidatedArtifact structural claims/evidence without reparsing native bytes, rejects unknown/mismatched media types, and leaves the existing Fake JSON evaluator identity, version, and behavior unchanged.
  - **依赖**: T022, T021.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_baseline_runner cpp_main_pipeline fake_pipeline_e2e`, `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_pipeline|fake_pipeline_e2e)$' --output-on-failure`, and `.venv/bin/python -m pytest tests/python/test_harness_fake_pipeline_e2e.py -q`.
  - **完成条件**: The cpp_main native evaluator tests, `fake_pipeline_e2e`, and `tests/python/test_harness_fake_pipeline_e2e.py` all pass. The Fake EvaluationReport evaluator identity/version remain unchanged; the independent lifecycles of Fake Artifact, BuildManifest, ValidationEvidence, and EvaluationReport remain unchanged; and the closed cpp_main condition does not alter Fake runner behavior. Native and Fake reports use their exact distinct identity/version objects; no unvalidated or unknown-media Artifact is evaluated; the runner is not installed; no evaluator Registry/plugin/generic dispatch, native-parser duplicate, Recall/QPS/latency/Parity logic, Fake/paper/Adapter factory, validated_graph_baseline registration, or cross-Partition concurrency is added.

- [ ] T025 [US3] Implement the explicit preflight/execute lifecycle and closure-report writing in harness/python/diskann_harness/cpp_main_baseline.py
  - **输入**: T023 tests, existing duplicate-key-rejecting SafeLoader/workspace/immutable JSON helpers, baseline schema/profile, fixed Dataset source path and checksum, Source/Build Identity evidence, and cpp_main_baseline_runner.
  - **输出**: `preflight` safely normalizes and validates the Profile/request, validates Source/Build Identity, copies the fixed Dataset bytes without transformation into a confined immutable DatasetArtifact payload, verifies source/destination checksums, emits Dataset ValidationEvidence, verifies the sharded-path predicate, writes immutable build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json, returns its path plus Dataset ArtifactRef, and records zero P0/B0/M0 invocations without starting the runner or creating any Stage factory. `execute` requires `--request`, verifies that immutable request and referenced validated DatasetArtifact, invokes cpp_main_baseline_runner once without repeating `--config` admission, and writes specs/002-cpp-main-upstream-baseline/unified-sharded-report.json referencing separate Artifacts, BuildManifests, ValidationEvidence, and the cpp_main native graph-integrity EvaluationReport.
  - **依赖**: T023, T024, T008.
  - **最小测试**: Execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q`.
  - **完成条件**: Positive/negative preflight, execute, normalization, immutable-request, and report tests pass; preflight cannot load/start the runner or create a factory; execute cannot accept `--config` or mutate the request; the report never embeds Artifact payload/provenance objects, contains no Graph Parity result or performance evaluation, and a failure cannot publish a success report.

- [ ] T026 [US3] Update but do not freeze experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml and synchronize its invocation in specs/002-cpp-main-upstream-baseline/quickstart.md
  - **输入**: completed implementation IDs/schema fields, research.md SIFT1M identity and Native Probe values, actual runner path, Source/Build admission requirements.
  - **输出**: executable draft profile with status: draft, exact SIFT1M/R/L/M/T/mode values and all derived/default stage parameters, plus a quickstart preflight command using this single Profile and an execute command using only the generated immutable request; no observed result, parity threshold, repetition protocol, frozen flag, registration, automatic retry, or performance metric.
  - **依赖**: T009, T015, T025.
  - **最小测试**: Execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q`; the test must also assert that the documented Profile, runner, workspace, and report paths are the planned repository-relative paths.
  - **完成条件**: The profile remains explicitly draft before and after execution, all values are traceable to source/probe evidence, and no parameter may be silently changed after a failed run.

- [ ] T027 [US3] Pass the complete small-data pre-SIFT gate and retain logs under build/cpp-main-baseline/test-results/phase3-pre-sift/
  - **输入**: T024–T026, all existing and new cpp_main unit/contract/integration tests.
  - **输出**: Release configure/build log, full cpp_main CTest log, existing C++ Fake E2E log, baseline and existing Fake Python Harness E2E logs, existing applicable Artifact/Registry/Pipeline regression log, git diff --check, upstream before/after state, and separate executed, failed, and applicable-unexecuted inventories. The cpp_main tests, C++ Fake E2E, and Python Fake Harness E2E are all pre-SIFT blocking tests.
  - **依赖**: T024, T025, T026.
  - **最小测试**: Execute `cmake --build build/cpp-main-baseline --target cpp_main_link_smoke cpp_main_source_build_admission cpp_main_native_formats cpp_main_descriptors_preflight cpp_main_p0 cpp_main_b0 cpp_main_m0 cpp_main_pipeline cpp_main_baseline_runner fake_pipeline_e2e`, `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_.*|fake_pipeline_e2e)$' --output-on-failure`, and `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py tests/python/test_harness_fake_pipeline_e2e.py -q`.
  - **完成条件**: Every applicable pre-SIFT cpp_main test, C++ `fake_pipeline_e2e`, and Python Fake Harness E2E test passes; any failure prohibits T028 from starting. Executed, failed, and applicable-unexecuted tests are recorded separately; upstream is pinned and clean.

- [ ] T028 [US3] Admit the fixed SIFT1M Dataset and sharded profile without starting graph construction using experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml
  - **输入**: T027 passing gate, sift1M_dataset/base_vector.bin, pinned source/build identities, and the draft profile.
  - **输出**: confined immutable DatasetArtifact and ValidationEvidence under build/cpp-main-baseline/runs/sift1m-unified/, pre-execution proof of 1,000,000×128 float32, 512,000,008 bytes, finite payload, exact SHA-256, source-derived strict sharded predicate and upstream clean state, plus immutable normalized request build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json and a terminal response containing its path and the Dataset ArtifactRef.
  - **依赖**: T027.
  - **最小测试**: Execute `.venv/bin/python -m diskann_harness.cpp_main_baseline preflight --config experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml --workspace build/cpp-main-baseline/runs/sift1m-unified` and `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q`.
  - **完成条件**: Dataset source and admitted bytes have identical checksum; estimated full-index RAM is greater than or equal to the M budget for the actual ABI; the exact immutable request and returned Dataset ArtifactRef exist and validate; the runner was not started, no Stage factory was created, P0/B0/M0 invocation counts are all zero, selected_query.csv was not read, and the draft profile was not modified.

- [ ] T029 [US3] Execute exactly one SIFT1M Unified sharded closure and generate specs/002-cpp-main-upstream-baseline/unified-sharded-report.json
  - **输入**: T028 admitted Dataset ArtifactRef and immutable build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json bound to the unchanged draft profile, cpp_main_baseline_runner, and writable workspace build/cpp-main-baseline/runs/sift1m-unified/.
  - **输出**: ordered P0 → B0 × N → M0 Artifacts, build/stage BuildManifests, ValidationEvidence, separate graph-integrity EvaluationReport with evaluator object `{implementation_id: cpp_main_native_graph_integrity_evaluator, version: 1.0.0}`, logs, terminal runner result, and specs/002-cpp-main-upstream-baseline/unified-sharded-report.json. The EvaluationReport is neither a Graph Parity Report nor a performance report.
  - **依赖**: T028.
  - **最小测试**: Execute `.venv/bin/python -m diskann_harness.cpp_main_baseline execute --request build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json --workspace build/cpp-main-baseline/runs/sift1m-unified --runner build/cpp-main-baseline/src/baseline/cpp_main/cpp_main_baseline_runner` exactly once, then execute `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py -q` to revalidate emitted Artifact/EvaluationReport references and parse specs/002-cpp-main-upstream-baseline/unified-sharded-report.json.
  - **完成条件**: Execute consumes but does not rewrite the exact T028 request and does not repeat admission through `--config`; the run succeeds without parameter mutation/retry; P0 produces at least two Partitions and no shard vectors; every explicit Partition receives exactly one sequential B0; M0 receives the complete valid set; final graph has 1,000,000 GlobalNodeId records and required valid medoids; all format/version/ID-space/capability/lineage/checksum gates pass; the separate native graph-integrity EvaluationReport validates; the report contains every plan-defined minimum field with structural_validation=PASS; upstream remains pinned and clean. On failure, evidence/report is retained but T029 remains incomplete and no modified-Profile retry is allowed.

- [ ] T030 [US3] Perform the final structural-closure audit and retain validation logs under build/cpp-main-baseline/test-results/phase3-final/
  - **输入**: T029 successful outputs, all Feature 002 tests, current Git/upstream state, specs/002-cpp-main-upstream-baseline/unified-sharded-report.json.
  - **输出**: Final executed/failed/unexecuted test inventory covering the cpp_main evaluator path, the existing Fake C++ evaluator E2E, and the existing Fake Python Harness/EvaluationReport lifecycle; CTest/pytest results; Artifact/Manifest/evidence/report reference audit; git diff --check result; upstream before/after identity; and scope audit.
  - **依赖**: T029.
  - **最小测试**: Execute `ctest --test-dir build/cpp-main-baseline -R '^(cpp_main_.*|fake_pipeline_e2e|registry_selection|compatibility_preflight|valid_artifacts|artifact_contract_amendment|invalid_artifact_metadata|invalid_graph_properties|pipeline_orchestration|document_codecs)$' --output-on-failure`, `.venv/bin/python -m pytest tests/python/test_cpp_main_baseline_config.py tests/python/test_harness_fake_pipeline_e2e.py -q`, `git diff --check`, `git -C upstream/diskann-cpp-main rev-parse HEAD`, and `git -C upstream/diskann-cpp-main status --porcelain=v1`.
  - **完成条件**: All applicable tests pass, and results for the cpp_main evaluator path, existing Fake C++ evaluator E2E, and existing Fake Python Harness/EvaluationReport lifecycle are all included in the final executed/failed/unexecuted inventory. Similar assertions in `cpp_main_pipeline` do not substitute for `fake_pipeline_e2e`. The report says profile_status: draft, formal_graph_parity: false, validated_graph_baseline_registered: false; no Graph Parity, validated_graph_baseline, performance evaluation, repeat-selection, larger dataset, upstream modification, generic Adapter, concurrency, auto-tuning, Loop Engine, or Feature 001 deferred hardening was introduced.

**Phase 3 checkpoint**: The first Unified SIFT1M sharded structural closure is auditable and
complete. It is not Graph Parity, the Profile remains draft, and Baseline registration is still
prohibited.

## Dependencies and Execution Order

### Phase dependency graph

~~~text
Phase 1: T001
  -> T002
  -> T003
  -> T005
  -> T006/T007
  -> T008
  -> T010

  T004 -> T009 -> T010

Phase 2: T010
  -> T011–T014
  -> T015/T017
  -> T016/T018/T019/T020
  -> T021

Phase 3: T021
  -> T022/T023
  -> T024
  -> T025
  -> T026
  -> T027
  -> T028
  -> T029
  -> T030
~~~

- Phase 1 is blocking for every Stage Binding.
- Phase 2 is blocking for runner assembly and any SIFT1M Unified execution.
- Phase 3 small-data tests and preflight are blocking for the single SIFT1M run.
- No task authorizes a dataset larger than SIFT1M.

### Parallel opportunities

- T001, T002, and T003 are intentionally sequential because each later red test depends on the
  preceding CMake/CTest scaffold; T004 may proceed in parallel with that chain.
- After T005, T006 and T007 can proceed in parallel; T009 can proceed independently after T004.
- T011, T012, T013, and T014 can be authored in parallel after Phase 1.
- After T015, P0 T016 and M0 T019 can proceed in parallel; the accessor T017 can proceed alongside
  them, while B0 T018 waits for T017.
- T020 touches only the existing executor and may proceed alongside stage implementations after
  descriptor tests exist.
- T022 and T023 can be authored in parallel. All real execution after T027 is intentionally
  sequential.

### Parallel example

~~~text
Phase 1:
  Sequential chain: T001 native-format scaffold/red test
                    -> T002 source/build-admission target/red test
                    -> T003 link-smoke target/red test
                    -> T005 real diskann_s integration
  Parallel to that chain: T004 draft profile tests

Phase 2:
  Task T012: P0 tests
  Task T013: B0 tests
  Task T014: M0 tests

Phase 3:
  Task T022: C++ small Pipeline tests
  Task T023: Python config/report tests
~~~

## Implementation Strategy

### Suggested MVP

Phase 1 is the minimum independently reviewable increment: it proves the fixed clean source/build
boundary, real libdiskann linkage, and native-format validation without exposing an incomplete
Stage Binding.

### Incremental delivery

1. Complete Phase 1 and stop if any source/build/format identity is uncertain.
2. Complete Phase 2 and validate P0, B0, and M0 separately on small inputs.
3. Complete Phase 3 pre-SIFT tests and admission before authorizing the one SIFT1M Unified run.
4. Stop after the structural closure. A later explicit planning decision is required for Profile
   freezing, Graph Parity, or validated_graph_baseline registration.

## Notes

- Every checkbox remains open until its stated minimum test and completion condition pass.
- Test-definition tasks may complete after recording their intended pre-implementation failure;
  implementation tasks require the corresponding test to pass.
- Any unexpected need to change a public Contract, modify upstream, copy an algorithm, introduce a
  generic Adapter, alter randomness, or expand beyond SIFT1M stops implementation and returns to
  explicit design review.
- Failed, invalid, or cancelled runs remain retained evidence and never enter successful aggregation.
