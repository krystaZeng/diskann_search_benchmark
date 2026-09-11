# Quickstart: XN-Descent Validation

This file records the implemented validation entries, the commands already exercised through
T034, and the still-pending external Linux and SIFT1M gates. A command labeled `[VERIFIED]` has
corresponding evidence under `validation/`; `[PENDING]` means it is an executable handoff and not a
claim that it has run.

The sequence is intentionally gated. A later gate cannot convert a failed or unrun earlier gate
into a pass.

## 1. Build entries

`[VERIFIED: T034 macOS]` Use the core-only path for macOS `semantic-small` validation:

```bash
cmake -S . -B build/xn-descent-core-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=OFF \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF
cmake --build build/xn-descent-core-debug --parallel
```

`[VERIFIED: T034 Docker Linux/amd64; PENDING: T037 external Linux]` Use the Baseline-enabled path
for complete `pipeline-compatible` and `paper-style` validation:

```bash
cmake -S . -B build/xn-descent-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF
cmake --build build/xn-descent-linux --parallel
```

The target names below are registered by the current CMake files. Both builds remain out-of-tree
and must not write into `upstream/diskann-cpp-main/`. Pipeline preflight, P0 → B1×Partition →
M0 closure, the Feature runner, and Baseline tests use only a Baseline-enabled Linux directory.

## 2. Parameter and Equation (2) unit gate

`[VERIFIED: T034]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_(parameters|equation)_test$'
```

Required cases:

- reject non-finite or `<1` `alpha`, zero `omega`, zero `lambda`, and zero cap before construction;
- accept all three declared profiles while enforcing the same core positive-`lambda` validation;
  `pipeline-compatible` has no maximum 10 and `paper-style` does not force 64;
- verify parameter-contract revision `1.1.0` through the exact B1 descriptor schema path, distinct
  versioned `$id`, byte-identical Feature/runtime copies, and matching `SOURCES.json` SHA-256;
- squared L2 uses `alpha^2`, never `alpha`;
- both comparisons are strict and equality in either comparison is non-occluding;
- zero, ordinary, very large exponent, invalid/non-finite, and direct-overflow cases have explicit
  expected outcomes.

## 3. TryInsert unit gate

`[VERIFIED: T034]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_try_insert_test$'
```

Required cases:

- duplicate insertion is idempotent;
- top `L` uses distance to the actual target center;
- equal distance retains lower LocalNodeId;
- `L` capacity is never exceeded;
- the two cross calls use centers `v` and `u`, respectively;
- a self or out-of-range candidate cannot enter logical state.

## 4. Small-graph round oracle gate

`[VERIFIED: T034]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_round_oracle_test$'
```

The fixture records every normalized boundary `C/R/G` state. It must prove:

- initial `G/R` empty and seeded `C` unique/non-self;
- `n-1 < omega` uses every available non-self node without padding;
- candidates derive only from boundary `C/R`;
- every candidate emits a reverse relation before acceptance;
- all cross-`TryInsert` effects occur even after its flag becomes false;
- earlier working-`G[i]` mutations affect later candidates for `i`;
- candidate-caused removals are not rolled back if capacity later evicts the candidate;
- `C<=omega`, `G<=lambda`, and `R` is not silently capped;
- complete normalized `C/R/G` equality converges immediately, while the specified `R` two-cycle
  fixture converges only after three consecutive complete rounds with both `C` and `G` unchanged;
  stable `G` alone remains insufficient.

## 5. Thread-count and scheduling determinism gate

`[VERIFIED: T034]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_determinism_test$'
```

For the same validated input, parameters, seed, build, and numerical environment, repeat with
thread budgets 1, 2, and the fixture's admitted maximum plus an injected alternative work schedule.
Require identical:

- native graph payload SHA-256;
- normalized final-state digest;
- completed rounds and fixed-point evidence;
- selected local entry point.

The test also verifies each node's random stream is independent of worker assignment.

## 6. Failure and resource gate

`[VERIFIED: T034]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_(termination|resource)_test$'
```

Required cases:

- fixed point on the last permitted round succeeds;
- changed `C` with stable `G` does not converge; changed `R` prevents a full-state fixed point but
  may converge only after the exact three-round stable-`C/G` two-phase closure;
- cap hit with any changed state returns `FAILED`, reason
  `iteration_cap_nonconverged`, evidence, and no candidate Artifact;
- checked-size overflow and known budget excess fail before major graph allocation/first iteration;
- round-time reverse-state exhaustion fails without truncating `R`;
- allocation failure and cancellation are controlled and publish no valid or reusable Artifact;
- effective worker count never exceeds the positive thread budget.

## 7. Artifact, Manifest, and validator contract gate

`[VERIFIED: T034 macOS]`

```bash
ctest --test-dir build/xn-descent-core-debug --output-on-failure \
  -R '^xn_descent_local_artifact_test$'
```

The accepted candidate must prove:

- LocalGraphArtifact/v1 and LocalNodeId;
- exact selected Partition ownership and complete copied LocalToGlobal map/checksum;
- no out-of-range, duplicate, or self neighbors;
- `declared_max_degree=lambda`;
- observed metadata = native header = actual payload maximum `<=lambda`;
- legal deterministic local-medoid entry point;
- existing Validator passes before publication;
- BuildManifest separately records implementation/source/toolchain/input identities, effective
  policies/seed, budgets, convergence and termination;
- no Recall, QPS, latency, or other EvaluationReport fields are placed in Artifact/Manifest.

Negative candidates with corrupted bytes, checksum, map, lineage, degree, entry point, self edge,
or duplicate edge must never enter normal publication.

### 7.1 Runner request provenance

`[VERIFIED: JSON request-provenance regression]` The Baseline-enabled
`xn_descent_experiment_runner` accepts two Feature-local wire versions:

- `1.0.0` keeps the historical caller-supplied `source_config` object with
  `yaml_sha256` and `normalized_json_sha256`;
- `1.1.0` is the JSON-only form used by new direct request files. It must omit
  `source_config`; the runner records `source_format=json`, the SHA-256 of the exact input bytes,
  the SHA-256 of the strictly parsed ordered-key compact `dump()` bytes, and
  `normalization_policy=xn-json-compact-v1` in every parent stage BuildManifest.

The normalization rule does not add defaults or a newline and does not reorder arrays. It rejects
duplicate keys and invalid JSON and is not an RFC 8785 claim. Whitespace or object-key order can
therefore change `raw_request_sha256` without changing `normalized_json_sha256`; changing an
effective parameter changes the normalized hash. The implementation intentionally does not use
the restricted canonicalizer because XN requests contain floating-point values.

The two existing pipeline CTests cover old-version preservation, new-version real small-graph
P0/B1/M0 execution, source propagation into all parent BuildManifests, hash sensitivity, and
pre-stage protocol rejection:

```bash
ctest --test-dir build/xn-descent-linux --output-on-failure --no-tests=error \
  -R '^xn_descent_(pipeline_preflight|small_pipeline)_test$'
```

Detailed Red/Green evidence is in
`specs/007-xn-descent-local-build/validation/json-request-provenance.md`.

## 8. Reusable M0 parameter gate

`[VERIFIED: T034 Docker Linux/amd64]`

```bash
ctest --test-dir build/xn-descent-linux --output-on-failure \
  -R '^xn_descent_pipeline_preflight_test$'
```

Submit requests with independent positive `lambda` and `merge.R`, including `lambda>10`. Legal
requests must not be rejected by the frozen B0 local degree. The request fixture at
`tests/fixtures/xn_descent/pipeline-compatible-request.json` must be read by the test rather than
merely existing on disk. Missing, boolean, string, fractional, negative, zero, and greater-than-
`UINT32_MAX` `merge.R` values must fail for an explicitly asserted merge-parameter reason before
Registry/factory creation or any P0/B1/M0 invocation; both factory and stage counters remain zero.
The public `ParameterValidator` is not credited with numeric range enforcement, and M0 retains its
runtime defense-in-depth check.

The revised descriptors must point exactly to
`contracts/baseline-v1/schemas/cpp-main-m0-parameters-v1.1.0.schema.json` and
`contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters-v1.1.0.schema.json`.
The old schema paths and historical evidence remain preserved.

The fixed Baseline request remains `R=16` with B0 local degree 10. In addition to the existing
configuration test, `tests/cpp/baseline/test_cpp_main_pipeline.cpp` must invoke the real
`cpp_main_baseline_runner` path supplied by `tests/cpp/baseline/CMakeLists.txt` and prove that an
otherwise valid `baseline_validation` request with merge `R=7` is rejected before workspace or
stage creation. A generic M0 success with `R=16` is not a substitute for this runner-entry test.

## 9. Small P0 → B1 → M0 closure

`[VERIFIED: T034 Docker Linux/amd64]`

```bash
ctest --test-dir build/xn-descent-linux --output-on-failure \
  -R '^xn_descent_small_pipeline_test$'
```

The closure uses one immutable request, the exact current P0 and M0 identities, B1 identity, and a
small native Dataset. It must show ordered validated P0 → exactly one B1 for each explicit
Partition → M0, complete local set, M0-admitted native media/map, valid final GlobalNodeIds, and
separate stage manifests and evidence. Use different legal values for `lambda` and `merge.R` and
verify LocalGraph declared degrees reflect `lambda` while the GlobalGraph declared degree reflects
`merge.R`. If an earlier B1 succeeds and a later B1 fails or omits output, the partial collection
must not enter M0; corrupted LocalGraph output has the same downstream result. The report labels
the run a component-level faithful adaptation, not full XN-Graph.

## 10. Partitioned `paper-style` validation

`[VERIFIED: T034 small pipeline]` The current test runs `paper-style` through the same complete
partitioned lifecycle as `pipeline-compatible`:

```bash
ctest --test-dir build/xn-descent-linux --output-on-failure \
  -R '^xn_descent_small_pipeline_test$'
```

The request selects `paper-style`, an explicit legal `lambda` (with 64 as one paper-derived
example, not a required value), explicit positive `merge.R`, P0, B1, and the existing M0. Success
requires one validated LocalGraphArtifact for every Partition before M0 and publishes a validated
GlobalGraph plus separate Manifest/evidence. It uses the same lifecycle as
`pipeline-compatible`, remains a component-level faithful adaptation, and does not claim BBP or
the complete XN-Graph system. The executable target
`xn_descent_experiment_runner` is available in a Baseline-ON build, but no paper-scale request has
been executed.

## 11. SIFT1M freeze and execution gate

`[PENDING: T038]` The frozen material has two parts:

1. `experiments/configs/xn-descent-local-build/sift1m-validation.json` contains exactly the fields
   accepted by `xn_descent_experiment_runner --request` using `request_version: 1.1.0`: request
   identity/classification, absolute workspace and budgets, Dataset Artifact reference, fixed
   P0/B1/M0 identities, profile, and three stage-parameter objects. It omits `source_config`, which
   the runner derives from the actual JSON bytes. It contains no environment, repetition,
   aggregation, or review-only fields that the strict runner would reject.
2. This section records the target environment, exact build command, one-attempt policy, retention
   and aggregation rules, and binds them to the final request path and SHA-256.

Read-only inspection found a usable *candidate*, not a frozen target-environment input:

- metadata:
  `build/ogp-partition-adaptation/runs/sift1m-structural/attempt-004/normal/datasets/sift1m/artifact.json`;
- Dataset Artifact aggregate checksum:
  `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`;
- raw `vectors.bin` payload checksum (independently rechecked):
  `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`;
- shape/type: 1,000,000 rows, 128 float32 dimensions, L2; payload length 512,000,008 bytes.

The raw payload checksum is not the Dataset Artifact aggregate checksum. This ignored build path is
not portable to the target Linux host and therefore cannot be copied into the formal request
without target-side admission and path verification.

The current repository gives evidence-backed parameter candidates only: fixed P0 uses `R=16`,
`build_DRAM_budget_gib=0.6`, and `replication_factor=2`; its `sample_probability` remains an
explicit choice. The paper reports typical `omega=32–40` and an all-memory comparison example
`lambda=64`; neither selects an XN SIFT1M profile, `merge.R`, seed, cap, thread count, or memory
budget on the user's behalf. Those values must be frozen explicitly and `lambda` must remain
independent of `merge.R`.

To avoid designing the request from scratch, the evidence-backed single-run recommendation for
one consolidated user decision is shown below. It is a proposal, not a frozen request or execution
approval:

```text
request_version: 1.1.0
request_id: xn-sift1m-paper-v1-seed42
profile: paper-style
P0: R=16, build_DRAM_budget_gib=0.6, replication_factor=2,
    sample_probability=0.256
B1: omega=40, lambda=64, alpha=1.2, seed=42, iteration_cap=128
M0: merge.R=64, use_filters=false
workspace suggestion: /data/xn-descent/workspaces/xn-sift1m-paper-v1-seed42
result suggestion: experiments/results/xn-descent-local-build/sift1m-validation.json
attempts: exactly one; retain success or failure; no parameter-changing retry or best-run selection
```

The P0 values and `alpha=1.2` come from the existing SIFT1M Baseline profile; `omega=40` is the
upper end of the paper's stated typical range; `lambda=64` is the paper's all-memory comparison
degree; `merge.R=64` is explicitly chosen independently so M0 does not lower that proposed output
cap. Seed 42 is the implemented default and cap 128 is the already validated safety-cap candidate,
not a paper-stated convergence value. The target host must still confirm the Dataset paths, a
16-GiB memory-budget proposal, a 16-thread-budget proposal (both reduced if the real host cannot
provide them), output capacity, and the complete environment record before these values can be
written into the formal request.

The standalone JSON provenance rule is now resolved by request version `1.1.0`: the request omits
`source_config`, and the runner derives the exact raw-byte digest and
`xn-json-compact-v1` normalized digest without a self-reference. The final file's separate SHA-256
must still be recorded below so T039 can verify that the reviewed file is the one submitted.
This protocol completion does not freeze target paths, resources, or experiment parameters, so
T038 remains incomplete until those values are confirmed together.

Before T038 can be checked, freeze all of the following together:

- the target Linux Dataset metadata/payload paths and successful Artifact validation, with both
  checksums above independently verified in that environment;
- `sample_probability`; profile; `omega`, `lambda`, `alpha`, seed and iteration cap; explicit
  positive `merge.R`; memory and thread budgets; absolute workspace and request ID;
- Linux compiler/version, complete CMake flags, CPU/RAM/OS/numerical environment, and result/log
  locations;
- one attempt only, preservation of both success and failure output, no parameter-changing retry,
  no best-run selection, and aggregation by retaining the sole attempt as-is;
- `request_version: 1.1.0`, omission of caller `source_config`, and the final request-file SHA-256
  bound to this policy.

After these are resolved, parse the JSON, validate its Dataset and parameter references without
executing stages, compute its SHA-256, and record here:

```text
request: experiments/configs/xn-descent-local-build/sift1m-validation.json
request_sha256: <record only after the final file exists>
status: frozen, awaiting T039 human approval; not authorized to run
```

Only after T039 records explicit approval may T040 invoke:

```bash
build/xn-descent-linux/src/papers/xn_graph/xn_descent/xn_descent_experiment_runner \
  --request experiments/configs/xn-descent-local-build/sift1m-validation.json
```

Missing or failed SIFT1M validation leaves the Feature incomplete. No SIFT100M or larger run is
authorized.

## 12. External Linux rebuild and replay handoff

`[PENDING USER EXECUTION: T037]` Docker evidence from T034 is useful regression evidence but is not
the user-executed external Linux record required here. The user must perform the following on the
target Linux host; the macOS Codex must not mark this task complete on their behalf.

### 12.1 Transfer the exact reviewed source

Record `git rev-parse HEAD`. For this handoff, use the final provenance-fix commit reported with the
completed local work, check out that exact commit with submodules, and verify the pinned gitlink.
Do not copy a branch tip that has advanced. If any later reviewed changes are intentionally kept
uncommitted, migrate both categories below and bind them to the recorded commit:

```bash
# On the reviewed source host; paths are examples outside the repository.
git diff HEAD --binary --full-index > /tmp/xn-reviewed-tracked.patch
git ls-files --others --exclude-standard -z \
  | tar --null -T - -czf /tmp/xn-reviewed-untracked.tar.gz
shasum -a 256 /tmp/xn-reviewed-tracked.patch \
  /tmp/xn-reviewed-untracked.tar.gz
```

An empty tracked patch or untracked list must be recorded as empty instead of producing a
misleading transfer artifact. On Linux, apply/extract only after checking every transfer hash,
then compare a complete sorted file hash inventory with the reviewed source. Never transfer only a
tracked patch while omitting new files. If the final handoff commit is clean, no patch/archive is
needed and `git status --short` must remain empty.

Verify before building:

```bash
git rev-parse HEAD
git status --short
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --short
git diff --submodule=short -- upstream/diskann-cpp-main
```

The upstream result must be
`78256bbab4685e1774e78d331e081a153be26823`, clean, with no gitlink difference.

### 12.2 Rebuild on Linux

Do not copy a macOS binary. In a new out-of-tree Linux directory, use the actual registered
targets:

```bash
cmake -S . -B build/xn-descent-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF

cmake --build build/xn-descent-linux --parallel --target \
  xn_descent_experiment_runner \
  xn_descent_parameters_test \
  xn_descent_equation_try_insert_test \
  xn_descent_round_oracle_test \
  xn_descent_local_artifact_test \
  xn_descent_determinism_test \
  xn_descent_pipeline_test \
  cpp_main_native_formats \
  cpp_main_descriptors_preflight \
  cpp_main_pipeline \
  production_surface
```

List and run all XN tests plus the required Baseline/surface regressions:

```bash
ctest --test-dir build/xn-descent-linux -N \
  -R '^(xn_descent_.*|cpp_main_(native_formats|descriptors_preflight|pipeline)|production_surface)$'

ctest --test-dir build/xn-descent-linux --output-on-failure --no-tests=error \
  -R '^(xn_descent_.*|cpp_main_(native_formats|descriptors_preflight|pipeline)|production_surface)$'
```

### 12.3 Linux-internal replay and returned evidence

Run `xn_descent_determinism_test` twice. Its production-path fixture already compares
thread budgets 1, 2, and at least the node count plus multiple owner schedules, so no public
scheduler or comparison tool is needed:

```bash
ctest --test-dir build/xn-descent-linux -V \
  -R '^xn_descent_determinism_test$' \
  > build/xn-descent-linux/xn-determinism-replay-1.log
ctest --test-dir build/xn-descent-linux -V \
  -R '^xn_descent_determinism_test$' \
  > build/xn-descent-linux/xn-determinism-replay-2.log
```

Preserve the two runs' native B1 payload checksum, complete normalized C/R/G digest, completed
rounds, convergence condition, entry point, and thread-variant assertions. Exclude run ID,
timestamps, and absolute paths. Compare replay only for the same Partition input within Linux; do
not require a random P0/M0 full pipeline or final GlobalGraph to be byte-identical, and report any
macOS/Linux checksum difference for disclosure rather than failure.

Return `linux-validation.md` content or raw evidence containing the project commit, dirty patch and
untracked-archive checksums if applicable, exact commands and exit results, complete selected test
list, compiler/version, CMake/generator/build flags, architecture, OS/kernel, CPU/RAM and numerical
environment, Dataset/Partition identities used by replay, and the Linux-internal hashes above. T037
remains unchecked until this real evidence is received and reviewed.

The JSON provenance patch is exercised by the two `xn_descent_pipeline_*` entries already selected
above. The returned evidence must state that `request_version: 1.1.0` completed the real small
partitioned path and that its parent BuildManifests carried the runner-generated source object;
this remains a Linux-host observation and is not inferred from the local Docker result.

## 13. Completion report template

The later implementation report must list separately:

- every executed command and result;
- failures, cause, and impact;
- applicable but unexecuted gates and why;
- macOS and Linux replay results independently;
- SIFT1M and human-review status;
- unchanged upstream commit/worktree status;
- explicit confirmation that BBP, Secondary Index, IMF Search, SSD behavior, and end-to-end paper
  performance were not reproduced.
