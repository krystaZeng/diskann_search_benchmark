# Quickstart: Framework Baseline Certification Validation Guide

This guide describes the validation sequence to run **after implementation**. It does not claim that
the commands, fixtures, or formal SIFT1M operations have been executed during planning. Paths marked
`<build>` or `<workspace>` must be replaced by fresh out-of-tree paths.

## 1. Read-only identity and source-authority checks

Before building, verify the actual Project branch/commit/clean state, the fixed read-only upstream
DiskANN `cpp_main` commit `78256bbab4685e1774e78d331e081a153be26823`, and the active Feature 006
directory. Confirm that:

- `GlobalGraphArtifact/v1` still has its original Feature 001 authoritative source and byte-identical
  runtime target;
- the new `global-graph-artifact-v2.schema.json` runtime target has exactly one authoritative source,
  this Feature 006 contract;
- no target has multiple sources and the existing path-safety, file-existence, source-SHA, and
  byte-identity checks remain enabled;
- no common Artifact type is added because `global_graph` already exists.

Expected result: v1 source authority is unchanged; v2 is additive and version-addressable.

## 2. Contract and version-aware validator fixtures

Run the existing schema/source-sync tests plus the focused GlobalGraph validator tests after the
runtime target and catalog entry have been implemented. The focused fixtures must cover:

1. an existing valid P0+B0+M0 `GlobalGraphArtifact/v1`, which remains valid through the original
   Partition/LocalGraph lineage branch;
2. a small valid `GlobalGraphArtifact/v2` with one Dataset source, `direct_native_final` lineage,
   an exact native merged graph, and medoids;
3. v2 failures for missing or extra sources, wrong Dataset identity, wrong payload role/media type,
   bad byte length/SHA/aggregate checksum, node-count mismatch, out-of-range IDs, self edges,
   degree overflow, truncated/trailing records, observed-degree mismatch, invalid medoid matrix,
   and entry point absent from medoids;
4. version-label mismatch and every unsupported GlobalGraph format version.

Expected result: v1 behavior is unchanged; valid v2 passes the authoritative validator; every
invalid v2 fails closed without a bypass.

## 3. Small native-reference capture fixture

Use a deterministic fake child process or small native fixture outside `upstream/` to exercise the
capture lifecycle. The test must pre-arm the exact-filename observer, create the same-filesystem hard
link while the native `_mem.index` path exists, let the child unlink its own path, and validate the
retained inode only after child completion. Verify independently persisted:

- final graph and medoids payloads;
- `GlobalGraphArtifact/v2` metadata and aggregate checksum;
- terminal BuildManifest with actual source, binary/toolchain, request, timestamps, resources,
  logs, status, and output references;
- ValidationEvidence bound to the exact v2 ArtifactRef.

Also exercise nonzero child exit, missed event, unexpected path, hard-link failure, invalid graph,
and invalid medoids. Each failure must retain terminal evidence, publish no normal Artifact, and
perform no automatic retry. No fixture may modify the fixed upstream source tree.

## 4. DiskLayoutArtifact/v1 compatibility

Using small validated fixtures, materialize and validate one DiskLayout/v1 from a GlobalGraph/v1 and
one from a GlobalGraph/v2. Confirm that the existing DiskLayout schema and lineage validation are
unchanged, while the fixed materializer descriptor accepts both GlobalGraph versions. For v2 verify:

- Dataset and graph identities match;
- medoids drive the existing `end_to_end` entry behavior;
- float32 full-precision vectors, `pq_disk_bytes=0`, `reorder=false`, and labels-disabled semantics
  remain unchanged;
- the existing PQFlash load probe succeeds without introducing DiskLayout/v2.

Expected result: both versions produce independently identified DiskLayout/v1 Artifacts, and an
unsupported or unvalidated graph fails admission.

## 5. Feature 002 Structural/Contract Gate reuse

Run the existing Feature 002 policy loader, checksum, report derivation, and report validator over
small Reference-v2 and Framework-v1 evidence. Store the new report outside Feature 002. Cover all
four outcomes without changing the frozen policy:

```text
missing required execution/graph -> NOT_REACHED
mandatory negative check         -> FAIL
mandatory missing check          -> INCONCLUSIVE
all mandatory checks pass        -> PASS
```

Verify that Structural PASS is recorded only as a necessary certification input, never as Search
Runner Parity, Quality Parity, graph equality, or final certification.

## 6. Feature 005 controlled quality fixture

Prepare two role workspaces using the same small Dataset, byte-identical PQ, QuerySet, exact
GroundTruth, runner identity, controls, query order, and evaluator. Use a v2 Reference graph/layout
and a v1 Framework graph/layout. The existing Feature 005 formal-search fixture path should produce
zero-cache and fixed-cache protocols for each role, four protocols total.

Before executing fixture search, freeze the Feature 006 QualityParityPolicy with all four exact
Protocol file hashes, protocol checksums, and search-execution checksums. Run only fixture searches
and the existing Feature 005 evaluator. Do not require cross-graph adjacency, ordered result IDs, or
distances to match.

Expected report behavior:

- Feature 005 remains the only Recall and metric owner;
- every failed, invalid, cancelled, mismatched, or excluded attempt remains auditable;
- informational performance values do not enter certification;
- each role/cache combination has its own SearchEvaluationReport.

## 7. Quality Parity and final-decision table

Run the Feature-local offline comparator on deterministic report fixtures. It must divide Feature
005 Recall percentages by 100 and, for each cache/L point and each of Recall@10 and Recall@100,
evaluate:

```text
shortfall = reference_fraction - framework_fraction
PASS iff shortfall <= 0.010
```

Exercise equality, Framework improvement, exactly 0.010 shortfall, greater-than-0.010 shortfall,
missing aggregate, zero eligible repetitions, identity mismatch, and retained exclusions. All
declared point/metric findings are conjoined; no averaging, compensation, best-point selection, or
replacement repetition is allowed.

Then verify the pure final mapping:

| Structural Gate | Other evidence | Decision |
|---|---|---|
| `FAIL` | any | `NOT_CERTIFIED` |
| `INCONCLUSIVE` or `NOT_REACHED` | any | `INCONCLUSIVE` |
| `PASS` | exact current parity/admission/quality has a conclusive `FAIL` | `NOT_CERTIFIED` |
| `PASS` | every remaining mandatory input is `PASS` | `CERTIFIED` |
| `PASS` | otherwise missing, stale, mismatched, invalid, incomplete, or ambiguous | `INCONCLUSIVE` |

Expected result: the decision references all available evidence and exclusions and writes no status
into a GlobalGraph, PQArtifact, or DiskLayoutArtifact.

## 8. Implementation validation record

Feature implementation and fixture validation completed on 2026-08-26 in the out-of-tree build
`build/framework-baseline-certification/`. The fixed Linux image was
`sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`.
The directly affected Linux CTests were `valid_artifacts`, `invalid_artifact_metadata`,
`cpp_main_native_formats`, `native_reference_capture`, and `post_graph_materialization`.

The first `post_graph_materialization` run reached its fixture load probe under container `/tmp`
and failed with `io_submit() ... Bad file descriptor`. Its original log is retained byte-for-byte at
`build/framework-baseline-certification/Testing/Temporary/LastTest.t020-io-submit-ebadf.log`
(15,593 bytes, SHA-256
`47eef2398fc40219e1521d75873a48f9b141d39cf85e40d228f6444b0575dc17`). A single controlled
rerun kept the image, build, executable, selector, and other container parameters unchanged and set
only `TMPDIR=/workspace/build/framework-baseline-certification/t020-tmp-workspace-fs`. Container
`/tmp` was a `tmpfs`; the corrected directory was on the writable `/workspace` `fuse.grpcfuse`
mount. The corrected test passed 1/1, so the first result is classified as an in-test load-probe
environment/filesystem-placement failure. The other four selected CTests were not rerun.

The final applicable Python command was:

```sh
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=harness/python .venv/bin/python -m pytest \
  tests/python/test_contract_schemas.py \
  tests/python/test_contract_source_sync.py \
  tests/python/test_cpp_main_structural_parity.py \
  tests/python/test_diskann_search_evaluation.py \
  tests/python/test_framework_baseline_certification.py -q
```

It passed 76 tests and skipped only the explicit opt-in small-real Linux fixture. All five Feature
006 design schemas and the public GlobalGraphArtifact/v2 target passed Draft 2020-12 legality;
source-sync passed; the Feature 006 module and test passed `py_compile`; and `git diff --check`
passed.

The final source/provenance audit recorded:

- the Feature 006 GlobalGraphArtifact/v2 authority and runtime target are byte-identical at SHA-256
  `2607e64182f13139e90e2989777d6ddacca7bbea8e99f53fcddff1c2da5433ad`;
- the unchanged Feature 001 GlobalGraphArtifact/v1 authority and runtime target remain
  byte-identical at SHA-256
  `2ca8e99abbb94893045d13730d99041078ec2a4c767f845f4c41e47f8d98e34a`;
- Feature 002 policy/helper and Feature 005 protocol/observation/report/evaluator files have no diff
  from Project HEAD; the frozen Feature 002 policy SHA-256 remains
  `fcc6310415bdae8ce62231c2e6edf3ed8330b8234b8e8a90cb708e830299bd1d`;
- Search Runner Parity admission remains exact to Project commit
  `10f8520d8617fdaf8de3abf8a87842346ae4c96f`, runner SHA-256
  `97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`, implementation
  `cpp_main_pqflash_search_instrumented_v1`, and DiskANN commit
  `78256bbab4685e1774e78d331e081a153be26823`;
- Artifact, BuildManifest, ValidationEvidence, structural report, SearchEvaluationReport, Quality
  Parity evidence, and final decision remain independent documents.

The Project remained on `feature/framework-baseline-certification` at commit
`10f8520d8617fdaf8de3abf8a87842346ae4c96f` with the uncommitted Feature 006 implementation and
the protected user files untouched. Upstream remained on clean `cpp_main` commit
`78256bbab4685e1774e78d331e081a153be26823`. No formal SIFT1M operation, formal Structural Gate,
formal DiskLayout, formal protocol/policy/search, formal Quality Parity or decision, Linux formal
performance experiment, or production Partition/Local Build/Merge ran.

## 9. Non-task manual execution boundary

Implementation and fixture validation must stop before every formal SIFT1M operation below:

1. native Reference build and final-graph capture;
2. Reference DiskLayout materialization;
3. any Framework materialization not already admitted for exact reuse;
4. Reference zero-cache and fixed-cache quality searches;
5. Framework zero-cache and fixed-cache quality searches.

Each operation requires a new immutable request bound to exact source, inputs, checksums, binaries,
environment, resource limits, and outputs, followed by a separate human approval for those exact
bytes. A successful preflight does not authorize execution. Linux formal performance remains a
separate, non-certifying experiment and is not part of this Feature.
