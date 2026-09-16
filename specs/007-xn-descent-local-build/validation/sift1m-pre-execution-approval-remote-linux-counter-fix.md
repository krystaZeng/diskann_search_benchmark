# Remote SIFT1M counter-fix: independent approval

- Approved at: 2026-09-16T06:58:53.662208+00:00
- User entered APPROVE-3580b964 after reviewing the scope and full request SHA.
- Request: experiments/configs/xn-descent-local-build/sift1m-validation-remote-linux-counter-fix.json
- Request SHA-256: 3580b9644a26fccb5258f8cc36b24568b525b3b40ba13cde7e3dd79e163609e9
- Reviewed code and prior regression commit: 5438bdd755f563267a02ddfa10286d04ce59b262
- Upstream: https://github.com/microsoft/DiskANN.git, cpp_main, 78256bbab4685e1774e78d331e081a153be26823.
- Authorize exactly one native Linux SIFT1M P0 -> B1 -> M0 construction and structural validation.
- Keep 16 threads, 68719476736-byte algorithm budget, Dataset identity and all remaining frozen parameters.
- Accept uncertain duration, allocation failure/OOM and experiment failure. The algorithm budget is not an RSS limit.
- Output: build/xn-sift1m-paper-v1-seed42-remote-linux-counter-fix.
- Result: the new output directory's result.json; never fabricate missing outputs.
- Preserve previous attempts. Previous remote attempt: user-authorized termination, exit 143, validation incomplete.
- No automatic retry, Search/Recall/QPS, larger data, push, or operations on Mac experiments.
- Only the frozen request and this approval may change among version-controlled files before execution.
- Rebuild runner from the clean material commit; record its full SHA and binary checksum in launch evidence.
- Prior regression evidence belongs to the reviewed code commit, not to the future rebuild.
- T043 remains incomplete pending complete valid outputs; stop at T041 human review after reporting actual results.

## Existing Linux regression evidence

- /ssd1/kk/diskann_search_benchmark/build/xn-cross-counter-linux/diagnostics/ctest-14.log; SHA-256 c8cc35aaf630c0c4a5a2a73b204277679c3d4698131d99a325c5bca1279aae80
- /ssd1/kk/diskann_search_benchmark/build/xn-cross-counter-linux/diagnostics/replay-1.log; SHA-256 9f324fa2648b41b5e1db5a47be63112e1fcf4aab90476d55e5426b6e34c1f2fa
- /ssd1/kk/diskann_search_benchmark/build/xn-cross-counter-linux/diagnostics/replay-2.log; SHA-256 15103432a72934cd5d572f4d039d6f8576f2ae558432b910c783a836e4950cbb
