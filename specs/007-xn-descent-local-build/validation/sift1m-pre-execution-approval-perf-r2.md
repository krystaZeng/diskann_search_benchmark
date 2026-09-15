# Second SIFT1M attempt: independent human execution approval

Recorded at 2026-09-15T03:03:19Z from the user's explicit approval in this conversation. This is an authorization record, not an execution or validation-success report.

## Exact approved scope

- Request: `experiments/configs/xn-descent-local-build/sift1m-validation-perf-r2.json`.
- Raw file SHA-256: `b63805c6392338bd8798c8e950728443b0e75e3e7a56600e2781cfe9bc949dc5`.
- Request ID: `xn-sift1m-paper-v1-seed42-perf-r2`; JSON-only request version `1.1.0`, no caller-supplied `source_config`.
- Reviewed implementation baseline: `48e9411184a294ed3e687f00a6bcb3b812b7690d`, branch `feature/xn-descent-local-build`. No algorithm, test, CMake, or contract modification is authorized.
- Upstream: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit `78256bbab4685e1774e78d331e081a153be26823`; source and gitlink remain unchanged.
- Docker image: `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`, platform `linux/amd64`, offline (`--network=none`, `--pull=never`).
- Fixed profile: `paper-style`, `single_paper_reproduction`, `faithful_adaptation`, `local_build_only`. Only P0 → B1 per Partition → existing M0 construction/structure validation; no Search, Recall, QPS, external Linux migration, or larger dataset.
- P0: R=16, DRAM=0.6 GiB, replication=2, sample probability=0.256. B1: omega=40, lambda=64, alpha=1.2, seed=42, cap=128. M0: R=64, use_filters=false. All implementation identities and policy constants remain exactly as frozen.
- Thread budget=6; algorithm memory budget=17179869184 bytes (16 GiB). Dataset aggregate checksum=`4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`; raw vectors checksum=`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`.

The user expressly accepts disclosed insufficient-memory, system-OOM, uncertain-duration, and experiment-failure risks. The algorithm budget is not the whole-process memory limit. Previously suggested approximately 4 GiB of extra disk space is planning advice, not a newly imposed admission threshold. The 34→6 micro-oracle distance-call reduction is not a measured SIFT1M speedup; random P0/M0 behavior prevents claiming an exact two-attempt performance comparison.

## First attempt and recovered evidence retention

The first attempt remains **user-authorized abort; SIFT1M validation incomplete**. The original request, original approval, output, log, and diagnostics are retained. Its consumed approval does not authorize this second attempt.

Already-passed recovery-validation JSON files were copied byte-for-byte, with `cmp` equality checked, into new files under the first attempt's existing diagnostics. No recovery or Artifact validation was repeated for this archival step, and no metadata/checksum was altered:

| Durable repository-relative location (ignored runtime evidence) | SHA-256 |
| --- | --- |
| `build/xn-sift1m-paper-v1-seed42/diagnostics/20260915-restored-partition-validate-result.json` | `8711fbba4f6bf0d3354178e82f6c7b8ddb1c426f86eabad3bad6df7ed988496e` |
| `build/xn-sift1m-paper-v1-seed42/diagnostics/20260915-restored-partition-validation-evidence.json` | `9b14489af3893322e7ab8ea276c5fae6fd38d2cf6269af19e7c0f6e322736797` |

Original sources are `/private/tmp/xn-first-attempt-map-validation.eQrQJr/validate-result.json` and `/private/tmp/xn-first-attempt-map-validation.eQrQJr/workspace/manifests/evidence/xn-first-attempt-restored-partition-validation-validate.json`. The result's original relative evidence reference is preserved unchanged; the second durable file above is the corresponding evidence object. It reports Partition `VALID`, aggregate `4351e898904c49ce6787fd2b55c9ba5ba58700f1974c8a6e00c7d9791fcab437`, validator `artifact-contract-validator/1.1.0`; result is `SUCCEEDED`, exit 0, with partition/local_build/merge invocation counts all zero. This proves the restored Partition's recorded checks, not completed SIFT1M construction or a terminal B1/M0 result.

## Mandatory clean-source regression and one-run boundary

Commit only the new frozen request, quickstart supplement, this approval, and the T040 second-attempt supplement. Call that resulting clean commit **C**; record its actual SHA in ignored build/run logs and the eventual result index. Do not insert a guessed self-referential commit SHA here.

From unchanged clean C, reconfigure the existing `build/xn-descent-docker-linux-t023-r2` offline with quickstart Sections 11.3/11.5 flags and pinned cached nlohmann_json. Build the runner and the existing executables for all ten XN tests plus `cpp_main_descriptors_preflight`, `cpp_main_m0`, and `cpp_main_pipeline`. List and execute exactly those 13 tests. Check configured/compiled project commit C and dirty=false; generated project-source Manifests must omit `dirty_source_checksum`. Preserve new logs without overwriting historical logs. Any identity, SHA, configure, build, or test failure stops before the experiment; no code repair, parameter adjustment, or automatic retry is authorized.

After these checks, prepare the following fresh host output root only if absent:

`/Users/krystal/Desktop/入职材料/Mirage 复现/向量数据库立项材料/0803_diskann_methods/build/xn-sift1m-paper-v1-seed42-perf-r2`

Use container `xn-sift1m-paper-v1-seed42-perf-r2-t040` and the frozen three mounts: repository → `/workspace` read-only; new output root → `/xn-run` writable; original Dataset directory `build/ogp-partition-adaptation/runs/sift1m-structural/attempt-004/normal/datasets/sift1m` → `/xn-run/workspace/normal/datasets/sift1m` read-only. Prepare only the necessary empty mount target. The relative Dataset ref remains `datasets/sift1m/artifact.json` under workspace normal_root. Do not copy or modify Dataset files.

Execute this request exactly once. Retain actual `/xn-run/result.json` if generated, `/xn-run/xn-sift1m-run.log`, Artifacts, Manifests, and failure diagnostics; write references and actual running commit C to `experiments/results/xn-descent-local-build/sift1m-validation-perf-r2.json`. Do not fabricate absent results, terminal Manifests, or graphs. Exit 137 alone does not establish OOM: retain actual diagnostic observations and report unknown causes as unknown.

T038/T039 keep their first-attempt historical meaning. T040 stays unchecked before execution and after any failed, aborted, or incomplete attempt; only all required valid outputs can complete it. Stop after reporting the actual result for T041 human review. This approval authorizes neither a third attempt nor automatic review, migration, push, or parameter-changing rerun.
