# Implementation Plan: DiskANN Post-GlobalGraph Search Evaluation

**Branch**: `feature/search-evaluation` | **Date**: 2026-08-19 | **Spec**: [spec.md](spec.md)

**Input**: Clarified Feature specification in
`specs/005-diskann-search-evaluation/spec.md`.

**Planning boundary**: This document records Phase 0 Research and Phase 1 Design only. The
separately generated tasks do not change these decisions; no implementation or
materialization/search execution occurs here.

## Summary

Build a downstream-only, single-GlobalGraph search evaluation path that starts from freshly
validated DatasetArtifact and GlobalGraphArtifact, reuses the fixed DiskANN post-graph callable
surface, publishes reusable PQArtifact and DiskLayoutArtifact, records one immutable raw
SearchObservation per cache/L/repetition point, and produces an independent Feature-local
SearchEvaluationReport.

The selected technical path links a thin project-owned wrapper to the already admitted
`diskann_s` target and calls `generate_quantized_data<float>`, `create_disk_layout<float>`, and
`PQFlashIndex<float>`. It never calls full `build_disk_index`, never invokes P0/B0/M0/M1, and never
adds a fourth Pipeline stage. The unmodified `search_disk_index` executable is the
upstream-faithful Search Runner Parity oracle; a separately identified instrumented callable runner
captures raw IDs, distances, QueryStats, and batch timing outside upstream source.

## Clarification lock confirmation

The checklist at [checklists/requirements.md](checklists/requirements.md) is complete. All
clarification placeholders are resolved. The Plan preserves the accepted semantics:

- only `sift1M_dataset/selected_query.csv` is the first formal QuerySet;
- `query_row_index`, not the opaque CSV `id`, is canonical query identity;
- no ID-based self-match exclusion;
- K=100 with one top-100 observation for Recall@10 and Recall@100;
- `entry_policy=end_to_end`;
- three retained repetitions per cache/L point and median eligible aggregation.

No statement in the clarified spec conflicts with these answers.

## Technical Context

**Language/Version**: Project public API C++17; pinned DiskANN C++17; Python 3.11-compatible
Feature Harness. No public API requires a newer C++ standard.

**Primary Dependencies**: admitted `diskann_s` through `diskann_cpp_main_upstream`; OpenMP; MKL
and Intel OpenMP as configured by pinned DiskANN; Boost program_options for upstream oracle/tools;
libaio and Linux aligned reader; existing nlohmann JSON, SHA-256/canonicalization, JSON Schema,
Validator, workspace, provenance, and quarantine code.

**Storage**: confined filesystem objects. Public immutable PQ/DiskLayout Artifacts, independent
BuildManifests/ValidationEvidence, Feature-local immutable Protocol/Observation/Report documents,
and retained logs/candidates. Formal timed disk layout must reside on a proven local SSD.

**Testing**: existing CTest/C++ contract, validator, workspace, reuse, cpp_main, and fake
regressions; new Feature-specific unit/contract/native-load/parity/small-real tests; pytest schema,
source-sync, Harness, governance, and environment-preflight tests.

**Target Platform**: Linux is required for normative QPS/latency/IO. macOS and Docker Desktop/macOS
are non-normative performance platforms but may run formally approved quality searches when common
search admission passes. A Linux container is normative only when its bind mount is a proven host-
local SSD and in-container O_DIRECT/libaio probes pass.

**Project Type**: C++ library + fixed Feature runner + Python Harness + versioned file contracts.

**Study Class**: `infrastructure_validation`; downstream search semantics/evaluation. It is not a
single-paper reproduction or a graph-construction Baseline certification.

**Baseline Reference**:

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
reproduction_modes:
  oracle: upstream_faithful
  structured_runner: instrumented
```

**Compiler/Build Flags**: reuse the admitted out-of-tree cpp_main build. Pinned upstream enables
C++17, OpenMP, AVX2/FMA, `-Ofast`, and `-march=native` on its Linux path. Formal manifests record
the actual compiler/version, generator, build type, complete CMake definitions, compile/link
commands, dependency identities, and runner/oracle SHA-256. No formal compiler identity exists at
Plan time.

**Dataset Identity**:

```yaml
dataset:
  logical_path: sift1M_dataset/base_vector.bin
  format: DiskANN float32 matrix
  points: 1000000
  dimension: 128
  byte_length: 512000008
  sha256: 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e
query_set:
  logical_path: sift1M_dataset/selected_query.csv
  format: CSV id + 128 vector columns
  queries: 4885
  dimension: 128
  byte_length: 2905792
  sha256: 0f9c1c0db3207d681694658771422f48de1a7a84e7945681d0999a04f45a52ec
ground_truth:
  metric: exact L2
  planned_width: 200
  rank_100_tie_closure_required: true
  status: not_generated_by_plan
```

**Performance Goals**: no target QPS/Recall/latency threshold. Success means faithful search,
metric, QueryStats, and timing-boundary parity plus complete traceable raw data. Formal metrics are
reported, not optimized.

**Constraints**: SIFT1M maximum; L2/float32; full-precision disk vectors; disk PQ 0; reorder/filter/
range/dynamic index disabled; W=2; no automatic tuning/sweep/retry/best-result selection;
P/B/M not executed; upstream and gitlink unchanged.

**Scale/Scope**: one graph per MVP protocol, 4,885 queries, K=100, L `[100,200,400]`, separate
zero/fixed-cache protocols, three repetitions each: 18 raw search-point documents total. Optional
multi-graph comparison is a downstream compatibility constraint only.

## Constitution Check — before Phase 0

| Gate | Result | Evidence |
|---|---|---|
| Baseline immutability | PASS | Only read-only source audit; all planned outputs outside `upstream/`. |
| Pinned provenance | PASS | Full repository/branch/commit fixed; actual compiler/binaries required at admission. |
| Implementation coexistence | PASS | Oracle/instrumented/materializers have distinct fixed identities; no stage replacement is introduced. |
| Node identity | PASS | Dataset row and disk record remain GlobalNodeId; LocalNodeId never enters layout. |
| Three-layer outputs | PASS | Artifact, BuildManifest, ValidationEvidence, Observation, and Report are separate. |
| Compatibility gate | PASS | Fixed Feature descriptors declare requires/provides; common and post-graph validation fail closed. |
| Study/report separation | PASS | Candidate graph context, Search Runner Parity, search quality, and performance scopes remain separate. |
| Fair protocol | PASS | Cache/L/repetition/timing/exclusion/aggregation are predeclared; no tuning or replacement. |
| Validation/approval | PASS | Unit/contract/small/common-admission gates precede any formal SIFT1M search; parity/Linux gates additionally precede formal performance claims; >SIFT1M excluded. |

The registry-selection phrase in the generic template is not applicable to this downstream fixed
runner: the current Registry only accepts the three graph StageKinds. Creating a fourth Stage to
satisfy that phrase would violate the Constitution and Feature boundary.

## Phase 0 Research decisions

The full Decision/Rationale/Source evidence/Alternatives/Consequences/Risk record is in
[research.md](research.md). The implementation-driving decisions are:

1. **Reuse current lifecycle**: extend public Artifact type/schema/codec/catalog/Validator only for
   PQ and DiskLayout; reuse existing ArtifactRef/checksum/evidence/workspace/quarantine.
2. **No full build**: direct callables are the post-graph boundary; `build_disk_index` is forbidden
   because it invokes `build_merged_vamana_index`.
3. **One PQ per Dataset**: canonical PQ uses B=0.03, QD=0, 32 chunks, sample 0.256, 256 centers,
   OPQ=false. Random training is not made deterministic; fairness reuses the exact published bytes.
4. **One layout per graph**: native GlobalGraph is read directly, full float vectors are joined by
   GlobalNodeId row, disk PQ/reorder false, and graph medoids/centroid fallback remain end-to-end.
5. **Narrow compatibility check**: positive graph degree and actual loader prerequisites are checked
   after common validation without changing GlobalGraphArtifact/v1 validity.
6. **Truthful search controls**: K100, L `[100,200,400]`, W2, six threads, IO limit UINT32_MAX,
   no warmup, 10,000-node fixed BFS cache, separate zero cache, three repetitions. Cache bytes are
   derived evidence, not a second frozen request control; the 128D/float32/R16 case yields a
   5,800,000-byte requested primary-payload projection before effective-node adjustment.
7. **One raw point per observation**: the timer encloses only the upstream-equivalent OpenMP query
   batch; persistence/evaluation are outside.
8. **Pinned metrics**: Recall set/tie behavior, batch QPS, arithmetic mean, P99.9 index selection,
   and QueryStats.n_ios are source mapped.
9. **Exact formal inputs**: CSV conversion preserves row-index/opaque-ID semantics; exact GT width
   200 is generated by the pinned tool and must prove rank-100 tie closure.
10. **Feature-local report**: public graph-integrity EvaluationReport cannot carry complete search
    lineage without changing meaning. The strict report references Search Runner Parity and P/B/M
    non-reexecution evidence; neither reference is graph certification or a new public report.
11. **No Search registry/stage**: fixed Feature-private descriptors and runner are sufficient.
12. **Normative environment**: Linux local SSD + O_DIRECT/libaio probes; no formal buffered fallback.

## Phase 1 Design

### Data model

[data-model.md](data-model.md) defines:

- public `PQArtifact/v1` (`id_space=none`, Dataset source, pivots+codes);
- public `DiskLayoutArtifact/v1` (`id_space=global`, Dataset+one GlobalGraph, disk index+medoids);
- immutable QuerySet/GroundTruth references;
- SearchProtocol execution/evaluation checksum separation;
- one Feature-local SearchObservation per raw point;
- Feature-local SearchEvaluationReport with separate per-repetition quality/performance eligibility
  and separate median aggregates;
- operation states, validity/reuse rules, provenance, formal-quality eligibility, and
  formal-performance eligibility.

### Minimal contracts

The Phase 1 designs are under [contracts/](contracts/README.md):

| File | Decision |
|---|---|
| `common.schema.json` | full public v1 source authority with only `pq`/`disk_layout` added |
| `build-manifest.schema.json` | full public v1 source authority with only document stage `search` added |
| `pq-artifact.schema.json` | source design for public `PQArtifact/v1` |
| `disk-layout-artifact.schema.json` | source design for public `DiskLayoutArtifact/v1` |
| `search-protocol.schema.json` | strict Feature-local immutable request |
| `search-observation.schema.json` | strict Feature-local raw point evidence |
| `search-evaluation-report.schema.json` | strict Feature-local report |
| `cli.md` | fixed runner/Harness operation and argv boundary |

The common Artifact type addition and `BuildManifest/v1 stage="search"` are additive v1
amendments. `search` is a document classification, not `StageKind`. Dataset, GlobalGraph,
ArtifactRef, ValidationEvidence, public EvaluationReport, RunnerResult, and Pipeline request
schemas keep their existing versions and semantics.

Each PQ generation, DiskLayout materialization, QuerySet conversion, exact GroundTruth generation,
instrumented search, offline evaluation, and individual upstream-oracle invocation writes one
independent existing BuildManifest/v1. `diskann_search_evaluation_runner` owns the terminal manifest
for each PQ generation, DiskLayout materialization, and instrumented-search invocation;
`harness/python/diskann_harness/diskann_search_evaluation.py` owns it for each QuerySet normalization,
exact GroundTruth generation, upstream-oracle invocation, and offline-evaluation invocation. Each
actual operation has exactly one owner and one terminal manifest; Python and C++ never duplicate a
manifest for the same invocation. The operation-specific manifest carries implementation and
pinned source identity, actual toolchain/build flags, Dataset and all input identities/checksums,
complete parameters/resources/timestamps/status/output references, and failure reason for a failed
terminal outcome. Search/oracle manifests use document `stage="search"`; offline evaluation uses
`evaluate`; the other preparation operations use `build`. Failed, invalid, and cancelled calls
still retain terminal manifests. Payloads, ValidationEvidence, SearchObservation, and
SearchEvaluationReport remain separate, and metrics never enter BuildManifest. Existing manifest
validator/helpers and persistence semantics are reused; no new provenance or manifest framework is
planned.

### Artifact contract implementation surface

During implementation, the following atomic public PQ/DiskLayout contract change set must be
executed:

- ArtifactType enum/string codec and C++ metadata projections;
- common artifact-type enum;
- runtime PQ/DiskLayout schemas copied from the explicit Feature 005 source inventory;
- schema catalog and validation-gate shape;
- type-specific native parsing/semantic validation;
- canonical checksum/codec/evidence/reuse compatibility;
- capabilities and fixed Feature-private requires/provides declarations;
- `contracts/v1/SOURCES.json` with the runtime common and BuildManifest targets moved to the full
  Feature 005 source-authority schemas, new PQ/DiskLayout mappings, and every other target still
  mapped to its single original Feature 001 source;
- source-sync governance that retains unique target ownership, safe confined paths, source/target
  existence, byte identity, and source SHA-256 validation while removing only the obsolete
  all-sources-are-Feature-001 assumption;
- public C++17, schema, codec, valid/invalid/checksum/reuse regressions.

Existing Dataset/GlobalGraph schemas and checksum projections are unchanged.

### Fixed implementation boundaries

| Identity | requires | provides | registry |
|---|---|---|---|
| `cpp_main_post_graph_pq_v1` | validated float32/L2 Dataset | PQArtifact/v1 | none; fixed Feature runner |
| `cpp_main_post_graph_disk_layout_v1` | validated Dataset + global graph + post-graph compatibility | DiskLayoutArtifact/v1 | none |
| `cpp_main_search_disk_index_oracle_v1` | PQ/Layout/Query/protocol controls | oracle bins/stdout | upstream executable |
| `cpp_main_pqflash_search_instrumented_v1` | PQ/Layout/Query/search execution | SearchObservation/v1 | none |
| `cpp_main_search_metrics_evaluator_v1` | valid observations + GT/evaluation | SearchEvaluationReport/v1 | none |

Each descriptor is strict and fixed; unknown identity/media type/capability/service fails closed.

### Execution and admission flow

```text
Feature-specific Python Harness normalization (sole owner)
  -> normalize one exact operation request
  -> source/build/path/environment/current-Artifact admission
  -> materialize PQ/Layout through the fixed C++ runner, or
     materialize QuerySet/GroundTruth through Feature-private Python suboperations
  -> existing validation/evidence
  -> immutable publication/reference or retained terminal failure

formal-search preflight after exact PQ/Layout/QuerySet/GT identities exist
  -> existing Validator fresh-validates PQ and Layout once into new exclusive evidence paths
  -> bind evidence path/SHA/expected ArtifactRef in both exact declarations
  -> normalize and write exactly one zero-cache and one fixed-cache immutable SearchProtocol
  -> reread and validate both Protocols and common search admission
  -> independently classify performance admission from environment/Parity evidence
  -> expose exact search requests for human review
  -> approved search consumes the frozen Protocol without renormalizing it
```

Search executes from already validated PQ/DiskLayout without PipelineExecutor. It loads once per
cache-specific protocol, builds cache outside timing, then runs repetition-major/L-order batches.
GroundTruth is not loaded until offline evaluation.

`harness/python/diskann_harness/diskann_search_evaluation.py` is the sole SearchProtocol
normalization owner. The single Feature-specific `preflight` implementation serves two points in
time. During implementation it runs in **non-executing formal-readiness mode**: it checks only
currently known source/binary/path/resource/environment/frozen controls, lists missing PQ,
DiskLayout, normalized QuerySet, GroundTruth, and final SearchProtocol identities as pending,
creates no SearchProtocol, and cannot claim formal-preflight PASS or formal SIFT1M readiness.

Inside the non-task manual execution boundary, formal operations proceed in one order: generate and
validate the PQArtifact; generate and validate the DiskLayoutArtifact; run Feature-private
`materialize --operation query_set` to produce the immutable native QuerySet payload, row mapping,
QuerySetReference, BuildManifest, and ValidationEvidence; run Feature-private
`materialize --operation ground_truth` through shell-free argv to the pinned `compute_groundtruth`
binary and validate its GroundTruthReference; fresh-validate the exact PQArtifact and
DiskLayoutArtifact once more with the existing Validator into new exclusive evidence paths, freeze
their path/SHA/expected ArtifactRef in both declarations, and then invoke the same `preflight` in
**formal-search mode**. Formal-search mode reads exact zero/fixed-cache declarations, validates and
binds those fresh grants, normalizes and writes the two
immutable SearchProtocols exactly once, immediately rereads and validates their schemas, checksums,
search-execution identities, all Artifact/input identities, binary, environment, path, and resource
bindings, and evaluates two independent results. **Common search admission** requires validated and
compatible PQ/Layout/QuerySet/GroundTruth identities, an exact loadable runner binary, usable ABI and
dynamic dependencies, prior load-probe or small fixture-search evidence on the current environment,
confined readable inputs/non-overwriting writable outputs, sufficient memory/disk/thread resources,
and frozen K/L/W/thread/cache/repetition/timing controls. It does not require native Linux, a local
physical SSD, normative O_DIRECT/libaio performance semantics, Search Runner Parity PASS, or
`performance_eligible=true`. **Performance admission** separately evaluates the declared native-
Linux/local-SSD/O_DIRECT/libaio, Search Runner Parity, timing, and performance-eligibility evidence.
A Mac Docker request that passes common admission may therefore be emitted for an approved formal
quality run while performance admission remains false; a broken binary, dependency, Artifact, or
fixed search path still fails common admission. Only a subsequent user approval bound to the exact
request permits `search`; search consumes the frozen Protocol without normalizing again. There is no
`protocol` command, second preflight, builder framework, registry, schema, or status system.

At search startup, a new evidence-bearing Protocol uses the prevalidated fast admission path:
metadata/ref/evidence/lineage checks plus required PQ/Layout payload existence and byte length. It
does not rehash Dataset/GlobalGraph/PQ/Layout payloads, rescan graph/layout structure, or reread
Artifact producer manifests. Legacy/no-evidence Protocols retain the prior full
`validate_with_lineage` path. Both paths fail closed and independently validate the actual runner
executable SHA, fixed implementation/source identity, and bound runner BuildManifest SHA/content.
Loader aliases prefer hard links in confined temporary storage and copy only for explicit
cross-filesystem/platform capability errors; source Artifacts remain immutable.

### Validation gates

1. **Contract/unit**: schema strictness, codecs, native readers, checksum/source mismatch,
   compatibility, path confinement, failure/cancellation, and legacy regressions.
2. **Small materialization**: real fixed callable produces independently valid PQ and DiskLayout;
   PQFlashIndex loads; Dataset/graph checksums unchanged; no P/B/M execution.
3. **Search Runner Parity**: exact ordered IDs, distance tolerance, Recall@10/100, IO mapping,
   formulas, and timing boundary under identical small controls.
4. **Small independent US2**: prebuilt validated assets -> all raw observations -> offline report,
   without rerunning materialization.
5. **Applicable regressions**: common contract/validator/reuse/fake/cpp_main/Harness tests.
6. **Non-executing formal-readiness preflight**: checks known declarations and reports every
   not-yet-created PQ/Layout/normalized-QuerySet/GT/final-protocol identity as pending; it generates
   no data or protocol and cannot claim formal-preflight PASS or formal readiness.
7. **Formal input preparation, formal search preflight, and bounded SIFT1M**: occur only inside the
   non-task manual boundary. After exact PQ/Layout/QuerySet/GT identities exist, the same Feature
   preflight writes and rereads the two immutable Protocols, validates the complete requests, and
   stops for explicit review/approval before search; never >SIFT1M.

Compilation is not a gate substitute. A complete valid observation from a non-normative
environment may pass quality eligibility and produce formal Recall aggregates with compatible
GroundTruth, but cannot pass formal performance eligibility. The Python Harness evaluator still
computes and displays QPS/mean/P99.9 latency/Mean IOs from the raw observation; persistence is
optional, and any persisted values are `informational_non_formal` and excluded from performance
aggregates. The C++ search runner remains limited to search and raw observation capture/persistence.

### Search Runner Parity versus graph certification

Feature 002's frozen report currently states:

```yaml
graph_structural_parity_gate: NOT_REACHED
framework_graph_composition:
  identity: cpp_main_p0_b0_m0_framework_candidate_v1
  state: candidate
```

This state permits Feature 005 contract/fixture/non-normative/search-runner-parity work. It does not
permit Feature 005 to claim Graph Parity, full Native-vs-Framework parity, Search Quality/Recall
Baseline parity, or final Certified Framework Baseline. Every report binds the exact graph identity
and its candidate/reference context. Final certification remains a later, independent evidence
closure.

## Project Structure

### Documentation (this Feature)

```text
specs/005-diskann-search-evaluation/
├── spec.md
├── checklists/requirements.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── README.md
│   ├── common.schema.json
│   ├── build-manifest.schema.json
│   ├── pq-artifact.schema.json
│   ├── disk-layout-artifact.schema.json
│   ├── search-protocol.schema.json
│   ├── search-observation.schema.json
│   ├── search-evaluation-report.schema.json
│   └── cli.md
└── tasks.md                         # generated implementation checklist
```

### Planned source and test surface

```text
upstream/diskann-cpp-main/            # pinned read-only; never a change target

contracts/v1/
├── SOURCES.json                      # add explicit Feature 005 schema sources
└── schemas/
    ├── common.schema.json            # additive pq/disk_layout types
    ├── pq-artifact.schema.json
    ├── disk-layout-artifact.schema.json
    └── build-manifest.schema.json    # additive document stage "search"

include/diskann_pipeline/
├── identifiers.hpp                   # additive ArtifactType values only
└── artifacts.hpp                     # PQ/DiskLayout metadata projections

src/
├── core/                             # existing codecs/canonicalization only as required
├── validation/                       # extend catalog/current Validator for new Artifacts
└── search/diskann/
    ├── CMakeLists.txt
    ├── fixed_descriptors.*           # Feature-private, not ImplementationRegistry
    ├── native_search_formats.*
    ├── post_graph_materializer.*
    ├── pqflash_instrumented_runner.*
    ├── search_metrics_evaluator.*
    └── diskann_search_evaluation_runner.cpp

harness/python/diskann_harness/
└── diskann_search_evaluation.py      # fixed preflight/argv/evaluate path

tests/
├── cpp/search/
│   ├── CMakeLists.txt
│   ├── test_search_artifact_contracts.cpp
│   ├── test_post_graph_materialization.cpp
│   ├── test_pqflash_observation.cpp
│   └── test_search_runner_parity.cpp
└── python/
    ├── test_diskann_search_evaluation.py
    ├── test_contract_schemas.py       # existing, extended
    └── test_contract_source_sync.py   # existing inventory governance fix

experiments/configs/diskann-search-evaluation/
├── zero-cache-search-protocol.json
└── fixed-cache-search-protocol.json
```

**Structure Decision**: one Feature-owned DiskANN search library/runner plus one Harness module.
No generic Search interface, registry, plugin, DAG, comparison engine, or fourth stage is created.
Public changes are limited to the reusable Artifact types and one truthful manifest-stage enum.

## Quickstart

[quickstart.md](quickstart.md) is a not-yet-executed guide for small fixture PQ/layout
materialization, validation, PQFlash load, oracle/instrumented parity, raw observation generation,
offline report recomputation, and Linux/non-normative boundaries. It contains no formal SIFT1M
execution claim or Gate upgrade.

## Constitution Check — after Phase 1

| Gate | Result | Design confirmation |
|---|---|---|
| Baseline immutability | PASS | Direct link to admitted out-of-tree `diskann_s`; no upstream edit/generation. |
| Pinned provenance | PASS | Every operation/report binds full DiskANN and project build/input/protocol identity. |
| Implementation coexistence | PASS | Fixed operation identities coexist outside the graph Registry; no M0/M1 or Baseline change. |
| Node identity | PASS | Layout and search results are GlobalNodeId; Dataset row order is explicit. |
| Three-layer outputs | PASS | New Artifact payloads, generation manifests/evidence, raw observations, and reports are separate. |
| Compatibility gate | PASS | Existing Validator plus one narrow source-backed post-graph gate; unknowns fail closed. |
| Study/report separation | PASS | Search Runner Parity and candidate graph context are explicitly non-certifying. |
| Fair protocol | PASS | Two cache protocols, fixed L/W/T/IO/warmup, 3 reps, median/exclusions fixed pre-run. |
| Validation/approval | PASS | Small/regression/common admission precede bounded approved SIFT1M quality runs; parity/normative environment separately precede formal performance claims; no larger run. |

No Constitution violation requires a Complexity Tracking exception.

## Risks and blocker scope

| Risk/blocker | Current state | Blocks |
|---|---|---|
| exact Linux symbol/ABI link smoke not run | implementation probe required | native implementation acceptance |
| small medoid-fallback/PQFlash load not run | implementation probe required | DiskLayout publication acceptance |
| exact GroundTruth not generated; ~2.4+ GiB working memory | separate resource/admission run required | formal Recall evaluation |
| normative Linux local-SSD/O_DIRECT/libaio host not admitted | external environment blocker | formal QPS/latency/Mean IO claims |
| Feature 002 graph gate `NOT_REACHED` | documented, unchanged | final Framework Baseline certification, not Feature 005 infrastructure |
| PQ random training bytes unknown | expected until one approved generation | formal PQ identity, not design |
| public contract source test assumes Feature 001 | planned minimal inventory fix | runtime schema synchronization |

None is a requirements clarification. They are implementation or formal-execution gates and must
not be bypassed by weakening validation, substituting inputs, or changing protocol parameters.

## Complexity Tracking

No approved design violates the Constitution. Public PQ/DiskLayout Artifact types are necessary
because they are reusable immutable data; all search execution/evidence remains Feature-local.

## Plan completion

- Phase 0 Research: complete in [research.md](research.md).
- Phase 1 Data Model: complete in [data-model.md](data-model.md).
- Minimal contract designs: complete under [contracts/](contracts/README.md).
- Quickstart: complete in [quickstart.md](quickstart.md).
- Outstanding clarification placeholders: none.
- Tasks: generated in [tasks.md](tasks.md); implementation has not started.
- Implementation/formal SIFT1M work: not run.
