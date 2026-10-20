# Feature 005 materialize/search/evaluate boundary

## Explicit measured-performance evaluation (revision 1.1)

The existing `evaluate` command selects behavior from the immutable protocol; there is no implicit
upgrade, new runner flag, condition-declaration interface, or environment probe. Protocol 1.0.0
keeps the v1 behavior documented below. To select v2, create a new protocol identity with:

```json
{
  "protocol_version": "1.1.0",
  "evaluation": {
    "evaluator": {
      "implementation_id": "cpp_main_search_metrics_evaluator_v2",
      "version": "2.0.0",
      "reproduction_mode": "instrumented"
    },
    "performance_eligibility_policy": "complete_valid_measurement_single_invocation_v2"
  }
}
```

This is a field-change illustration, not a complete protocol. Retain all required fields and the
exact original search_execution; use the existing normalizer and immutable writer to publish a new
protocol. Only protocol identity/evaluation and their document checksum change. Never rebind or
rewrite the original SearchObservation files. Incompatible GroundTruth or execution changes fail.

```bash
.venv/bin/python -m diskann_harness.diskann_search_evaluation evaluate \
  --protocol <new-frozen-protocol-1.1.json> \
  --observations <existing-observation-directory> \
  --workspace <existing-workspace>
```

Every otherwise valid observation retains the existing Recall/performance values in report v2;
CLI performance values equal persisted values. Native Linux is not a measurement-validity gate.
Search Manifest membership and absence of recorded condition conflicts control single-call
performance medians. Missing, unreadable, malformed or unbound search Manifest evidence keeps
values with an explicit non-aggregation reason.
Unknown environment facts limit interpretation, not measurement storage. Existing fingerprint
files still must be readable, valid JSON and match their declared checksums.

Parity is consumed offline without launching oracle/search. Missing or incomplete verification is
reported; actual semantic errors still exclude affected points. Corrupt/foreign evidence or missing
auxiliary files keeps valid measurements and records UNRESOLVED with the cause; affected aggregates
are withheld (all performance aggregates when scope is unknown). Previously confirmed errors remain
excluded. A report that cannot be read/parsed, or has no valid result, is INVALID_EVIDENCE rather
than MISSING/NOT_REACHED/PASS. No synthetic NOT_REACHED file is created by the v2 evaluator.
Existing report/Manifest targets remain exclusive-create. Re-evaluation needs a new protocol_id
and preserves all old inputs, reports and evaluation manifests.

The old preflight `performance_admitted` declaration remains a legacy v1 classification, never a
v2 validity grant. Paper-main-experiment selection is separate and no selection engine is added.

## Scope

The implementation exposes one fixed Feature runner and one Feature-specific Python Harness
module. This is not a public Search interface, backend registry, Pipeline stage, or generic
execution engine.

The top-level Feature operations remain exactly `materialize`, `load-probe`, `search`, `parity`,
`evaluate`, and `preflight`. `query_set` and `ground_truth` below are Feature-private Python Harness
suboperations of `materialize`; they are not C++ runner operations, public CLI families, Pipeline
stages, or Registry entries.

Planned identities:

```text
C++ executable: diskann_search_evaluation_runner
Python module:  diskann_harness.diskann_search_evaluation
```

All paths supplied to either boundary resolve under one admitted workspace. The Python layer owns
profile loading, strict normalization, source/environment admission, immutable request/protocol
write, and argv execution. The C++ layer owns native calls, native parsing, Artifact candidates, and
SearchObservation data. `diskann_search_evaluation_runner` owns the one terminal BuildManifest for
each PQ-generation, DiskLayout-materialization, and instrumented-search invocation. The Python
Harness owns the one terminal BuildManifest for each QuerySet-normalization, exact-GroundTruth,
upstream-oracle, and offline-evaluation invocation. Python and C++ never create duplicate manifests
for the same operation. Every owner reuses existing BuildManifest/v1 validation, helpers, and
persistence semantics; failed/invalid/cancelled manifests remain retained and separate from
Artifacts, ValidationEvidence, SearchObservation, and SearchEvaluationReport, and contain no
metrics. Neither layer may invoke P0, B0, M0, or M1.

## Normalized materialization request

The Python Harness writes one strict Feature-private request containing only:

- request/run identity and workspace root;
- exact DatasetArtifactRef;
- optional exact GlobalGraphArtifactRef (required for layout, absent for PQ-only);
- selected operation (`pq` or `disk_layout`);
- fixed implementation identity/version/reproduction mode;
- complete source/build/toolchain identity;
- operation parameters and resource budget;
- candidate/output paths.

For `pq` version 1.0.0, parameters are the historical R-004 projection (`B=0.03`, QD=0, chunks=32, sample rate 0.256,
OPQ=false, L2). For `disk_layout`, parameters are full-precision float32, disk PQ 0, reorder false,
labels false, end-to-end entry policy, and medoid-vector fallback. The C++ runner independently
validates operation, implementation identity/version and parameter types before input admission.

### General PQ policy 1.1.0

New experiments explicitly select the same implementation ID with version 1.1.0:

```json
{
  "operation": "pq",
  "implementation": {
    "implementation_id": "cpp_main_post_graph_pq_v1",
    "implementation_version": "1.1.0",
    "reproduction_mode": "upstream_faithful"
  },
  "parameters": {"B": 0.03, "QD": 0}
}
```

This illustrates the policy fields; retain DatasetArtifactRef, workspace/run identity and resource
budget in the complete request. B is a finite positive GiB value; QD is a JSON integer in [0,512]
and must not exceed the validated Dataset dimension. Derived chunks/sample rate and alternative
OPQ/metric/centroid controls are not request fields. Only the C++ private policy implementation
derives effective parameters, after validated actual N and dimension are available.

The Python `materialize --operation pq|disk_layout --request ... --runner ...` path normalizes
before launching the runner. It forwards a temporary normalized document and preserves the
original request bytes. Unknown versions, parameter drift and CLI/request operation mismatch fail.
Historical implicit native requests (no implementation, empty PQ parameters or empty/fallback-only
layout parameters) remain explicitly frozen 1.0.0 after normalization; they never select 1.1.0
based on N. Explicit 1.0.0 requests retain the full frozen parameter document.

PQ 1.0.0 retains its <=1M gate. PQ 1.1.0 and the existing DiskLayout 1.0.0 remove only the artificial
1M gate; native formats, ID/length/checksum/lineage checks still apply. This technical admission
does not authorize large runs. No Artifact or SearchObservation format changes.

The new PQ BuildManifest records requested B/QD, actual point_count/dimension, effective_budget_bytes,
effective pq_chunks/sample_rate, fixed constraints and the pinned derivation reference, alongside
existing project/upstream source, binary, toolchain and declared resources. Declared budgets are
neither measured peak memory nor hard allocation limits. Artifact metadata retains structural
facts and Dataset lineage only; EvaluationReport remains separate. Reusing a run/output identity
cannot overwrite existing records.

Planned invocation:

```bash
diskann_search_evaluation_runner materialize \
  --request <workspace-relative-materialization-request.json>
```

Output is a terminal existing BuildManifest path plus either a candidate Artifact metadata path or
failure/quarantine evidence. That manifest is owned by the C++ runner and is not duplicated by the
Python caller. Publication requires the existing Validator and ValidationEvidence.

## Feature-private input materialization

The existing Python Harness `materialize` operation also owns two input-preparation suboperations:

```bash
python -m diskann_harness.diskann_search_evaluation materialize \
  --operation query_set \
  --request <workspace-relative-query-set-request.json>

python -m diskann_harness.diskann_search_evaluation materialize \
  --operation ground_truth \
  --request <workspace-relative-ground-truth-request.json>
```

Neither suboperation invokes `diskann_search_evaluation_runner materialize` or adds a new C++
operation.

The `query_set` request binds the admitted workspace; exact source CSV path, byte length, SHA-256,
query count, dimension, and float32 type; zero-based `query_row_index` identity; opaque
`source_query_id` semantics; confined native-payload and row-mapping output paths; implementation,
source, and toolchain identity; and resource budget. Success writes an immutable native QuerySet
payload, immutable `query_row_index -> source_query_id` mapping, Feature-local QuerySetReference,
independent existing `stage="build"` BuildManifest/v1, independent ValidationEvidence, and terminal
success evidence. Failure retains a terminal manifest/evidence and publishes no normal
QuerySetReference. It does not create QuerySetArtifact or perform ID-based self-match exclusion.

The `ground_truth` request binds the freshly validated DatasetArtifact identity/checksum; validated
QuerySetReference and native query payload; fixed L2; frozen width and tie semantics; admitted
pinned `compute_groundtruth` binary path/SHA-256; workspace/output paths; implementation/source/
toolchain identity; and resource budget. The Harness invokes that binary by shell-free argv, retains
stdout, stderr, and exit status, and does not implement or copy an exact-KNN engine. Success writes
a Feature-local GroundTruthReference, independent existing `stage="build"` BuildManifest/v1,
independent ValidationEvidence, and terminal evidence. Failure retains terminal evidence and emits
no normal GroundTruthReference.

Only small fixtures run during `/speckit-implement`. Formal SIFT1M QuerySet normalization and exact
GroundTruth generation remain exact-request operations inside the non-task manual execution
boundary and require explicit user approval.

## PQFlash load probe

```bash
diskann_search_evaluation_runner load-probe \
  --pq-artifact <pq-metadata.json> \
  --disk-layout-artifact <layout-metadata.json> \
  --workspace <root>
```

The probe fresh-validates both Artifacts, stages loader-required filenames in a confined temporary
prefix, calls `load_from_separate_paths`, verifies counts/dimension/degree/entry policy, and writes
no normal output Artifact.

## Upstream-faithful oracle parity

The Harness invokes the unmodified admitted `search_disk_index` executable by argv, never shell
composition. Each parity point uses the exact same assets, query native payload, K, one L,
beamwidth, threads, cache count, IO policy, and no warmup as the instrumented point. GroundTruth is
supplied only for Recall parity. Oracle result bins/stdout/stderr/exit code are retained outside the
timed region. The Harness owns one independent terminal `stage="search"` BuildManifest for each
actual oracle invocation.

```bash
python -m diskann_harness.diskann_search_evaluation parity \
  --protocol <frozen-small-protocol.json> \
  --oracle <admitted-search_disk_index> \
  --runner <admitted-diskann_search_evaluation_runner> \
  --workspace <root>
```

## Instrumented search

The immutable SearchProtocol is the normalized search request. The formal path accepts a single
cache-specific protocol and verifies its canonical checksums before load.

```bash
diskann_search_evaluation_runner search \
  --protocol <frozen-search-protocol.json> \
  --workspace <root>
```

The runner:

1. validates the frozen Protocol and runner executable/BuildManifest identity, then either admits
   PQArtifact/DiskLayoutArtifact through the fresh ValidationEvidence references bound by
   non-fixture formal-search preflight or, for a legacy/no-evidence Protocol, retains the existing
   full `validate_with_lineage` path; QuerySetReference keeps full payload/checksum/shape/row-
   mapping validation in both paths;
2. loads PQFlashIndex and constructs the declared cache outside timing; `requested_nodes` is the
   only cache budget input, while admission/runtime evidence derives requested/effective primary
   bytes from the admitted dimension, scalar width, degree, and actual cache-list size;
3. executes repetition-major, then ordered L;
4. writes one immutable SearchObservation/v1 per raw point after its timer ends;
5. writes its one C++-runner-owned search BuildManifest and retains failed/invalid/cancelled
   evidence;
6. never loads GroundTruth and never evaluates.

Zero-cache and fixed-cache require separate process/protocol invocations. They must not be switched
within one timed execution.

## Offline evaluation

```bash
python -m diskann_harness.diskann_search_evaluation evaluate \
  --protocol <frozen-search-protocol.json> \
  --observations <confined-observation-directory> \
  --workspace <root>
```

Evaluation validates complete observations and GroundTruth compatibility, independently derives
quality/performance eligibility, invokes pinned Recall, mean, and percentile semantics, and retains
every failed/invalid/cancelled/identity-mismatched/excluded observation as an audit/exclusion
reference with identity, path, checksum, terminal status, and reason. Such a reference contributes
no metric, aggregate, median, or comparison finding and cannot make either eligibility true. The
operation emits one immutable Feature-local SearchEvaluationReport/v1.
The shared offline evaluator used by both Python and this CLI first admits the complete declared
trial set, using protocol/search-execution identities and each cache/L/repetition slot rather than
filenames or record counts. Duplicate slots, missing repetitions/groups, out-of-range repetitions,
undeclared parameter points, and untrustworthy identities or canonical observation checksums fail
the invocation; no successful report is published. Where its existing metadata prerequisites are
available, the FAILED evaluate BuildManifest records input paths and the specific reason.
Identifiable foreign-protocol inputs remain audit exclusions and do not complete the current set.
Identity/checksum-correct failed or metric-invalid records count as retained attempts, but have
no metrics. A complete set can have fewer eligible records than its declared repetition count;
zero eligible records in one group produces no aggregate for that group, while no evaluable
observation in the entire batch remains an error.

Before interpreting environment content for an otherwise eligible record, the evaluator checks
the actual fingerprint file SHA-256 against that record's declaration and parses the same bytes.
Missing/unreadable files, invalid JSON, or mismatched checksums fail evaluation. Already excluded
records need no environment I/O and cannot supply the report environment reference. That reference
contains the verified path and actual checksum. Identical environment evidence may be copied to
different paths; this does not relax environment/parity admission or authorize cross-environment
aggregation. No separate CLI or Certification production check duplicates this admission path.

A complete non-normative observation may enter formal Recall aggregation when quality eligibility
passes. For every complete successful quality-eligible observation, this Python Harness operation
must calculate and display QPS, mean latency, P99.9 latency, and Mean IOs from the retained raw
`batch_wall_clock_us`, `total_us`, and `n_ios`. Persisting those non-formal performance values in the
report is optional; if persisted, they are `informational_non_formal` and excluded from formal
performance aggregation. The C++ runner does not derive these metrics. The Python Harness owns one
independent terminal `stage="evaluate"` BuildManifest for each actual offline-evaluation invocation.
Evaluation performs no SSD search. Evaluation-only protocol changes may point to existing
observations only when their search execution identity/checksum is unchanged.

## Preflight and environment classification

`harness/python/diskann_harness/diskann_search_evaluation.py` is the sole SearchProtocol
normalization owner. Both uses below are modes of the same Feature-specific command and
implementation:

```bash
python -m diskann_harness.diskann_search_evaluation preflight \
  --mode readiness \
  --config <feature-readiness-config> \
  --workspace <root>

python -m diskann_harness.diskann_search_evaluation preflight \
  --mode formal-search \
  --config <exact-zero-and-fixed-cache-declarations> \
  --workspace <root>
```

Readiness mode validates only currently known source/build identity, paths, resources, environment,
and frozen controls. It lists absent PQArtifact, DiskLayoutArtifact, normalized QuerySet,
GroundTruth, and final Protocol identities as pending; it creates no SearchProtocol and starts no
formal operation.

Formal-search mode is allowed only after exact validated PQArtifact, DiskLayoutArtifact,
QuerySetReference, native QuerySet payload, and GroundTruthReference identities exist. It reads the
exact zero-cache and fixed-cache input declarations, normalizes and writes exactly two immutable
SearchProtocols once, then immediately rereads and validates both schemas, canonical checksums,
search-execution identities, every Artifact/input identity, runner binary, environment, paths, and
resources. Before a non-fixture invocation, the existing Validator fresh-validates PQArtifact and
DiskLayoutArtifact into two new exclusive-create ValidationEvidence paths. The config supplies each
evidence path/SHA-256/expected ArtifactRef; preflight requires schema-valid `result=VALID` evidence
whose candidate and claimed aggregate checksum exactly match the corresponding ArtifactRef and
binds both references into `search_execution` (and therefore its canonical checksum). Publication
evidence and admission booleans cannot substitute for these two fresh references. Fixture mode may
omit them and remain on the legacy full-validation path; an explicit fixture-evidence case covers
the fast path without turning fixture evidence into formal admission evidence. It then reports two
independent admission decisions:

- **Common search admission** requires validated compatible PQ/Layout/QuerySet/GroundTruth
  identities; the exact runner binary and usable ABI/dynamic dependencies; successful existing
  load-probe or small fixture-search evidence for the fixed DiskANN path in the current environment;
  readable inputs, confined writable non-overwriting outputs, sufficient memory/disk/thread
  resources; and frozen K/L/beamwidth/thread/cache/repetition/timing controls. It does not require a
  native Linux host, local physical SSD, normative O_DIRECT/libaio semantics, Search Runner Parity
  PASS, or `performance_eligible=true`. A binary, dependency, Artifact, or fixed search path that
  cannot actually work in Mac Docker fails this admission rather than being relabeled non-formal.
- **Performance admission** separately requires the existing native-Linux/local-SSD/O_DIRECT/libaio,
  Search Runner Parity, timing, and performance-eligibility evidence for formal QPS/latency/Mean IOs.

When common admission passes, the command emits exact search requests for review even if performance
admission is false; it labels that request quality-only/non-normative for performance and does not
start the C++ runner or search. After explicit approval, `search` consumes those frozen Protocols
without renormalization. An evidence-bearing Protocol performs metadata/ref/lineage checks and
payload existence/byte-length checks without rehashing large Artifact payloads or rescanning
graph/layout structure; any mismatch fails closed. Dataset/GlobalGraph producer manifests or
validation evidence are not reread per search. Legacy/no-evidence Protocols keep full lineage
validation. Both paths still hash and verify the actual runner and its bound
BuildManifest/source identity. SearchProtocol keeps the declared performance-environment requirement and
SearchObservation keeps the actual environment fingerprint; the mismatch affects performance
eligibility only and does not rewrite their identity/checksum rules.

Both modes record P/B/M non-reexecution evidence. There is no `protocol` command, second preflight,
Protocol builder framework, Registry, backend, schema, or status system.

Failure to prove Linux local-SSD O_DIRECT/libaio or Search Runner Parity makes performance admission
fail and `performance_eligible=false`; it cannot silently fall back and preserve a formal
performance claim. It does not block a request that passed common search admission, does not by
itself make `quality_eligible=false`, and neither derived decision is written into SearchObservation.

## Exit and persistence rules

- `0`: requested operation completed and all required outputs/evidence were persisted.
- nonzero: terminal BuildManifest/logs/candidate evidence are retained; no reusable normal output
  is published, and no metric evaluation or aggregation may consume the failed output. Retained
  terminal evidence may later be read only for its identity, path, checksum, terminal status, and
  reason to emit the required SearchEvaluationReport audit/exclusion reference.
- Cancellation uses existing `CANCELLED` semantics when actually observed.
- Unknown operation, implementation identity, format/media type, path escape, checksum mismatch,
  partial query set, or protocol mismatch fails closed.

No command in this contract authorizes SIFT1M execution during planning.
