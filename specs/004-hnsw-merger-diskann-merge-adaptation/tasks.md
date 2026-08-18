# Tasks: HNSW-Merger DiskANN Merge Adaptation

**Input**: Approved design documents in
`specs/004-hnsw-merger-diskann-merge-adaptation/`

**Authority**: `spec.md`, `plan.md`, `research.md`, `source-mapping.md`, `data-model.md`,
`contracts/`, and `quickstart.md`. Tasks implement the frozen design; they do not reopen algorithm,
lambda, merge-order, overlap, reuse-path, or contract-gap decisions.

**Tests**: Every behavior task is test-first. Add the named cases, run them to expose the missing
behavior, implement the minimum change, and rerun the exact focused commands. A compile-only result
does not complete a task.

**Scope guard**: All writes and builds stay outside `upstream/diskann-cpp-main/` and the audited
HNSWMerger source. This Feature does not build or invoke an HNSW index, add a generic graph
framework, modify M0/P0/B0, run Query/Search, tune parameters, or execute data above SIFT1M.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: safe to implement independently in different files with no unfinished dependency.
- **[US1]**, **[US2]**, **[US3]**: task ownership by the three stories in `spec.md`.
- Setup and Foundational tasks intentionally have no story label.

---

## Phase 1: Setup - Fixed Source Identity and Build Wiring

**Purpose**: Admit the already-audited source basis and establish only the build/test surface used by
this Feature.

- [x] T001 Establish the fixed source/build boundary and minimal Feature CMake wiring in `CMakeLists.txt`, `src/CMakeLists.txt`, `src/papers/hnsw_merger/vamana/CMakeLists.txt`, `tests/cpp/CMakeLists.txt`, and `tests/cpp/papers/hnsw_merger/CMakeLists.txt`
  - **Inputs**: pinned HNSWMerger identity
    `https://github.com/Kimchuls/HNSWMerger.git` / `main` /
    `88f6b7179f651cf5f284bd4e52249a4b1323b5af`, paper DOI/PDF checksum from
    `research.md`, and pinned DiskANN identity `78256bbab4685e1774e78d331e081a153be26823`.
  - **Work**: add `DISKANN_HNSW_MERGER`; conditionally expose the approved Feature source and test
    directories; link the future project-owned M1 target only to existing project/DiskANN
    dependencies; keep the official HNSWMerger tree source-mapping-only and never compile, copy, or
    invoke its HNSW experiment binary. Configure all output under
    `build/hnsw-merger-merge-adaptation/`.
  - **Minimum validation**: verify both source identities and clean states read-only, then run:
    ```sh
    cmake -S . -B build/hnsw-merger-merge-adaptation \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
      -DDISKANN_CPP_MAIN_BASELINE=ON \
      -DDISKANN_HNSW_MERGER=ON
    ```
  - **Complete when**: configure succeeds without referring to not-yet-created targets; the fixed
    identities and `integration_mode=source_mapping_only`/`official_binary_linked=false` projection
    have one implementation-facing definition; generated files are outside both upstream trees;
    DiskANN and the audited HNSWMerger checkout remain clean. If a trustworthy HNSWMerger checkout
    is unavailable, record that check as blocked rather than substituting moving `main`.

---

## Phase 2: Foundational - Existing GlobalGraph Contract Enforcement

**Purpose**: Close the one concrete shared-contract enforcement gap proven by the Plan before M1
can publish a graph.

**CRITICAL**: T002 must pass before any M1 output is treated as valid.

- [x] T002 Add test-first native GlobalGraph self-edge rejection to `tests/cpp/contract/test_valid_artifacts.cpp`, `tests/cpp/integration/test_artifact_reuse_core.cpp`, and the existing native validator path in `src/baseline/cpp_main/native_formats.cpp`
  - **Inputs**: existing valid native GlobalGraph fixtures and existing M0/M1 media/capability
    contract; do not change schemas, format versions, or canonical checksum rules.
  - **Test first**: mutate a native graph to contain `node -> node`, recompute the candidate
    Artifact's own aggregate checksum, and prove the current gap by expecting validator rejection
    before evaluation/reuse while keeping legal M0-style graphs valid.
  - **Implementation**: add only the self-edge check to the existing native GlobalGraph validation
    path; use the current Validator/failure lifecycle and do not create another validator.
  - **Minimum test**:
    ```sh
    cmake --build build/hnsw-merger-merge-adaptation \
      --target valid_artifacts artifact_reuse_core pipeline_failure_gates cpp_main_m0 cpp_main_pipeline
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^(valid_artifacts|artifact_reuse_core|pipeline_failure_gates|cpp_main_m0|cpp_main_pipeline)$' \
      --output-on-failure
    ```
  - **Complete when**: a legal native GlobalGraph still validates; a self-edge returns invalid
    output before evaluation or reuse; existing M0 tests pass; no public contract or M0 algorithm
    semantics changed.

**Checkpoint**: The existing GlobalGraph validity grant now enforces the output property required
by both M0 and M1.

---

## Phase 3: User Story 1 - Produce a Valid Multi-Shard M1 Global Graph (Priority: P1) 🎯 MVP

**Goal**: Add one independently selectable, source-audited Vamana faithful-adaptation Merge that
consumes existing local graphs and publishes an existing GlobalGraphArtifact/v1.

**Independent Test**: On deterministic multi-shard fixtures including overlap, select M1 and prove
paper forward/lazy-backward behavior, the frozen large-first composition, legal native output,
medoids, lineage, and acceptance by the existing Validator.

- [x] T003 [US1] Define and test the M1 descriptor, fixed parameters, and provenance projection in `tests/cpp/papers/hnsw_merger/test_m1_semantics.cpp`, `src/papers/hnsw_merger/vamana/stage_binding.hpp`, `src/papers/hnsw_merger/vamana/descriptors.cpp`, and `contracts/papers/hnsw-merger-v1/schemas/hnsw-merger-vamana-parameters.schema.json`
  - **Depends on**: T001-T002.
  - **Test first**: register failing expectations for identity
    `hnsw_merger_vamana_merge_v1@1.0.0`, Merge StageKind, exact two-input
    requires/provides/version/ID-space/capability/service declaration, M0/M1 coexistence with no
    overwrite, accepted frozen constants, and rejection of missing/additional/wrong/free-lambda or
    strategy-selector parameters. Add the base stage-owned provenance contract cases: a confined,
    terminal existing BuildManifest/v1 can be returned through unchanged
    `StageResult.build_manifest_path`; existing PipelineExecutor persists and references it only as
    parent `parameters.stage_execution_manifest {path, sha256}`; execution facts are absent from the
    parent `stage_parameters`; empty paths preserve M0/legacy parent parameters.
  - **Implementation**: create the descriptor/factory declaration and byte-identical runtime copy
    of the approved parameter schema; in the same task, update
    `src/papers/hnsw_merger/vamana/CMakeLists.txt` and
    `tests/cpp/papers/hnsw_merger/CMakeLists.txt` to register the first M1 library and
    `hnsw_merger_m1_semantics` target before rerunning the canonical parent configure. Define the
    M1 stage-owned existing BuildManifest projection without changing StageResult or any Manifest
    schema: parent `source` identifies the compiled project adaptation; the stage-owned document
    retains pinned paper/DiskANN identities, paper DOI/PDF checksum, evidence classifications,
    `integration_mode=source_mapping_only`, `official_binary_linked=false`, and later execution
    facts. Do not put those facts in the algorithm parameter schema or copy them into parent stage
    parameters; do not embed source commits in `implementation_id`.
  - **Minimum test**:
    ```sh
    cmake -S . -B build/hnsw-merger-merge-adaptation \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DDISKANN_CPP_MAIN_BASELINE=ON -DDISKANN_HNSW_MERGER=ON
    cmake --build build/hnsw-merger-merge-adaptation --target hnsw_merger_m1_semantics
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^hnsw_merger_m1_semantics$' --output-on-failure
    ```
  - **Complete when**: descriptor and parameter negatives pass, schema files are byte-identical,
    the existing stage-owned-manifest reference contract is covered without API/schema change, M0
    and M1 resolve separately under existing Registry rules, and no alternate strategy or sweepable
    lambda is admitted.

- [x] T004 [US1] Implement test-first LocalGraph loading and the frozen overlap/replica projection in `tests/cpp/papers/hnsw_merger/test_m1_semantics.cpp`, `src/papers/hnsw_merger/vamana/m1_algorithm.hpp`, and `src/papers/hnsw_merger/vamana/m1_algorithm.cpp`
  - **Depends on**: T003.
  - **Test first**: cover native LocalNodeId-to-GlobalNodeId mapping, multiple ordered
    LocalGraphs, exact Dataset/Partition/Local lineage, one-Partition input, valid shared replicas,
    conflicting replica-local adjacency union and candidate deduplication. Keep gate expectations
    explicit: common missing/duplicate/unknown/out-of-order ownership, corrupt identity/map/neighbor
    and lineage errors are pre-invocation cases owned by T007's existing admission path; incomplete
    combined coverage, duplicate same-operand logical records and other frozen M1-only semantic
    errors detected after complete input parsing return existing failure status with empty
    `output_candidate` before normal publication.
  - **Implementation**: load through existing cpp_main native readers/`InMemGraphStore`; resolve
    vectors only through existing `VectorAccessor` and L2 `DistanceFunction`; build the private
    GlobalNodeId-keyed logical views and apply exactly the approved replica candidate projection.
    Validate IDs before deduplication and fail self edges rather than silently removing them.
  - **Minimum test**: rebuild and run only `hnsw_merger_m1_semantics` with the exact commands from
    T003.
  - **Complete when**: legal cross-shard replicas remain as candidate evidence but yield one logical
    node per GlobalNodeId; common admission errors remain pre-invocation, M1-specific semantic errors
    return no normal candidate, and no new Artifact, semantic preflight or generic replica
    abstraction exists.

- [x] T005 [US1] Implement and test the two-index paper Merge core in `tests/cpp/papers/hnsw_merger/test_m1_semantics.cpp`, `src/papers/hnsw_merger/vamana/m1_algorithm.hpp`, and `src/papers/hnsw_merger/vamana/m1_algorithm.cpp`
  - **Depends on**: T004.
  - **Test first**: use deterministic graphs/vectors to assert smaller-to-larger direction,
    target-entry start, immutable target traversal, bounded forward hits, reverse candidate records,
    lazy backward direct-connect with no reverse search, existing-neighbor plus merge-candidate
    combination, `(distance, GlobalNodeId)` tie-break, duplicate collapse, self/range failure, fixed
    Vamana occlusion behavior, and degree `<= R`. Deterministically cancel at stage start and after
    input loading/semantic preparation; each case returns `CANCELLED`, no `output_candidate`, and no
    search/prune work after the observed checkpoint.
  - **Implementation**: add only the private `hnsw_merger_single_layer_search` faithful Algorithm 1
    helper and private `hnsw_merger_vamana_prune` translation fixed at R=16, alpha=1.2,
    max-candidates=750, no saturation, progressive alpha 1.0 then 1.2. Do not call or recreate an
    HNSW index and do not introduce generic search/prune/backend classes.
  - **Minimum test**: rebuild and run only `hnsw_merger_m1_semantics` with the exact commands from
    T003.
  - **Complete when**: fixtures prove forward discovery and lazy backward candidate generation,
    no reverse-search path exists, cancellation gates fail without a candidate, output is
    deterministic and degree-bounded, and the only new helpers remain private to this paper
    implementation.

- [x] T006 [US1] Complete the frozen multi-shard M1 stage and publish existing GlobalGraphArtifact/v1 in `tests/cpp/papers/hnsw_merger/test_m1_semantics.cpp`, `tests/cpp/papers/hnsw_merger/test_m1_native_output.cpp`, `src/papers/hnsw_merger/vamana/m1_algorithm.cpp`, and `src/papers/hnsw_merger/vamana/m1_merge.cpp`
  - **Depends on**: T005.
  - **Test first**: cover three-or-more inputs, unique-GlobalNodeId logical sizes, large-first pair
    selection, lexicographic PartitionId lineage tie-break, source/target direction, intermediate
    lineage, paper adaptive-log lambda epochs/floor/cap/reset, degenerate one-Partition mapping,
    mapped entry, ascending-Partition medoids, complete Dataset coverage, legal GlobalNodeIds,
    degree bound, exact source Artifact order, and checksum/lineage mutations. Include one small
    real input made from actual cpp_main native LocalGraph/map bytes plus the real vector/L2
    services; do not substitute an HNSW-format fixture. Deterministically cancel between pairwise
    rounds and immediately before normal candidate return; assert no new round starts and no normal
    candidate is returned. For a successful actual M1 invocation, parse the returned stage-owned
    BuildManifest/v1 and prove its source/toolchain, paper/DiskANN identities, evidence classes,
    effective policy and complete `PairwiseMergeDecision[]`; prove the parent reference path/SHA-256
    resolves to it and the parent does not duplicate those facts.
  - **Implementation**: perform the one approved in-memory large-first pairwise chain, record every
    `PairwiseMergeDecision` in the M1 stage-owned BuildManifest parameters, return that existing
    document through `StageResult.build_manifest_path`, retain the selected target entry, write
    native graph plus medoids with existing cpp_main format support, return the existing
    GlobalGraphArtifact/v1 candidate, and use existing candidate validation/publication. M1 never
    publishes an Artifact and no intermediate Artifact is published. Update
    `tests/cpp/papers/hnsw_merger/CMakeLists.txt` in this task to
    register `hnsw_merger_m1_native_output`, then rerun the canonical parent configure before its
    first build.
  - **Minimum test**:
    ```sh
    cmake --build build/hnsw-merger-merge-adaptation \
      --target hnsw_merger_m1_semantics hnsw_merger_m1_native_output
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^(hnsw_merger_m1_semantics|hnsw_merger_m1_native_output)$' \
      --output-on-failure
    ```
  - **Complete when**: the plan-frozen strategy is observably executed and fully traceable through
    the valid parent/stage-owned Manifest reference, cancellation produces no candidate, graph and
    medoids pass the existing Validator/evaluator path, illegal output fails closed, M0/legacy empty
    `build_manifest_path` behavior remains unchanged, and no M1-specific Artifact family or public
    report exists.

**Checkpoint**: US1 is independently complete when deterministic and native-output tests prove a
valid multi-shard M1 GlobalGraph through the existing validity lifecycle.

---

## Phase 4: User Story 2 - Compare M0 and M1 on Identical Upstream Graphs (Priority: P2)

**Goal**: Start exactly at one fresh-validated Partition plus complete LocalGraph set and run M0 and
M1 without rebuilding P0/B0.

**Independent Test**: Construct two Merge-only requests from the same immutable ArtifactRef values,
prove per-executor 0/0/1 stage counts and successful enclosing runner aggregate 0/0/2, and validate
the Feature-local same-input summary while keeping both output lifecycles separate.

- [x] T007 [US2] Implement and test the approved Merge-only executor amendment, Feature-private request normalization, and thin same-input runner in `tests/cpp/papers/hnsw_merger/test_merge_only_execution.cpp`, `tests/python/test_hnsw_merger_merge_adaptation.py`, `include/diskann_pipeline/pipeline.hpp`, `src/pipeline/pipeline_executor.cpp`, `harness/python/diskann_harness/hnsw_merger_merge_adaptation.py`, and `src/papers/hnsw_merger/vamana/hnsw_merger_experiment_runner.cpp`
  - **Depends on**: T006.
  - **Test first**: cover absent optional input preserving legacy behavior; rejection of simultaneous
    `reused_partition`; Dataset-first and fresh Partition/Local validation; complete, ordered,
    exact-owner collection; type/version/checksum/ID/capability/Dataset/lineage mismatch; no
    Partition or Local Build descriptor resolution/factory creation; per-executor Merge-only
    invocation counts 0/0/1 and stage order `[merge]`; successful enclosing runner aggregate
    counts 0/0/2; exact BuildManifest input order; M0/M1 identical ArtifactRef
    tuples; mismatched-reference comparison admission rejection; separate Artifacts, manifests,
    evidence and EvaluationReports; and fixed Baseline runner isolation. In Python/C++ contract
    cases, require one normalized Feature-private request with one shared Dataset/Partition/ordered-
    Local tuple; one real Feature runner process consumes it via
    `execute --request FILE --result FILE`, invokes M0 and M1 once each from the same parsed refs,
    and never invokes P0/B0. Reject malformed/unknown request fields and invalid or mismatched input
    identities before either Merge factory; prove exactly one M0 and one M1 execution through the
    aggregate count plus the comparison summary's single M0 and single M1 BuildManifest references;
    keep public RunRequest, existing runner-result and comparison-summary schemas unchanged.
  - **Implementation**: add only optional `MergeOnlyInputSet` to the C++17 request and the closed
    executor branch specified by `contracts/merge-only-execution.md`; reuse Registry,
    compatibility preflight, Validator, quarantine, publication and evaluation. Register a real
    Feature runner target that consumes the one normalized comparison request, invokes the existing
    executor once for M0 and once for M1 using the one shared tuple, and writes only the approved
    Feature-local comparison summary. Implement the minimum Feature-private profile validation and
    immutable request writer in the named Python Harness module: Python preflight is the sole profile
    normalizer; the C++ runner validates the normalized wire document but does not normalize the
    profile again. In the same task, update
    `src/papers/hnsw_merger/vamana/CMakeLists.txt` and
    `tests/cpp/papers/hnsw_merger/CMakeLists.txt` to register
    `hnsw_merger_experiment_runner` and `hnsw_merger_merge_only_execution`, then rerun the canonical
    parent configure before either target is built; do not serialize a new public RunRequest field
    or copy the stage loop into the runner.
  - **Minimum test**:
    ```sh
    cmake --build build/hnsw-merger-merge-adaptation \
      --target hnsw_merger_merge_only_execution hnsw_merger_experiment_runner \
               public_api_cxx17 pipeline_orchestration artifact_reuse_core
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^(hnsw_merger_merge_only_execution|public_api_cxx17|pipeline_orchestration|artifact_reuse_core)$' \
      --output-on-failure
    .venv/bin/python -m pytest \
      tests/python/test_hnsw_merger_merge_adaptation.py -q
    ```
  - **Complete when**: one normalized request and one runner invocation drive both implementations
    from the same in-memory ordered references, no P0/B0 factory is created or invoked, common
    contract/lineage errors fail before M1 invocation, every reused input is freshly validated,
    legacy executor and M0 semantics pass regression, and the comparison summary traces the shared
    inputs/two Merge executions but grants no Artifact validity.

**Checkpoint**: US2 is complete when the exact-same-input experiment can be constructed without a
second P0/B0 run or a second orchestration implementation.

---

## Phase 5: User Story 3 - Bounded SIFT1M Structural Validation (Priority: P3)

**Goal**: First prove the real composed path at small scale and pass every applicable blocking
regression, then execute and audit the one frozen bounded SIFT1M structural validation.

**Independent Test**: The built Feature runner first completes a real small P0+B0+M1 closure and a
real small same-input M0/M1 Merge-only comparison with all affected regressions passing; the same
one-request/one-runner model then completes the admitted SIFT1M structural comparison.

- [x] T008 [US3] Build the real small closures, thin Harness path, and blocking regressions in `tests/cpp/papers/hnsw_merger/test_m1_pipeline.cpp`, `tests/python/test_hnsw_merger_merge_adaptation.py`, and `harness/python/diskann_harness/hnsw_merger_merge_adaptation.py`
  - **Depends on**: T007 and all US1/US2 focused tests passing.
  - **Test first**: make the C++ test execute a small real M1, a real multi-shard M1, one normal
    P0+B0+M1 chain, an invalid-upstream fail-closed case, and a same-input M0/M1 Merge-only run;
    make the Python test launch the built `hnsw_merger_experiment_runner` rather than reproduce the
    stage loop. Assert existing P0/B0/M0 identities and fixed Baseline behavior are unchanged,
    GlobalGraph validation/evaluation succeeds, and the comparison has identical inputs and
    separate output lifecycles.
  - **Implementation**: finish only the runner registration/configuration and extend the T007 thin
    Python normalization/invocation path as needed by those real small tests. Update
    `tests/cpp/papers/hnsw_merger/CMakeLists.txt` in this task to register
    `hnsw_merger_m1_pipeline`, then rerun the canonical parent configure before its first build.
    Do not create the SIFT1M profile or run SIFT1M preflight in this task.
  - **Minimum test and blocking regression**:
    ```sh
    cmake --build build/hnsw-merger-merge-adaptation \
      --target hnsw_merger_experiment_runner hnsw_merger_m1_pipeline \
               valid_artifacts artifact_reuse_core pipeline_orchestration pipeline_failure_gates \
               fake_pipeline_e2e public_api_cxx17 cpp_main_descriptors_preflight cpp_main_m0 cpp_main_pipeline
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^(valid_artifacts|artifact_reuse_core|pipeline_orchestration|pipeline_failure_gates|fake_pipeline_e2e|public_api_cxx17|cpp_main_descriptors_preflight|cpp_main_m0|cpp_main_pipeline|hnsw_merger_.*)$' \
      --output-on-failure
    .venv/bin/python -m pytest \
      tests/python/test_cpp_main_baseline_config.py \
      tests/python/test_harness_fake_pipeline_e2e.py \
      tests/python/test_hnsw_merger_merge_adaptation.py -q
    ```
  - **Complete when**: all focused and blocking regressions pass; real small M1, P0+B0+M1, and
    same-input M0/M1 paths pass; invalid upstream input blocks Merge; existing M0, fixed Baseline,
    Fake C++/Python E2E, public C++17 and cpp_main pipeline behavior remain unchanged. Record
    executed, failed and applicable-unexecuted checks before proceeding.

**Checkpoint**: T008 is the blocking pre-SIFT prerequisite for US3; it does not complete US3. No
SIFT1M preflight or Merge may start before every listed small-real and regression result passes.

- [x] T009 [US3] Admit and execute one same-input SIFT1M M0/M1 structural comparison and final audit using `experiments/configs/hnsw-merger-diskann-merge-adaptation/sift1m-structural.yaml`, `harness/python/diskann_harness/hnsw_merger_merge_adaptation.py`, `specs/004-hnsw-merger-diskann-merge-adaptation/validation/same-input-structural-comparison.json`, and `specs/004-hnsw-merger-diskann-merge-adaptation/quickstart.md`
  - **Depends on**: T008 PASS. The approved candidate inputs must be one fresh-validated
    DatasetArtifact, PartitionArtifact and complete ordered LocalGraphArtifact collection; do not
    rerun P0/B0.
  - **Test first**: before formal preflight/execution, keep/extend the existing Python admission and
    normalized-request contract tests to prove
    fixture preflight emits exactly one immutable normalized comparison request, its shared Dataset
    and Partition refs occur once, and its LocalGraph refs occur once in the frozen order. Reject
    malformed request shape, changed parameters, changed input identities, parameter sweep, tuning,
    preferred-result selection, unsupported query/performance fields and any dataset above SIFT1M.
    Runner-level test coverage, if needed beyond T008, MUST use a deterministic or small-real
    fixture request and prove that M0/M1 receive the same parsed tuple and execute once while P0/B0
    remain zero. It MUST NOT launch or otherwise consume
    `build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json`.
    Run these tests before creating the formal request; T008 already supplies the blocking real
    small-runner/composed-closure gate.
  - **Preflight**: create and review exactly one predeclared configuration with fixed Dataset
    checksum, ordered immutable input references, fixed M0/M1 source/build identities and
    parameters, thread budget one, no sweep/tuning, and the predeclared identical-config-only
    infrastructure retry rule. Reuse existing Harness normalization, Registry/parameter/source/
    build admission, resource/workspace checks, and fresh complete-lineage validation. Run:
    ```sh
    .venv/bin/python -m diskann_harness.hnsw_merger_merge_adaptation preflight \
      --config experiments/configs/hnsw-merger-diskann-merge-adaptation/sift1m-structural.yaml \
      --workspace build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix \
      --validation-runner build/hnsw-merger-merge-adaptation/src/cli/diskann_pipeline_runner \
      --container-image diskann-cpp-main-baseline-builder:jammy
    ```
    It must perform admission, fresh validation and profile normalization exactly once and write
    the one immutable Feature-private
    request defined by `contracts/merge-only-execution.md` at
    `build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json`
    for human review. Preflight MUST NOT start the Feature runner or execute any Merge and must leave
    P0/B0/M0/M1 factory invocation counts all zero. Candidate inputs under
    `build/cpp-main-baseline/runs/sift1m-unified/` are not trusted until this passes. A missing
    canonical committed project-adaptation identity or invalid candidate Artifact stops the task;
    do not rebuild P0/B0 or weaken admission.
  - **Execution**: only after T008 PASS, the T009 admission tests PASS, formal preflight PASS and
    human review of that exact immutable request, run the formal main path once:
    ```sh
    .venv/bin/python -m diskann_harness.hnsw_merger_merge_adaptation execute \
      --request build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json \
      --runner build/hnsw-merger-merge-adaptation/src/papers/hnsw_merger/vamana/hnsw_merger_experiment_runner \
      --container-image diskann-cpp-main-baseline-builder:jammy
    ```
    This is one reviewed immutable request -> one Feature runner process -> one M0 Merge -> one M1
    Merge. The Python execute path starts exactly one runner process; internally that runner invokes
    the existing PipelineExecutor once for M0 and once for M1 from the single shared tuple. No
    test-first step may launch this formal request. Run exactly the predeclared configuration. The
    retained identical-config infrastructure-failure rule is not an automatic second main-path
    execution: a terminal failed attempt remains retained, and any permitted retry is a separately
    reviewed attempt over the identical immutable request; never change inputs/parameters or select
    a best result.
  - **Required evidence**: each M0/M1 executor result has per-call invocation counts 0/0/1; the one
    unchanged runner-result records successful aggregate counts 0/0/2; and the Feature-local
    comparison contains exactly one M0 and one M1 run record with respective BuildManifest
    references, without new invocation-count fields. Together these prove exactly one execution per
    arm while preserving the existing schemas. M0 and M1 have identical ordered Dataset/Partition/
    Local Artifact identity tuples; both outputs have independent Artifact,
    BuildManifest, ValidationEvidence and native graph-integrity EvaluationReport lifecycles; each
    GlobalGraph fresh-validates; M1 proves 1,000,000 logical nodes, legal non-self GlobalNodeId
    adjacency, degree `<=16`, complete lineage, valid entry/medoids; the Feature-local comparison
    validates and reports shared-input and structural PASS without query/performance fields.
  - **Final validation**:
    ```sh
    ctest --test-dir build/hnsw-merger-merge-adaptation \
      -R '^(valid_artifacts|artifact_reuse_core|pipeline_orchestration|pipeline_failure_gates|fake_pipeline_e2e|public_api_cxx17|cpp_main_descriptors_preflight|cpp_main_m0|cpp_main_pipeline|hnsw_merger_.*)$' \
      --output-on-failure
    .venv/bin/python -m pytest \
      tests/python/test_cpp_main_baseline_config.py \
      tests/python/test_harness_fake_pipeline_e2e.py \
      tests/python/test_hnsw_merger_merge_adaptation.py -q
    git diff --check
    ```
  - **Complete when**: the reviewed formal request is launched once on the successful main path; the
    one request/one runner execution, comparison file at the stated Feature path and all independent
    evidence exist and validate; applicable tests are inventoried as executed/passed/failed/
    applicable-unexecuted; both upstream trees and the DiskANN gitlink remain unchanged/clean;
    `quickstart.md` records only commands and outcomes actually observed. The conclusion is limited
    to structural faithful adaptation and does not claim Graph Parity, complete HNSW-Merger
    reproduction, Recall, QPS, latency, performance improvement, tuning, or results above SIFT1M.
    This task, not T008, completes US3.

---

## Dependencies & Execution Order

### Phase Dependencies

```text
T001 Setup
  -> T002 existing GlobalGraph enforcement
  -> T003 descriptor/parameters
  -> T004 local input and overlap projection
  -> T005 two-index paper core
  -> T006 multi-shard native publication
  -> T007 same-input Merge-only execution
  -> T008 small-real and regression gate
  -> T009 SIFT1M preflight, one bounded execution, and final audit
```

- No task may consume an Artifact that has not passed the existing Validator freshly in that
  execution or preflight.
- T007 depends on a valid M1 from US1 but does not rerun P0/B0.
- T008 is the hard gate before T009. Any blocking CTest/pytest failure stops progression; any T009
  preflight failure stops execution.
- T009 may reuse an existing upstream Artifact set only after complete lineage/source/build
  validation; failure is not permission to rebuild or substitute a merely similar set.

### Parallel Opportunities

No whole task is marked `[P]`: these are intentionally coarse vertical tasks sharing the Feature
CMake target, M1 test surface, and ordered contract gates. Independent test cases inside a task may
run concurrently through CTest/pytest, but task completion remains sequential.

## Requirement Coverage

| Requirement group | Tasks |
|---|---|
| Fixed sources, faithful Merge-only scope, read-only upstreams (FR-001-FR-003, FR-007-FR-009, FR-028) | T001, T003, T005, T009 |
| M1 identity, declaration, required cancellation service and fixed parameter policy (FR-004, FR-006, FR-015-FR-016) | T003, T005-T006 |
| Existing inputs, multi-shard and overlap semantics (FR-005, FR-010-FR-012) | T004-T006 |
| Legal GlobalGraph, entry/medoids and existing Validator (FR-013-FR-014, FR-017-FR-018) | T002, T006 |
| Exact same-input Merge-only comparison and unchanged Baseline (FR-019-FR-022) | T007-T009 |
| Separate parent/stage-owned BuildManifest and complete execution provenance (FR-023) | T003, T006-T009 |
| Ordered small/SIFT1M structural validation and separate evidence (FR-024-FR-026) | T008-T009 |
| No Query/Search/performance claims (FR-027) | T009 |
| Formal source/build binding and upstream immutability (SC-001, SC-010) | T001, T003, T006, T008-T009 |
| Coexistence and two-layer fail-closed admission (SC-002-SC-003) | T002-T007 |
| Multi-shard output structure and ID/degree contract (SC-004-SC-005) | T004-T006, T008-T009 |
| Same-input identity and bounded single strategy/run (SC-006-SC-008) | T007-T009 |
| No unsupported reproduction or performance claim (SC-009) | T009 |

## Delivery Strategy

1. Complete T001-T002 to establish the only shared prerequisites.
2. Complete T003-T006 and stop to validate US1 independently; this is the implementation MVP.
3. Complete T007 to establish the fair same-input M0/M1 experiment without rebuilding upstream
   graph stages.
4. Complete T008 and stop after the small-real and regression inventory.
5. In T009, create and preflight the one immutable request; execute it only after preflight and
   review pass. Retain all valid, invalid, failed and allowed infrastructure-retry evidence; never
   select only a preferred result.
