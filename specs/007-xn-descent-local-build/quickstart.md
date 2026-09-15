# Quickstart: XN-Descent Validation

This file records the implemented validation entries, the commands already exercised through
T034, and the still-pending external Linux and SIFT1M gates. A command labeled `[VERIFIED]` has
corresponding evidence under `validation/`; `[PENDING]` means it is an executable handoff and not a
claim that it has run.

The sequence is intentionally gated. The current remote path is T037 → T043 → T041 → T042;
local SIFT1M success is not a prerequisite for remote preparation. T038/T039 retain their local
historical meaning, and neither remote success nor a later gate converts an unfinished local
T040 attempt into a pass.

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
`xn_descent_experiment_runner` is available in a Baseline-ON build. The local SIFT1M attempts in
Section 11 have been started; they are not established successful scale results.

## 11. Local Docker SIFT1M freeze and execution gate

`[HISTORICAL FREEZE: T038; FIRST T039 APPROVAL CONSUMED]` The immutable request and this section
defined the first local Docker Linux/amd64 construction-scale attempt. The freeze itself did not
run any stage. The one approved T040 attempt later completed P0 and was then stopped with explicit
user authorization before any B1 termination Manifest or runner result existed; SIFT1M validation
therefore remains incomplete. Its request, approval, logs, outputs, and diagnostics remain retained.

### 11.1 Frozen request and Dataset identity

```text
request: experiments/configs/xn-descent-local-build/sift1m-validation.json
request_sha256: b71453ecad9360f4af9d33c02ff1a05e69f07c6ef3c63599097d104d8a0c1f23
request_version: 1.1.0
request_id: xn-sift1m-paper-v1-seed42
status: first one-run approval consumed; attempt user-authorized abort; T040 incomplete
```

The request omits `source_config`; the runner derives the raw-byte and `xn-json-compact-v1` hashes
at invocation. Freeze-time static review confirmed the runner's exact top-level/nested field sets,
implementation identities, parameter types and fixed policy strings. It did not call
`operation=execute` as a fake dry run.

The local-only Dataset source is the ignored build directory:

```text
build/ogp-partition-adaptation/runs/sift1m-structural/attempt-004/normal/datasets/sift1m
```

Read-only validation at that source established:

- Dataset metadata is strict JSON with no duplicate keys;
- type/version is `dataset` / `diskann-float32-dataset/v1`;
- shape is 1,000,000 rows by 128 float32 dimensions under L2;
- `vectors.bin` is 512,000,008 bytes and its native header is `(1000000,128)`;
- raw payload SHA-256 is
  `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`;
- the repository's `diskann-artifact-c14n-v1` implementation recomputes aggregate checksum
  `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`.

The raw payload and aggregate checksums are different identities. The request uses relative
`dataset.metadata_path=datasets/sift1m/artifact.json`; with workspace root `/xn-run/workspace`, it
resolves under `workspace.normal_root` to
`/xn-run/workspace/normal/datasets/sift1m/artifact.json`, and that metadata's relative payload path
resolves to `/xn-run/workspace/normal/datasets/sift1m/vectors.bin`. It contains no absolute Dataset
path or `..` component.

### 11.2 Frozen parameters and observed resources

```text
profile: paper-style
P0: R=16, build_DRAM_budget_gib=0.6, replication_factor=2,
    sample_probability=0.256
B1: omega=40, lambda=64, alpha=1.2, seed=42, iteration_cap=128
M0: merge.R=64, use_filters=false
workspace.root: /xn-run/workspace
memory_budget_bytes: 17179869184
thread_budget: 6
```

The implementation tuple is the current descriptor tuple: pinned P0
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1`, B1
`xn_graph_xn_descent_local_build_v1`, and pinned M0
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1`. `lambda` and
`merge.R` remain independent even though both are 64 in this request.

The 2026-09-14 offline diagnostic container used image
`sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`
as `linux/amd64` and observed:

- LinuxKit kernel `5.15.49-linuxkit`, x86_64;
- 6 processors; cgroup CPU `max 100000` (no narrower quota observed);
- total memory 20,991,467,520 bytes and currently available memory 19,054,678,016 bytes;
- cgroup memory `max` (no narrower cgroup limit observed);
- declared-memory-budget margins of 3,811,598,336 bytes against total and 1,874,808,832 bytes
  against the point-in-time available value;
- CMake 3.22.1 and GCC 11.4.0;
- 16,498,421,760 bytes free on the `/xn-run` filesystem at inspection time;
- repository mount read-only and output mount read-write.

These observations show only that six workers are visible and the declared 16-GiB algorithm
budget is below the observed total/available memory at inspection. The budget is not a process
hard limit and does not reserve memory for P0, B1, M0, the OS, Docker, or other allocations. The
remaining disk space has been recorded, not certified as sufficient. Actual peak memory, disk
consumption, and completion were not proved by the later aborted attempt.

### 11.3 Frozen future build and mount layout

The host output directory is:

```text
/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/0803_diskann_methods/build/xn-sift1m-paper-v1-seed42
```

It was created empty during T038 and now retains the first attempt's incomplete output, logs, and
diagnostics. They must not be deleted, overwritten, or reused. The frozen runtime mounts were:

1. repository root to `/workspace`, read-only;
2. the host output directory above to `/xn-run`, read-write;
3. the existing host Dataset directory to
   `/xn-run/workspace/normal/datasets/sift1m`, read-only.

T038 did not create `/xn-run/workspace/normal`, copy the Dataset, or perform the third mount. The
later authorized first-attempt preparation created only the required nested mount target and used
the frozen read-only Dataset mount; it did not change the source `artifact.json`, `vectors.bin`,
relative payload path, Validator, or confinement rules. Freeze-time validation at the source path
was not represented as the mounted Artifact Validator result.

For the first attempt, T040 was required to start from the clean commit containing the T038
material and T039 approval record and reconfigure the existing writable r2 build directory
offline:

```bash
cmake -S . -B build/xn-descent-docker-linux-t023-r2 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_XN_DESCENT=ON \
  -DDISKANN_GP_ANN_OGP=OFF \
  -DDISKANN_HNSW_MERGER=OFF \
  -DDISKANN_SEARCH_EVALUATION=OFF \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/workspace/build/xn-descent-docker-linux-t023-r2/_deps/nlohmann_json-src

cmake --build build/xn-descent-docker-linux-t023-r2 --parallel \
  --target xn_descent_experiment_runner
```

The configure/build container additionally bind-mounts the existing host r2 directory at the same
`/workspace/build/xn-descent-docker-linux-t023-r2` location as writable. Before execution, T040
must verify the compiled source identity equals the committed review baseline, `dirty=false`, and
the resulting Manifest omits `dirty_source_checksum`.

The runner path is:

```text
/workspace/build/xn-descent-docker-linux-t023-r2/src/papers/xn_graph/xn_descent/xn_descent_experiment_runner
```

The configured first-attempt output locations were:

```text
runner result: /xn-run/result.json
runner log: /xn-run/xn-sift1m-run.log
repository result index: experiments/results/xn-descent-local-build/sift1m-validation.json
```

The repository result index was reserved to reference external immutable results, Artifacts,
Manifests, logs, and any failure impact without duplicating those objects. It was not created after
the aborted attempt. The run log, incomplete workspace, and diagnostics remain under the host
output directory; no `result.json` exists.

### 11.4 Historical first-attempt execution and claim policy

T039 reviewed and explicitly approved the exact request SHA-256, Dataset identities, parameters,
image/toolchain, memory headroom, disk observation, build identity, mounts, and retention policy.
It authorized exactly one local Docker Linux/amd64 attempt. Success and controlled failure were to
be retained; there was no automatic retry, parameter-changing retry, best-run selection, `R`
truncation, relaxed validation, or SIFT100M-or-larger execution.

This validates only SIFT1M P0 → B1×Partition → M0 construction, Artifact/Manifest integrity,
resource behavior, and termination. It does not execute or claim Search, Recall, QPS, IMF Search,
BBP, Secondary Index, SSD behavior, end-to-end paper accuracy, or complete XN-Graph reproduction.
The local six-thread choice does not constrain a later remote Linux run that declares and records
a different legal thread count.

The local approval history remains, while the current remote order is independent:

```text
Local history: T038 frozen material → T039 approval → T040 local attempts
Remote path:   T037 native rebuild / 14 tests / two replays
               → T043 independent remote freeze / approval / SIFT1M / actual outcome
               → T041 human result review → T042 final audit
```

The aborted attempt consumed that approval and remains incomplete. Neither the original freeze
nor the original T039 decision authorizes another SIFT1M execution. T037 no longer waits for
T040 or T041; T043 requires its own remote request and explicit approval.

### 11.5 Performance-fix second-attempt freeze (separately approved once)

`[LOCAL HISTORY; SEPARATELY APPROVED ATTEMPT STARTED, NO TERMINAL RESULT ESTABLISHED HERE]`
The second attempt was launched from clean commit
`39d61450935f04d5b74b2bf83a9c3dcb4bbb0d2b`; the latest retained observation was running.
This documentation-only handoff does not inspect or control the container and does not infer a
new current or terminal status. The following freeze/build requirements remain historical facts,
not instructions to launch again; all existing outputs and diagnostics remain retained.

The performance-fix source baseline is commit
`48e9411184a294ed3e687f00a6bcb3b812b7690d`. The final macOS 8/8 and Docker 13/13 regressions
were run before that commit against HEAD `3fc14732b57a1510b2136752cbf2d94847e63d14` with the exact
performance-fix code and tests present in a dirty worktree, recorded checksum
`2cef23beabbf1ebde0dd8af566773b81cc8950e5230d47e17096a71bac738e6a`. Direct review of the
commit diff and current run-related source/test content confirms that the committed implementation
matches the validated content; this preparation does not claim that commit `48e9411` was rebuilt.
The first attempt remains recorded as a user-authorized abort under
`build/xn-sift1m-paper-v1-seed42/diagnostics/` and is not a successful scale result.

The independent second request is:

```text
request: experiments/configs/xn-descent-local-build/sift1m-validation-perf-r2.json
request_sha256: b63805c6392338bd8798c8e950728443b0e75e3e7a56600e2781cfe9bc949dc5
request_version: 1.1.0
request_id: xn-sift1m-paper-v1-seed42-perf-r2
container: xn-sift1m-paper-v1-seed42-perf-r2-t040
status: approved once and started; latest retained observation running; T040 remains unchecked
```

Strict parsing found no duplicate key, placeholder, or caller-supplied `source_config`. A
structured comparison with the original request found exactly one change: `request_id`. Thus the
Dataset, implementation tuple, algorithm policies, P0 parameters, B1 `omega=40`, `lambda=64`,
`alpha=1.2`, `seed=42`, `iteration_cap=128`, M0 `R=64`, six-thread budget, and
17,179,869,184-byte algorithm memory budget remain unchanged. This attempt tests only the repaired
implementation; it does not change quality parameters or extend the iteration cap.

Read-only Dataset revalidation used the repository's existing `diskann-artifact-c14n-v1`
implementation and confirmed 1,000,000 by 128 float32 L2 vectors, a 512,000,008-byte payload with
native header `(1000000,128)`, raw payload SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`, and Dataset Artifact
aggregate checksum `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`.

The 2026-09-15 offline diagnostic used the same fixed Linux/amd64 image and observed six
processors (`cpuset.cpus.effective=0-5`; `cpu.max=max 100000`), 20,991,467,520 bytes total memory,
18,949,177,344 bytes available, and `memory.max=max`. The declared budget was 3,811,598,336 bytes
below total memory and 1,769,308,160 bytes below the point-in-time available value. CMake 3.22.1
and GCC 11.4.0 remained available. The new output path's filesystem had 12,443,967,488 bytes free
and was 98% used. The memory budget is not a whole-process hard limit and does not reserve memory
for P0, M0, Docker, the OS, or other allocations; current memory and especially disk headroom leave
material runtime-failure risk and do not prove that the attempt can complete.

At freeze time, the second host output path did not exist; it now belongs exclusively to that
attempt and must not be overwritten or reused:

```text
/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/0803_diskann_methods/build/xn-sift1m-paper-v1-seed42-perf-r2
```

The separately approved execution preparation required a fresh directory and refusal to reuse an
existing one. The three mounts for the second attempt are:

1. repository root to `/workspace`, read-only;
2. the new host output path above to `/xn-run`, read-write;
3. the retained source Dataset directory
   `/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/0803_diskann_methods/build/ogp-partition-adaptation/runs/sift1m-structural/attempt-004/normal/datasets/sift1m`
   to `/xn-run/workspace/normal/datasets/sift1m`, read-only.

The request's relative `dataset.metadata_path=datasets/sift1m/artifact.json` therefore resolves to
`/xn-run/workspace/normal/datasets/sift1m/artifact.json`; its payload resolves beside it as
`vectors.bin`. Approval-time preparation, not this freeze, must create the empty nested mount
target without copying or changing the Dataset. Container-internal result paths remain
`/xn-run/result.json` and `/xn-run/xn-sift1m-run.log`. The independent repository result index is
`experiments/results/xn-descent-local-build/sift1m-validation-perf-r2.json`; none of these new
output or result paths was created in this preparation.

The existing `build/xn-descent-docker-linux-t023-r2` cache and the exact offline Release configure
flags in Section 11.3 remain the build path. The independent user approval is recorded in
`specs/007-xn-descent-local-build/validation/sift1m-pre-execution-approval-perf-r2.md`, including
checksums and durable references for the restored first-attempt Partition validation. After the
new freeze, approval, and T040 supplement are committed together, call that exact clean commit C.
Keep C and the worktree unchanged through configure, build, regression, and launch. Rebuild
`xn_descent_experiment_runner`, `xn_descent_parameters_test`,
`xn_descent_equation_try_insert_test`, `xn_descent_round_oracle_test`,
`xn_descent_local_artifact_test`, `xn_descent_determinism_test`, `xn_descent_pipeline_test`,
`cpp_main_descriptors_preflight`, `cpp_main_m0`, and `cpp_main_pipeline`. List and execute exactly
the 13 CTests selected by `^(xn_descent_.*|cpp_main_(m0|descriptors_preflight|pipeline))$`.
Any check/configure/build/test failure stops execution without a code repair or experiment retry.
Verify compiled project commit C, `dirty=false`, and generated project-source Manifests without
`dirty_source_checksum`; retain new logs in the ignored build directory, not tracked documents.

The original T039 approval does not authorize this request. One new approval may authorize only
one new attempt, with no automatic retry, parameter change, best-result selection, deletion of the
first attempt, or exclusion of the first abort from the audit. The micro-oracle reduction from 34
to 6 distance calls is not a SIFT1M speedup measurement. P0 and M0 retain random behavior, so equal
parameters do not require identical Partitions or GlobalGraph edges across attempts and do not make
the two attempts a strict performance comparison. This material and the independent approval now
supplement the still-unchecked T040 with the second request, its SHA, approval, and result-index
paths. T038/T039 retain their first-attempt historical meaning. A vague instruction to
"continue T040," the old request path, or the checked T039 is not authority for further attempts.
The explicit second-request approval covers only that already-started attempt; it does not
authorize a restart, retry, or remote execution.
Retain failures and incomplete output truthfully; exit code 137 alone does not establish OOM.
Only complete required Artifacts and successful validation may complete T040. T037/T043 proceed
independently; their success cannot complete T040 or substitute for either local attempt's result.
T041 now reviews the remote outcome together with separately retained local history.

## 12. External Linux rebuild and replay handoff

`[PENDING: T037; DOES NOT DEPEND ON LOCAL SIFT1M OR T041]` Docker evidence is not the user-executed
native remote Linux record required here. Transfer the clean preparation commit from
`codex/xn-remote-linux-prep` (full SHA supplied as `C_remote` in the delivery), then the user must
perform the following on the target Linux host. The macOS Codex must not mark T037 complete without
that actual evidence. This preparation commit changes only tasks/quickstart from project baseline
`39d61450935f04d5b74b2bf83a9c3dcb4bbb0d2b`; it does not claim a new build or test execution.

### 12.1 Transfer the exact reviewed source

This handoff uses only a clean commit plus a full, single-branch Git bundle. Create the bundle in
a new directory outside the repository, referencing `refs/heads/codex/xn-remote-linux-prep` with no
incremental exclusions/prerequisites, no `--all`, and no bare-SHA replacement for the branch ref.
Verify it with `git bundle verify` and `git bundle list-heads`, and record its SHA-256 and the full
branch-tip commit `C_remote` in the delivery. Do not transfer a dirty patch or untracked archive.

The bundle contains the project's complete reachable history and pinned submodule gitlink, not
the submodule's separate object database. It contains no ignored build output, Dataset, logs,
dependency cache, or macOS/Docker binary. On remote Linux, the fixed upstream commit and native
build dependencies must be available separately; if unavailable, stop rather than substitute a
different version. The submodule command below obtains the recorded commit, not a floating branch.

On the target Linux host, supply the delivered bundle path/SHA, `C_remote`, and a new destination
directory; these are user environment values, not paths copied from the local Docker request:

```bash
set -euo pipefail
: "${XN_REMOTE_BUNDLE:?Set the absolute received bundle path}"
: "${XN_REMOTE_BUNDLE_SHA256:?Set the delivered bundle SHA-256}"
: "${XN_REMOTE_COMMIT:?Set the delivered full C_remote SHA}"
: "${XN_REMOTE_REPO:?Set a new absolute destination path}"
printf '%s  %s\n' "$XN_REMOTE_BUNDLE_SHA256" "$XN_REMOTE_BUNDLE" | sha256sum --check -
test ! -e "$XN_REMOTE_REPO"
git clone --single-branch --branch codex/xn-remote-linux-prep \
  "$XN_REMOTE_BUNDLE" "$XN_REMOTE_REPO"
cd "$XN_REMOTE_REPO"
git bundle verify "$XN_REMOTE_BUNDLE"
git bundle list-heads "$XN_REMOTE_BUNDLE"
test "$(git rev-parse HEAD)" = "$XN_REMOTE_COMMIT"
test "$(git rev-parse refs/heads/codex/xn-remote-linux-prep)" = "$XN_REMOTE_COMMIT"
git submodule update --init --recursive upstream/diskann-cpp-main
```

Verify before building:

```bash
git rev-parse HEAD
git status --short
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --short
git diff --submodule=short -- upstream/diskann-cpp-main
git diff --cached --submodule=short -- upstream/diskann-cpp-main
test -z "$(git status --porcelain)"
test "$(git -C upstream/diskann-cpp-main rev-parse HEAD)" = \
  78256bbab4685e1774e78d331e081a153be26823
test -z "$(git -C upstream/diskann-cpp-main status --porcelain)"
```

The upstream result must be
`78256bbab4685e1774e78d331e081a153be26823`, clean, with no gitlink difference.

### 12.2 Rebuild on Linux

Do not copy macOS or Docker binaries. In a new out-of-tree native Linux directory, use the actual
registered targets. Record native compiler/CMake, OS/architecture, flags and numerical environment;
stop on a missing dependency or build failure rather than importing a local cache/binary. Configure
from the verified clean commit and confirm generated source identity matches `C_remote`,
`dirty=false`, with no project `dirty_source_checksum` in the resulting Manifests:

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

List and run exactly 14 tests: all ten XN tests plus the four required Baseline/surface regressions.
Missing, skipped or failed tests do not satisfy T037. Preserve configure/build/CTest commands,
exit statuses and logs in the ignored Linux build directory:

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
ctest --test-dir build/xn-descent-linux -V --no-tests=error \
  -R '^xn_descent_determinism_test$' \
  > build/xn-descent-linux/xn-determinism-replay-1.log 2>&1
ctest --test-dir build/xn-descent-linux -V --no-tests=error \
  -R '^xn_descent_determinism_test$' \
  > build/xn-descent-linux/xn-determinism-replay-2.log 2>&1
```

Preserve the two runs' native B1 payload checksum, complete normalized C/R/G digest, completed
rounds, convergence condition, entry point, and thread-variant assertions. Exclude run ID,
timestamps, and absolute paths. Compare replay only for the same Partition input within Linux; do
not require a random P0/M0 full pipeline or final GlobalGraph to be byte-identical, and report any
macOS/Linux checksum difference for disclosure rather than failure.

Return `linux-validation.md` content or raw evidence containing the clean project commit and bundle
SHA-256, exact commands and exit results, complete selected test
list, compiler/version, CMake/generator/build flags, architecture, OS/kernel, CPU/RAM and numerical
environment, Dataset/Partition identities used by replay, and the Linux-internal hashes above. T037
remains unchecked until this real evidence is received and reviewed.

The JSON provenance patch is exercised by the two `xn_descent_pipeline_*` entries already selected
above. The returned evidence must state that `request_version: 1.1.0` completed the real small
partitioned path and that its parent BuildManifests carried the runner-generated source object;
this remains a Linux-host observation and is not inferred from the local Docker result.

### 12.4 Independent remote SIFT1M gate (T043), then T041/T042

`[PENDING; NO REMOTE APPROVAL OR EXECUTION]` Only after T037's native rebuild, 14 tests, and two
replays pass, prepare `experiments/configs/xn-descent-local-build/sift1m-validation-remote-linux.json`
as a new JSON-only `request_version=1.1.0` request. Independently freeze its exact SHA-256, real
remote Dataset/metadata/payload paths and both checksum identities, implementation tuple and
policies, P0/B1/M0 parameters with independent `lambda`/`merge.R`, CPU/thread/memory/disk conditions,
native toolchain, unique request ID, new unused remote output directory, and result/log locations.
Keep Dataset references relative to that remote workspace's `normal_root` and preserve existing
Artifact path rules. Local Docker mounts, paths, budgets or approvals are not remote defaults or
permission; the actual remote configuration must be reviewed on its own.

Before any scale execution, record explicit user approval bound to the new request SHA, reviewed
source and remote conditions in
`specs/007-xn-descent-local-build/validation/sift1m-pre-execution-approval-remote-linux.md`.
Commit the freeze and approval, then reconfigure/rebuild natively from that exact clean execution
commit and verify the compiled commit, `dirty=false`, and the corresponding Manifest source
identity without `dirty_source_checksum`. Distinguish this execution commit from `C_remote` if
approval/material commits have advanced it. No local T039 or perf-r2 approval authorizes T043.

Execute the specifically approved request once, preserving success, controlled failure, abnormal
exit and any incomplete outputs without automatic retry, parameter changes or best-run selection.
Record actual execution identity and immutable output/Artifact/Manifest/validation/log references
in `experiments/results/xn-descent-local-build/sift1m-validation-remote-linux.json`; never fabricate
missing results or infer OOM from exit code 137 alone. The result index is not itself proof of success.

T043 may be marked successfully complete only when every expected Partition has its complete valid
B1 LocalGraph, the entire collection passes the existing Validator before M0, M0 produces a valid
GlobalGraph, and all required Manifests and successful validation evidence are present and agree
with the frozen inputs/execution identity. A failed, aborted or incomplete attempt remains
“验证未完成” with T043 unchecked, even if it exits or an index is written. This is construction and
structural validation only: no Search/Recall/QPS, full-paper-system claim or larger dataset.

T041 reviews the actual remote result (including failure), its independent approval and T037
evidence, alongside separately retained histories of both local attempts. T042 audits required
remote gates and records each local/remote status truthfully. Neither gate relabels a local
unfinished T040 attempt as successful, and an incomplete remote result cannot establish completed
validation. This handoff creates none of the future request, approval, result or validation files.

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
