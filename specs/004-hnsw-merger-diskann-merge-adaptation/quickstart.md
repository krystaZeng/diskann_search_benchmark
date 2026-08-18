# Quickstart: HNSW-Merger Vamana Merge Structural Validation

This guide records the implemented T001-T009 structural-validation surface, including the one
reviewed formal SIFT1M comparison.
The guide stops at structural SIFT1M and never runs Query/Search or performance
evaluation.

## Current observed status (2026-08-17)

- T001-T008 passed in the local Linux builder image
  `diskann-cpp-main-baseline-builder:jammy`
  (`sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`).
- The blocking CTest selection passed 13/13, including real small P0+B0+M1 and exact-same-input
  M0/M1 execution. The final blocking pytest selection passed 28/28.
- Formal preflight over clean project commit
  `1f86330fa96a8b9ab75b8aa351840e1d03ecf03b` passed. Dataset, Partition and all three ordered
  LocalGraph inputs fresh-validated, and the immutable formal request has SHA-256
  `9baed481b1f2441326c378cf6b286a18e57393508440757850c13202856a6349`.
- The reviewed request was launched exactly once. M0 completed, published GlobalGraph aggregate
  checksum `4850d4cec344d0a7e7b98dd3c8a777c77c051ede5c3624d497c3b7d84dc090b5`, and passed the existing
  Validator and native graph-integrity evaluator.
- The execution was then manually and safely stopped while M1 was running because source review
  showed `hnsw_merger_single_layer_search` traversing the complete target graph for every source
  node and applying lambda truncation only after that traversal. This is not the approved bounded
  Vamana forward-search semantics and makes the SIFT1M work unacceptable.
- For Quickstart/final-audit description only, the old attempt is classified
  **INVALID_IMPLEMENTATION / MANUALLY_ABORTED**. These are explanatory labels, not new
  RunnerResult, StageStatus or schema values. No normal M1 Artifact, M1 parent BuildManifest,
  terminal RunnerResult or same-input comparison summary was published or reconstructed. The old
  workspace and all evidence actually emitted before termination remain intact, but the attempt is
  excluded from eligible result aggregation.
- The exposed forward-search defect has since been corrected without rerunning formal SIFT1M:
  `hnsw_merger_single_layer_search` now maintains the bounded Algorithm 1 candidate/top queues,
  applies the `(distance, GlobalNodeId)` ordering, and stops once the closest pending candidate is
  worse than the full top-lambda set's worst member. The adaptive lambda and pruning policies are
  unchanged.
- The immutable bounded-search request at
  `build/hnsw-merger-merge-adaptation/runs/sift1m-structural-bounded-fix/requests/merge-comparison-request.json`
  (SHA-256 `38a81597b912a642b21531e035c60357280e907401e548c18abbbd013eca63e1`)
  was subsequently launched once. M0 completed and published a Validator/evaluator-accepted
  GlobalGraph with aggregate checksum
  `48bb9f19752e65f89696057fb44f097f8dfed8a0cc1ea5931907ed967a131045`.
  M1 was manually stopped after diagnostic observation showed that every distance evaluation still
  reopened/sought/read the Dataset through `CppMainVectorAccessor`, while candidate comparators
  recalculated the same distances. The bounded Algorithm 1 semantics were correct, but the
  implementation did not have acceptable SIFT1M vector-access scalability.
- For Quickstart/final-audit description only, that second attempt is classified
  **IMPLEMENTATION_SCALABILITY_DEFECT / MANUALLY_ABORTED**. It has no terminal RunnerResult, normal
  M1 Artifact, M1 parent/stage-owned BuildManifest or comparison summary. Its immutable request,
  M0 output, manifests, ValidationEvidence and existing host evidence remain retained; the attempt
  is excluded from eligible result aggregation and its old request is ineligible for re-execution.
- The M1-only scalability repair adds one invocation-private contiguous float32 cache sourced from
  the existing VectorAccessor. Allocation is budget/overflow checked, rows populate lazily by
  GlobalNodeId, each row reaches the underlying accessor at most once, cache misses observe the
  existing CancellationToken, and two scratch vectors are reused for distance evaluation.
  Candidate ordering now decorates each candidate once with `(distance, GlobalNodeId)` before
  sorting. Algorithm 1, adaptive lambda, pruning and merge order are unchanged.
- Post-repair non-formal validation passed 13/13 applicable CTests. The real small P0+B0+M1 plus
  same-input M0/M1 CTest completed in 5.38 seconds; 28/28 applicable pytest cases passed after the
  existing Linux fixture binaries were rebuilt.
- The vector-cache repair was admitted from clean project commit
  `fb4c971c9adde172e5df862f5272e292f7e979fe`. Formal preflight fresh-validated the Dataset,
  Partition and all three ordered LocalGraphs, invoked no stage, and emitted the immutable request
  at
  `build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json`
  with SHA-256 `1285e928213955b1d5b69d72f1fd4c185d643d98c8b6437b6f406ff214e31871`.
- After human review, that exact request was launched once. The terminal runner result is
  `SUCCEEDED` with aggregate stage invocations `partition=0`, `local_build=0`, `merge=2`. M0 and M1
  produced GlobalGraph aggregate checksums
  `ae4d266202ee3fe49008b8012db050270ac9ddb1c3e9f7b5fe749748f0dcd4ef` and
  `3c161de688956aaef691dff21a53038b4f17e7c857a9b7e4516cb837e14031a7`, respectively.
- Both GlobalGraphs independently passed the existing Validator and native graph-integrity
  evaluator with 1,000,000 nodes, maximum observed degree 16, zero invalid neighbors, zero self
  edges and complete lineage. The comparison reports `shared_input_identity=PASS` and
  `structural_validation=PASS`; `query_evaluation_performed=false` and
  `formal_graph_parity=false` remain explicit.
- Exactly one M0 parent Merge BuildManifest and one M1 parent Merge BuildManifest were retained.
  M0 preserves the empty `build_manifest_path` legacy behavior. M1's parent references the valid
  stage-owned BuildManifest at
  `manifests/hnsw-merger-sift1m-structural-m1/stage-execution/merge.json` with SHA-256
  `1a37eff69640100b3fae7d4dd47dc16831a9df1445e72c105bc4228fd7881229`.
- Both historical manually aborted workspaces remain intact and excluded from valid comparison
  aggregation. The successful comparison is persisted separately at
  `validation/same-input-structural-comparison.json`; T009 is complete only for structural faithful
  adaptation and makes no Query/Search, Recall, QPS, latency, performance or Graph Parity claim.

## 1. Verify immutable source identities

Verify DiskANN:

```sh
git ls-files --stage upstream/diskann-cpp-main
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1 --untracked-files=all
```

Expected HEAD:

```text
78256bbab4685e1774e78d331e081a153be26823
```

Use an independent read-only audit checkout of the official paper source outside any upstream
source tree:

```sh
HNSW_MERGER_SOURCE=/absolute/path/to/HNSWMerger
git -C "$HNSW_MERGER_SOURCE" remote get-url origin
git -C "$HNSW_MERGER_SOURCE" branch --show-current
git -C "$HNSW_MERGER_SOURCE" rev-parse HEAD
git -C "$HNSW_MERGER_SOURCE" status --porcelain=v1 --untracked-files=all
git -C "$HNSW_MERGER_SOURCE" submodule status
```

Expected:

```text
repository: https://github.com/Kimchuls/HNSWMerger.git
branch: main
commit: 88f6b7179f651cf5f284bd4e52249a4b1323b5af
status: clean
submodules: none
```

The source is an audited algorithm basis. M1 does not build or invoke its HNSW experiment binary.

## 2. Configure and build outside upstream

After implementation creates the planned targets:

```sh
cmake -S . -B build/hnsw-merger-merge-adaptation \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_HNSW_MERGER=ON

cmake --build build/hnsw-merger-merge-adaptation --parallel
```

All generated files must remain under `build/hnsw-merger-merge-adaptation` or the configured
experiment workspace. Both audited source trees must remain clean.

## 3. Focused unit and contract gates

Planned CTest names are registered by the future implementation before use:

```sh
ctest --test-dir build/hnsw-merger-merge-adaptation \
  -R '^(hnsw_merger_m1_semantics|hnsw_merger_m1_native_output|hnsw_merger_merge_only_execution)$' \
  --output-on-failure
```

Expected coverage:

- M1 descriptor and fixed parameters;
- paper forward candidates and lazy backward direct-connect with no reverse search;
- single-layer search entry and stable distance/ID tie-break;
- large-first three-or-more-shard order and adaptive lambda epochs;
- overlap shared-node normalization, candidate dedup and one GlobalNodeId output node;
- self/range/coverage/degree/lineage failures;
- native GlobalGraph self-edge mutation rejected by the existing Validator before evaluation/reuse;
- native GlobalGraph plus medoids and existing Validator acceptance;
- M1 cooperative cancellation at stage start, after semantic preparation, between pairwise rounds
  and before candidate return, always with no normal candidate;
- M1 stage-owned BuildManifest provenance and the parent
  `parameters.stage_execution_manifest {path, sha256}` reference, with M0 legacy behavior unchanged;
- Merge-only exact collection admission, fresh validation, no P0/B0 factory, per-executor 0/0/1
  invocation counts and successful enclosing runner aggregate 0/0/2;
- unchanged legacy executor path.

## 4. Small real gates

```sh
cmake --build build/hnsw-merger-merge-adaptation \
  --target hnsw_merger_experiment_runner hnsw_merger_m1_pipeline \
  --parallel

ctest --test-dir build/hnsw-merger-merge-adaptation \
  -R '^hnsw_merger_m1_pipeline$' \
  --output-on-failure

.venv/bin/python -m pytest \
  tests/python/test_hnsw_merger_merge_adaptation.py -q
```

The real test path must be:

```text
thin Harness
-> real hnsw_merger_experiment_runner
-> existing PipelineExecutor
-> same validated Dataset/Partition/LocalGraphs
   +-> existing M0
   +-> M1
-> two independent valid GlobalGraphArtifacts/EvaluationReports
-> Feature-local same-input structural comparison
```

The test must prove no P0/B0 rebuild in the Merge-only comparison and must separately demonstrate
one small P0+B0+M1 closure. Failure blocks SIFT1M.

## 5. Applicable regressions

After focused tests pass, use the actual existing targets:

```sh
ctest --test-dir build/hnsw-merger-merge-adaptation \
  -R '^(valid_artifacts|artifact_reuse_core|pipeline_orchestration|pipeline_failure_gates|fake_pipeline_e2e|public_api_cxx17|cpp_main_descriptors_preflight|cpp_main_m0|cpp_main_pipeline|hnsw_merger_.*)$' \
  --output-on-failure

.venv/bin/python -m pytest \
  tests/python/test_cpp_main_baseline_config.py \
  tests/python/test_harness_fake_pipeline_e2e.py \
  tests/python/test_hnsw_merger_merge_adaptation.py \
  -q
```

Executed, failed and applicable-unexecuted tests must be inventoried separately.

## 6. Prepare one SIFT1M structural configuration

Only after the small-real gate passes, create and review exactly one configuration at:

```text
experiments/configs/hnsw-merger-diskann-merge-adaptation/sift1m-structural.yaml
```

It binds:

```yaml
study_class: single_paper_reproduction
reproduction_mode: faithful_adaptation
replacement_scope: merge
dataset:
  path: sift1M_dataset/base_vector.bin
  global_node_count: 1000000
  vector_dimension: 128
  byte_length: 512000008
  checksum: 8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e
m1:
  implementation_id: hnsw_merger_vamana_merge_v1
  implementation_version: 1.0.0
  R: 16
  alpha: 1.2
  max_candidate_size: 750
  metric: l2
  use_filters: false
  multi_shard_strategy: large_first_pairwise_v1
  lambda_policy: paper_adaptive_log_v1
execution:
  thread_budget: 1
  parameter_sweep: false
```

The full configuration also contains exact M0/M1 source/build identities, one immutable reusable
Dataset/Partition/ordered Local Artifact set, workspace, retention and identical-config
infrastructure retry policy. It is frozen before preflight/execute; changed-config retry is prohibited.
The M1 parent stage `source` is the actual project adaptation repository identity. Every actual M1
invocation returns one existing stage-owned BuildManifest/v1 through
`StageResult.build_manifest_path`; that document retains `paper_source_identity`,
`diskann_source_identity`, evidence classes, effective policy and `PairwiseMergeDecision[]`. The
parent only references its immutable copy through
`parameters.stage_execution_manifest {path, sha256}`. M0 keeps its empty-path legacy behavior, and
Artifact validity still comes only from the existing Validator. Formal preflight requires one
canonical remote and a clean committed adaptation revision; an absolute local path is not accepted.

## 7. SIFT1M preflight

The formal preflight command actually executed was:

```sh
.venv/bin/python -m diskann_harness.hnsw_merger_merge_adaptation preflight \
  --config experiments/configs/hnsw-merger-diskann-merge-adaptation/sift1m-structural.yaml \
  --workspace build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix \
  --validation-runner build/hnsw-merger-merge-adaptation/src/cli/diskann_pipeline_runner \
  --container-image diskann-cpp-main-baseline-builder:jammy
```

Preflight must:

- admit exact paper/DiskANN/project source and build identity;
- validate Dataset checksum/header;
- fresh-validate Partition and every LocalGraph in order with complete lineage;
- prove M0/M1 requests contain the exact same ArtifactRefs;
- validate Registry compatibility and M1 parameters;
- write an immutable normalized request;
- create no P0, B0, M0 or M1 factory and publish no Artifact.

Admission and normalized-request tests use deterministic or small-real fixture requests. They must
not launch the formal request at
`build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json`.
Preflight writes that request for review and performs no Merge.

The observed preflight passed with stage invocations `partition=0`, `local_build=0`, `merge=0` and
produced the immutable request with SHA-256
`1285e928213955b1d5b69d72f1fd4c185d643d98c8b6437b6f406ff214e31871`.

The request is the Feature-private wire contract in `contracts/merge-only-execution.md`. Python
preflight is its only normalization owner. It contains one shared DatasetArtifactRef, one shared
PartitionArtifactRef and one ordered LocalGraphArtifactRef array, plus the fixed M0/M1 identities,
parameters and existing execution/source/build configuration. The shared tuple is not duplicated.
Public RunRequest and runner-result schemas remain unchanged.

Candidate reusable inputs under `build/cpp-main-baseline/runs/sift1m-unified/` are not trusted until
this gate passes. A failure stops the run; preflight does not rebuild P0/B0.

## 8. One bounded SIFT1M execution

After the small-real gate, admission tests and preflight passed and the exact immutable request was
reviewed, the formal main path was launched once with:

```sh
.venv/bin/python -m diskann_harness.hnsw_merger_merge_adaptation execute \
  --request build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json \
  --runner build/hnsw-merger-merge-adaptation/src/papers/hnsw_merger/vamana/hnsw_merger_experiment_runner \
  --container-image diskann-cpp-main-baseline-builder:jammy
```

That Python command starts one Feature runner process with the existing argv contract:

```sh
build/hnsw-merger-merge-adaptation/src/papers/hnsw_merger/vamana/hnsw_merger_experiment_runner \
  execute \
  --request build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/requests/merge-comparison-request.json \
  --result build/hnsw-merger-merge-adaptation/runs/sift1m-structural-vector-cache-fix/reports/runner/result.json
```

The runner validates the normalized request without establishing another profile-normalization
policy, parses the shared tuple once, and invokes the existing PipelineExecutor once for M0 and once
for M1. It does not run P0/B0, duplicate the tuple or pass M0 output to M1.

Observed successful evidence:

- each individual M0/M1 executor result has invocation counts 0/0/1;
- the unchanged enclosing runner-result has aggregate invocation counts 0/0/2;
- the Feature-local summary has one M0 and one M1 run record with respective BuildManifest
  references; together with the aggregate runner count this proves exactly one execution per arm
  without adding summary fields or changing public schemas;
- their BuildManifests contain identical ordered Dataset/Partition/Local input references;
- both GlobalGraphArtifacts independently pass the existing Validator and native evaluator;
- M1 has one logical node per Dataset GlobalNodeId, legal non-self neighbors and degree <=16;
- separate M0/M1 Artifacts, manifests, evidence and EvaluationReports remain intact;
- `validation/same-input-structural-comparison.json` validates and reports input identity PASS;
- both upstream trees remain unchanged and clean.

No test-first step launches the formal request. The predeclared identical-configuration
infrastructure retry is not automatic: a failed attempt is retained, and any permitted retry is a
separately reviewed attempt over the identical immutable request, never a changed-configuration or
best-result selection path.

No result from this guide is a Recall/QPS/latency, Graph Parity, query-performance or complete
HNSW-Merger reproduction claim.
