# Data Model: OGP Partition Adaptation

This Feature adds no public Artifact or EvaluationReport family. It introduces one OGP
implementation, one optional stage-source aggregate, uses the existing
`StageResult.build_manifest_path` for stage-owned execution provenance, defines one official
subprocess boundary, and retains one frozen SIFT1M configuration whose attempts use existing
run/evidence objects.

## 1. GpAnnSourceAdmission

Purpose: prove that formal OGP code and executable come from the pinned official recursive source.

| Field | Rule |
|---|---|
| repository | `https://github.com/larsgottesbueren/gp-ann.git` |
| branch | `main` |
| commit | `df5efb4a6b303011c795666ed267f29d994b558c` |
| dirty_worktree | must be `false` for formal execution |
| recursive_dependencies | exact ParlayLib, KaMinPar, hnswlib and message-queue commits from `source-mapping.md` |
| source_root | independent checkout outside `upstream/diskann-cpp-main` |
| build_root | out-of-tree path outside all admitted source trees |
| toolchain/build options | actual compiler/version, CMake version, generator, build type and effective options |
| executable | regular admitted `Partition` binary path plus SHA-256 |
| status | `ADMITTED` or `REJECTED` with reason |

Validation rules:

- Root and recursive identities are checked before Registry/factory creation.
- A missing `.git`, wrong remote/branch/commit, dirty tree, incomplete/mismatched submodule,
  in-tree build, missing binary, or binary/source mismatch rejects formal execution.
- The unauthenticated research snapshot can support source study only and can never become
  `ADMITTED`.
- A clean-build failure is retained as a blocker; it does not authorize an upstream edit.

## 2. StageSourceIdentities

Purpose: bind mixed-source stages without changing the per-manifest SourceIdentity contract.

```text
StageSourceIdentities
  partition  -> SourceIdentity
  local_build -> SourceIdentity
  merge       -> SourceIdentity
```

Rules:

- The aggregate is optional on `PipelineExecutionRequest`.
- When absent, every stage uses the existing `request.source` value.
- When present, all three values are required; partial maps and generic source dictionaries do not
  exist.
- A formal OGP request binds Partition to admitted GP-ANN and Local Build/Merge to fixed DiskANN.
- Each BuildManifest still stores exactly one ordinary SourceIdentity selected for its stage.
- `stage_sources` is authoritative only for SourceIdentity; it does not carry stage toolchains or
  change the existing `ToolchainIdentity` meaning.

### Shared source_config

`source_config_json` retains the existing request-level run/config identity:

```json
{
  "yaml_sha256": "<sha256>",
  "normalized_json_sha256": "<sha256>"
}
```

It is shared input and may appear unchanged in every parent Stage BuildManifest. It is not
stage-specific build provenance and never contains the GP-ANN external build identity. The public
RunRequest schema remains unchanged.

### Existing StageResult build-manifest transport

`StageResult.build_manifest_path` has these fixed semantics:

| Value | Executor behavior |
|---|---|
| empty | Preserve legacy parent Manifest parameters; add no stage-execution reference. |
| non-empty | Require a confined current-stage BuildManifest/v1, validate it with existing contract/codec rules, persist its exact bytes immutably in the current run manifest area, compute SHA-256, and reference only that persisted copy from the corresponding parent Manifest. |

The parent reference uses the existing object-valued `parameters` only:

```json
{
  "stage_execution_manifest": {
    "path": "<workspace-relative persisted path>",
    "sha256": "<sha256>"
  }
}
```

The executor processes a non-empty subordinate path for either a successful or failed outer
StageResult before finalizing its parent Manifest. The subordinate document must bind the current
run/stage invocation and have an internally valid terminal status/failure shape. Its status need not
equal the outer status: subprocess success followed by post-process rejection is valid. A missing,
malformed, mismatched or path-escaping document fails closed. The reference is not copied into
another stage. No StageResult field, BuildManifest field/schema/codec, source-config schema,
Registry or generic provenance framework is added.

For OGP, the subordinate BuildManifest source is admitted GP-ANN and its toolchain is the actual
external C++20 build. B0 and M0 continue returning an empty `build_manifest_path`, select DiskANN as
their parent Manifest source, and retain legacy cpp_main provenance.

## 3. OgpParameters

| Field | Type/rule |
|---|---|
| requested_num_clusters | integer, `1..Dataset.global_node_count` |
| requested_overlap | finite number, `>= 0`, with overflow-safe `overlap * N` admission |
| partitioning_mode | `default` or `strong` |

Derived provenance:

| Field | Value/source |
|---|---|
| effective_epsilon | `0.05` |
| effective_epsilon_source | `upstream_cli_constant` |

`epsilon` is not a user field. Requested cluster count is not assumed to equal the effective
Partition count. Requested overlap is not assumed to equal actual assignment amplification.

## 4. OfficialPartitionInvocation

Purpose: one confined execution of the admitted official CLI.

| Field | Rule |
|---|---|
| executable | admitted path and SHA-256 |
| staged_input | confined `.fbin` hard-link or byte-identical copy |
| output_prefix | confined stage path |
| argv | exact seven-token invocation defined by the subprocess contract |
| configured_threads | requested StageContext budget placed in `PARLAY_NUM_THREADS` |
| stdout/stderr | separate retained confined files and checksums |
| exit_status | exact child process status |
| expected_output | exactly one regular expected cluster file and checksum |

Rules:

- Launch without a shell and with method token `OGP`.
- Verify staged input length/checksum equal the validated Dataset payload before launch.
- A nonzero exit, missing/ambiguous output, path escape, or malformed output fails the stage.
- Configured threads are recorded as configured provenance. The model has no observed worker fields
  because the official CLI exposes none.
- For every process actually started, write one existing BuildManifest/v1 and return its path via
  `StageResult.build_manifest_path`. This subordinate Manifest is the authority for the invocation
  values above and for the admitted source/build/executable identity.
- Retained stdout, stderr and other raw process evidence live in a confined Feature-private
  directory below the existing workspace report area. They are not Artifacts, EvaluationReports or
  a new report family.

Subordinate Manifest parameters contain at least exact argv, `effective_epsilon=0.05`, configured
`PARLAY_NUM_THREADS`, staged input path/checksum, stdout/stderr paths/checksums, expected output
path/checksum when present, exit status, and admitted executable path/SHA-256. Its source is GP-ANN;
its toolchain is the admitted external C++20 build/toolchain identity.

Lifecycle:

- process exit zero: subordinate status `SUCCEEDED`;
- nonzero process exit: subordinate status `FAILED`, `failure.exit_code` is the actual child exit
  code, and stdout/stderr remain retained;
- exit zero followed by cluster parse, OGP-invariant or Artifact-validation failure: subordinate
  status remains `SUCCEEDED`, while the parent Partition Manifest records `FAILED` or
  `INVALID_OUTPUT`;
- admission failure before launch: no subordinate subprocess Manifest is fabricated; existing
  admission/runner failure evidence remains authoritative.

## 5. RawOgpClusters

Purpose: represent official text output only inside the Partition stage before publication.

```text
RawOgpClusters
  clusters[]
    raw_index
    memberships[]: GlobalNodeId
```

Validation order:

1. parse strict decimal uint32 tokens and reject malformed/trailing content;
2. reject empty clusters;
3. reject IDs outside `[0, Dataset.global_node_count)`;
4. reject duplicates within one raw cluster before sorting;
5. require every Dataset GlobalNodeId in the union;
6. retain repetitions across different clusters;
7. sort each cluster ascending.

Raw output is temporary process data, not an Artifact.

## 6. CanonicalOgpMembership

Purpose: validated in-memory source for native LocalToGlobal maps.

| Field | Rule |
|---|---|
| partition_id | contiguous `0..effective_partition_count-1` |
| local_node_count | positive and equal to membership count |
| local_to_global[] | ascending, in range, same-shard unique |

The array position becomes LocalNodeId. A GlobalNodeId may occur once in several Partitions and
every occurrence must remain represented.

Derived facts such as effective Partition count, shard sizes, total assignments and assignment
amplification require no separate stored model: they are derivable from validated Artifact metadata
and requested parameter provenance when a future evaluation Feature needs them.

## 7. PartitionArtifact/v1 (existing)

OGP publishes the existing aggregate Artifact with:

- exact Dataset identity and one Dataset source Artifact identity;
- `id_space: local`;
- `partition_count = effective_partition_count`;
- one non-empty record and native uint32 LocalToGlobal map for each contiguous PartitionId;
- capabilities:
  - `partition.complete_local_to_global_map/v1`;
  - `partition.local_to_global_ascending/v1`;
  - `partition.overlap/v1`;
- no shard-vector or centroid payload;
- payload SHA-256 values and aggregate checksum.

The existing common Validator remains the only publication grant. Requested parameters,
subprocess logs, configured threads and evaluation conclusions are not Artifact fields.

## 8. OgpComposedRun

| Field | Rule |
|---|---|
| study_class | `single_paper_reproduction` |
| reproduction_mode | `faithful_adaptation` |
| replacement_scope | `partition` |
| implementations | OGP `gp_ann_ogp_partition_v1`, existing cpp_main B0 and M0 |
| stage_sources | GP-ANN / DiskANN / DiskANN |
| stage order | Partition once, B0 once per effective PartitionId, M0 once |
| artifacts | existing Dataset, Partition, LocalGraph[] and GlobalGraph references |
| evidence | existing BuildManifests, ValidationEvidence, runner result/quarantine, final graph EvaluationReport |
| query claims | prohibited |

State progression uses existing execution statuses rather than a new Feature lifecycle:

```text
source/build admitted
  -> executor preflight valid
  -> OGP candidate validated and published
  -> every B0 output validated
  -> M0 output validated
  -> existing GlobalGraph evaluation emitted
```

Any early failure leaves B0/M0 invocation counts zero and follows existing failure/quarantine
behavior. Failed or invalid output never enters reuse, downstream execution or aggregation.

## 9. FrozenSift1mConfiguration and Attempt

The single immutable configuration binds:

- exact SIFT1M path, shape, scalar type, metric, byte length and checksum;
- exact GP-ANN/DiskANN source/build/executable identities;
- OGP, B0 and M0 implementation identities and complete parameters;
- requested memory/thread/resource budget and configured child environment;
- `parameter_sweep: false` and no query evaluation;
- retention and workspace locations.

Each attempt records:

| Field | Rule |
|---|---|
| run_id | unique and immutable |
| configuration_checksum | identical across all attempts |
| started/ended/status | exact terminal evidence |
| outputs/evidence | existing references only |
| retry_of | absent for first attempt; prior run ID for retry |
| retry_reason | required for retry and limited to infrastructure/external execution failure |

A retry cannot change Dataset, implementations, parameters or resources. Every attempt remains
retained. A failed/invalid attempt is valid historical evidence but cannot satisfy the successful
SIFT1M criterion or enter successful result aggregation.

## 10. Evidence Separation

| Object | Contains | Must not contain |
|---|---|---|
| Artifact | immutable Dataset/maps/graphs and structural metadata | subprocess logs, parameters, performance claims |
| Parent Stage BuildManifest | stage-selected source, shared two-digest source config, inputs, parameters, resources, outputs, status/failure, and optional same-stage subordinate reference | another stage's subordinate reference or Recall/QPS/latency conclusions |
| OGP subordinate BuildManifest/v1 | one actual official subprocess source/toolchain, argv/environment, retained evidence references, executable/output identity, status/failure | Artifact validity grant or B0/M0 provenance |
| ValidationEvidence | contract-validation result for one Artifact | paper-quality conclusions |
| EvaluationReport | existing final GlobalGraph integrity findings | Partition generation provenance or Baseline relabeling |

No `OgpStructuralReport`, WorkerProvenance object, or new public report/evaluator schema is part of
this Feature. The parent Partition Manifest links the OGP subordinate BuildManifest through
`parameters.stage_execution_manifest {path, sha256}`. B0/M0 do not receive that reference, and the
shared `source_config` remains the unchanged run/config digest object in all parent manifests.
