# Independent remote Linux SIFT1M approval

Recorded at 2026-09-16T10:01:58+08:00; this is record-creation time, not an invented approval timestamp.
Authority: the user's explicit approval in this conversation and confirmation to proceed.

Request: experiments/configs/xn-descent-local-build/sift1m-validation-remote-linux.json
SHA-256: 1312b7c260fdbcd790830339186d28e1e2470fb0ca092cd2c3ee97c49b8f66cc
Request ID: xn-sift1m-paper-v1-seed42-remote-linux
16 threads; 64 GiB algorithm memory budget; all other frozen parameters unchanged.

The user understands that the budget is not a process memory hard limit and accepts
insufficient memory, system OOM, uncertain duration and experiment-failure risks.

First retain existing T037 evidence, record approval and commit the frozen materials.
Then rebuild natively from the final exact clean commit. Algorithm, tests and build code
must remain identical to 687017cc3cefefea913267f8eb976c572a13d841. Record the actual later execution commit.
Validation evidence: specs/007-xn-descent-local-build/validation/linux-validation.md. Dataset/output/result paths: quickstart.md Section 12.5.

Authorization covers exactly one P0 -> B1 -> M0 SIFT1M construction and structural validation.
No automatic retry, parameter changes, Search/Recall/QPS, larger Dataset, push or Mac experiment
operations. Preserve actual results/failures; do not fabricate missing outputs or infer OOM
solely from exit 137. Incomplete output remains validation incomplete and leaves T043 unchecked.
Only complete required outputs and successful validation can complete T043; stop at T041 review.

Local approvals are not reused. The first local attempt remains aborted/incomplete.
The second local attempt's current state is not queried or changed here.
Status: approved; final clean-commit rebuild and remote scale execution pending.
