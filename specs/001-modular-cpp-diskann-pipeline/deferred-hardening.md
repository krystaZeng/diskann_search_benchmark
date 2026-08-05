# Feature 001 Deferred Hardening

**Status**: Deferred; none of the items below is complete.  
**Delivery boundary**: Feature 001 is frozen as an infrastructure prototype / contract MVP.

This register preserves the open T092–T098 work without extending the Feature 001 Convergence
phases. The original task descriptions and completion state remain unchanged in `tasks.md`. These
items are not prerequisites for starting Feature B, and deferral does not delete existing code or
tests.

| Original task | Current gap | Reason for deferral | Reassessment trigger |
|---|---|---|---|
| T092 | The post-T093–T098 full validation rerun and validation-record refresh have not been performed. | Its prerequisite hardening tasks are deferred, so running their aggregate completion gate would be misleading. | Reassess only after one or more deferred tasks are deliberately resumed and their implementation scope is agreed. |
| T093 | Canonical/reuse execution does not yet have the proposed end-to-end requirement to bind a complete terminal build-lifecycle BuildManifest and reject every incomplete fallback path. | The correct provenance boundary should be verified against the actual Baseline build and launch path instead of being inferred solely from Fake execution. | Reassess in Feature B when the real `cpp_main` Baseline build and execution lifecycle is integrated. |
| T094 | GlobalGraph structural source references do not yet receive the proposed additional full type/version/checksum identity hardening and mutation tests. | The exact source-lineage shape should be checked against real Baseline-produced Artifacts before further public-contract tightening. | Reassess in Feature B when real Partition and Local Graph Artifacts are available. |
| T095 | Executor-level cancellation gates before the first factory and between every stage, together with the complete independently observable cancellation matrix, are not implemented. | A Fake-only exhaustive cancellation matrix is deferred until a real execution path establishes actual cancellation boundaries and operational need. | Reassess after real Baseline or paper stage execution exposes cancellation behavior that must be governed. |
| T096 | The named Fake negative helpers for reuse dataset mismatch and PartitionId selection do not yet manufacture every proposed error through the most semantically exact input location. | Further Fake negative-matrix refinement has diminishing value before real implementation failures provide representative cases. | Reassess when Feature B or a paper implementation produces a matching real failure mode or reveals a contract ambiguity. |
| T097 | Direct C++ runner validation does not yet enforce the proposed exhaustive nested RunRequest checks before every workspace mutation. | The consumed request surface and admission boundary should be validated against the real Baseline runner integration before additional manual validation is frozen. | Reassess in Feature B when production stage selections and their real request fields are introduced. |
| T098 | Adapter evidence does not yet prove the declared legal target range for every explicit Local/Global ID relabeling. | Feature 001 contains no production Adapter; expanding evidence in the absence of a real conversion path would be speculative. | Reassess when a real Baseline or paper Adapter requires explicit ID relabeling. |

## Deferral rules

- Baseline build, provenance, lineage, and runner-boundary gaps are reassessed during Feature B
  using evidence from the real Baseline integration.
- Complete cancellation coverage, additional Fake negative-fixture coverage, and Adapter evidence
  remain deferred until a real Baseline or paper implementation creates a concrete requirement.
- No item in this document may be marked complete without implementation, its applicable targeted
  tests, and an updated validation record.
- Deferral does not change the frozen Fake payload bytes, adjacency tables, or checksums, and does
  not authorize modification of `upstream/diskann-cpp-main`.
