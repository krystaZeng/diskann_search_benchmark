# Feature 006 CLI and Invocation Boundary

This contract defines a narrow Feature-local orchestration surface. It does not create a public
Pipeline stage, Registry entry, generic lineage service, or certification framework. Existing
Feature 005 commands remain the only materialize/search/evaluate owners for PQ, DiskLayout, queries,
GroundTruth, search, parity, and evaluation.

## 1. Feature-local operations

The project-owned module is planned as:

```text
python -m diskann_harness.framework_baseline_certification <operation> ...
```

The only Feature 006 operations are:

| Operation | Mutation | Formal execution |
|---|---:|---:|
| `reference-preflight --config <path> --request <path>` | exclusive-creates one immutable native Reference request | No |
| `reference-execute --request <path>` | invokes the fixed native build once and retains terminal evidence | Yes; exact external approval required |
| `derive-structural --input <path> --output <path>` | exclusive-creates a Feature 002-compatible report snapshot | Pure evidence derivation |
| `freeze-quality-policy --input <path> --output <path>` | exclusive-creates the frozen Feature 006 policy | No search |
| `compare-quality --policy <path> --input <path> --output <path>` | exclusive-creates Quality Parity evidence | Offline only |
| `decide --input <path> --output <path>` | exclusive-creates the final decision | Offline only |

There is no generic `validate`, `certify`, `protocol`, or Pipeline-stage command. Schema and Artifact
validation continue through the existing authoritative validators.

## 2. Native Reference preflight

`reference-preflight` must:

1. verify actual Project and fixed upstream source identities and require both clean;
2. fresh-validate the DatasetArtifact and its ValidationEvidence;
3. verify the out-of-tree upstream `build_disk_index` Linux binary, binary SHA, independent build
   evidence, toolchain, fixed profile SHA, exact argv controls, resources, and workspace confinement;
4. require an exclusive output workspace on a filesystem that supports a hard link between the
   native `_mem.index` pathname and the capture pathname;
5. reject every existing request, output, capture, Manifest, evidence, or log path;
6. normalize one `native-reference-build-request/v1`, set `execution_authorized=false`, write it with
   exclusive-create semantics, re-read it, schema-validate it, and report path/length/SHA-256;
7. stop without launching a native process.

An external human approval binds the exact request path, byte length, SHA-256, inputs, binary,
environment, resources, and outputs. The immutable `execution_authorized=false` field is not edited;
the separate approval is the execution authority.

## 3. Native Reference execution

`reference-execute` must dispatch one project-owned Linux capture runner with a shell-free argv. The
capture runner is an operation owner, not a stage or graph implementation. It must:

1. re-read and validate the exact immutable request and every identity admitted by preflight;
2. capture actual `started_at` before observer setup or process launch;
3. pre-arm exact-filename inotify on the confined native output directory;
4. launch the pinned upstream `build_disk_index` binary once with the exact request argv;
5. on creation of the exact `_mem.index`, exclusive-create a same-filesystem hard link at the
   captured graph path; no copy fallback, symbolic link, or second attempt is allowed;
6. wait for the child and observer, record stdout/stderr/exit/observer events, and only after child
   completion validate the captured inode as the final graph;
7. copy the persistent `_disk.index_medoids.bin` to the immutable medoids payload path and validate
   it;
8. publish `GlobalGraphArtifact/v2` only after authoritative schema, checksum, Dataset source,
   native graph, medoids, entry-point, and aggregate checks all pass;
9. write independent terminal BuildManifest/v1 and ValidationEvidence/v1.

The terminal BuildManifest uses `stage=build`. Its `source` is the fixed upstream repository/branch/
commit/clean state. Its parameters separately bind the project operation-owner source/binary/build
identity, Reference role, request SHA, upstream binary, exact argv/profile, observer mechanism,
container/environment, stdout/stderr, and post-run upstream clean state. Metrics never enter the
Manifest.

On nonzero exit, missing event, capture failure, cancellation, partial output, checksum mismatch, or
invalid native structure, the owner retains a terminal `FAILED | CANCELLED | INVALID_OUTPUT`
Manifest and failure evidence, publishes no normal ArtifactRef, and performs no automatic retry.

## 4. Authoritative v2 validation

The existing `diskann_pipeline_runner validate` path remains authoritative after its planned
version-aware extension. Validation accepts v2 only when:

- exactly one validated DatasetArtifact is supplied and equals the v2 source identity and embedded
  Dataset identity;
- `lineage_mode=direct_native_final`, global count equals Dataset count, and
  `medoid_count=partition_count>=2`;
- capabilities are sorted/unique and include global adjacency and medoids;
- exact graph and medoids payload roles/media types, byte lengths, and SHA-256 match;
- the native graph declares its actual file size, contains exactly one record per GlobalNodeId, has
  no truncated/trailing records, valid entry point, frozen count zero, no self edges, only in-range
  neighbor IDs, and degrees no greater than the declared bound;
- the claimed observed maximum degree equals the full scan result;
- the medoids matrix is exactly `partition_count × 1`, every ID is in range, and the graph entry point
  is one of the medoids;
- the existing Artifact canonical projection recomputes the exact aggregate checksum.

V1 continues through its current Partition plus LocalGraph branch unchanged. A v2 document sent as
v1, a v1 document sent as v2, or any unsupported version fails closed.

## 5. Structural Gate and quality flow

`derive-structural` calls Feature 002's existing `load_policy`, `policy_checksum`, `derive_report`,
and `validate_report` behavior. It writes a new report instance outside Feature 002 and never edits
the frozen policy or retained `NOT_REACHED` report.

Only after Structural Gate `PASS`, complete exact Search Runner Parity admission, graph-specific
DiskLayout validation, and common search admission may existing Feature 005 formal-search preflight
be run once per graph role. Each call generates zero/fixed protocols and requests and then stops.
After all four immutable protocols exist and before any search executes, `freeze-quality-policy`
binds their exact identities plus the frozen controls and 0.010 rule.

Formal Reference and Framework searches remain separate Feature 005 `search` invocations with exact
human approval. Feature 005 `evaluate` remains the sole Recall/metric owner.

`compare-quality` only admits and compares the existing reports. It converts source Recall percent
to fraction, applies `reference - framework <= 0.010` separately to Recall@10 and Recall@100 for
every declared cache/L point, and uses an all-point conjunction. It retains all failed, invalid,
cancelled, mismatched, and excluded attempts and creates no replacement repetition.

`decide` is a pure deterministic mapping over the structural result, exact Search Runner Parity,
mandatory admission result, and Quality Parity result. It writes no state into any Artifact.

## 6. Exit and persistence rules

- Normalization or evidence derivation exits zero only after exclusive output write, re-read, schema
  validation, canonical checksum validation, and input-reference validation.
- Missing/invalid/ambiguous evidence produces an auditable `INCONCLUSIVE` comparison or decision
  when the frozen mapping permits evaluation; it is never inferred as PASS.
- Complete current-identity negative evidence maps to FAIL/`NOT_CERTIFIED` under the frozen rules.
- Existing output paths fail closed. No operation overwrites an Artifact, request, Protocol,
  Observation, report, policy, decision, log, or evidence file.
- Formal native build, DiskLayout materialization, and quality searches never run during Specify,
  Clarify, Plan, Tasks, or Implement and never follow merely from a successful preflight.
