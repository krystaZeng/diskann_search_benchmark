# Official GP-ANN Partition Subprocess Contract

## Identity gate

Before Registry creation, factory creation or subprocess launch, the Feature runner must admit:

```yaml
repository: https://github.com/larsgottesbueren/gp-ann.git
branch: main
commit: df5efb4a6b303011c795666ed267f29d994b558c
dirty_worktree: false
```

It must also verify the recursive submodule gitlinks listed in `../research.md`, an out-of-tree
build, `MIPS_DISTANCE=OFF`, actual toolchain/build options, and the official `Partition` binary
path and SHA-256. A missing, dirty, mismatched or ambiguous identity fails before any OGP factory is
created. The unauthenticated candidate snapshot is never accepted.

## Dataset staging

Input is an already validated `diskann-float32-dataset/v1` payload. The official reader dispatches
by suffix, so the stage creates a confined temporary file ending `.fbin` by hard-linking the exact
payload when supported or copying exact bytes otherwise. It must verify:

- staged byte length equals admitted payload length;
- staged SHA-256 equals Dataset payload checksum;
- no normalization, conversion, reordering or scalar rewrite occurred.

The staged file is B0-independent OGP preparation and is not an Artifact payload.

## Argv

The executable is launched without a shell using exactly:

```text
argv[0] = admitted Partition executable
argv[1] = confined staged input ending .fbin
argv[2] = confined stage output prefix
argv[3] = normalized requested_num_clusters decimal token
argv[4] = OGP
argv[5] = default or strong
argv[6] = normalized requested_overlap decimal token
```

The expected output path is exactly:

```text
<argv[2]>.k=<argv[3]>.OGP.o=<argv[6]>
```

The wrapper must not invoke `GP` and rely on the CLI's implicit rewrite.

## Parameter truth

- `epsilon` is not accepted from config.
- Normalized provenance records `effective_epsilon=0.05` and
  `effective_epsilon_source=upstream_cli_constant`.
- `requested_num_clusters` is a positive integer and at most Dataset node count.
- `requested_overlap` is finite, non-negative and must pass overflow-safe assignment arithmetic.
- `partitioning_mode` maps exactly to the official `default|strong` token.
- Effective Partition count and actual assignments are read from output, not predicted from argv.

## Worker environment

The child environment must be finalized before the official process initializes Parlay. Set
`PARLAY_NUM_THREADS` to the requested StageContext thread budget and retain the exact environment
entry as configured resource provenance. Pinned source establishes that OGP computes the KaMinPar
argument as `min(32, parlay::num_workers())`, but the official CLI exposes neither the active Parlay
worker count nor the active KaMinPar worker count. The Feature therefore records no observed-worker
field and introduces no worker-completeness status. Configured values must never be labeled
observed.

## Output and logs

For every actual subprocess invocation, the process writes stdout and stderr plus required raw
execution evidence to a confined Feature-private directory below the existing workspace report
area. It also writes one existing BuildManifest/v1 and returns its path through the existing
`StageResult.build_manifest_path`. This subordinate Manifest is the authoritative invocation
record. Evidence records:

- exact argv tokens;
- executable checksum;
- source/build identity;
- staged input and original Dataset checksums;
- child environment relevant to threads;
- stdout/stderr paths and checksums;
- exit status;
- expected output path and checksum.

Exit zero plus one regular expected output file is required before parsing. Nonzero exit, missing
output, ambiguous expected output, malformed text or path escape fails the Partition stage. Logs
and provenance are not PartitionArtifact payloads, EvaluationReports or a new report family.

The subordinate BuildManifest uses admitted GP-ANN as `source`, the actual external C++20
compiler/CMake/generator/build/options identity as `toolchain`, and stores the evidence above in its
existing object-valued `parameters`. On exit zero its status is `SUCCEEDED`. On nonzero exit its
status is `FAILED`, its `failure.exit_code` is the actual child status, and stdout/stderr remain
retained.

The PipelineExecutor confines and validates the returned path, immutably persists the subordinate
Manifest in the current run manifest area, calculates its SHA-256, and places only a
`parameters.stage_execution_manifest {path, sha256}` reference in the parent Partition
BuildManifest. It does not copy the reference into B0 or M0.

Exit zero followed by cluster-parse, OGP-invariant or Artifact-validation failure does not rewrite
the successful subprocess history: the subordinate BuildManifest remains `SUCCEEDED`, and the
outer Partition BuildManifest records `FAILED` or `INVALID_OUTPUT`. Source admission failure before
launch creates no subordinate subprocess Manifest and continues through existing admission/runner
failure evidence.

## Cluster parse and canonicalization

Parse one output line as one Partition, with strict decimal uint32 tokens. Before sorting, reject:

- an empty line/shard;
- malformed or trailing non-whitespace input;
- any ID outside `[0, Dataset.global_node_count)`;
- a repeated ID in the same line;
- any Dataset GlobalNodeId absent from the union of all lines.

Cross-line repetition is valid. After those checks, sort each line ascending and write one existing
cpp_main native uint32 map. Do not deduplicate or remove an empty shard. The existing common
Validator performs the formal Artifact publication gate.

## Clean-build caveat

The official README suggests manually removing a metrics block from hnswlib. This contract does
not authorize that edit. First configure/build the clean recursive checkout out of tree. If target
`Partition` cannot build without the edit, fail source/build admission and request an explicit
reviewed policy; do not patch the pinned source silently.
