# Feature 007 completion report

Audit recorded at 2026-09-17T03:28:40.890109+00:00.
Scope: native-Linux XN-Descent Local Build, component-level faithful adaptation.
T037 and T043 are complete; T041 is explicitly accepted in specs/007-xn-descent-local-build/validation/sift1m-post-run-review.md.
T042 closeout is confirmed only if the final commit and all post-commit checks below pass. Historical local T040 remains incomplete and is not claimed successful.

## Identities and evidence

- Actual SIFT1M execution commit: 57c27cbc967bd9b6998b587bab4b27ccd5766c6b.
- Reviewed implementation fix: 783a81c29ba7b407fdb8c0c73d3a5604aa520096.
- Upstream: https://github.com/microsoft/DiskANN.git, branch cpp_main, commit 78256bbab4685e1774e78d331e081a153be26823; current worktree and gitlink unchanged.
- Frozen request/approval, Artifact identities, separate BuildManifests, validation records, construction EvaluationReport, timing/logs and failures are referenced with hashes in experiments/results/xn-descent-local-build/sift1m-validation-remote-linux-counter-fix.json.
- Index SHA-256: d3a155e1c562b8f28643a09efb942815ec1f4d0768ff1bd45ec41c4c5f041727.
- Original T037 at 687017cc3cefefea913267f8eb976c572a13d841: the original section of specs/007-xn-descent-local-build/validation/linux-validation.md references its configure/build/test commands and environment evidence.
- Counter-fix regression at 5438bdd755f563267a02ddfa10286d04ce59b262: the supplement records the 14-test and two-replay results, log paths and hashes. This closeout does not claim to have newly verified every counter-fix configure/build command.
- SIFT1M execution at 57c27cbc967bd9b6998b587bab4b27ccd5766c6b: actual execution evidence and recorded toolchain, flags, Dataset identity, parameters and budgets are retained in the referenced BuildManifests. This is not a full regression rerun at the execution commit; no commands are reconstructed or rerun here.
- Source mapping and capability definitions are unchanged from the reviewed implementation. Artifact, Manifest and EvaluationReport remain separate.

## Executed validation and results

- Historical macOS validation remains separately documented in validation/macos-validation.md; the counter-fix macOS 8/8 evidence was pre-commit dirty-source testing, not a clean-commit or cross-platform-byte-equality claim.
- Native Linux counter-fix regression: 14/14 selected tests and two deterministic replays passed. Original logs and hashes are retained in the Linux validation supplement and result index.
- SIFT1M: P0=1, B1=3, M0=1; pipeline SUCCEEDED, exit 0; five Artifact validation records VALID.
- Partition 0: 20 rounds; three_round_c_g_stability; terminal Manifest /ssd1/kk/diskann_search_benchmark/build/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/workspace/manifests/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/stage-execution/local-0.json; SHA-256 fe566ba4f046827b23efa0fc0f1ff0e18bc4f03a66ea1aeea84a5eca279d4a2b
- Partition 1: 22 rounds; three_round_c_g_stability; terminal Manifest /ssd1/kk/diskann_search_benchmark/build/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/workspace/manifests/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/stage-execution/local-1.json; SHA-256 cd908ec4aa02e148104bd3d24b9997305a295906c30938ceaccae789d0bfdd54
- Partition 2: 22 rounds; three_round_c_g_stability; terminal Manifest /ssd1/kk/diskann_search_benchmark/build/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/workspace/manifests/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/stage-execution/local-2.json; SHA-256 accc120c444844450155449b5356065088e4190c6b6e477069ed724039edf99e
- GlobalGraph: structural evaluation VALID, 1,000,000 logical nodes, declared/observed maximum degree 64/64.
- Additional searches: zero/fixed_bfs, L=100/200/400, three repetitions per point; six aggregated rows retained in /ssd1/kk/diskann_search_benchmark/build/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix/diagnostics/search-metrics-57c27cb/summary.json. These are not formal performance certification.

## Failures, limitations and unexecuted scope

- Prior remote attempt: user-authorized SIGTERM, exit 143, incomplete; its evidence remains referenced separately.
- Local T040: first attempt aborted/incomplete; second attempt's latest retained record has no confirmed terminal outcome. Neither is relabeled by remote success.
- The final request/approval commit did not receive another full 14-test rerun; prior regression and final runner execution identities are distinguished.
- Search EvaluationManifest source-field attribution remains a disclosed limitation; supplemental Python evaluator provenance is preserved with the metrics summary.
- No full-paper-system reproduction, formal Baseline speedup, parameter sweep or larger-than-SIFT1M validation is claimed.
- This closeout checks existing evidence and hashes; it does not rerun tests, validate every binary payload anew, rebuild, search, or operate the Mac experiment.

## Repository closeout

Only the six named documentation/result-index files belong to this archival commit; implementation, tests, CMake, contracts, upstream and runtime evidence are unchanged.
The archive commit is the commit containing this report, not the historical execution commit.
Final clean-worktree, parent-commit, exact committed-tree and upstream checks run after the commit; only their success prints XN_FEATURE_007_REMOTE_CLOSEOUT_COMMITTED.
If a later step fails, file edits, staged changes or an already-created commit may remain. There is no automatic rollback; preserve the state and inspect the failure rather than rerunning the entire script or resetting.
This commit stores documentation and the result index, not the ignored build/ graph payloads, logs or validation evidence. Preserve the original experiment and diagnostics directories; the commit is not a backup of those files.
No push is authorized or performed.
