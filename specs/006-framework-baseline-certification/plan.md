# Implementation Plan: Framework Baseline Certification

**Branch**: `feature/framework-baseline-certification` | **Date**: 2026-08-25 | **Spec**: `specs/006-framework-baseline-certification/spec.md`

**Status**: `PHASE_0_AND_PHASE_1_COMPLETE_NOT_IMPLEMENTED`

**Input**: Feature specification from `specs/006-framework-baseline-certification/spec.md`, including
the three clarified quality/decision rules and the explicit additive `GlobalGraphArtifact/v2`
planning authority.

## Summary

Feature 006 will determine, only for the fixed SIFT1M/L2/static scope, whether the Framework
`P0+B0+M0` candidate is a trustworthy DiskANN baseline. The implementation will retain and
authoritatively validate a fresh final graph from the fixed upstream native sharded path, re-derive
the unchanged Feature 002 Structural/Contract Gate, run control-identical Feature 005 quality
searches over graph-specific DiskLayouts and shared PQ/QuerySet/GroundTruth, apply the frozen 0.010
per-point Recall shortfall rule, and derive one Feature-local
`CERTIFIED | NOT_CERTIFIED | INCONCLUSIVE` decision.

Phase 0 confirmed that `GlobalGraphArtifact/v1` cannot truthfully represent a native final-only
lineage because v1 requires actual Partition and LocalGraph Artifacts. The user explicitly
authorized a minimal additive `GlobalGraphArtifact/v2` for `direct_native_final`. That version keeps
`artifact_type=global_graph`, binds the admitted Dataset, stores the exact final graph and medoids,
uses the normal source catalog and authoritative validator, and leaves v1 unchanged. This removes
the Constitution blocker without fabricated intermediates or a validator bypass.

## Technical Context

**Language/Version**: C++17 for public Artifact validation, native capture, DiskLayout compatibility,
and existing search code; Python 3.12 for Feature-local orchestration and pure evidence derivation;
JSON Schema Draft 2020-12

**Primary Dependencies**: fixed Microsoft DiskANN `cpp_main`; existing Artifact, ArtifactRef,
BuildManifest, ValidationEvidence, canonical checksum, schema source-sync, validator, and DiskLayout
infrastructure; Feature 002 frozen structural parity policy/helper; Feature 005 PQ/DiskLayout,
formal-search preflight, runner, Observation, evaluator, and report contracts

**Storage**: immutable documents and native payloads in confined out-of-tree workspaces, addressed
by relative path, byte length, SHA-256, ArtifactRef, and canonical document checksum

**Testing**: existing CTest and pytest schema/source-sync, codec, validator, native-format,
DiskLayout materialization/load-probe, preflight, search, evaluation, and parity tests; additive v2
contract/validator/native capture and Feature-local pure-decision fixtures

**Target Platform**: Linux x86-64 for fixed upstream native Reference build/capture and clean
binaries; Mac Docker may produce formal Recall after common search admission; formal Linux/local
SSD/O_DIRECT/libaio performance remains out of certification scope

**Project Type**: existing C++ library/CLI plus Python Harness and immutable evidence documents

**Study Class**: fixed Baseline certification, not a paper implementation, cross-paper composition,
or general certification platform

**Baseline Reference**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`, role
`cpp_main_native_sharded_reference_v1`, upstream-faithful, source path
`upstream/diskann-cpp-main`

**Framework Candidate**: existing `P0+B0+M0`, role
`cpp_main_p0_b0_m0_framework_candidate_v1`; remains a candidate until a valid decision is
`CERTIFIED`

**Search Runner Identity**: reusable Search Runner Parity PASS is limited to Project commit
`8cd169255c483999be0628e53045f2c3248b5b5e`, runner SHA-256
`4c711fdb2f68249f4b8051744f9f5c6278eae1c0f2862d0a8292244e5aec1b7d`,
implementation `cpp_main_pqflash_search_instrumented_v1`, and fixed DiskANN commit
`78256bbab4685e1774e78d331e081a153be26823`

**Search Runner Parity Admission**: the Feature-local admission additionally requires the frozen
zero/fixed parity document SHAs, Protocol file and canonical checksums, search-execution checksums,
fixed upstream oracle binary SHA, the complete 3 × 3 L/repetition matrix, and each linked
Observation/Protocol/oracle terminal identity. Only the exact PASS package is admitted; a missing,
stale, cross-bound, or mismatched package is `INCONCLUSIVE`.

**Compiler/Build Identity**: each future formal request binds actual compiler path/version, CMake
version/generator, Release, `-O3`, `-DNDEBUG`, C++17, applicable AVX/OpenMP flags, Feature CMake
definitions, binary path/length/SHA, and clean source state; no build occurs in this Plan

**Dataset Identity**: existing SIFT1M DatasetArtifact, 1,000,000 × 128 float32, L2; aggregate
checksum `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`;
vectors SHA-256 `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`;
Feature 005 formal QuerySet and compatible exact GroundTruth are reused unchanged

**Performance Goals**: none. Recall quality is certifying evidence. QPS, latency, and Mean IOs may be
retained as `informational_non_formal` but do not enter this decision.

**Constraints**: upstream read-only; v1 unchanged; no fabricated native intermediates; no graph,
adjacency, ordered result-ID, or distance equality across different graphs; no parameter sweep; no
new Pipeline stage, Registry, Adapter, backend, MetricPlugin, certification ID, claim-eligibility
system, generic lineage framework, or performance certification

**Scale/Scope**: exactly SIFT1M, L2, static unfiltered graph, labels disabled, Reference and
Framework only; no larger dataset and no OGP/HNSW-Merger certification

## Constitution Check — Before Phase 0

- [x] **Baseline immutability**: all capture/orchestration remains outside the fixed upstream tree.
- [x] **Pinned provenance**: roles, source, Dataset, profile, toolchain, binaries, requests,
      resources, statuses, and checksums are mandatory.
- [x] **Implementation coexistence**: Reference, Framework, capture owner, and instrumented search
      retain distinct identities.
- [x] **Node identity**: final graphs are GlobalNodeId; no transient LocalNodeIds or maps are
      reconstructed.
- [x] **Three-layer outputs**: Artifact, BuildManifest, ValidationEvidence, evaluation evidence, and
      decision remain independent.
- [ ] **Compatibility gate — initial blocker**: v1 requires actual Partition/LocalGraph lineage that
      the fixed native final-only path does not retain and this Feature must not fabricate.
- [x] **Study/report separation**: the outcome is fixed-scope certification, not paper, graph-parity,
      or performance certification.
- [x] **Fair protocol**: the 0.010 shortfall, all-point conjunction, existing repetitions,
      eligibility, aggregation, and exclusions are frozen before execution.
- [x] **Manual execution**: every formal SIFT1M operation stays behind exact request and separate
      human approval.

**Initial gate result**: `ERROR`. The v1-only representation would require false lineage or a
validator bypass. Phase 0 stopped at that point and recorded the evidence in `research.md`.

**Authorized resolution**: the user subsequently authorized an additive, versioned public
`GlobalGraphArtifact/v2` solely for `direct_native_final`, with version-aware schema/catalog/
validator admission and v1 preserved. With that authority, the compatibility item becomes PASS for
the Phase 1 design below.

## Phase 0 Research Decisions

Detailed evidence and rejected alternatives are in `research.md`.

### D1. Native Reference final graph retention

Use the fixed upstream top-level native sharded `build_disk_index` path. A project-owned Linux
capture runner, outside upstream and not a Pipeline stage, pre-arms exact-filename inotify and
exclusive-creates a same-filesystem hard link to `<prefix>_mem.index` while that path exists. It
allows upstream to unlink its pathname, waits for child completion, retains the persistent medoids,
and only then parses, hashes, validates, and publishes.

The formal operation has one immutable request and one external approval. Missing events, hard-link
failure, path mismatch, nonzero exit, cancellation, dirty source, malformed payload, or failed final
validation retains terminal failure evidence, publishes no Artifact, and causes no automatic retry.
Copy-race fallback, symlink, upstream modification, disk-layout reconstruction, and fabricated
intermediates are rejected.

### D2. Additive GlobalGraphArtifact/v2

V2 retains the existing `global_graph` Artifact type, GlobalNodeId, canonical Artifact checksum,
graph payload role, medoids role, and independent Manifest/Evidence layers. It adds only:

- `format_version=global-graph-artifact/v2`;
- `lineage_mode=direct_native_final`;
- exactly one admitted Dataset source;
- final graph structural facts and partition/medoid counts needed for authoritative validation.

The version-aware validator fully parses graph and medoids, verifies Dataset identity, counts,
degree bounds, Global IDs, entry point, no self edges, no truncation/trailing data, payload SHA/size,
aggregate checksum, medoid ranges, and entry membership. Unknown or cross-labelled versions fail.
The v1 schema, catalog rule, canonicalization, source order, and semantic validator branch remain
unchanged.

### D3. Schema source authority and catalog

The Feature 006 `contracts/global-graph-artifact-v2.schema.json` file is the authoritative design
source for future runtime target `contracts/v1/schemas/global-graph-artifact-v2.schema.json`.
Implementation adds exactly one `contracts/v1/SOURCES.json` mapping, with byte identity and source
SHA checks preserved. Existing target/source mappings, including v1, remain unchanged. The schema
catalog adds one `(global_graph, global-graph-artifact/v2)` entry; common Artifact types need no
change.

The source-sync test may learn that this one target is Feature 006-owned, but must not weaken unique
target, path confinement, source/target existence, byte identity, or checksum validation.

### D4. DiskLayoutArtifact/v1 compatibility

DiskLayout/v1 already admits a source whose Artifact type is `global_graph` without pinning its
format version. No schema amendment and no DiskLayout/v2 are needed. The fixed DiskLayout descriptor
will accept validated GlobalGraph v1 and v2; existing materialization and validation continue to
enforce matching Dataset identity, graph/medoid payload semantics, full-precision float32,
`pq_disk_bytes=0`, `reorder=false`, labels disabled, and `end_to_end` entry behavior.

### D5. Feature 002 Gate reuse

Use Feature 002's policy JSON, policy/report schemas, role identities, helper, precedence, and all
four states unchanged. Write a fresh report instance in the Feature 006 execution workspace; never
overwrite Feature 002's retained report. Mapping remains:

- `FAIL` -> final `NOT_CERTIFIED`;
- `INCONCLUSIVE | NOT_REACHED` -> final `INCONCLUSIVE`;
- `PASS` -> necessary but not sufficient.

Structural PASS is not Search Runner Parity, Quality Parity, graph equality, or certification.

### D6. Feature 005 controlled quality reuse

Reference v2 and Framework v1 graphs each receive a separately validated DiskLayout/v1. Both roles
reuse the exact same byte-identical PQArtifact, QuerySetReference, exact GroundTruthReference,
runner/evaluator identity, K, ordered L list, beamwidth, threads, IO limit, repetitions, cache
controls, query order, entry policy, and timing boundary.

Run existing formal-search preflight once per role to create zero/fixed protocols, four total. Each
role/cache protocol gets its own raw observations and Feature 005 EvaluationReport. Feature 005
remains the only Recall/metric owner. Different graphs may produce different adjacency, ordered
result IDs, and distances.

Feature 006 compares existing aggregates only. It converts Feature 005 Recall percentage to fraction
and requires, separately for Recall@10 and Recall@100 at every declared cache/L point:

```text
reference_fraction - framework_fraction <= 0.010
```

All findings must pass. No averaging, compensation, best-point selection, or replacement repetition
is allowed. A missing/unevaluable aggregate is `INCONCLUSIVE`.

### D7. Search Runner Parity admission

The current PASS is reusable only for the exact frozen Project commit, runner binary SHA,
implementation identity, fixed DiskANN identity, runner BuildManifest, and zero/fixed parity
evidence. Any source/binary/implementation/DiskANN mismatch requires a new parity run. A complete
current-identity FAIL is conclusive negative evidence; missing, stale, identity-mismatched, or
ambiguous evidence without such a FAIL is inconclusive. Runner parity remains a same-graph
instrumented/oracle claim only.

### D8. Feature-local policy, evidence, and decision

Feature 006 adds only a native Reference request, frozen QualityParityPolicy, QualityParityEvidence,
and final decision schemas. The policy freezes exact role inputs and protocols before search. The
comparison is a pure transformation over Feature 005 reports and audit refs. The decision is a pure
mapping over Structural Gate, exact runner parity, mandatory admission, and Quality Parity:

```text
Structural FAIL                         -> NOT_CERTIFIED
Structural INCONCLUSIVE or NOT_REACHED  -> INCONCLUSIVE
Structural PASS and any conclusive FAIL -> NOT_CERTIFIED
Structural PASS and all remaining PASS  -> CERTIFIED
Structural PASS and otherwise missing,
invalid, stale, mismatched or ambiguous  -> INCONCLUSIVE
```

No status is written into a graph, PQ, or DiskLayout; no certification ID or public subsystem is
introduced.

## Constitution Re-check — After v2 Authorization / Before Phase 1

- [x] Upstream remains read-only; capture and publication are project-owned and out-of-tree.
- [x] The exact upstream, Project operation owner, Dataset, profile, toolchain, binary, request,
      resource, status, and output identities remain mandatory and independently auditable.
- [x] V2 represents only observed GlobalNodeId final bytes and medoids; it neither reconstructs nor
      claims native intermediate lineage.
- [x] Artifact, BuildManifest, ValidationEvidence, structural/quality evidence, and decision remain
      separate objects.
- [x] The compatibility gate is now satisfiable through an authoritative additive schema/catalog/
      validator branch; v1 stays unchanged and no bypass is permitted.
- [x] Feature 002 and Feature 005 authorities are reused without changing their claims or rules.
- [x] The frozen fair-comparison and manual-approval boundaries remain intact.

**Re-check result**: `PASS`. The explicit v2 authority resolves the only prior Constitution error,
so Phase 1 design may proceed.

## Phase 1 Design

### Data model

`data-model.md` defines the v2 Artifact structure/validation, native capture lifecycle, Feature 002
snapshot, Reference/Framework input relationship, frozen quality policy, per-attempt/group evidence,
pure final decision, checksum domains, and manual-execution states. It explicitly preserves v1 and
all object-layer separation.

### Minimal contracts

```text
specs/006-framework-baseline-certification/contracts/
├── global-graph-artifact-v2.schema.json
├── native-reference-build-request.schema.json
├── quality-parity-policy.schema.json
├── quality-parity-evidence.schema.json
├── framework-baseline-certification-decision.schema.json
├── README.md
└── cli.md
```

Only the GlobalGraph v2 schema is a future additive public runtime source. The other four schemas
are Feature-local. Existing Feature 002/005 contracts are referenced, not copied or redefined.

### Quickstart

`quickstart.md` is a not-yet-executed validation guide covering source authority, v1/v2 schema and
validator behavior, small native capture, DiskLayout/v1 compatibility, Feature 002 four-state
derivation, Feature 005 two-role fixtures, Quality Parity boundaries, final-decision mapping, and the
formal SIFT1M manual boundary.

## Future Implementation Change Surface

The later implementation is limited to these categories; this Plan does not modify them:

```text
contracts/v1/
├── SOURCES.json                                      # one new v2 source mapping
└── schemas/global-graph-artifact-v2.schema.json      # byte-identical Feature 006 source

src/validation/
├── schema_catalog.cpp                                # one v2 catalog rule
├── validation_gate.cpp                               # version-aware structural shape dispatch
└── artifact_validator.cpp                            # explicit direct_native_final v2 branch

src/search/diskann/fixed_descriptors.cpp              # accept validated global_graph v1 or v2

harness/python/diskann_harness/
└── framework_baseline_certification.py               # bounded request/capture orchestration,
                                                       # structural reuse, comparison, decision

src/ or harness/ Feature 006 capture entry             # project-owned native observer owner

tests/
├── existing schema/source-sync/catalog/validator tests
├── existing DiskLayout/materialization/load tests
├── focused v1 regression and v2 native-format tests
├── small capture success/failure integration tests
└── Feature 006 pure policy/comparison/decision tests
```

No change is planned for the v1 schema/source, common Artifact enum, DiskLayout schema/version,
Feature 002 policy/helper, Feature 005 search/evaluation contracts, Feature 001–005 specs, or
upstream. Native validation should extend existing parsers/helpers rather than create a second
generic native-format framework.

## Project Structure

### Phase 0/1 outputs

```text
specs/006-framework-baseline-certification/
├── spec.md                    # clarified requirements; unchanged in this Plan
├── checklists/
│   └── requirements.md        # existing passed checklist; unchanged
├── plan.md                    # this Phase 0/1 implementation plan
├── research.md                # source/contract/reuse decisions and rejected alternatives
├── data-model.md              # entities, relationships, checksums, lifecycles
├── quickstart.md              # future fixture/static/formal-boundary validation guide
└── contracts/
    ├── README.md
    ├── cli.md
    ├── global-graph-artifact-v2.schema.json
    ├── native-reference-build-request.schema.json
    ├── quality-parity-policy.schema.json
    ├── quality-parity-evidence.schema.json
    └── framework-baseline-certification-decision.schema.json
```

### Existing authorities reused, not modified

```text
.specify/memory/constitution.md
AGENTS.md
contracts/v1/
src/validation/
src/search/diskann/
harness/python/diskann_harness/
specs/002-cpp-main-upstream-baseline/
specs/005-diskann-search-evaluation/
upstream/diskann-cpp-main/       # read-only fixed commit
```

**Structure Decision**: use one additive public GlobalGraph format source and a bounded Feature-local
orchestrator/evidence layer. This is the smallest structure that admits the real native final graph
through the existing public Artifact boundary and reuses Feature 002/005 unchanged.

## Test Strategy for Later Implementation

Tests must precede their coupled implementation tasks and use only schemas, deterministic fixtures,
and small native graphs until separately approved formal execution:

1. source-sync/schema catalog tests prove one authoritative v2 source, byte identity, safe path, and
   unchanged v1 mapping;
2. codec/validator tests prove exact v1 regression and full v2 success/failure behavior;
3. small capture tests prove observer ordering, exact hard-link retention, terminal Manifest/
   Evidence separation, no retry, and fail-closed publication;
4. DiskLayout tests prove v1 and v2 graph inputs both produce validated DiskLayout/v1 and load;
5. Feature 002 tests reuse the existing helper and exercise all four states without policy changes;
6. Feature 005 fixture tests prove shared PQ/QuerySet/GroundTruth/controls and separate graph/layout/
   protocol/report identities;
7. comparison tests cover 0.010 equality boundary, improvement, threshold failure, missing
   aggregate, identity mismatch, exclusions, and all-point conjunction;
8. decision tests cover every structural precedence and negative/insufficient mapping;
9. existing affected regressions, `py_compile`, schema legality/source sync, and `git diff --check`
   complete implementation validation.

No formal native Reference build, formal DiskLayout, formal SIFT1M search, or Linux performance run
belongs to these implementation tests.

## Formal Execution and Approval Boundary

After implementation and all applicable fixture validation, the following remain independent formal
operations requiring exact immutable requests and separate human approvals:

1. native SIFT1M Reference build and v2 final-graph retention;
2. Reference DiskLayout/v1 materialization;
3. any Framework materialization not admitted for exact reuse;

After Reference capture, the Structural/Contract Gate is explicitly re-derived from exact immutable
Reference-v2 and Framework-v1 evidence. Once both graph-specific DiskLayouts and all shared inputs
are complete or admitted for exact reuse, the existing Feature 005 `preflight --mode formal-search`
runs separately for the Reference and Framework roles and generates and re-validates four immutable
protocols: Reference zero-cache, Reference fixed-cache, Framework zero-cache, and Framework
fixed-cache.

Before any search below starts, a human must explicitly trigger the existing Feature 006
`freeze-quality-policy` offline evidence operation. The exclusive-created policy is immediately
re-read and validated and binds each of the four Protocol paths, file SHAs, and protocol identities;
both graphs and graph-specific DiskLayouts; the shared byte-identical PQ, QuerySet, and GroundTruth;
and the evaluator, cache/L declarations, and every frozen search control. It cannot be created,
replaced, or modified after any certification search starts. This operation uses the existing
Feature-local QualityParityPolicy contract, adds no request schema or authorization subsystem, is
not itself search approval, and does not replace the separate approval required for each search.
Only after this policy is successfully frozen may the following searches receive approval:

4. Reference zero-cache search;
5. Reference fixed-cache search;
6. Framework zero-cache search;
7. Framework fixed-cache search.

The Quality Parity comparator and final decision are offline evidence transformations, but they may
only consume the admitted formal evidence produced under the above boundary. A successful preflight
never implies approval. Formal Linux performance is outside Feature 006 and never affects the
certification result.

## Constitution Check — After Phase 1

- [x] **Baseline immutability**: capture is project-owned and upstream remains untouched/read-only.
- [x] **Pinned provenance**: request, BuildManifest, Artifact, and Evidence together bind exact
      Project/upstream, Dataset, profile, toolchain, binary, resource, timestamps, status, and output.
- [x] **Implementation coexistence**: every Reference/Framework/capture/search identity is explicit.
- [x] **Node identity**: v2 stores only GlobalNodeId adjacency and medoids; it does not invent local
      lineage.
- [x] **Three-layer outputs**: public/Feature-local schemas keep all output kinds independent.
- [x] **Compatibility gate**: explicitly authorized additive v2 uses authoritative source, catalog,
      checksum, native parser, and semantic validator; v1 remains unchanged; DiskLayout reuse stays
      version-aware and fail-closed.
- [x] **Study/report separation**: certification is fixed-scope and carries explicit non-claims.
- [x] **Fair protocol**: identical controls/shared inputs, frozen threshold, all-point conjunction,
      existing evaluator, complete audit refs, and no post-hoc adjustment are encoded.
- [x] **Validation and approval**: implementation fixtures and formal SIFT1M approvals remain
      separate, with no automatic formal execution.

**Post-design gate result**: `PASS`. The former v1 incompatibility is resolved by explicit bounded
authority, not an exception or bypass. Task generation is complete; implementation has not begun.

## Complexity Tracking

No post-design Constitution violation requires an exception. The additive v2 contract is the
authorized compatibility resolution rather than a bypass: v1 cannot truthfully represent the
observed native lineage, and the new version follows the existing schema/catalog/validator path.
The project-owned capture owner and Feature-local policy/evidence/decision are bounded operation and
document owners. None creates a new Artifact type, Pipeline stage, Registry, Adapter, generic
lineage/certification framework, or performance certification.

## Completion Boundary

- Phase 0 Research and Phase 1 Design are complete.
- No `NEEDS CLARIFICATION` remains and no design blocker remains.
- `tasks.md` is generated but not implemented.
- No runtime schema, validator, catalog, source code, test, Feature 001–005 file, upstream file, or
  formal experiment output is modified by this Plan.
- No build, test, materialization, search, evaluation, native Reference run, SIFT1M operation, or
  performance experiment is executed.
