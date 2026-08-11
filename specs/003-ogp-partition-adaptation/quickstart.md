# Quickstart: OGP → cpp_main B0 → cpp_main M0 Validation

This guide records the implemented structural-adaptation workflow and the completed SIFT1M
structural run. No command runs query evaluation, a parameter sweep, or a Dataset larger than
SIFT1M, and the recorded success is not a query-performance or full GP-ANN reproduction claim.

## 1. Admit the fixed sources

Use an independent clean recursive GP-ANN checkout:

```text
repository: https://github.com/larsgottesbueren/gp-ann.git
branch: main
commit: df5efb4a6b303011c795666ed267f29d994b558c
```

From the repository root:

```sh
GPANN_SOURCE_ROOT=/absolute/path/to/clean/gp-ann
test -d "$GPANN_SOURCE_ROOT/.git"
git -C "$GPANN_SOURCE_ROOT" remote get-url origin
git -C "$GPANN_SOURCE_ROOT" branch --show-current
git -C "$GPANN_SOURCE_ROOT" rev-parse HEAD
git -C "$GPANN_SOURCE_ROOT" status --porcelain=v1 --untracked-files=all
git -C "$GPANN_SOURCE_ROOT" submodule status --recursive
```

Expected status is empty and all recursive commits match `source-mapping.md`. Do not use the
unauthenticated research snapshot.

Verify the fixed DiskANN Baseline separately:

```sh
git ls-files --stage upstream/diskann-cpp-main
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
```

Expected DiskANN HEAD is `78256bbab4685e1774e78d331e081a153be26823` and status is empty.

## 2. Configure and build out of tree

```sh
cmake -S . -B build/ogp-partition-adaptation \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_GP_ANN_OGP=ON \
  -DGPANN_SOURCE_DIR="$GPANN_SOURCE_ROOT"
cmake --build build/ogp-partition-adaptation --parallel
```

Expected:

- the official `Partition` target is built under `build/ogp-partition-adaptation`, never in the
  source checkout;
- the actual toolchain, effective options, recursive identities and executable checksum are
  retained;
- the admitted `Partition` path and SHA-256 are the single executable identity used by source
  admission, small real tests, the Feature runner and SIFT1M; no alternate PATH binary is used;
- Feature-owned/public code remains C++17 while official GP-ANN remains private C++20;
- neither source tree becomes dirty.

When a Feature task adds a new source, test or runner target, rerun the same canonical parent
configure before its first build. Initial configure must not depend on target source files that a
later task has not created yet.

`source_config` keeps the existing shared run/config identity and contains only:

```json
{
  "yaml_sha256": "<sha256>",
  "normalized_json_sha256": "<sha256>"
}
```

It may appear unchanged in the Partition, B0 and M0 parent BuildManifests. It is not GP-ANN build
or subprocess provenance.

For every official OGP process actually started, the stage writes one existing subordinate
BuildManifest/v1 and returns it through the existing `StageResult.build_manifest_path`. The
executor validates and immutably persists it, then adds only to the parent Partition Manifest:

```json
{
  "stage_execution_manifest": {
    "path": "<workspace-relative path>",
    "sha256": "<sha256>"
  }
}
```

The subordinate Manifest selects admitted GP-ANN as source and records the actual external C++20
toolchain/build, executable SHA-256, argv, configured `PARLAY_NUM_THREADS`, staged input,
stdout/stderr, exit status and output evidence. B0/M0 select DiskANN and do not receive this
reference. Existing stages returning an empty `build_manifest_path` keep legacy behavior.

If the clean target requires an upstream edit, stop and record the build blocker. Do not patch the
pinned recursive checkout or substitute an unrelated executable.

## 3. Run focused contract and parser tests

```sh
ctest --test-dir build/ogp-partition-adaptation \
  -R '^(stage_source_provenance|gp_ann_ogp_partition)$' \
  --output-on-failure
```

These tests prove:

- request-level source fallback and the GP-ANN/DiskANN/DiskANN source split;
- the shared `source_config` remains the existing two-digest object in every parent Manifest;
- empty `build_manifest_path` preserves legacy parameters, while a valid non-empty path adds one
  confined, checksum-bound reference only to its corresponding parent Stage;
- malformed or path-escaping subordinate Manifests fail closed;
- exact clean source/build/binary admission before OGP factory creation;
- only cluster count, overlap and mode are user parameters;
- epsilon is absent and the effective source-fixed value is recorded as `0.05`;
- strict parsing, duplicate-before-sort, empty/range/coverage rejection and valid overlap;
- Partition payload checksum corruption is rejected by the existing Validator with no normal
  publication and zero B0/M0 invocations;
- checksum-identical `.fbin` staging;
- one small real official CLI run publishes an existing Validator-accepted
  `PartitionArtifact/v1`.

Subprocess evidence has three distinct terminal paths:

1. Exit zero: subordinate BuildManifest is `SUCCEEDED`; normal parse/publication determines the
   parent Partition result.
2. Nonzero exit: subordinate BuildManifest is `FAILED`, records the actual `failure.exit_code`, and
   retains stdout/stderr; the parent Partition stage fails.
3. Exit zero followed by parse, OGP-invariant or Artifact-validation failure: subordinate remains
   `SUCCEEDED`; the parent Partition Manifest records `FAILED` or `INVALID_OUTPUT`.

If admission fails before launch, no subprocess BuildManifest is fabricated.

## 4. Run the small composed closure

```sh
cmake --build build/ogp-partition-adaptation \
  --target ogp_experiment_runner gp_ann_ogp_pipeline fake_pipeline_e2e \
  --parallel

ctest --test-dir build/ogp-partition-adaptation \
  -R '^(gp_ann_ogp_pipeline|fake_pipeline_e2e)$' \
  --output-on-failure
```

Expected:

1. Official OGP runs once.
2. Valid maps retain cross-shard overlap and validate before consumption.
3. Existing B0 runs once per explicit effective PartitionId.
4. Existing M0 runs once only after all LocalGraph Artifacts validate.
5. Invalid OGP output leaves B0/M0 invocation counts zero.
6. The fixed cpp_main Baseline runner continues to reject OGP.
7. Final GlobalGraph Artifact and existing graph-integrity EvaluationReport remain separate from
   Artifacts, BuildManifests and ValidationEvidence.
8. The Partition parent Manifest has a valid path/checksum reference to its GP-ANN subordinate
   BuildManifest; B0/M0 have no such reference and retain DiskANN provenance.

Run the Feature Harness tests:

```sh
.venv/bin/python -m pytest tests/python/test_ogp_partition_adaptation.py -q
```

The Python test must launch the built real `ogp_experiment_runner` on the approved small Dataset
and observe the path `Harness -> runner -> PipelineExecutor -> official OGP -> B0 x effective
PartitionId -> M0`. An in-process test chain or Python-side stage loop is not a substitute. Failure
to build, launch or complete this path blocks SIFT1M.

## 5. Run applicable regressions

The optional request field and existing B0/M0 composition require the affected public and Baseline
regressions:

```sh
ctest --test-dir build/ogp-partition-adaptation \
  -R '^(fake_pipeline_e2e|public_api_cxx17|document_codecs|registry_selection|compatibility_preflight|valid_artifacts|pipeline_orchestration|pipeline_failure_gates|production_surface|cpp_main_descriptors_preflight|cpp_main_b0|cpp_main_m0|cpp_main_pipeline)$' \
  --output-on-failure
```

```sh
.venv/bin/python -m pytest \
  tests/python/test_contract_schemas.py \
  tests/python/test_cpp_main_baseline_config.py \
  tests/python/test_harness_fake_pipeline_e2e.py \
  tests/python/test_production_assembly.py \
  -q
```

At final audit, run the complete configured CTest and Python suites once:

```sh
ctest --test-dir build/ogp-partition-adaptation --output-on-failure
.venv/bin/python -m pytest tests/python -q
```

## 6. Freeze one SIFT1M configuration

After all prior gates passed, the one frozen configuration was created at:

```text
experiments/configs/ogp-partition-adaptation/sift1m-structural.yaml
```

The immutable experimental fields include:

```yaml
study_class: single_paper_reproduction
reproduction_mode: faithful_adaptation
replacement_scope: partition
status: frozen
implementations:
  partition: gp_ann_ogp_partition_v1
  local_build: cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1
  merge: cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1
stage_parameters:
  partition:
    requested_num_clusters: 40
    requested_overlap: 0.2
    partitioning_mode: default
effective_constants:
  epsilon: 0.05
  epsilon_source: upstream_cli_constant
execution:
  parameter_sweep: false
```

The complete document also binds the exact SIFT1M checksum, GP-ANN/DiskANN source/build identities,
recursive dependencies, unchanged B0/M0 parameters, resource budget, configured
`PARLAY_NUM_THREADS`, retention rule and workspace locations.

## 7. Preflight and execute SIFT1M

Preflight validates the frozen configuration and writes an immutable request without invoking a
stage:

```sh
.venv/bin/python -m diskann_harness.ogp_partition_adaptation preflight \
  --config experiments/configs/ogp-partition-adaptation/sift1m-structural.yaml \
  --workspace build/ogp-partition-adaptation/runs/sift1m-structural
```

Expected Dataset identity:

```text
path: sift1M_dataset/base_vector.bin
npts: 1000000
dim: 128
scalar type: little-endian float32
metric: L2
byte length: 512000008
SHA-256: 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e
```

Execute an attempt with an explicit unique run ID:

```sh
.venv/bin/python -m diskann_harness.ogp_partition_adaptation execute \
  --request build/ogp-partition-adaptation/runs/sift1m-structural/requests/ogp-request.json \
  --workspace build/ogp-partition-adaptation/runs/sift1m-structural/attempt-004 \
  --run-id sift1m-structural-attempt-004 \
  --runner build/ogp-partition-adaptation/linux/src/papers/gp_ann/ogp/ogp_experiment_runner \
  --retry-of sift1m-structural-attempt-003 \
  --retry-reason external_execution_failure
```

Retain success, failure or invalidity through existing Manifests, ValidationEvidence, runner result
and quarantine records. A retry is allowed only for a documented infrastructure/external failure,
must use the identical frozen configuration and a new run ID/workspace, and must retain the prior
attempt. Do not change parameters, run a grid or select only a preferred outcome.

A successful attempt produces the validated existing Artifact chain and existing final GlobalGraph
EvaluationReport. It does not produce an OGP-specific report and does not establish Recall, QPS,
latency, Graph Parity, full GP-ANN reproduction or query-performance improvement.

### Observed SIFT1M record (2026-08-10)

- Preflight succeeded with configuration SHA-256
  `ee51d0c42b252fbc627076dddc397300f7ad2da79b1af4ca3989bf3b40ce0cf6`, Dataset
  SHA-256 `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`, and
  zero P/B/M invocations.
- Attempts 001–003 remain retained. They failed before a valid Partition publication because of,
  respectively, a Docker nested-mount infrastructure error, missing runtime `git` for source
  admission, and missing `libtbb.so.12` for the admitted official executable. No failed attempt was
  selected as successful evidence and none changed the frozen experimental fields.
- The immutable runtime image remained
  `sha256:7b2782df15ca76874f902fd6336c66cab7b077f8eeb788adf148165c65f9dfcb`.
  The out-of-tree Linux runtime used a read-only Git 2.34.1 tool (SHA-256
  `587ef21868c948b883993e23209b86a72a6ddc06aab1545c697ffc31075acd4a`) for the mandatory
  in-run source admission and the admitted `libtbb.so.12.5` (SHA-256
  `662cde99c415f7ac2bcedca97f884345f9c158d3b446678d59a8a98603e7938e`) required by the
  already-pinned Partition executable. Both were mounted read-only; neither source tree or
  executable was modified.
- Attempt 004 succeeded with invocation counts `Partition=1`, `Local Build=46`, `Merge=1`.
  Requested clusters were 40; the official output produced 46 effective Partitions and 1,200,000
  assignments (cross-shard overlap retained). Partition sizes are recorded in the published
  `PartitionArtifact/v1`.
- Partition aggregate checksum:
  `32f7d4f0f191174dc7239e8bf8c5655b4c48708fb203c91f200de2e641175b1d`.
  GlobalGraph aggregate checksum:
  `6f4837da5b837b379515ff939be9cbaebfb3a3a98ac5a100db377ea13a8d0662`.
- The final graph contains 1,000,000 GlobalNodeIds, has declared/observed maximum degree 16 and
  zero invalid neighbors. All 48 Stage BuildManifests, 48 stage ValidationEvidence documents plus
  the Dataset admission evidence, all published Artifacts, and the independent native
  GlobalGraph EvaluationReport validated successfully.

### T094 lineage revalidation (2026-08-11)

After completing the existing GlobalGraph lineage validator's type/version/checksum comparison,
attempt 004 was revalidated without executing OGP, B0 or M0 and without changing any existing
Artifact, Manifest, evidence, result, report, request or log. The existing validate operation was
used in dependency order for the DatasetArtifact, PartitionArtifact and all 46
LocalGraphArtifacts, followed by the GlobalGraphArtifact. Every revalidation runner result records
zero Partition, Local Build and Merge invocations.

The new, independently named evidence is retained under the existing lifecycle roots:

- source-chain evidence: `manifests/evidence/t094-chain-*.json` (48 VALID documents);
- GlobalGraph evidence: `manifests/evidence/t094-lineage-revalidation-validate.json` (VALID);
- evaluation-time fresh evidence:
  `manifests/evidence/t094-lineage-reevaluation-evaluate.json` (VALID);
- new native graph report: `reports/t094-lineage-reevaluation-evaluation.json`.

The new report has report ID `t094-lineage-reevaluation-evaluation`, a new `generated_at`, refers
to the original immutable GlobalGraph aggregate checksum
`6f4837da5b837b379515ff939be9cbaebfb3a3a98ac5a100db377ea13a8d0662`, and refers to the new
evaluation-time ValidationEvidence. Its native integrity findings remain: 1,000,000 nodes,
declared/observed maximum degree 16, zero invalid neighbors, and `passed: true`. The original
`reports/sift1m-structural-attempt-004-evaluation.json` and original merge evidence remain present
and were not overwritten.

## 8. Final integrity checks

```sh
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
git -C "$GPANN_SOURCE_ROOT" rev-parse HEAD
git -C "$GPANN_SOURCE_ROOT" status --porcelain=v1 --untracked-files=all
git diff --check
```

Report executed, failed and applicable-unexecuted checks separately. SIFT100M or larger execution
requires a new scope, completed SIFT1M evidence, human diff review and explicit approval.

Observed final audit:

- full Linux build: PASS;
- full configured CTest: 35 passed, 0 failed;
- full `tests/python` pytest: 72 passed, 6 failed;
- `git diff --check`: PASS;
- DiskANN HEAD remained `78256bbab4685e1774e78d331e081a153be26823` and clean;
- formal GP-ANN HEAD remained `df5efb4a6b303011c795666ed267f29d994b558c`, branch `main`, with its
  admitted recursive dependency identities and clean status;
- admitted Partition SHA-256 remained
  `f77f19242ae170332f829053c528b56fb5c10df05ff2bb47822c90a146c70bcb`.

The six Python failures are retained as applicable-unresolved cross-Feature checks: one Feature 001
governance assertion requires branch `master` although this Feature's approved current branch is
the documented historical spelling `feature/opg-partition-adaptation`; five Feature 001
determinism tests require the deferred terminal-BuildManifest hardening that this Feature explicitly
does not implement. They do not invalidate the successful OGP SIFT1M evidence, but they keep T009
open and prohibit claiming an all-repository Python validation pass.
