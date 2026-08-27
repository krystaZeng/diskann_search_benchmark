# Phase 0 Research: Framework Baseline Certification

**Feature**: `006-framework-baseline-certification`

**Branch**: `feature/framework-baseline-certification`

**Date**: 2026-08-25

**Outcome**: `PHASE_0_COMPLETE`

This research resolves the technical source, format, validation, Feature 002, and Feature 005
questions without changing any frozen experimental semantics. It preserves the original v1 contract
blocker evidence and records how the explicitly authorized additive `GlobalGraphArtifact/v2`
resolves that blocker without fabricating native intermediates or bypassing validation.

## R1. Fixed native Reference execution and final graph retention

**Decision**: The Reference role must execute the unmodified upstream top-level native sharded
`build_disk_index` path from fixed commit `78256bbab4685e1774e78d331e081a153be26823`.
Project-owned orchestration remains outside `upstream/`. Before launch it arms an exact-filename
observer for `<prefix>_mem.index`; when that file appears, the observer exclusive-creates a
same-filesystem hard link in the confined capture workspace. The native process continues to consume
and unlink its own pathname; the retained inode remains available after exit. The persistent medoids
side output is copied separately. Hashing, format parsing, publication, and final validation happen
only after the native process exits successfully.

The observer must be ready before launch. Missing events, inability to hard-link, unexpected paths,
partial lengths, nonzero process exit, dirty upstream state, or invalid graph/medoids fail closed.
There is no copy-race fallback, automatic retry, upstream modification, `LD_PRELOAD` interception,
or reconstruction from the disk index.

**Rationale**:

- `upstream/diskann-cpp-main/src/disk_utils.cpp` executes the sharded P0/B0/M0 sequence, consumes the
  merged in-memory graph to create the disk layout, and then removes the merged graph.
- Feature 002 `source-mapping.md` freezes this lifecycle in SM-024 through SM-032.
- A pre-armed same-filesystem hard link preserves exact final bytes without changing upstream source,
  argv, graph semantics, random state, or control flow and without retaining native B0 payloads.
- The independent BuildManifest can record actual source, binary/toolchain, Dataset/profile, argv,
  resource budget, observer evidence, stdout/stderr, timestamps, terminal status, and output refs.

**Alternatives considered**:

- Calling only `build_merged_vamana_index`: rejected because it is not the fixed native top-level
  execution identity and skips the surrounding native lifecycle.
- Reconstructing from `index_disk.index`: rejected because a DiskLayout is not the transient merged
  GlobalGraph and Feature 002 explicitly forbids this inference.
- Modifying upstream cleanup: rejected by the fixed read-only Baseline.
- Asynchronous post-M0 copy: rejected as race-prone; the previous Reference probe missed the graph.
- Retaining all transient local graphs merely to satisfy the current schema: rejected by frozen
  FR-013/FR-014.

## R2. GlobalGraph v1 blocker and authorized additive v2 resolution

**Finding retained**: There is no truthful, validator-approved `GlobalGraphArtifact/v1`
representation for the native final-graph-only retention required by Feature 006.

**Evidence**:

- `contracts/v1/schemas/global-graph-artifact.schema.json` requires
  `structural.source_partition_artifact`, a non-empty
  `structural.source_local_graph_artifacts`, and at least two `source_artifacts`.
- `src/validation/artifact_validator.cpp` requires the first real validated source to be the
  PartitionArtifact and requires exactly one real validated LocalGraphArtifact per partition. It
  checks their identities against the GlobalGraph metadata.
- A Dataset, final merged graph, medoids, BuildManifest, and ValidationEvidence cannot satisfy those
  fields. ValidationEvidence records a result; it does not waive the Artifact's source lineage.
- The fixed native top-level run deletes the corresponding intermediates, while Feature 006
  FR-013/FR-014 say they must not be made newly mandatory or fabricated.

**Rationale**: Constitution Principle V requires the authoritative validator to pass before a graph
enters DiskLayout materialization, structural comparison, search, or certification. The Constitution
explicitly says validator bypass is never a justifiable exception. A plan that labels the native
bytes as v1 would be non-executable and would falsify provenance.

**Alternatives considered**:

- Borrow Framework P0/B0 ArtifactRefs: rejected as false Reference lineage.
- Invent missing native ArtifactRefs or metadata-only intermediates: rejected as fabricated evidence.
- Use a Feature-local validator exception: rejected as a validator bypass.
- Define a new Feature-local ReferenceGraph Artifact type: rejected because the Feature requires the
  existing GlobalGraph Artifact boundary and existing DiskLayout/search reuse.
- Delete lineage after one successful validation: rejected because later fresh validation and safe
  reuse would fail.

**Authorized decision**: add `GlobalGraphArtifact/v2` as an additive public format version in the
existing `global_graph` Artifact family. The user explicitly authorized this bounded contract
resolution after the v1 blocker was reported. V1 remains byte-for-byte and behavior-for-behavior
unchanged. V2 has exactly one lineage mode, `direct_native_final`; binds one admitted Dataset source;
retains only the exact final merged graph and medoids payloads; and keeps BuildManifest and
ValidationEvidence independent.

The complete authoritative design source is
`specs/006-framework-baseline-certification/contracts/global-graph-artifact-v2.schema.json`. Future
implementation must copy it byte-identically to
`contracts/v1/schemas/global-graph-artifact-v2.schema.json` through one new `SOURCES.json` mapping,
add the `(global_graph, global-graph-artifact/v2)` catalog rule, and add an explicit v2 semantic
branch to the existing authoritative validator. Unknown versions, cross-labelled v1/v2 documents,
invalid Dataset lineage, invalid native graph structure, or invalid medoids fail closed.

No common Artifact enum change is needed because `global_graph` already exists. No overlay, schema
composition engine, validator bypass, Registry, Adapter, or generic lineage framework is introduced.

**DiskLayout compatibility finding**: `DiskLayoutArtifact/v1` already constrains its graph source by
`artifact_type=global_graph` and does not hard-code the graph format version in its schema. Its
existing semantic validator consumes an already validated graph, Dataset identity, native graph
payload, and medoids. Therefore DiskLayout/v1 needs no schema/version change. Future implementation
only extends the fixed DiskLayout materializer descriptor's accepted GlobalGraph versions from v1 to
v1+v2 and adds version-aware regression/integration coverage.

## R3. Feature 002 Structural/Contract Gate reuse

**Decision**: Reuse Feature 002 without copying or changing its policy, schemas, roles, checks, or
four-state outcome logic.

The authoritative inputs are:

- `specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.json`
- `specs/002-cpp-main-upstream-baseline/contracts/structural-parity-policy.schema.json`
- `specs/002-cpp-main-upstream-baseline/contracts/structural-parity-report.schema.json`
- `harness/python/diskann_harness/cpp_main_structural_parity.py`

The fixed derivation order is:

```text
required execution or final graph unavailable -> NOT_REACHED
else any mandatory check FAIL                 -> FAIL
else any mandatory check MISSING              -> INCONCLUSIVE
else                                           -> PASS
```

The current report stays `NOT_REACHED`; Feature 006 must write a new report instance in its own
execution workspace rather than overwrite Feature 002's retained report. The new report binds the
frozen policy path/SHA and real Reference/Framework evidence. Feature 006's decision layer then maps:

- structural `FAIL` to `NOT_CERTIFIED`;
- structural `INCONCLUSIVE` or `NOT_REACHED` to `INCONCLUSIVE`;
- structural `PASS` to a necessary, never sufficient, certification condition.

Structural PASS remains explicitly separate from Search Runner Parity, Quality Parity, Graph-byte or
adjacency equality, and final certification.

**Alternatives considered**:

- Copying the policy into Feature 006: rejected because it creates a second authority.
- Adding checks or changing precedence: rejected by the frozen Feature 002 semantics.
- Inferring missing Reference evidence from logs or native DiskLayout: rejected; missing remains
  missing.

## R4. Feature 005 controlled quality-search reuse

**Decision**: Use two role-specific Feature 005 search workspaces. Each role runs one existing
`preflight --mode formal-search` call that produces exactly one zero-cache and one fixed-cache
protocol. This yields four protocol identities total without adding a comparison backend or search
framework.

The two roles use:

- separate validated GlobalGraph ArtifactRefs;
- separate graph-specific validated DiskLayoutArtifact/v1 identities and end-to-end entry lineage;
- the same byte-identical, fresh-validated PQArtifact/v1;
- the same QuerySetReference and compatible exact GroundTruthReference;
- identical K=100, ordered L list, beamwidth, threads, IO limit, query order, timing boundary,
  requested-node cache control, three repetitions, eligibility, exclusions, and evaluator identity.

The existing C++ runner remains the owner of raw search observations. The existing Python evaluator
remains the owner of Recall and optional informational performance metrics. Feature 006 does not
recompute Recall and does not require cross-graph graph bytes, adjacency, ordered result IDs, or
distances to match.

Each role's Feature 005 SearchEvaluationReport is single-graph evidence. A Feature 006 comparator
must admit the exact Protocol/report/observation identities, verify that every eligible observation
matches its Protocol's Dataset/Graph/PQ/Layout/QuerySet/search-execution identity, verify GroundTruth
compatibility, and reproduce only the existing median selection from the already calculated
per-repetition metrics. This is cross-document admission, not a second Recall evaluator.

**Scale conversion**: Feature 005 stores Recall as percentages in `[0,100]`; its evaluator returns
`matched * (100 / k)`. Feature 006's frozen threshold is `0.010` on `[0,1]`. The comparator must
record source percentages and normalize both values by dividing by 100 before computing:

```text
shortfall = reference_fraction - framework_fraction
pass = shortfall <= 0.010
```

This is equivalent to allowing at most 1.0 point on Feature 005's percentage scale. Equality or a
higher Framework value passes. Recall@10 and Recall@100 are compared separately at every declared
cache/L aggregate; all findings are conjoined and never averaged or compensated across points.

**Alternatives considered**:

- One mixed observation directory: rejected because the existing evaluator audits every observation
  in its directory and would retain the other graph as identity-mismatched noise.
- Cross-graph ordered-result parity: rejected; that belongs only to same-input runner/oracle parity.
- A new metric plugin or evaluator: rejected; the existing Feature 005 evaluator is authoritative.

## R5. Search Runner Parity admission

**Superseded historical identity**: The originally planned reusable PASS bound Project commit
`10f8520d8617fdaf8de3abf8a87842346ae4c96f` to runner SHA-256
`97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`. It is retained here as
historical evidence but is superseded for active Feature 006 admission by the fresh exact-current
zero/fixed PASS package below.

**Decision**: Treat the user-frozen reusable PASS as admissible only when the certification search
binds the complete exact identity tuple:

- Project commit `8cd169255c483999be0628e53045f2c3248b5b5e`;
- runner SHA-256 `4c711fdb2f68249f4b8051744f9f5c6278eae1c0f2862d0a8292244e5aec1b7d`;
- implementation `cpp_main_pqflash_search_instrumented_v1` and its version/mode;
- fixed DiskANN repository, branch, and commit;
- exact runner BuildManifest and zero/fixed parity evidence paths and SHA-256.

The active exact package additionally freezes oracle SHA-256
`51846c26b3b743a4c2065f6bbc733329bc4865cf19dd5370b87738ef9a3d573d`; zero evidence SHA
`61d0e7d2e4379666a3406f79677b34e07f6c7b26223fa3b4a05243f3cd76e8d3`, Protocol file SHA
`e8460a326db797ae6b39c6c532cc08e6d06f9b9c09fb7cefea2f7be35bdccbc5`, Protocol checksum
`f285c6436f9fea69697fea36a70731ea3e619fab23d8524cd9820047726e0f76`, and execution checksum
`312f950fc4870e12712ba961719f725885fdfc85ed92184db9eac6a95b438b5d`; fixed evidence SHA
`9ab05ba9f5969de2dce1c92beea63bcacb420f67e07b2496bb14b80160444148`, Protocol file SHA
`635e9e1d548185e4c1d44d88a200c3bc48790d4ba586496df9da105802058912`, Protocol checksum
`86eb2702abfd25461552e8d7db1749b5506012b508fd346d5af36b9e272389f5`, and execution checksum
`da8dce48bac56d5742b98077296edc6e70fe3dcafdaf3cd7fd94fcfed4239f55`.

An identity mismatch or stale/missing/incompletely bound PASS is not silently upgraded. A current,
complete FAIL is conclusive negative evidence and maps to `NOT_CERTIFIED`; a missing, stale,
identity-mismatched, or ambiguous package without a current FAIL maps to `INCONCLUSIVE`. The evidence
remains a same-graph runner/oracle claim only.

**Alternatives considered**:

- Accepting a PASS label without the source/binary/manifest tuple: rejected as insufficient
  provenance.
- Treating runner parity as graph or quality parity: rejected by Feature 002/005 claim boundaries.

## R6. Minimal Phase 1 contract set

**Decision**: The resolved admission boundary requires one additive public schema source and four
minimal Feature-local schemas:

1. `global-graph-artifact-v2.schema.json`: the complete authoritative source for the public v2
   direct-native-final Artifact format.
2. `native-reference-build-request.schema.json`: exact immutable request for one upstream native
   top-level build and final-graph capture; execution still needs separate approval.
3. `quality-parity-policy.schema.json`: pre-search frozen graph/layout/shared-input/protocol/control/
   evaluator identities and the 0.010 all-point rule.
4. `quality-parity-evidence.schema.json`: all admitted reports and attempts, exclusions,
   normalized per-point/per-metric findings, and `PASS | FAIL | INCONCLUSIVE`.
5. `framework-baseline-certification-decision.schema.json`: exact bounded scope, all available
   structural/parity/admission/quality evidence, reasons, exclusions, explicit non-claims, and
   `CERTIFIED | NOT_CERTIFIED | INCONCLUSIVE`.

The final four documents remain Feature-local. They are requests, policy/evidence/report documents,
not Artifacts, Registry entries, certification IDs, Pipeline stages, or generic certification
infrastructure. They stay separate from all Artifacts, BuildManifests, ValidationEvidence,
SearchObservations, and SearchEvaluationReports. The additive public v2 schema is necessary because
the existing public GlobalGraph boundary, not the comparison layer, owns downstream graph admission.

## R7. Pure final decision precedence

**Decision**: After the structural mapping, the pure decision order is:

1. Structural Gate `FAIL` -> `NOT_CERTIFIED`.
2. Structural Gate `INCONCLUSIVE | NOT_REACHED` -> `INCONCLUSIVE`.
3. Only for Structural Gate `PASS`:
   - exact-current Search Runner Parity `FAIL` -> `NOT_CERTIFIED`;
   - missing/stale/mismatched/ambiguous parity without current FAIL -> `INCONCLUSIVE`;
   - complete mandatory admission incompatibility -> `NOT_CERTIFIED`;
   - missing/invalid/incomplete/ambiguous admission -> `INCONCLUSIVE`;
   - any required cache/L aggregate missing or unevaluable -> `INCONCLUSIVE`;
   - any complete point/metric shortfall greater than 0.010 -> `NOT_CERTIFIED`;
   - all mandatory evidence complete and every point/metric passes -> `CERTIFIED`.

All failed, invalid, cancelled, mismatched, excluded, and incomplete evidence remains referenced for
audit and cannot contribute to favorable metrics, aggregates, or decisions. A structural Gate other
than PASS cannot be overridden by later quality evidence.

## R8. Formal execution and approval boundary

**Decision**: Specify, Clarify, Plan, Tasks, and Implement perform no formal SIFT1M operation.
After implementation and applicable small-fixture validation, every formal operation uses one
exclusive-created immutable request bound to exact inputs, checksums, binaries, environment,
resources, outputs, and separate human approval. The boundary covers:

- native SIFT1M Reference build and final graph retention;
- Reference DiskLayout materialization;
- any Framework materialization not already admitted for reuse;
- Reference zero/fixed quality search;
- Framework zero/fixed quality search;
- any future Linux formal-performance run (outside this certification scope).

Offline evaluation, Quality Parity derivation, and final decision are pure evidence transformations,
but they cannot be run as formal certification steps during this Plan and cannot compensate for
missing execution approvals.

## Phase 0 Conclusion

There are no unresolved `NEEDS CLARIFICATION` markers. The experimental semantics are fully frozen.
The original hard contract incompatibility is resolved at design time by the user's explicit,
bounded authority for additive `GlobalGraphArtifact/v2`. The solution leaves v1 unchanged, requires
the normal authoritative schema/catalog/validator path, and needs no fabricated lineage or bypass.
Phase 1 may therefore proceed with the Data Model, contracts, and Quickstart. No research blocker or
experimental-semantic clarification remains.
