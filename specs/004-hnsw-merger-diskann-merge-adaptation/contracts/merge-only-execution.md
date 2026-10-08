# Merge-Only Execution Contract

## Scope

This contract closes the one proven execution gap needed for an exact same-input M0/M1 experiment.
It is intentionally not an arbitrary-stage resume API.

## C++17 request model

The public types are semantically equivalent to:

```cpp
struct MergeOnlyInputSet {
  ArtifactRef partition;
  std::vector<ArtifactRef> local_graphs;
};

struct PipelineExecutionRequest {
  // existing fields unchanged
  std::optional<MergeOnlyInputSet> merge_only_inputs;
};
```

No C++20 type, StageResult change, BuildManifest schema change or RunRequest JSON field is added.

## Admission

If `merge_only_inputs` is absent, existing execution is unchanged.

If present, the executor MUST:

1. reject simultaneous use of `reused_partition`;
2. validate `request.dataset`;
3. resolve the selected Merge descriptor and validate only Merge parameters;
4. not resolve/create Partition or Local Build factories;
5. fresh-validate Partition against Dataset;
6. fresh-validate every LocalGraph against Partition;
7. require exactly one LocalGraph per PartitionId in ascending PartitionId order;
8. reject missing, duplicate, unknown, out-of-order or wrong-lineage owners;
9. require Dataset identity, type/version/checksum/ID-space and capabilities to match;
10. run Merge compatibility preflight against the validated available Artifact contracts and
    existing StageContext services;
11. pass the validated Partition and ordered LocalGraphs to existing candidate validation and
    publication so both top-level and structural source identities are checked;
12. invoke only the selected Merge factory through the existing execution, publication,
    quarantine and evaluation lifecycle.

Any admission failure occurs before factory creation and returns no normal GlobalGraph.

M1 additionally requires `vector_accessor/dataset-bound-v1` and
`distance_function/squared-l2-v1`. Shared runtime admission compares the actual accessor's retained
Dataset validation grant with the full validated input Dataset identity and requires explicit
squared-L2 semantics. M1's direct stage also checks against `load_merge_inputs().dataset` before
vector cache creation or distance evaluation. Correctly bound generic/in-memory accessors remain
supported; no CppMain accessor cast is added. Missing evidence, another Dataset, ordinary L2 and
unknown distance semantics are rejected without conversion. Valid input evidence is retained;
service failures do not imply invalid Artifacts or quarantine. M0 acquires neither requirement.

The service virtual-query extension requires recompiling all linked components; old/new binaries
must not be mixed. M1 implementation identity/version, numerical computations, Artifact, parameter
and request formats are unchanged. These are admission corrections, not new algorithm semantics.

## Evidence

- each individual `PipelineExecutor` result MUST report `partition=0`, `local_build=0`, `merge=1`
  when its selected Merge is reached;
- stage order MUST be exactly `[merge]`;
- the Merge BuildManifest input list MUST be Dataset, Partition, then LocalGraphs in ascending
  PartitionId order;
- input fresh-validation evidence paths MUST be retained;
- `REUSED` describes the input provenance, not a new stage execution;
- output validation and EvaluationReport behavior remains existing behavior.

The unchanged runner-result document belongs to the enclosing one-process comparison invocation,
not to either individual Merge arm. A successful comparison therefore records aggregate
`stage_invocations` as `partition=0`, `local_build=0`, `merge=2`. The Feature-local comparison
summary does not gain invocation-count fields. Exactly one M0 and one M1 execution are proven by
the aggregate count together with the summary's single `m0` run record, single `m1` run record and
their respective BuildManifest references. Focused C++ tests additionally assert the per-executor
`0/0/1` results. On a failed or cancelled comparison, the aggregate Merge count records only the
invocations actually reached and MUST NOT be presented as successful two-arm evidence. No public
runner-result or comparison-summary schema changes.

## Fair comparison use

From the single Feature-private wire request, the Feature runner constructs two in-process
`PipelineExecutionRequest` values with the exact same immutable `ArtifactRef` objects, changing only
the selected Merge implementation, its parameters/run identity and corresponding source provenance.
It calls the existing executor twice. This is not two wire requests or two runner invocations. It
MUST NOT run P0 or B0 and MUST NOT perform Merge orchestration itself.

## Feature-private normalized comparison request

This request is the sole wire document between Python preflight and the C++ Feature runner. It is
not the public RunRequest and does not create a generic comparison request family. Shared upstream
ArtifactRefs occur exactly once.

### Required fields

| Field | Type | Validation rule / consumer |
|---|---|---|
| `request_version` | string | const `hnsw-merger-comparison-request/v1` |
| `operation` | string | const `execute` |
| `request_id` | stable ID string | identifies the comparison; child run IDs are deterministically `<request_id>-m0` and `<request_id>-m1` |
| `study_class` | string | const `single_paper_reproduction` |
| `reproduction_mode` | string | const `faithful_adaptation` |
| `replacement_scope` | string | const `merge` |
| `profile_ref` | repository-relative path | the reviewed Feature profile |
| `profile_checksum` | SHA-256 string | exact profile bytes |
| `source_config` | object | exactly existing `yaml_sha256` and `normalized_json_sha256` run/config identity |
| `workspace` | object | confined `root`, positive `memory_budget_bytes`, and positive `thread_budget`; formal M1 uses one thread |
| `source_admission` | object | exact project-adaptation, pinned paper and pinned DiskANN source identities plus the admitted project/DiskANN build identity required by the current runner |
| `shared_inputs` | object | exactly one `dataset`, one `partition`, and non-empty ordered `local_graphs` |
| `merges` | object | exactly `m0` and `m1`, each containing fixed `implementation_id`, `implementation_version`, and its validated `parameters` object |

There are no optional fields in v1. Each ArtifactRef is the existing four-field
`artifact_type`, `format_version`, `aggregate_checksum`, `metadata_path` projection. The shared
Dataset and Partition appear once; LocalGraphs appear once in ascending owning PartitionId order.
Preflight rejects unknown fields, malformed types, wrong fixed identities/parameters, path escape,
duplicate/missing/out-of-order LocalGraphs, any Artifact identity mismatch, non-frozen configuration,
sweep/tuning/best-selection fields, or input above SIFT1M.

`source_admission` has exactly these fields:

- `project_adaptation`: clean SourceIdentity with `repository`, `branch`, full 40-character
  `commit`, and `dirty_worktree=false`;
- `paper`: the pinned clean HNSWMerger SourceIdentity plus DOI, PDF SHA-256,
  `integration_mode=source_mapping_only`, and `official_binary_linked=false`;
- `diskann`: the pinned clean cpp_main SourceIdentity;
- `toolchain`: the existing ToolchainIdentity projection actually admitted for the compiled Feature
  runner and linked DiskANN: `os`, compiler path/version, CMake version, generator, build type,
  effective CMake definitions, compile commands and link commands.

`merges.m0` is fixed to existing
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1@1.0.0` with
parameters `{R: 16, use_filters: false}`. `merges.m1` is fixed to
`hnsw_merger_vamana_merge_v1@1.0.0` and the seven constants in the approved M1 parameter schema.
Neither object accepts an alternate strategy or free lambda.

### Ownership and invocation

```text
Feature profile
  -> Python preflight validates and normalizes exactly once
  -> immutable Feature-private comparison request
  -> one C++ Feature runner process
  -> same parsed shared tuple constructs M0 and M1 MergeOnlyInputSet values
  -> PipelineExecutor executes M0 once and M1 once
```

The C++ runner validates the normalized wire shape and formal source/build admission but does not
reinterpret the profile or establish another normalization policy. M1 never consumes M0 output.
The exact runner argv is:

```text
hnsw_merger_experiment_runner execute --request FILE --result FILE
```

`FILE` for `--request` is the immutable normalized request; `FILE` for `--result` is a confined path
for the unchanged existing runner-result document. The runner writes the already-approved
Feature-local same-input structural comparison separately. No public RunRequest, runner-result,
Artifact or EvaluationReport schema changes.

### Formal SIFT1M boundary

Tests of admission, normalization and runner wiring use deterministic or small-real fixture
requests. They MUST NOT launch the formal request at
`build/hnsw-merger-merge-adaptation/runs/sift1m-structural/requests/merge-comparison-request.json`.
Formal preflight only admits and fresh-validates inputs, normalizes once, writes that one immutable
request for human review, and leaves P0/B0/M0/M1 factory invocation counts at zero; it performs no
Merge. After the exact request is reviewed, the formal main path starts it once: one Feature runner
process executes M0 once and M1 once. No test-first step may consume that formal request or cause a
second formal comparison execution.

The predeclared identical-configuration infrastructure-failure rule is not an automatic second
main-path execution. If it is triggered after a terminal infrastructure failure, the failed attempt
is retained and any retry is a separately reviewed attempt over the identical immutable request;
changed inputs or parameters and best-result selection remain prohibited.

## Physical input admission

The comparison runner binds M0's `m0_input_admission` and M1's `input_admission`. After existing
fresh validation of Dataset, Partition and every ordered LocalGraph, Executor checks every
Partition map and each LocalGraph's map/graph media type before creating the Merge factory.
Mixed native/JSON inputs cannot pass because one input has a supported encoding. Partial known
sets used during generation only check present inputs; full merge-only completeness checks remain.

M1 retains any correctly Dataset-bound generic VectorAccessor and the existing squared-L2
service check; it does not require `CppMainVectorAccessor`. A structurally valid but unsupported
encoding yields consumer admission failure, retaining VALID evidence without quarantine. Direct
M1 load/service checks remain. No format/tag migration, graph rebuild or parameter change is
required for existing supported artifacts; affected code must be recompiled.
