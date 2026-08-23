# Tasks: DiskANN Post-GlobalGraph Search Evaluation

**Input**: Approved design documents in `specs/005-diskann-search-evaluation/`.

**Authority**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `contracts/`,
`quickstart.md`, `.specify/memory/constitution.md`, and repository `AGENTS.md`. These tasks implement
the frozen downstream design; they do not reopen QuerySet, self-match, K, entry, repetition,
aggregation, cache, metric, or source-mapping decisions.

**Test-first rule**: Every behavior or contract test task must be completed as a red test before its
corresponding implementation task. A build or compile result alone never completes a task. Only
tests that actually exist after the task's declared CMake wiring may be invoked.

**Scope guard**: Every write, build, fixture, and output remains outside
`upstream/diskann-cpp-main/`. This Feature never executes P0/B0/M0/M1, never calls full
`build_disk_index`, never adds a fourth Pipeline stage or Search Registry, and never implements an
optional multi-GlobalGraph comparison. Formal SIFT1M PQ generation, DiskLayout materialization,
exact GroundTruth generation, and search are all beyond the non-task manual boundary at the end of
this document.

## Format: `[ID] [P?] [Story] Description`

- **[P]** is used only for work with no unfinished dependency and no overlapping files.
- **[US1]** and **[US2]** identify the only two User Stories in `spec.md`.
- Setup, Foundational, and Final tasks intentionally have no User Story label.

---

## Phase 1: Setup - Fixed Build and Linux Symbol Gate

**Purpose**: Establish the one Feature-owned build/test surface and one Linux-only ABI/link smoke
without creating future-only abstractions.

- [X] T001 Establish the minimal Feature option, out-of-tree build wiring, and the single Linux ABI/link smoke in `CMakeLists.txt`, `src/CMakeLists.txt`, `src/search/diskann/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/cpp/CMakeLists.txt`, `tests/cpp/search/CMakeLists.txt`, and `tests/cpp/search/test_diskann_search_link_smoke.cpp`
  - **Inputs**: active Feature directory `specs/005-diskann-search-evaluation`, project branch
    `feature/search-evaluation`, pinned DiskANN identity
    `https://github.com/microsoft/DiskANN.git` / `cpp_main` /
    `78256bbab4685e1774e78d331e081a153be26823`, existing `diskann_cpp_main_upstream` /
    `diskann_s` integration, and the real CMake/test/Harness surfaces.
  - **Work**: add only `DISKANN_SEARCH_EVALUATION`; conditionally expose the Feature source and
    test directories; keep all output under `build/diskann-search-evaluation/`. Register exactly one
    Linux-native smoke target/CTest, `diskann_search_link_smoke`, that links the admitted fixed
    DiskANN build and proves the callable/symbol surface for
    `generate_quantized_data<float>`, `create_disk_layout<float>`, and
    `PQFlashIndex<float>` plus the required OpenMP/MKL/libaio ABI. Do not implement a second smoke
    later and do not write, generate, or format any upstream file.
  - **Dependency semantics**: `smoke_definition_ready` means this one target/CTest is registered and
    a non-Linux configure can truthfully identify it as inapplicable or Linux-only pending;
    `linux_native_smoke_pass` means the same smoke actually executed and passed with the applicable
    Linux toolchain. These names describe task dependencies only; do not persist them as enums,
    schema fields, Artifacts, Gate objects, or a new status system.
  - **Minimum validation**:
    ```sh
    cmake -S . -B build/diskann-search-evaluation \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
      -DDISKANN_CPP_MAIN_BASELINE=ON \
      -DDISKANN_SEARCH_EVALUATION=ON
    cmake --build build/diskann-search-evaluation --target diskann_search_link_smoke
    ctest --test-dir build/diskann-search-evaluation \
      -R '^diskann_search_link_smoke$' --output-on-failure
    ```
  - **Complete when**: configure uses only files created by this task; source/build identities and
    upstream clean state are recorded; and `smoke_definition_ready` is proven. On an applicable
    Linux toolchain the smoke must actually pass to establish `linux_native_smoke_pass`. On macOS or
    another non-normative environment, schema/contract/Harness/non-normative functional work may
    proceed after the definition is ready, but Linux native acceptance, formal performance claims,
    and formal SIFT1M performance eligibility remain `Linux-only pending`; common search admission
    and an explicitly approved quality run remain independently possible, with no substitute smoke
    or fabricated PASS.

---

## Phase 2: Foundational - Public Search-Asset Contracts

**Purpose**: Add only the public reusable PQ/DiskLayout data contracts required by both stories.

**CRITICAL**: T002 must expose the missing contract behavior, and T003 must make all foundational
tests pass, before either User Story may publish or consume a new search asset.

- [X] T002 Add failing contract, schema-source, codec, checksum, validator, and reuse tests for `PQArtifact/v1` and `DiskLayoutArtifact/v1` in `tests/python/test_contract_schemas.py`, `tests/python/test_contract_source_sync.py`, `tests/cpp/contract/test_public_api_cxx17.cpp`, `tests/cpp/contract/test_checksum_canonicalization.cpp`, `tests/cpp/contract/test_document_codecs.cpp`, `tests/cpp/contract/test_valid_artifacts.cpp`, `tests/cpp/contract/test_invalid_artifact_metadata.cpp`, `tests/cpp/integration/test_artifact_reuse_core.cpp`, `tests/cpp/search/test_search_artifact_contracts.cpp`, and `tests/cpp/search/CMakeLists.txt`
  - **Depends on**: T001 build/test wiring and `smoke_definition_ready`, not
    `linux_native_smoke_pass`.
  - **Test first**: cover additive `pq`/`disk_layout` ArtifactType strings, exact v1 formats and ID
    spaces, public C++17 projections, canonical aggregate checksum, exact source cardinality/order,
    payload path/length/SHA-256, wrong source/type/version/checksum, path escape, reuse only after
    VALID evidence, and strict rejection of additional or malformed fields. For DiskLayout, prove
    schema rejection of declared-centroid without a centroid payload and medoid fallback with a
    centroid payload. Prove existing Dataset/Partition/LocalGraph/GlobalGraph codecs, checksums,
    validators, and reuse behavior remain unchanged.
  - **Source sync**: require the runtime `common.schema.json` target to have the sole full source
    `specs/005-diskann-search-evaluation/contracts/common.schema.json`, and add sole Feature 005
    sources for PQArtifact/DiskLayoutArtifact. Every unrelated target remains mapped to its original
    Feature 001 source. Remove only the old all-sources-under-Feature-001 assumption: unique target
    ownership, path confinement, source/target existence, byte identity, and source SHA-256 checks
    remain fail-closed. Do not add overlay/patch/multi-source composition. Feature-local Protocol,
    Observation, and Report schemas remain absent from this foundational runtime inventory.
  - **Complete when**: the new cases are registered as `search_artifact_contracts`, fail for the
    expected missing runtime support, and no production implementation is added in this task.

- [X] T003 Implement the atomic additive PQ/DiskLayout Artifact contract and existing-validator integration from `specs/005-diskann-search-evaluation/contracts/common.schema.json`, `specs/005-diskann-search-evaluation/contracts/pq-artifact.schema.json`, and `specs/005-diskann-search-evaluation/contracts/disk-layout-artifact.schema.json` into `contracts/v1/SOURCES.json`, `contracts/v1/schemas/common.schema.json`, `contracts/v1/schemas/pq-artifact.schema.json`, `contracts/v1/schemas/disk-layout-artifact.schema.json`, `include/diskann_pipeline/identifiers.hpp`, `include/diskann_pipeline/artifacts.hpp`, `src/core/json_codec.cpp`, `src/core/document_codecs.cpp`, `src/validation/schema_catalog.cpp`, `src/validation/validation_gate.cpp`, `src/validation/artifact_validator.cpp`, `src/validation/validator.cpp`, `src/search/diskann/native_search_formats.hpp`, `src/search/diskann/native_search_formats.cpp`, and `src/search/diskann/CMakeLists.txt`
  - **Depends on**: T002 red tests.
  - **Implementation**: copy the approved full common and PQ/DiskLayout Feature 005 schema designs
    byte-identically into the runtime catalog with the explicit one-source-per-target mappings; the
    common source differs from the prior runtime v1 schema only by `pq`/`disk_layout`, while every
    unrelated target retains its original Feature 001 mapping. Add only the two Artifact types, metadata projections,
    codec/canonical-checksum paths, source/lineage checks, payload/native admission checks, and
    existing ValidationEvidence/reuse integration. Native validation parses only the pinned
    headers/offsets/counts needed by the public contract and reuses existing native helpers where
    possible; it must not reproduce a full PQFlash reader or create another parser/validator
    framework. Existing Artifact schemas and checksum projections do not change.
  - **Minimum test**:
    ```sh
    cmake --build build/diskann-search-evaluation \
      --target search_artifact_contracts public_api_cxx17 checksum_canonicalization \
               document_codecs valid_artifacts invalid_artifact_metadata artifact_reuse_core
    ctest --test-dir build/diskann-search-evaluation \
      -R '^(search_artifact_contracts|public_api_cxx17|checksum_canonicalization|document_codecs|valid_artifacts|invalid_artifact_metadata|artifact_reuse_core)$' \
      --output-on-failure
    .venv/bin/python -m pytest \
      tests/python/test_contract_schemas.py \
      tests/python/test_contract_source_sync.py -q
    ```
  - **Complete when**: every positive/negative/source-sync regression passes; PQ and DiskLayout
    remain Artifact/BuildManifest/ValidationEvidence-separated; no SearchProtocol, GroundTruth,
    Observation, Report, search/evaluator identity, new Registry, or new validation subsystem has
    entered Foundational.

**Checkpoint**: The two reusable search-asset types can be represented, checksummed, validated,
and reused without implementing either User Story.

---

## Phase 3: User Story 1 - Materialize a Searchable Disk Index (Priority: P1) - MVP

**Goal**: From freshly validated Dataset and GlobalGraph inputs, produce independently valid and
loadable PQArtifact and DiskLayoutArtifact without rebuilding or mutating the graph.

**Independent Test**: A small real float32/L2 Dataset and GlobalGraph are freshly validated,
materialized through the pinned post-graph callables, validated as separate assets, and loaded by
`PQFlashIndex<float>` while the input graph checksum and P/B/M non-execution evidence remain
unchanged.

- [X] T004 [US1] Add failing materialization, admission, publication, loadability, and non-reexecution tests in `tests/cpp/search/test_post_graph_materialization.cpp`, `tests/python/test_diskann_search_evaluation.py`, `tests/fixtures/search/pq-materialization.yaml`, `tests/fixtures/search/disk-layout-materialization.yaml`, and `tests/cpp/search/CMakeLists.txt`
  - **Depends on**: T003.
  - **Test first**: cover fresh Dataset/complete-GlobalGraph lineage validation; SIFT1M/L2/float32/
    native-format admission; positive-degree post-graph compatibility; fixed
    `generate_quantized_data<float>` parameters; PQ independence from GlobalGraph and byte reuse;
    fixed `create_disk_layout<float>` full-precision/PQ-disk-0/reorder-false semantics; GlobalNodeId
    row order; input graph checksum before/after; exact Dataset+one-GlobalGraph lineage; current
    runtime `medoid_vector_fallback`; native payload parse; and a real
    `PQFlashIndex<float>` load probe. Prove the existing schema still validates the two centroid
    cardinality branches, but runtime materialization rejects `declared_centroids` as unsupported
    before the callable because current GlobalGraph/v1 has no source centroid payload.
  - **Failure cases**: invalid/stale inputs fail before the callable; failed/cancelled operations
    retain separate manifests/logs; invalid candidates retain ValidationEvidence/quarantine but do
    not publish a normal Artifact. Reuse existing control-flow or invocation evidence to prove
    Partition/Local Build/Merge were not selected or invoked; add no production counter or P/B/M
    instrumentation.
  - **BuildManifest tests**: PQ generation and DiskLayout materialization must each produce an
    independent existing BuildManifest/v1 owned by `diskann_search_evaluation_runner`, with no
    duplicate Python-owned manifest for the same invocation. Using the existing manifest validator/
    helpers and operation fixtures, jointly verify implementation identity/version; pinned
    repository, branch, full commit, and dirty state; toolchain/build flags; Dataset plus every
    operation input identity and checksum; complete parameters and resource budget; `started_at`/
    `ended_at`; terminal status; output references; and failure reason when failed/invalid/cancelled.
    Verify payloads, ValidationEvidence, and BuildManifest stay separate and no Recall/QPS/latency/
    IO appears in a manifest. Do not create per-field/status test files, builders, or a provenance
    framework.
  - **Complete when**: the `post_graph_materialization` and Harness cases are registered and fail
    specifically because the Feature materializer/runner is not yet implemented.

- [X] T005 [US1] Implement fixed PQ and DiskLayout descriptors plus native post-graph materialization in `src/search/diskann/fixed_descriptors.hpp`, `src/search/diskann/fixed_descriptors.cpp`, `src/search/diskann/post_graph_materializer.hpp`, `src/search/diskann/post_graph_materializer.cpp`, and `src/search/diskann/CMakeLists.txt`
  - **Depends on**: T004 red tests.
  - **Implementation**: add fixed identities `cpp_main_post_graph_pq_v1@1.0.0` and
    `cpp_main_post_graph_disk_layout_v1@1.0.0` with exact requires/provides, format versions, ID
    spaces, capabilities, and runtime services; do not register a graph StageKind. Fresh-validate
    inputs through the existing Validator, run the narrow positive-degree compatibility gate, call
    only `generate_quantized_data<float>` and `create_disk_layout<float>`, and return candidates for
    existing validation/publication. PQ uses B=0.03, QD=0, chunks=32, sample=0.256, 256 centers,
    OPQ=false. Layout uses full-precision float32, PQ disk bytes 0, reorder/labels false, and only
    pinned medoid-vector fallback. Never call `build_disk_index`, generate a centroid, or modify the
    input Dataset/GlobalGraph.
  - **Evidence lifecycle**: the C++ native materialization path used by
    `diskann_search_evaluation_runner` writes the invocation's sole independent existing
    BuildManifest/v1; populate the complete provenance asserted by T004 and retain that terminal
    manifest for failed, invalid, and cancelled invocations. Candidate validation writes independent
    ValidationEvidence; only VALID candidates publish. Artifact metadata never embeds generation
    evidence or metrics, and no new manifest/provenance abstraction is introduced.
  - **Minimum test**: build and run `post_graph_materialization` only, then rerun
    `search_artifact_contracts` and `valid_artifacts`.
  - **Complete when**: small PQ and layout candidates are native-parseable and valid, invalid
    output is quarantined, graph bytes/checksum stay identical, and no P/B/M or declared-centroid
    runtime path exists.

- [X] T006 [US1] Implement the fixed `materialize` and `load-probe` runner/Harness path and complete the independent small-real US1 checkpoint in `src/search/diskann/diskann_search_evaluation_runner.cpp`, `harness/python/diskann_harness/diskann_search_evaluation.py`, `src/search/diskann/CMakeLists.txt`, and `tests/python/test_diskann_search_evaluation.py`
  - **Depends on**: T005.
  - **Implementation**: register the real `diskann_search_evaluation_runner`; implement only the
    approved normalized argv operations `materialize --request ...` and `load-probe ...`; make
    Python the sole config normalization/path/source admission owner and C++ the native-operation
    owner, including the sole PQ/DiskLayout terminal BuildManifest for each materialization
    invocation; the Python caller must not duplicate it. Reuse `diskann_pipeline_runner validate`
    for fresh reusable-asset evidence; do not add a generic `validate` subcommand to the Feature
    runner. Stage loader-required names in a confined temporary prefix, call
    `load_from_separate_paths`, and publish no output from the probe.
  - **Minimum test**:
    ```sh
    cmake --build build/diskann-search-evaluation \
      --target diskann_search_evaluation_runner post_graph_materialization \
               search_artifact_contracts valid_artifacts artifact_reuse_core
    ctest --test-dir build/diskann-search-evaluation \
      -R '^(post_graph_materialization|search_artifact_contracts|valid_artifacts|artifact_reuse_core)$' \
      --output-on-failure
    .venv/bin/python -m pytest tests/python/test_diskann_search_evaluation.py -q
    ```
  - **Complete when**: a real small materialize/load run validates and loads both assets; the PQ is
    reusable by Dataset identity; the DiskLayout binds exactly one graph; the graph checksum and
    existing P/B/M evidence prove no reconstruction; all failures preserve separate terminal
    evidence without normal publication. This is the independently deliverable MVP.

**Checkpoint**: US1 is complete and deployable without any search/evaluation implementation.

---

## Phase 4: User Story 2 - Execute and Evaluate Disk Search (Priority: P2)

**Goal**: Search prebuilt valid PQ/DiskLayout assets through pinned PQFlashIndex semantics, retain
one complete raw observation per point, prove oracle parity, and evaluate those observations
offline with separately derived quality/performance eligibility.

**Independent Test**: Reuse prebuilt validated small PQ/DiskLayout assets without running US1,
convert a small selected-query-style CSV, use the pinned exact-L2 GroundTruth tool, execute both
cache-specific protocols, retain every top-100 raw point, and generate a separate report whose raw
metrics can be recomputed without SSD search.

- [X] T007 [US2] Add failing Feature-local contract, QuerySet/GroundTruth preparation, observation, evaluator, eligibility, and CLI/Harness tests in `tests/python/test_contract_schemas.py`, `tests/cpp/contract/test_document_codecs.cpp`, `tests/cpp/search/test_pqflash_observation.cpp`, `tests/python/test_diskann_search_evaluation.py`, `tests/fixtures/search/prebuilt/`, `tests/fixtures/search/selected_query_small.csv`, `tests/fixtures/search/protocol-zero-cache.json`, `tests/fixtures/search/protocol-fixed-cache.json`, and `tests/cpp/search/CMakeLists.txt`
  - **Depends on**: T003; it may be designed independently of US1 but its end-to-end execution must
    use prebuilt valid PQ/DiskLayout fixtures rather than invoking materialization in the same test.
  - **Prebuilt fixture contract**: `tests/fixtures/search/prebuilt/` contains one small fixed valid
    PQArtifact and DiskLayoutArtifact with complete metadata, required native payloads, payload and
    aggregate checksums, and existing ValidationEvidence. A fixture README plus existing metadata/
    evidence records its origin and bound Dataset/GlobalGraph identities. Tests fresh-validate both
    assets before use and prove the US1 materializer path was not called using existing test/control-
    flow evidence, without adding production counters or P/B/M/US1 instrumentation. Do not create a
    FixtureArtifact, fixture schema, or fixture registry.
  - **Test first — contracts and inputs**: validate the existing Feature-local
    SearchProtocol/Observation/Report schemas without promoting them to public Artifact types;
    require the runtime BuildManifest target to have the sole full source
    `specs/005-diskann-search-evaluation/contracts/build-manifest.schema.json`, byte-identical to it
    with source SHA-256 verified, while unrelated sources remain unchanged; require additive
    BuildManifest document `stage="search"`; preserve QuerySet
    `query_row_index`, exact opaque `source_query_id` lexeme, no ID-based self exclusion, native
    conversion checksum/manifest/evidence, and pinned `compute_groundtruth` exact-L2 width/tie/
    checksum/identity semantics. Exercise these paths only as the Python Harness-private
    `materialize --operation query_set` and `materialize --operation ground_truth` suboperations:
    assert their admitted workspace/input/output/resource identities, immutable native QuerySet and
    row mapping, QuerySetReference/GroundTruthReference, separate ValidationEvidence and terminal
    BuildManifests, plus shell-free pinned-tool argv/stdout/stderr/exit evidence for GroundTruth.
    Failure must retain terminal evidence without publishing a normal reference. Reject copied or
    newly implemented brute-force GroundTruth logic, a new top-level command, or a C++ runner
    operation. Formal SIFT1M QuerySet/GroundTruth operations are not executed by these tests.
  - **Test first — protocol/observation**: require two separate protocols, K=100, ordered
    L `[100,200,400]`, W=2, threads=6, IO limit UINT32_MAX, no warmup, end-to-end entry, three
    repetition-major attempts per L, requested cache nodes 0 or 10,000, and no independent cache
    byte request. A successful query has exactly 100 IDs, 100 finite distances, `total_us`, and
    `n_ios`; all other QueryStats fields remain optional and may be absent. GroundTruth is forbidden
    from search execution/observation.
  - **Test first — evaluation**: require compatible GroundTruth for every report; derive
    `quality_eligible` independently of environment and `performance_eligible` only from parity,
    execution/timing, and Linux local-SSD/O_DIRECT/libaio admission. Performance true requires
    metrics classified `formal`; false forbids performance aggregation. For every complete
    successful quality-eligible observation, assert that the Python Harness `evaluate` invocation
    computes and displays QPS, mean/P99.9 latency, and Mean IOs from the retained raw facts while the
    C++ runner performs no derived-metric calculation. Persisted performance metrics may be absent
    or present as `informational_non_formal` values with an explicit reason. Check separate median
    aggregates, no replacement repetition, and Graph Gate enum
    `PASS|FAIL|INCONCLUSIVE|NOT_REACHED` without changing Feature 002. Every failed, invalid,
    cancelled, identity-mismatched, or excluded observation must remain in the report as an
    audit/exclusion reference containing identity, path, checksum, terminal status, and reason, but
    contribute no metric, aggregate, median, or comparison finding and never make either eligibility
    true.
  - **BuildManifest tests**: QuerySet conversion, exact GroundTruth generation, instrumented
    search, and offline evaluation each write one independent existing BuildManifest/v1. Assert that
    the Python Harness is the sole owner for QuerySet, GroundTruth, and offline evaluation, while the
    C++ runner is the sole owner for instrumented search; neither layer duplicates the other's
    manifest. Reuse the existing validator/helpers and the same operation fixtures to verify
    implementation identity/version; pinned repository/branch/full commit/dirty state; toolchain/
    build flags; Dataset and all input identities/checksums; full parameters/resources; timestamps;
    terminal status; output references; and failure reason for failed/invalid/cancelled outcomes.
    Search uses document `stage="search"`, evaluation uses `evaluate`, and preparation uses `build`.
    Verify SearchObservation, SearchEvaluationReport, prepared payload/evidence, and manifests are
    separate and metrics are absent from manifests; do not add per-field/status test files or a new
    framework.
  - **Complete when**: the new C++/Python cases are registered and fail only because Feature-local
    preparation/search/evaluation support and `stage="search"` are missing.

- [X] T008 [US2] Implement Feature-local protocol/input preparation and fixed implementation declarations from `specs/005-diskann-search-evaluation/contracts/build-manifest.schema.json` into `contracts/v1/SOURCES.json`, `contracts/v1/schemas/build-manifest.schema.json`, `src/search/diskann/fixed_descriptors.hpp`, `src/search/diskann/fixed_descriptors.cpp`, `harness/python/diskann_harness/diskann_search_evaluation.py`, `src/search/diskann/diskann_search_evaluation_runner.cpp`, and `src/search/diskann/CMakeLists.txt`
  - **Depends on**: T007 red tests.
  - **Implementation**: make the approved full Feature 005 BuildManifest schema the sole source for
    the byte-identical runtime target in `SOURCES.json`, changing only the truthful document
    classification `search` (not Pipeline `StageKind`) and retaining every unrelated Feature 001
    mapping and source-sync safety/checksum rule; fixed identities `cpp_main_search_disk_index_oracle_v1@1.0.0`,
    `cpp_main_pqflash_search_instrumented_v1@1.0.0`, and
    `cpp_main_search_metrics_evaluator_v1@1.0.0`; strict Feature-local schema loading/canonical
    checksums; and the two Python Harness-private `materialize --operation query_set|ground_truth`
    suboperations. QuerySet normalization binds the admitted workspace, selected-query path/size/
    SHA-256/count/dimension/float32 semantics, row identity/mapping outputs, implementation/source/
    toolchain identity, resource budget, and produces an immutable native payload plus
    QuerySetReference. GroundTruth binds the validated Dataset and QuerySet exact identities,
    fixed-L2/width/tie semantics, admitted output/resources, and uses an argv-only wrapper around
    the pinned `compute_groundtruth` binary and its path/SHA-256 for small exact-L2 fixtures.
    The Python Harness owns the sole independent, complete terminal BuildManifest for each QuerySet
    normalization and GroundTruth invocation asserted by T007, with separate ValidationEvidence
    outside timing and no duplicate C++ manifest; failure retains stdout/stderr/exit and terminal
    evidence but publishes no normal reference.
  - **Boundary**: do not add QuerySetArtifact, GroundTruthArtifact, a public Protocol/Observation/
    Report, Search Registry/backend, GroundTruth engine/backend, new C++ runner operation, or a new
    top-level GroundTruth/query-preparation command. Do not generate formal SIFT1M QuerySet or
    GroundTruth inputs in this task.
  - **Minimum test**: run the T007 schema/query/truth/identity cases and existing
    `test_contract_schemas.py`/`document_codecs`; keep US1 tests green.
  - **Complete when**: small QuerySet rows round-trip with correct identities; the pinned tool's
    exact truth validates with width >=100 and tie closure; evaluation-only identity is separate
    from search execution; all public contracts except the additive manifest classification remain
    unchanged.

- [X] T009 [US2] Implement the instrumented PQFlash search, raw observation persistence, offline metrics evaluator, and approved Feature CLI/Harness operations in `src/search/diskann/pqflash_instrumented_runner.hpp`, `src/search/diskann/pqflash_instrumented_runner.cpp`, `src/search/diskann/search_metrics_evaluator.hpp`, `src/search/diskann/search_metrics_evaluator.cpp`, `src/search/diskann/diskann_search_evaluation_runner.cpp`, `harness/python/diskann_harness/diskann_search_evaluation.py`, and `src/search/diskann/CMakeLists.txt`
  - **Depends on**: T008 and prebuilt valid small PQ/DiskLayout fixtures.
  - **Implementation — search**: implement only `search --protocol ... --workspace ...`; fresh-
    validate PQ/Layout/QuerySet, load PQFlash, build zero or fixed BFS cache outside timing, and run
    repetition-major then L order. The frozen fixed-cache input is only
    `requested_nodes=10000`; admission derives requested primary bytes from admitted dimension,
    scalar width, degree, and pinned cache allocation semantics, while each observation records
    actual effective nodes/primary bytes. Time only the upstream-equivalent OpenMP query batch;
    preallocate/capture IDs, distances, and directly observed QueryStats in memory and serialize one
    immutable point document after the timer. Optional stats stay optional and are never inferred.
    The C++ runner owns the sole independent complete `stage="search"` BuildManifest for each actual
    instrumented-search invocation required by T007, including terminal failure evidence, without a
    duplicate Python manifest or embedded observations/metrics.
  - **Implementation — evaluate**: implement only the approved Harness `evaluate` path; never run
    SSD search there. Validate every observation and compatible GroundTruth, calculate pinned
    Recall@10/100 from the same top-100 rows plus K=100 QPS/mean/P99.9/Mean IOs, derive separate
    quality/performance eligibility, and aggregate eligible medians independently. For every
    complete successful quality-eligible observation, display all four derived performance metrics
    in the evaluate invocation output even when performance eligibility is false; report persistence
    is optional in that case, and any persisted values are `informational_non_formal`. The C++ search
    runner remains limited to result/QueryStats/raw-observation capture and persistence. Retain every
    failed/invalid/cancelled/identity-mismatched/excluded observation only as a report audit/
    exclusion reference with identity, path, checksum, terminal status, and reason; it contributes
    no metric, aggregate, median, or comparison finding and cannot make either eligibility true.
    Keep metrics out of Artifacts/BuildManifests and keep the
    Feature 002 graph context non-certifying. The Python Harness owns the sole complete terminal
    `stage="evaluate"` BuildManifest for each offline-evaluation invocation, separate from the
    report and metrics; the C++ runner creates no evaluation manifest.
  - **Approved CLI set**: the complete Feature surface is only `materialize`, `load-probe`,
    `search`, `parity`, `evaluate`, and Feature-specific `preflight`; do not add a generic
    `validate`, comparison, sweep, retry, or tuning operation.
  - **Minimum test**:
    ```sh
    cmake --build build/diskann-search-evaluation \
      --target diskann_search_evaluation_runner pqflash_observation post_graph_materialization
    ctest --test-dir build/diskann-search-evaluation \
      -R '^(pqflash_observation|post_graph_materialization)$' --output-on-failure
    .venv/bin/python -m pytest tests/python/test_diskann_search_evaluation.py -q
    ```
  - **Complete when**: both small protocols retain every declared point and query without
    cross-point averaging; invalid/partial evidence cannot contribute metrics, eligibility,
    aggregates, medians, or comparison findings, but its retained identity, path, checksum, terminal
    status, and reason can be emitted as an audit/exclusion reference; compatible non-normative
    observations can produce formal Recall, their evaluate call displays all derived performance
    metrics, and any persisted values are only informational/non-formal; a report can be recomputed
    offline and at least one extra aggregate from existing raw fields is demonstrated without new
    search.

- [X] T010 [US2] Add the failing small Search Runner Parity gate against the fixed upstream oracle in `tests/cpp/search/test_search_runner_parity.cpp`, `tests/python/test_diskann_search_evaluation.py`, and `tests/cpp/search/CMakeLists.txt`
  - **Depends on**: T009.
  - **Test first**: on identical prebuilt small Dataset/PQ/Layout/QuerySet/GroundTruth and identical
    K/L/W/thread/cache/IO/no-warmup controls, compare the admitted unmodified
    `search_disk_index` oracle with the independently identified instrumented runner. Require exact
    ordered IDs, distance `atol=1e-5`/`rtol=1e-6`, Recall@10/100 equality, Mean IO/required
    QueryStats semantic equality, formula equality, canonical timing-region equivalence, and proof
    that observation persistence is outside the timer. Explicitly do not require independent
    wall-clock QPS/latency numbers to match.
  - **Isolation**: assert parity evidence does not write or upgrade Feature 002's
    `graph_structural_parity_gate`, candidate Framework Baseline state, or any certification field.
  - **Oracle manifest**: every upstream oracle invocation must have its own existing
    `stage="search"` BuildManifest owned solely by the Python Harness, with no C++ duplicate. In the
    same parity fixtures and via existing manifest helpers, assert the complete T007 provenance set,
    terminal failure reason when applicable, retained stdout/stderr/result references, and
    separation from parity evidence and all metrics.
  - **Complete when**: `search_runner_parity` is registered, fails against a deliberately perturbed
    instrumented result/control/timing case, and still awaits the real parity implementation.

- [X] T011 [US2] Implement the fixed oracle argv parity operation and complete its platform-applicable implementation closure in `harness/python/diskann_harness/diskann_search_evaluation.py`, `src/search/diskann/diskann_search_evaluation_runner.cpp`, `tests/python/test_diskann_search_evaluation.py`, and `tests/cpp/search/test_search_runner_parity.cpp`
  - **Depends on**: T010 red tests and T001 `smoke_definition_ready`; executing the real Linux
    native parity path additionally depends on T001 `linux_native_smoke_pass`.
  - **Implementation**: invoke the admitted unmodified out-of-tree `search_disk_index` executable
    by argv, retain stdout/stderr/result bins/exit status, compare with the instrumented observation,
    and make the Python Harness write the invocation's sole independent complete terminal
    `stage="search"` BuildManifest (including failed/invalid/cancelled outcomes), with no C++
    duplicate. Persist only Feature-local parity evidence referenced by evaluation. Do not modify or
    copy upstream, add a parity Report family, or reinterpret this gate as Graph/Search-Quality
    Baseline certification.
  - **Platform-applicable test**:
    ```sh
    cmake --build build/diskann-search-evaluation \
      --target diskann_search_link_smoke diskann_search_evaluation_runner \
               pqflash_observation search_runner_parity
    ctest --test-dir build/diskann-search-evaluation \
      -R '^(diskann_search_link_smoke|pqflash_observation|search_runner_parity)$' \
      --output-on-failure
    .venv/bin/python -m pytest tests/python/test_diskann_search_evaluation.py -q
    ```
  - **Complete when**: the parity implementation, admitted oracle argv path, evidence persistence,
    prebuilt-fixture isolation, observation/report checks, and every test applicable on the current
    platform pass while Feature 002 remains `NOT_REACHED`/candidate. If applicable Linux is
    available, the actual small oracle/instrumented semantic parity must also PASS. If it is not
    available, this checkbox may close implementation after all applicable non-Linux tests pass,
    but the task record must explicitly keep `linux_native_smoke_pass=pending`, actual Search
    Runner Parity `pending`, and US2 native/performance acceptance `pending`; the US2 checkpoint,
    Search Runner Parity Gate, and formal-performance qualification are not PASS. Do not use a
    non-equivalent oracle or represent these dependency labels as schema fields, enums, Artifacts,
    or a Gate subsystem.

**Checkpoint**: US2 implementation may close at T011 on the platform-applicable terms above, but
US2 native/performance acceptance is independently complete only when prebuilt assets support
complete instrumented search and offline evaluation and the actual Linux Search Runner Parity gate
passes without US1 execution. A pending Linux smoke or parity run keeps this checkpoint pending.

---

## Final Phase: Cross-Cutting Validation and Non-Executing Formal Readiness

**Purpose**: Verify the completed implementation and implement the non-executing formal-readiness
use of the one Feature-specific preflight. This phase does not perform any formal SIFT1M data
operation and cannot claim formal-preflight PASS or formal SIFT1M readiness.

- [X] T012 Add failing non-executing formal-readiness preflight, environment-admission, and manual-boundary tests in `tests/python/test_diskann_search_evaluation.py`
  - **Depends on**: T006 and T011 implementation closure; it does not depend on a Linux parity run
    that is inapplicable on the current host.
  - **Test first**: using deterministic/small fixtures only, prove the Feature-specific preflight's
    readiness mode validates currently known source/binary/config/path/resource identities, known
    fresh Artifact evidence when applicable, frozen controls, requested-node-derived cache bytes,
    and the Linux local-SSD/O_DIRECT/libaio probes. It must list absent exact PQ/Layout/normalized
    QuerySet/GroundTruth/final zero-cache and fixed-cache SearchProtocol identities as
    pending; it must not start the C++
    runner, invoke materialization/search/GroundTruth tools, create an Artifact/GroundTruth/
    SearchProtocol/Observation/Report, invoke P/B/M, or report formal-preflight PASS/formal SIFT1M
    ready. With exact deterministic/small PQ/Layout/QuerySet/GroundTruth identities, exercise that
    same preflight's formal-search mode and prove the sole Python Harness normalization path writes
    exactly one immutable zero-cache and one fixed-cache SearchProtocol, immediately rereads and
    validates their schemas/checksums/search-execution and all input identities plus binary/
    environment/path/resource bindings, then reports separate common-search and performance
    admission decisions, emits reviewable requests whenever common admission passes, and starts no
    search. Common admission must pass for a usable Mac Docker fixture despite failed normative
    performance admission, and must fail for an unusable binary/dependency/fixed search path. An
    approved `search` fixture consumes the frozen Protocol without renormalization. Do not add a
    `protocol` command, second preflight, schema, runner, builder framework, or state system.
  - **Environment cases**: real admitted Linux local SSD can pass performance readiness; macOS,
    Docker-on-macOS, network storage, unavailable libaio/O_DIRECT, or ambiguous storage remains
    non-normative/pending for formal performance while preserving independent quality semantics and
    does not block a formal-search fixture request that passes common search admission.
    Add no HardwareRegistry, EnvironmentProfile, device hierarchy, or host-inventory subsystem.
  - **Complete when**: the tests fail against the missing Feature-specific readiness behavior and
    explicitly prove it is non-executing and reports missing identities as pending; readiness mode
    cannot emit a formal search request, while formal-search fixture mode must emit reviewable exact
    requests but must not execute them.

- [X] T013 Implement and run the Feature-specific non-executing formal-readiness preflight, rerun all applicable regressions and the existing Linux smoke, and record the final audit in `harness/python/diskann_harness/diskann_search_evaluation.py`, `tests/python/test_diskann_search_evaluation.py`, and `specs/005-diskann-search-evaluation/quickstart.md`
  - **Depends on**: T012 red tests, T006 and T011 implementation closure, and all US1/US2 focused tests
    applicable on the current host passing; missing Linux execution remains a recorded acceptance
    gate rather than a blocker for non-normative readiness implementation.
  - **Implementation**: add only the Feature-local `preflight` path required by
    `contracts/cli.md`. In readiness mode it checks known SIFT1M Dataset/GlobalGraph/selected-query
    declarations, pinned source and clean state, available binaries, output confinement, resources,
    frozen controls, local-SSD/O_DIRECT/libaio environment, and dependency order for future PQ
    generation, layout materialization, QuerySet normalization, exact GroundTruth generation, and
    search. It explicitly lists absent exact PQ/Layout/QuerySet/GroundTruth/final protocol
    identities as pending, generates none of
    them, emits no runnable search request, and cannot claim formal-preflight PASS or formal SIFT1M
    readiness. The planned
    `experiments/configs/diskann-search-evaluation/zero-cache-search-protocol.json` and
    `experiments/configs/diskann-search-evaluation/fixed-cache-search-protocol.json` paths remain
    absent/pending until those immutable identities exist; do not write placeholders or weaken
    their schemas merely to make readiness look complete. Implement formal-search mode in this same
    Feature-specific preflight for the T012 small-fixture tests only: after all exact identities
    exist, the Python Harness is the sole normalization owner and writes exactly the two immutable
    cache-specific Protocols once, immediately rereads and validates them, and emits reviewable
    exact requests without search when common search admission passes. It reports performance
    admission independently; Linux/local-SSD/O_DIRECT/libaio or Search Runner Parity failure does not
    block a quality request, while a broken runner/ABI/dependency/load path still fails common
    admission. The implementation-phase SIFT1M invocation remains readiness
    mode only; do not run formal SIFT1M input preparation or formal-search mode here.
  - **Full regression**:
    ```sh
    cmake --build build/diskann-search-evaluation --parallel
    ctest --test-dir build/diskann-search-evaluation \
      -R '^(public_api_cxx17|checksum_canonicalization|document_codecs|valid_artifacts|invalid_artifact_metadata|artifact_reuse_core|pipeline_orchestration|pipeline_failure_gates|fake_pipeline_e2e|cpp_main_native_formats|cpp_main_link_smoke|cpp_main_pipeline|diskann_search_link_smoke|search_artifact_contracts|post_graph_materialization|pqflash_observation|search_runner_parity)$' \
      --output-on-failure
    .venv/bin/python -m pytest \
      tests/python/test_contract_schemas.py \
      tests/python/test_contract_source_sync.py \
      tests/python/test_cpp_main_baseline_config.py \
      tests/python/test_harness_fake_pipeline_e2e.py \
      tests/python/test_yaml_safety.py \
      tests/python/test_phase1_config_and_workspace.py \
      tests/python/test_harness_cli.py \
      tests/python/test_diskann_search_evaluation.py -q
    git diff --check
    ```
  - **Linux gate**: rerun the one existing `diskann_search_link_smoke` and the Feature-local
    O_DIRECT/libaio/local-SSD preflight on the intended filesystem. If the current host cannot do
    so, record the exact Linux-only/formal-performance gate as pending; do not implement another
    smoke, fallback to buffered IO, or claim `linux_native_smoke_pass` or formal performance
    readiness. Linux native implementation acceptance and formal performance eligibility require
    the actual smoke and actual Linux Search Runner Parity PASS; this gate does not by itself block
    common search admission or quality eligibility.
  - **Final audit**: verify project/upstream status, full DiskANN commit, compiler/build flags,
    runner/oracle hashes, schema/source sync, provenance separation, no P/B/M control-flow/output
    evidence, no Feature 002 Gate upgrade, all executed/failed/applicable-unexecuted checks, and no
    operation above SIFT1M. Update `quickstart.md` only with commands and results actually observed.
  - **Complete when**: every applicable non-formal test passes; the non-executing formal-readiness
    preflight has run without generating PQ/Layout/QuerySet/GroundTruth/Protocol or executing
    search; the fixture formal-search-mode tests pass without a formal data operation; each
    missing Linux/input/output prerequisite is truthfully pending; upstream remains pinned and
    clean. Completion of T013 is not formal search preflight PASS and does not authorize or complete
    formal SIFT1M validation.

---

## Dependencies and Execution Order

### Phase dependencies

```text
T001 Setup
  -> T002 contract tests -> T003 Foundational implementation
       -> T004 US1 tests -> T005 materializers -> T006 US1 checkpoint
       -> T007 US2 contract/behavior tests -> T008 input/contracts
          -> T009 search/evaluation -> T010 parity tests -> T011 parity implementation closure
T006 + T011 -> T012 readiness-preflight tests -> T013 final validation/readiness
```

- **Setup** has no dependency.
- **Foundational** depends on T001 wiring plus `smoke_definition_ready`, not on execution of the
  Linux smoke, and blocks both stories.
- **US1** depends only on Foundational and is the suggested MVP.
- **US2** depends on Foundational for contracts and uses prebuilt valid search assets; it must not
  invoke US1 in its independent test. T011 implementation closure can feed T012 on a non-Linux
  host, while its real native/performance acceptance requires both T001 `linux_native_smoke_pass`
  and actual Linux Search Runner Parity PASS.
- **Final** depends on both story implementations and all applicable focused tests. If Linux is
  unavailable, T011 implementation/Harness work may feed T012-T013, but the US2 native checkpoint,
  Linux native acceptance, and formal performance eligibility remain pending until the one smoke
  and actual Linux parity run pass. Common search admission and quality eligibility remain
  independently decidable.

### Parallel opportunities

- After T003, US1 test design in T004 and US2 test design in T007 are logically independent, but
  their shared CMake/Python test files require sequencing or explicit file ownership; therefore no
  unchecked `[P]` marker is claimed.
- Within implementation, PQ and DiskLayout fixture preparation may run in separate confined
  workspaces only after the same Dataset identity is fixed, but their shared CMake/runner code keeps
  T005 as one atomic task.
- Zero-cache and fixed-cache small tests use distinct protocol/process/workspace paths, but they are
  both acceptance evidence for T009/T011 and are not separate implementation tasks.

## User Story Completion Standards

### US1 independent completion

US1 is complete at T006 only when a small real validated Dataset+GlobalGraph produces a reusable
validated PQArtifact and one graph-bound validated DiskLayoutArtifact, the exact PQFlash loader
accepts both, graph checksum is unchanged, and no Partition/Local Build/Merge execution occurred.
No search, GroundTruth, or EvaluationReport is needed for this checkpoint.

### US2 independent completion

T011's checkbox closes the platform-applicable US2 implementation only when prebuilt validated
PQ/DiskLayout fixtures are consumed without materialization, every zero/fixed-cache L/repetition
point retains complete top-100 observations, offline quality/performance evaluation obeys the
frozen eligibility/median rules, and all tests applicable on the current platform pass. US2 native/
performance acceptance additionally requires actual Linux smoke and small upstream-oracle parity
PASS; if unavailable, all three pending facts required by T011 remain explicit. No formal SIFT1M
run or US1 execution is part of this checkpoint.

## Implementation Strategy

1. Complete T001-T003 to establish the shared contract foundation.
2. Complete T004-T006 and stop at the independently testable US1 MVP.
3. Complete T007-T011 to add search/evaluation and the formal-performance Search Runner Parity gate.
4. Complete T012-T013 to run the non-executing formal-readiness checks and applicable regressions.
5. Stop at the manual boundary below; report all Linux-only and formal-input prerequisites still
   pending.

## Requirement-to-Task Coverage

| Requirement group | Tasks |
|---|---|
| Fixed source, downstream-only scope, no P/B/M, no fourth stage | T001, T004-T006, T012-T013 |
| PQ/DiskLayout public contracts, checksums, validation, reuse | T002-T006 |
| QuerySet/GroundTruth identity and exact pinned preparation | T007-T008 |
| Protocol, raw observations, timing, repetitions, cache controls | T007-T009 |
| Recall/QPS/latency/IO and separate eligibility/aggregation | T007-T009 |
| Search Runner Parity without Graph Gate upgrade | T010-T011, T013 |
| Linux normative environment and failure semantics | T001, T009, T012-T013 |
| Optional multi-graph compatibility without implementation | T002-T003, T009; no comparison task |
| Full regressions, provenance separation, manual execution Gate | T013 and boundary below |

---

## Non-Task Manual Execution Boundary

The checklist ends at T013. Human approval is deliberately **not** a checkbox task, and completion
of `/speckit-implement` through T013 is not approval to run any formal SIFT1M data operation.

After implementation, applicable small-real/regression tests and common source/build/path/resource
readiness checks complete, the implementation must stop and report known readiness and pending
identities. Formal SIFT1M work remains applicable but unexecuted until the user separately approves
exact immutable execution request(s) covering:

1. PQ generation from the admitted Dataset;
2. DiskLayout materialization from the same Dataset and one admitted GlobalGraph;
3. QuerySet normalization for `sift1M_dataset/selected_query.csv` through the Feature-private
   `materialize --operation query_set` suboperation;
4. exact-L2 GroundTruth generation through the Feature-private
   `materialize --operation ground_truth` suboperation;
5. zero-cache and fixed-cache formal search under fully frozen SearchProtocols.

The first four generated outputs remain separate approved operations and must be validated. Once
their exact identities exist, the same Feature-specific `preflight --mode formal-search`
implemented by T013 runs inside this manual boundary against the actual declared environment and
exact runner binary. The Python Harness is the sole
SearchProtocol normalization owner: this invocation reads the exact zero/fixed-cache declarations,
writes exactly two immutable Protocols once, then immediately rereads and validates their schemas,
checksums, search-execution/input identities, binaries, environment, paths, and resources. It does
not search and is not a second preflight command or framework. It reports common search admission
and performance admission separately. Common admission does not require Linux/local-SSD/O_DIRECT/
libaio or Search Runner Parity and may emit a Mac Docker quality request; failure of the runner,
ABI/dependencies, validated inputs, fixed search path, confinement, or basic resources blocks it.
Only after common admission passes may its exact immutable requests be presented for separate
explicit user review/approval; only that approval permits item 5 search execution, which consumes
the frozen Protocols without renormalization. Formal performance claims additionally require the
existing performance admission, including Linux/local SSD/O_DIRECT/libaio and Search Runner Parity.

Each approval must bind exact inputs/checksums, parameters, implementation and binary hashes,
actual environment, resources, and output workspace. An earlier approval may cover dependency-
ordered generation of items 1-4 only when it explicitly names their exact requests; it cannot
pre-approve the not-yet-created formal search request. No step is inferred from an earlier
approval; no missing PQ/Layout/GroundTruth identity is fabricated; no retry, sweep,
best-result selection, Query backend, optional multi-graph comparison, or execution above SIFT1M is
authorized. A missing common/formal-input gate blocks any formal SIFT1M search and must be reported;
a pending Linux-only performance gate blocks formal performance claims but not an explicitly
approved quality run that passed common search admission.

## Notes

- All generated build/test/output paths are outside `upstream/diskann-cpp-main/`.
- A compile-only result is never a task PASS.
- Every task completion report separates executed/passed, executed/failed, and applicable-unexecuted
  checks and preserves failed/invalid/cancelled evidence.
- Search Runner Parity proves runner/search semantics only; Feature 002 remains
  `graph_structural_parity_gate=NOT_REACHED` and Framework Graph Composition remains a candidate.
- No task implements optional multi-GlobalGraph comparison, Query/Search backend abstraction,
  Search Registry, Loop Engine, MetricPlugin, Cache/Centroid Artifact, or Feature 002 Gate closure.
