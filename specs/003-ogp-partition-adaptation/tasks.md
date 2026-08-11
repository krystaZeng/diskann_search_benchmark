# Tasks: OGP Partition Adaptation

**Input**: `spec.md`, `plan.md`, `research.md`, `source-mapping.md`, `data-model.md`, `quickstart.md`
and active files under `contracts/`, resolved from `.specify/feature.json` as
`specs/003-ogp-partition-adaptation`.

**Scope**: Invoke only the pinned official GP-ANN `Partition` CLI with method `OGP`, publish the
existing `PartitionArtifact/v1`, compose it with unchanged cpp_main B0/M0 through the existing
`PipelineExecutor`, and retain one frozen SIFT1M structural configuration. No task writes under an
admitted source tree or adds a new Artifact/report family.

**Test rule**: Within each behavior task, add the named failing test coverage before implementation,
then run the exact repository entry points named by that task. Compilation alone is not validation.

## Format: `[ID] [P?] [Story?] Description`

- Setup/foundational/final tasks have no story label.
- Story tasks use `[US1]`, `[US2]` or `[US3]` from `spec.md`.
- Tasks are intentionally coarse vertical deliverables. Each task is complete only when all files,
  tests and evidence named in its description are complete.

---

## Phase 1: Setup — Fixed Official Source and Build

**Purpose**: Establish the only admitted real OGP executable before implementing a wrapper.

- [X] T001 Resolve `.specify/feature.json`, inspect `git status --short`, verify the fixed clean DiskANN gitlink/HEAD, then obtain and admit an independent clean recursive GP-ANN checkout at repository `https://github.com/larsgottesbueren/gp-ann.git`, branch `main`, commit `df5efb4a6b303011c795666ed267f29d994b558c` with every dependency from `specs/003-ogp-partition-adaptation/source-mapping.md`; add only the Feature option, `cmake/GpAnnOgp.cmake`, the official `Partition` out-of-tree build/admission, and the minimum root wiring in `CMakeLists.txt` that can configure successfully at this point—do not pre-register source, test or runner targets whose files are created by T003/T004/T006; run the canonical parent configure `cmake -S . -B build/ogp-partition-adaptation -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON -DDISKANN_GP_ANN_OGP=ON -DGPANN_SOURCE_DIR="$GPANN_SOURCE_ROOT"`, build official target `Partition` only under `build/ogp-partition-adaptation/gp-ann-official`, retain source/dependency commits, compiler/version, CMake/generator/build type, effective options, logs, executable path and SHA-256 outside both source trees, and make this admitted executable the single identity consumed later by admission tests, the small real runner and SIFT1M; stop with retained evidence rather than patching or substituting it if exact clean admission/build fails.

**Checkpoint**: Both admitted source identities are exact and clean, all build/output paths are out
of tree, and the official executable is traceable to the admitted recursive checkout.

---

## Phase 2: Foundational — Mixed Stage Source Identity

**Purpose**: Correct per-stage BuildManifest source identity and activate the existing optional
stage-owned BuildManifest transport without changing public shapes or legacy callers.

- [X] T002 Add failing request-contract and generic execution-provenance tests in `tests/cpp/contract/test_public_api_cxx17.cpp` and the newly created `tests/cpp/integration/test_stage_source_provenance.cpp`, update `tests/cpp/CMakeLists.txt` in this same task to register target/CTest `stage_source_provenance`, then implement the exact optional complete `StageSourceIdentities { partition, local_build, merge }` aggregate in `include/diskann_pipeline/pipeline.hpp`, one private stage-source selector and one minimal existing-`StageResult.build_manifest_path` ingestion helper in `src/pipeline/pipeline_executor.cpp`; test that absent stage sources preserve request-level source fallback, present sources select GP-ANN/DiskANN/DiskANN for new and reused stages, an empty `build_manifest_path` preserves legacy parent parameters, a non-empty path is confined/read/validated/immutably persisted and adds `parameters.stage_execution_manifest {path, sha256}` only to the corresponding parent stage, malformed/mismatched/path-escaping subordinate Manifests fail closed, and the shared `source_config` remains exactly `yaml_sha256` plus `normalized_json_sha256` with legacy semantics in every parent Manifest; rerun the canonical T001 parent configure before the first target build, and prove unchanged StageResult shape, BuildManifest schema/codecs and C++17 public compatibility by building/running CTest targets `public_api_cxx17`, `stage_source_provenance`, `document_codecs` and `pipeline_orchestration`.

**Checkpoint**: Legacy requests/stages are behaviorally unchanged; formal OGP requests can produce
truthful source identity in every parent Manifest and a confined checksum-bound same-stage
execution-Manifest reference without a map/plugin/provenance framework.

---

## Phase 3: User Story 1 — Produce a Valid Official OGP Partition (Priority: P1) 🎯 MVP Part 1

**Goal**: Independently publish a common-validator-accepted Partition Artifact from the official
CLI while rejecting corrupt source, parameters, process output and memberships before B0.

**Independent Test**: `gp_ann_ogp_partition` covers descriptor/admission/parser behavior and invokes
the admitted official CLI on a small real float32 Dataset to publish one existing valid Artifact.

- [X] T003 [US1] Add the source/descriptor/parameter/process and subordinate-Manifest lifecycle cases to the newly created `tests/cpp/papers/gp_ann/test_ogp_partition.cpp` and schema cases to the newly created `tests/python/test_ogp_partition_adaptation.py` first; in this same task update `src/CMakeLists.txt`, create/update `src/papers/gp_ann/ogp/CMakeLists.txt`, update `tests/CMakeLists.txt` and `tests/cpp/CMakeLists.txt`, and create/update `tests/cpp/papers/gp_ann/CMakeLists.txt` to register target/CTest `gp_ann_ogp_partition`; then implement `src/papers/gp_ann/ogp/stage_binding.hpp`, `descriptor.cpp`, `source_build_identity.hpp`, `source_build_identity.cpp` and `official_partition_process.cpp`: register stable `gp_ann_ogp_partition_v1` with exact `requires`/`provides`, ID spaces, capabilities and services; copy `specs/003-ogp-partition-adaptation/contracts/ogp-parameters.schema.json` to `contracts/papers/gp-ann-ogp-v1/schemas/ogp-parameters.schema.json` with `contracts/papers/gp-ann-ogp-v1/SOURCES.json`; admit only the exact clean source/build/binary; accept only cluster count/overlap/mode; create a checksum-identical confined `.fbin` view; launch the exact shell-free `OGP` argv; retain stdout/stderr and required raw process evidence in a confined Feature-private directory below the existing workspace report area; for every actual invocation create an existing BuildManifest/v1 whose source is admitted GP-ANN, whose toolchain is the actual admitted external C++20 build, and whose parameters record exact argv, `effective_epsilon=0.05`, configured `PARLAY_NUM_THREADS`, staged input path/checksum, stdout/stderr paths/checksums, expected output path/checksum when present, exit status and admitted executable path/SHA-256, then return it through the existing `StageResult.build_manifest_path`; assert exit zero yields subordinate `SUCCEEDED`, nonzero exit yields subordinate `FAILED` with actual `failure.exit_code` while retaining stdout/stderr, exit-zero later parse/invariant/Artifact failure does not rewrite subordinate success and is recorded by the outer Partition lifecycle, and pre-launch admission failure creates no fake subprocess Manifest; claim no observed worker value, add no StageResult field/metrics or report family, and rerun the canonical T001 parent configure before first building the new target.
- [X] T004 [US1] Add malformed/empty/range/same-shard-duplicate/missing-coverage/valid-overlap/ascending/native-map cases plus one PartitionArtifact payload checksum-mismatch/corruption case to the existing `tests/cpp/papers/gp_ann/test_ogp_partition.cpp` before implementing `src/papers/gp_ann/ogp/cluster_output.cpp` and `ogp_partition.cpp`; the checksum case must prove that the existing Validator rejects the candidate, no normal PartitionArtifact is published, and B0/M0 invocation counts remain zero; update `src/papers/gp_ann/ogp/CMakeLists.txt` and `tests/cpp/papers/gp_ann/CMakeLists.txt` for every newly created source without adding another Validator or test file, rerun the canonical T001 parent configure before the first affected build, then detect duplicates before sorting, preserve every valid cross-shard replica, assign contiguous effective PartitionIds, emit only existing native uint32 maps and capabilities into `PartitionArtifact/v1`, and submit the candidate to the existing common publication gate without centroids, shard vectors, StageResult metrics or an OGP-specific report.
- [X] T005 [US1] Complete the small real section of `tests/cpp/papers/gp_ann/test_ogp_partition.cpp` using the single T001-admitted executable and a confined float32 fixture; prove the executable/input/output checksums, actual subprocess evidence, valid overlap retention, existing Validator acceptance, no source-tree writes, and B0/M0 invocation counts zero for every invalid pre-publication case; rerun the canonical parent configure `cmake -S . -B build/ogp-partition-adaptation -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON -DDISKANN_GP_ANN_OGP=ON -DGPANN_SOURCE_DIR="$GPANN_SOURCE_ROOT"` before `cmake --build build/ogp-partition-adaptation --target gp_ann_ogp_partition --parallel`, then run `ctest --test-dir build/ogp-partition-adaptation -R '^gp_ann_ogp_partition$' --output-on-failure` and `.venv/bin/python -m pytest tests/python/test_ogp_partition_adaptation.py -q`.

**Checkpoint**: Official OGP independently produces a valid existing Partition Artifact; no
downstream graph stage or new report type is needed to establish US1.

---

## Phase 4: User Story 2 — Compose OGP with Existing B0 and M0 (Priority: P2) 🎯 MVP Part 2

**Goal**: Prove the real Partition-only replacement through the existing executor while preserving
all Baseline identities and validation order.

**Independent Test**: `gp_ann_ogp_pipeline` runs OGP once, B0 once per explicit effective
PartitionId and M0 once after all local outputs validate; invalid OGP starts neither downstream
stage and the fixed Baseline runner rejects OGP.

- [X] T006 [US2] Add failing composed-chain, invalid-output, lineage, invocation-count, source-split/fallback, parent/subordinate provenance and Baseline-isolation cases to the newly created `tests/cpp/papers/gp_ann/test_ogp_pipeline.cpp` and lifecycle/config cases to `tests/python/test_ogp_partition_adaptation.py`; require all Partition/B0/M0 parent Manifests to preserve the same shared two-digest `source_config` semantics; require the Partition parent Manifest source to be GP-ANN and its `parameters.stage_execution_manifest` path/SHA-256 to resolve to the immutable subordinate BuildManifest whose source is GP-ANN, toolchain is the admitted GP-ANN external C++20 build, executable SHA-256 matches the admitted `Partition` binary, and argv/stdout/stderr/exit/output evidence is traceable; require B0 and M0 parent Manifest sources to be DiskANN, to retain existing cpp_main provenance and to contain no GP-ANN subprocess-manifest reference; require legacy stages with empty `StageResult.build_manifest_path` to remain unchanged; then implement the non-installed `src/papers/gp_ann/ogp/ogp_experiment_runner.cpp` and thin `harness/python/diskann_harness/ogp_partition_adaptation.py`: admit both fixed sources, register existing P0/B0/M0 plus OGP without overwrite, seal/select OGP+B0+M0, provide GP-ANN/DiskANN/DiskANN stage sources, configure existing vector/runtime services and invoke `PipelineExecutor` exactly once; in this same task update `src/papers/gp_ann/ogp/CMakeLists.txt` and `tests/cpp/papers/gp_ann/CMakeLists.txt` to register the real executable target `ogp_experiment_runner` and target/CTest `gp_ann_ogp_pipeline`, rerun the canonical T001 parent configure before their first build, and do not change `src/baseline/cpp_main/cpp_main_baseline_runner.cpp`, add an OGP branch to B0/M0, implement another stage loop or add a new report/evaluator.
- [X] T007 [US2] Rerun the canonical T001 parent configure, then execute the pre-SIFT blocking build `cmake --build build/ogp-partition-adaptation --target ogp_experiment_runner gp_ann_ogp_pipeline fake_pipeline_e2e public_api_cxx17 document_codecs registry_selection compatibility_preflight valid_artifacts pipeline_orchestration pipeline_failure_gates production_surface cpp_main_descriptors_preflight cpp_main_b0 cpp_main_m0 cpp_main_pipeline --parallel`; run `ctest --test-dir build/ogp-partition-adaptation -R '^(gp_ann_ogp_pipeline|fake_pipeline_e2e|public_api_cxx17|document_codecs|registry_selection|compatibility_preflight|valid_artifacts|pipeline_orchestration|pipeline_failure_gates|production_surface|cpp_main_descriptors_preflight|cpp_main_b0|cpp_main_m0|cpp_main_pipeline)$' --output-on-failure` and `.venv/bin/python -m pytest tests/python/test_ogp_partition_adaptation.py tests/python/test_contract_schemas.py tests/python/test_cpp_main_baseline_config.py tests/python/test_harness_fake_pipeline_e2e.py tests/python/test_production_assembly.py -q`; `tests/python/test_ogp_partition_adaptation.py` must actually launch the built `ogp_experiment_runner` on the approved small Dataset and prove `Python Harness -> real runner -> existing PipelineExecutor -> official OGP -> B0 x effective PartitionId -> M0` completes without the Harness reproducing orchestration; all named C++/Python checks are blocking, and any runner build/launch/composed-run or regression failure fails the MVP checkpoint and prohibits T008; retain executed/failed/applicable-unexecuted results and confirm both source trees, the single admitted `Partition` executable identity and the fixed Baseline composition remain unchanged.

**Checkpoint — MVP**: A small real official OGP→validated PartitionArtifact→existing B0×N→
existing M0 closure passes through the current executor and evidence stores. This checkpoint must
pass before SIFT1M.

---

## Phase 5: User Story 3 — Validate One SIFT1M Structural Configuration (Priority: P3)

**Goal**: Establish bounded scale evidence for the completed MVP without tuning or query claims.

**Independent Test**: Every attempt references the same frozen configuration checksum; a successful
attempt produces the complete validated existing Artifact chain and final existing GlobalGraph
EvaluationReport, while every failure/retry remains visible.

- [X] T008 [US3] Extend `tests/python/test_ogp_partition_adaptation.py` first to enforce the exact SIFT1M identity/checksum, one immutable configuration, no sweep, unchanged retry fields, new run IDs, retained prior attempts, allowed infrastructure/external retry reasons and rejection of datasets above SIFT1M; then create `experiments/configs/ogp-partition-adaptation/sift1m-structural.yaml` with OGP `40/0.2/default`, unchanged Feature 002 B0/M0 parameters and complete source/build/toolchain/resource/output identity, implement only the required preflight/attempt handling in `harness/python/diskann_harness/ogp_partition_adaptation.py`, run its pytest suite and mutation-free preflight, then execute and retain the authorized SIFT1M attempt(s) under distinct `build/ogp-partition-adaptation/runs/sift1m-structural/attempt-*` workspaces; validate every produced Artifact/evidence reference, keep failures/invalidity excluded from successful evidence, and emit no OGP-specific report, Recall/QPS/latency, Graph Parity, full-reproduction or performance claim.

**Checkpoint**: US3 is complete only if one frozen configuration has a successful structurally
valid retained attempt. A retained failure is valid historical evidence but does not satisfy the
success criterion; retry does not authorize changed experimental fields.

---

## Final Phase: Full Validation and Audit

- [ ] T009 Run `cmake --build build/ogp-partition-adaptation --parallel`, `ctest --test-dir build/ogp-partition-adaptation --output-on-failure`, `.venv/bin/python -m pytest tests/python -q` and `git diff --check`; recheck the parent DiskANN gitlink and both admitted source identities/clean states, review the complete diff and SIFT1M evidence before any future larger-scale proposal, update `specs/003-ogp-partition-adaptation/quickstart.md` only with commands/results actually observed, and produce a final inventory separating executed/passed, failed, and applicable-unexecuted validation plus remaining risks without claiming completion beyond evidence.

---

## Dependencies and Execution Order

```text
T001 fixed source/build
  -> T002 truthful mixed-stage source + existing subordinate-Manifest transport
  -> T003 descriptor/process
  -> T004 cluster conversion
  -> T005 independent small real Partition (US1)
  -> T006 composed runner and tests
  -> T007 small closure + affected regressions (US2/MVP)
  -> T008 frozen SIFT1M attempt(s) (US3)
  -> T009 full audit
```

- US1 depends on fixed source/build admission and the source-selection foundation.
- US2 depends on the independently valid official OGP Partition from US1.
- US3 depends on the complete small real composed MVP and applicable regressions.
- No task authorizes SIFT100M or a larger Dataset. Any future larger-scale work requires a new
  scope, completed SIFT1M validation, recorded human diff review and explicit approval.

## Parallel Opportunities

No top-level task is marked `[P]`: the reduced plan prioritizes one evidence-backed vertical path,
and each task consumes the admitted identity or validated output of its predecessor. Test cases
inside T003/T004/T006 may be drafted independently before their shared implementation, but this
does not change task-level dependencies.

## Independent Test Criteria

- **US1**: `gp_ann_ogp_partition` proves exact official invocation and common-validator acceptance,
  and proves every declared invalid source/parameter/output/membership case leaves B0/M0 at zero.
- **US2**: `gp_ann_ogp_pipeline` proves one existing executor composition, exact stage counts,
  validation-before-consumption, source split/fallback and fixed Baseline isolation.
- **US3**: pytest plus retained attempt evidence proves one immutable SIFT1M configuration, honest
  retry/retention, complete validated structural closure on success and zero prohibited claims.

## Implementation Strategy

The MVP is T001–T007: a trusted small real official OGP Partition and a trusted small real
OGP→B0→M0 closure. T008 is the Constitution-required SIFT1M validation after semantic work. T009
closes regressions and evidence reporting. Deferred abstractions in `plan.md` must not be added as
"helpful" implementation extras.
