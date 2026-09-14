# SIFT1M Pre-Execution Approval

## Decision

- Decision: **APPROVED FOR ONE LOCAL DOCKER LINUX/AMD64 RUN**
- Human decision recorded: 2026-09-14T04:36:21Z
- Approval source: explicit user approval in the active XN-Descent T039 review workflow
- Execution gate satisfied: T039 only
- Not authorized by this decision: parameter changes, automatic retry, SIFT100M or larger,
  external Linux execution, T041 review, T037 migration, or push

## Exact approved request

```text
path: experiments/configs/xn-descent-local-build/sift1m-validation.json
sha256: b71453ecad9360f4af9d33c02ff1a05e69f07c6ef3c63599097d104d8a0c1f23
request_id: xn-sift1m-paper-v1-seed42
request_version: 1.1.0
```

Approval is invalid if the request bytes or SHA-256 differ at execution time.

## Reviewed identities and configuration

- Review-time project HEAD: `69904ab24de4bd6e5941e6b4ab6cdc18e741711a`
- Pinned upstream commit: `78256bbab4685e1774e78d331e081a153be26823`
- Docker image: `sha256:d7e34358676d7098befc6daa6d63973670333108d2bbe7029483a055f6c1cabf`
- Docker platform: `linux/amd64`
- Dataset: 1,000,000 x 128 float32 vectors under L2
- Dataset payload SHA-256:
  `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`
- Dataset Artifact aggregate checksum:
  `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`
- P0: `R=16`, `build_DRAM_budget_gib=0.6`, `replication_factor=2`,
  `sample_probability=0.256`
- B1: `omega=40`, `lambda=64`, `alpha=1.2`, `seed=42`, `iteration_cap=128`
- M0: `R=64`, `use_filters=false`
- Execution budget: 6 threads and 17,179,869,184 bytes
- Attempt policy: exactly one execution, with no parameter-changing retry or best-result selection

## Evidence reviewed

- Requirements checklist: 16/16 complete.
- Clean-source macOS XN core regression: 8/8 passed.
- Clean-source Docker Linux/amd64 XN plus affected cpp_main regression: 13/13 passed.
- Earlier comprehensive regressions: macOS 33/33 and Docker Linux/amd64 25/25 passed.
- Existing evidence covers Algorithm 1, Equation (2), `TryInsert`, cross updates, deterministic
  replay, both convergence conditions, budget/cancellation failures, Artifact/Manifest separation,
  validator gates, reusable M0, and complete small P0 -> B1 x Partition -> M0 execution.

## Accepted risks and claim boundary

The human approver explicitly accepts the reviewed risks: the 16-GiB XN budget is not a hard
whole-process limit; P0, M0, Docker, the OS, and unbounded logical reverse-neighbor state may
consume additional memory; available disk and wall-clock completion are not guaranteed; and the
single attempt may end in a controlled failure or abnormal exit. Every outcome must be retained
without changing parameters and retrying.

The approved run validates SIFT1M construction scale, the complete partitioned P0 -> B1 -> M0
path, Artifact/Manifest integrity, resource behavior, and termination. It does not validate Search,
Recall, QPS, IMF Search, SSD behavior, or the complete XN-Graph system.

## Conditions before execution

T040 may begin only after this approval record, the frozen request, the T038 quickstart material,
and the T038/T039 task states are committed together. The runner must then be reconfigured and
built from that exact clean commit. Before the one approved invocation, the request SHA-256,
project/upstream identities, clean-worktree state, Docker image/platform, mount layout, Dataset
identity, and compiled clean source identity must be rechecked.
