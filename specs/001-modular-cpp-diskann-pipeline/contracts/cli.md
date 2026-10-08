# Harness and C++ Runner CLI Contract

## 1. Boundary

The Python 3.10+ Harness is the user-facing orchestration CLI. It safely loads an experiment YAML
file, validates and normalizes it into an immutable JSON RunRequest, invokes CMake or the C++ runner
as a subprocess, and validates the JSON result. It does not use Python/C++ object bindings.

The installed production C++ runner contains only common core services and formal registrations and
constructs its Registry with `RegistryAdmissionPolicy::kProduction`. For this Feature it has no real
stage implementation. CTest/pytest use a separate non-installed `diskann_pipeline_fake_runner`
linked with `tests/cpp/fakes` and constructed with `RegistryAdmissionPolicy::kTest`.

Every Feature 001 request uses `study_class: infrastructure_validation`. The pinned cpp_main
Baseline remains a `not-executed` provenance reference. A future Feature that executes a real
Baseline uses its own versioned request schema supporting `baseline_validation` rather than
relabeling this Feature.

## 2. Python Harness Commands

```text
diskann-harness build
  --build-dir PATH
  [--build-type Debug|Release]

diskann-harness execute
  --config YAML_PATH
  --workspace PATH
  --runner PATH

diskann-harness validate
  --artifact-ref JSON_PATH
  --workspace PATH
  --runner PATH

diskann-harness evaluate
  --artifact-ref JSON_PATH
  --workspace PATH
  --runner PATH
```

Common behavior:

- input paths are resolved before mutation and must not escape their declared roots;
- YAML uses duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation: one project
  `yaml.SafeLoader` subclass rejects duplicate mapping keys, the top level must be a mapping, every
  mapping key must be a string, and recursive values are limited to `dict`, `list`, `str`, `bool`,
  `int`, finite `float`, and `None`; Python object/custom tags, unsafe constructors, NaN, Infinity,
  date/datetime, bytes, set, and other non-JSON types fail before schema validation;
- normalized JSON is validated against `run-request.schema.json` before C++ starts;
- the Harness writes manifests/results atomically inside the requested workspace;
- stdout is concise human status, stderr contains diagnostics, and machine state is JSON files;
- no command searches for or silently substitutes an implementation identity.

JSON Schema remains responsible for deciding whether a specific RunRequest field permits null,
float, or another otherwise JSON-compatible value. The Harness does not invoke a second YAML
parser.

`build` configures and builds out of tree and writes a BuildManifest containing the actual compiler,
generator, cache definitions, commands, and flags. It never writes inside `upstream/`.

## 3. C++ Runner Commands

```text
diskann_pipeline_runner preflight --request REQUEST.json --result RESULT.json
diskann_pipeline_runner execute   --request REQUEST.json --result RESULT.json
diskann_pipeline_runner validate  --request REQUEST.json --result RESULT.json
diskann_pipeline_runner evaluate  --request REQUEST.json --result RESULT.json
diskann_pipeline_runner registry  --result RESULT.json
```

Every command writes exactly one `runner-result.schema.json` document through a temporary file and
atomic rename. It never emits a valid result path before the write completes.

`execute` performs preflight internally even if a separate preflight command already passed. It
validates every input immediately before its consumer, invokes stages, validates candidates, and
publishes or quarantines each candidate before continuing.

For physical input admission, the common dispatch path first validates Dataset, then checks
request compatibility/services and parameters. Dataset-only callbacks cannot certify future
outputs. A standalone `preflight` with `reuse_artifact` resolves and validates that actual
Partition once through `ArtifactReuseService`, using the existing Dataset grant, then checks the
Local/Merge chain and physical maps against that input. The skipped Partition producer's
`provides` is not used to describe the reused Artifact.
That existing Partition validation evidence is persisted and referenced by the result's
`validation_evidence_path`, including when subsequent consumer admission rejects the input.
Consumer incompatibility leaves legal input evidence `VALID`; it causes neither revalidation
nor quarantine.

For `execute` with reuse, the CLI retains request/selection/parameter/service checks and the
Dataset-only callback, but leaves actual Partition validation and admission to Executor's
existing reuse path. It does not add a second complete Partition validation. A prior independent
preflight is not a cached execution grant. Non-reuse execution retains declared-chain preflight;
new payloads are admitted only after validation/publication. `validate` and `evaluate` retain
their earlier dispatch branches and do not run consumer admission callbacks.

Physical consumer rejection within Executor has the existing FAILED/exit 4 result and zero calls
to the rejected consumer's factory/stage. Earlier successful producers keep their manifests and
VALID evidence. Standalone preflight rejection uses exit 3. This does not classify a legal Artifact
as damaged, require migration, or change Artifact/BuildManifest/EvaluationReport separation.

## 4. Exit Codes

| Code | Meaning | Invocation guarantee |
|---:|---|---|
| `0` | requested lifecycle completed successfully | Result JSON is complete |
| `2` | CLI or JSON request/schema invalid | No stage invoked |
| `3` | Registry or compatibility preflight rejected | No selected stage invoked |
| `4` | implementation failed or run cancelled | No later stage invoked |
| `5` | produced output failed validation | Candidate quarantined; no consumer invoked |
| `6` | explicit validation rejected input | No evaluation or downstream invocation |
| `7` | evaluation refused or failed | Artifact remains unchanged |
| `10` | internal Harness/runner error | No success claim; inspect result/evidence if present |

Unrequested signal termination or inability to write a result is treated as an internal failure by
Python. When the Harness itself receives an interruption while waiting for the runner, it follows
the explicit cancellation lifecycle in Section 6 and publishes Harness-owned cancellation evidence;
it does not fabricate an Artifact or claim that the runner completed normally.

## 5. RunRequest Operations

- `preflight`: requires dataset ref, complete three-stage selection, parameters, and budgets.
- `execute`: same as preflight plus workspace and optional validated reuse input.
- `validate`: requires one ArtifactRef and expected consumer contract; it cannot publish a staging
  candidate supplied through the ordinary CLI.
- `evaluate`: requires one currently validated normal ArtifactRef and evaluator configuration.

YAML paths are normalized to workspace-relative JSON paths where stored. Human-authored YAML
provides the workspace root and budgets; `workspace.py` normalizes that root and writes the derived
normal, temporary, manifest, report, and quarantine roots into the normalized JSON RunRequest. The
original YAML digest and normalized JSON digest are both recorded in the Harness-level
BuildManifest.

For `preflight` and `execute`, the repetition policy is exactly `count: 2` and
`aggregation: retain_all`. Both repetitions use identical input and configuration. The Harness
creates distinct run IDs, report IDs, timestamps, and provenance, retains both results, and compares
payload checksums, aggregate Artifact checksums, and the deterministic EvaluationReport findings
projection. It never requires the complete report JSON documents to be byte-identical.

Partition execution produces one aggregate PartitionArtifact. The executor validates it, enumerates
its Partition records, and invokes Local Build once per selected record with the same aggregate
ArtifactRef and that record's explicit PartitionId. A missing ID, an ID outside
`0 <= partition_id < partition_count`, or an ID absent from the aggregate is rejected before the
LocalGraphBuilder is invoked. Each LocalGraphArtifact must record the selected owning PartitionId
and the aggregate source PartitionArtifact checksum.

The complete Feature-local YAML example is
[`examples/fake-experiment.yaml`](examples/fake-experiment.yaml).

## 6. Cancellation

Fake Partition, Local Build, and Merge each observe their in-process cooperative token at stage
start, after input resolution/validation, and before output publication. The Harness has no IPC for
remotely mutating that token. If the Harness itself is interrupted while waiting, it requests normal
termination of the runner subprocess, waits for a bounded grace period, and forces termination only
when necessary. It records whether forced termination was required, reports `CANCELLED` with exit
code `4`, publishes neither a normal nor an `INVALID_OUTPUT` Artifact, and cannot start a later
stage after the subprocess has terminated. Unrequested runner termination remains an internal
failure.

## 7. Security and Isolation Rules

- Ordinary execute/validate/evaluate commands cannot accept quarantine paths or diagnostic IDs.
- The runner does not follow symlinks out of the workspace or Artifact root.
- It creates stage temporary directories itself and refuses pre-existing unexpected contents.
- Output paths are not shell-interpolated; Python passes subprocess argument arrays.
- YAML configuration cannot request execution larger than the Feature's Fake dataset. Future scale
  approval belongs to a separate experiment Feature and is not inferred from configuration.
