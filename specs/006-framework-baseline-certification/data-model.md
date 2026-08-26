# Phase 1 Data Model: Framework Baseline Certification

## 1. Scope and ownership

Feature 006 combines existing evidence; it does not redefine pipeline or search objects. The only
public data-format addition is `GlobalGraphArtifact/v2`, an additive version of the existing
`global_graph` Artifact type for direct native-final lineage. Framework P0+B0+M0 continues to emit
v1. Artifact, BuildManifest, ValidationEvidence, structural report, search documents, Quality Parity
evidence, and final decision remain separate immutable objects.

## 2. Reused immutable objects

| Object | Authority | Feature 006 use |
|---|---|---|
| DatasetArtifact | public contracts/v1 | One admitted SIFT1M identity shared by both graphs and all derived inputs |
| GlobalGraphArtifact/v1 | public contracts/v1 | Existing Framework P0+B0+M0 graph only |
| ArtifactRef | public contracts/v1 | Exact type/version/aggregate/metadata identity |
| BuildManifest/v1 | public contracts/v1 | Independent provenance for Reference build, layouts, searches, and evaluation |
| ValidationEvidence/v1 | public contracts/v1 | Independent authoritative validation result |
| PQArtifact/v1 | public contracts/v1 | One byte-identical Artifact shared by Reference and Framework |
| DiskLayoutArtifact/v1 | public contracts/v1 | One separately generated layout per graph |
| Structural policy/report | Feature 002 | Frozen policy and fresh four-state report instance |
| QuerySetReference | Feature 005 | Same `selected_query.csv`, row identity, payload, mapping, and checksum |
| GroundTruthReference | Feature 005 | Same exact-L2 Dataset/QuerySet-bound truth |
| SearchProtocol/v1 | Feature 005 | Four immutable protocols: role × zero/fixed cache |
| SearchObservation/v1 | Feature 005 | One raw point per role/cache/L/repetition |
| SearchEvaluationReport/v1 | Feature 005 | Four single-protocol offline reports |
| Search Runner Parity evidence | Feature 005 | Same-input runner/oracle claim admitted only for the exact frozen identity |

Immutable data and references do not receive artificial lifecycle states. They are admitted,
rejected, or reused based on authoritative schema, checksum, lineage, and compatibility checks.

## 3. GlobalGraphArtifact/v2

### 3.1 Purpose

V2 represents the exact final merged graph and medoids produced by the fixed upstream native
sharded top-level path when its transient Partition/LocalGraph payloads are not retained. It is not a
generic lineage union and does not alter v1.

### 3.2 Identity fields

```text
schema_version: 1.0.0
canonicalization: diskann-artifact-c14n-v1
artifact_type: global_graph
format_version: global-graph-artifact/v2
dataset: DatasetIdentity
id_space: global
aggregate_checksum: sha256 of the existing Artifact canonical projection
```

Changing the Dataset identity, structural facts, capability set, Dataset source identity, payload
length/SHA, or format version changes the aggregate checksum. The BuildManifest and
ValidationEvidence are not embedded in this projection.

### 3.3 Structural facts

| Field | Rule |
|---|---|
| `lineage_mode` | exactly `direct_native_final` |
| `global_node_count` | equals admitted Dataset global count |
| `declared_max_degree` | positive native/Profile bound; fixed to 16 for this certification request |
| `observed_max_degree` | exact result of full graph scan and no greater than declared bound |
| `partition_count` | native sharded partition count, at least 2 |
| `medoid_count` | equals `partition_count` |

No PartitionArtifact, LocalGraphArtifact, local IDs, LocalToGlobal maps, native intermediate
references, BuildManifest fields, quality metric, or certification state may appear.

### 3.4 Relationships

```text
DatasetArtifact (validated)
    └── source_artifacts[0]
          └── GlobalGraphArtifact/v2
                ├── global_graph payload
                └── medoids payload

Native Reference BuildManifest.output_artifacts ──> v2 ArtifactRef
ValidationEvidence.artifact_candidate             ──> v2 ArtifactRef
DiskLayoutArtifact/v1.source_artifacts[1]          ──> v2 ArtifactRef
```

The BuildManifest is the sole record of how the native composite process produced the final graph.
It binds the fixed upstream source, exact top-level binary and argv, full profile, Dataset,
toolchain, resources, project capture owner, observer events, timestamps, status, logs, and clean
state. The v2 Artifact only answers what immutable data was retained.

### 3.5 Validation rules

The version-aware validator must:

1. schema-admit v2 and reject unknown or cross-labelled versions;
2. require exactly one already validated Dataset source and exact embedded Dataset equality;
3. verify sorted unique capabilities include `graph.global_adjacency/v1` and
   `graph.medoids/v1`;
4. hash both payloads and recompute the existing aggregate checksum;
5. fully parse the native merged graph, verifying declared/actual file size, count, GlobalNodeId
   ranges, entry point, frozen count zero, degree bound, every record, no self edges, and no
   truncated/trailing bytes;
6. require scanned maximum degree to equal `observed_max_degree`;
7. parse exactly `partition_count × 1` medoids, require all IDs in range, require
   `medoid_count=partition_count`, and require graph entry point to occur in the medoid list.

V1 keeps its current schema, catalog entry, canonical projection, Partition/LocalGraph source order,
and semantic branch byte-for-byte and behavior-for-behavior unchanged.

## 4. NativeReferenceBuildRequest

An immutable Feature-local request freezes one formal upstream native Reference invocation.

Required relationships:

- fixed upstream repository/branch/commit/path/clean state;
- Reference role and upstream-faithful reproduction mode;
- exact project capture-owner source/binary/BuildManifest;
- exact upstream `build_disk_index` binary path/length/SHA/build evidence;
- admitted Dataset ArtifactRef and fresh ValidationEvidence ref;
- frozen profile path/SHA/status and complete effective controls;
- Linux workspace, memory/thread/disk budgets;
- exact source and capture names, same-filesystem hard-link mechanism, and exclusive output paths;
- Artifact, Manifest, ValidationEvidence, logs, observer evidence, and structural projection paths.

It contains no runtime `started_at`/`ended_at`. Those are captured by the execution owner and written
only to the terminal BuildManifest. `execution_authorized=false` records that request creation is
not execution approval; separate human approval binds exact request bytes.

## 5. Native Reference build lifecycle

```text
request preparation
  -> request_created | rejected

execution admission
  -> admitted | rejected

observer setup
  -> ready | failed

native build/capture
  -> succeeded | failed | cancelled | invalid_output

Artifact validation
  -> VALID | INVALID

publication
  -> v2 ArtifactRef + terminal BuildManifest + ValidationEvidence
     | terminal failure evidence only
```

Rules:

- The observer is ready before the child process launches.
- One approval permits one process launch; there is no automatic retry.
- Hard-link capture is exact-path, exclusive, same-filesystem, and has no copy/symlink fallback.
- Child success alone does not publish an Artifact. Capture, post-run upstream clean state, payload
  parsing, source validation, and aggregate checksum must all pass.
- Failed/cancelled/invalid candidates and logs remain auditable but are not normal Artifacts.

## 6. StructuralGateSnapshot

This is an unchanged Feature 002 Structural/Contract Parity Report instance stored in the Feature
006 execution workspace. Its policy reference, role identities, 11 mandatory checks, claim boundary,
and derivation helper remain Feature 002-owned.

State is exactly:

```text
required execution or graph unavailable -> NOT_REACHED
else any mandatory FAIL                 -> FAIL
else any mandatory MISSING              -> INCONCLUSIVE
else                                     PASS
```

Feature 006 separately binds the report file path/SHA and underlying Artifact/Manifest/evidence
refs. It never overwrites Feature 002's retained current report.

## 7. CertificationSearchInputSet

The input set is an immutable relationship projection, not an Artifact:

```text
                    shared DatasetArtifact
                      /              \
       Reference GlobalGraph/v2   Framework GlobalGraph/v1
                 |                          |
    Reference DiskLayout/v1      Framework DiskLayout/v1
                 \                          /
                  shared byte-identical PQ/v1
                  shared QuerySetReference
                  shared GroundTruthReference
                  exact runner/parity identity
```

DiskLayoutArtifact/v1 needs no schema change: its second source is already any
`artifact_type=global_graph` SourceArtifactIdentity. The future implementation only extends the
fixed DiskLayout materializer descriptor's accepted versions to v1 and v2. Existing lineage
validation continues to require Dataset+Graph sources, matching Dataset identities, graph medoids,
full-precision float32 disk vectors, `pq_disk_bytes=0`, `reorder=false`, and `end_to_end` entry.

The two layouts must be different identities because their source graph checksums differ. The PQ
must be the same exact ArtifactRef and, if copied between role workspaces, metadata and both payloads
must be byte-identical.

## 8. QualityParityPolicy

The policy is frozen after all four Feature 005 Protocols exist and before either role's formal
quality search executes.

It binds:

- exact Reference v2 graph and Layout, Framework v1 graph and Layout, shared Dataset/PQ;
- exact QuerySet and GroundTruth document/canonical identities;
- Reference zero/fixed and Framework zero/fixed Protocol path, file SHA, protocol checksum, and
  search-execution checksum;
- K=100, ordered L list, beamwidth=2, threads=6, IO limit, warmup, query order, timing boundary,
  repetition count 3, schedule, end-to-end entry, zero-cache and requested-nodes=10000 fixed-cache;
- evaluator `cpp_main_search_metrics_evaluator_v1@1.0.0`;
- Feature 005 quality eligibility and `median_eligible`, with no replacement repetitions;
- Recall@10 and Recall@100, source report scale `[0,100]`, comparison scale `[0,1]`, and maximum
  shortfall 0.010;
- all-metrics-at-every-cache/L conjunction and no cross-point compensation.

Feature 005 emits an aggregate whenever at least one of the three declared attempts is eligible.
Feature 006 does not invent a stricter repetition minimum. Zero eligible repetitions means no
aggregate and therefore `INCONCLUSIVE`.

## 9. QualityParityEvidence

This Feature-local report is a pure transformation over the frozen policy and admitted Feature 005
documents. It contains:

- exact policy, role, Artifact, shared-input, and four EvaluationReport identities;
- every attempted Observation for both roles, including terminal status, eligibility, reason, and
  Recall metrics only when quality-eligible;
- all retained failed, invalid, cancelled, mismatched, and excluded audit refs;
- one group per declared cache/L point;
- per role: attempted count 3, eligible count 0..3, existing median aggregate when eligible;
- per metric: source percent, normalized fraction, signed shortfall, threshold, and PASS/FAIL;
- overall `PASS | FAIL | INCONCLUSIVE` and reason.

For each metric:

```text
reference_fraction = reference_percent / 100
framework_fraction = framework_percent / 100
shortfall_fraction = reference_fraction - framework_fraction
PASS iff shortfall_fraction <= 0.010
```

Framework equality or improvement therefore passes. A complete threshold violation produces FAIL.
Missing reports, missing/duplicate groups, no eligible aggregate, identity mismatch, invalid policy,
or otherwise unevaluable evidence produces INCONCLUSIVE. Results are conjoined; no L/cache/metric
may compensate for another.

Quality evidence lifecycle:

```text
input admission -> admitted | rejected
comparison       -> PASS | FAIL | INCONCLUSIVE
write/re-read    -> evidence_generated | rejected
```

The comparator does not recompute per-query Recall, alter Feature 005 metrics, or consume
informational performance values.

## 10. FrameworkBaselineCertificationDecision

The final Feature-local report has no certification ID and is not an Artifact or Registry record.
It references every available object required by FR-039, including missing/excluded evidence when a
complete set does not exist.

Decision inputs normalize to:

- Structural Gate: `PASS | FAIL | INCONCLUSIVE | NOT_REACHED`;
- exact Search Runner Parity admission: `PASS | FAIL | INCONCLUSIVE`;
- mandatory non-structural admission: `PASS | FAIL | INCONCLUSIVE`;
- Quality Parity: `PASS | FAIL | INCONCLUSIVE`.

Pure decision order:

```text
Structural FAIL                         -> NOT_CERTIFIED
Structural INCONCLUSIVE or NOT_REACHED  -> INCONCLUSIVE
Structural PASS and any conclusive FAIL -> NOT_CERTIFIED
Structural PASS and all remaining PASS  -> CERTIFIED
Structural PASS and otherwise missing,
invalid, stale, mismatched or ambiguous  -> INCONCLUSIVE
```

For the third line, conclusive FAIL is exact-current Search Runner Parity FAIL, complete mandatory
admission incompatibility, or complete Quality threshold violation. Structural mapping always has
precedence and cannot be overridden by quality evidence generated out of order.

`CERTIFIED` sets only `fixed_scope_framework_certified=true`. Every decision fixes these non-claims
to false: graph-byte/adjacency parity, complete Native/Framework equivalence, Framework-wide
certification, performance certification, and paper Feature certification.

Decision lifecycle:

```text
evidence admission -> complete | negative_complete | insufficient
pure derivation     -> CERTIFIED | NOT_CERTIFIED | INCONCLUSIVE
immutable write     -> decision_generated | rejected
```

## 11. Document checksums

Feature-local policy/evidence/decision checksums use the existing Python Harness canonical JSON
projection: UTF-8 JSON, object keys sorted, separators `,` and `:`, non-finite numbers forbidden,
and the document's own checksum field omitted. This is named
`diskann-json-sort-keys-sha256-v1`. File SHA-256 is still recorded independently in every external
document reference.

Artifact v1/v2 aggregate checksums continue to use `diskann-artifact-c14n-v1`; the two mechanisms
are not interchangeable.

## 12. Formal approval boundary

The following operations remain separate, exact-request, human-approved formal actions:

1. native SIFT1M Reference build and v2 final graph retention;
2. Reference DiskLayout/v1 materialization;
3. any Framework input materialization not already admitted for reuse;
4. Reference zero-cache search;
5. Reference fixed-cache search;
6. Framework zero-cache search;
7. Framework fixed-cache search.

Plan, Tasks, and Implement may build code and run deterministic/small fixtures only. Successful
implementation or preflight never implies approval for any operation above. Linux formal
performance remains a separate non-certifying experiment outside this Feature.
