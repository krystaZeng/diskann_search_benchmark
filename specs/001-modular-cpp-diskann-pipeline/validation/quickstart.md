# Canonical Quickstart Validation

**Date**: 2026-08-05  
**Platform**: macOS, AppleClang 14.0.0, CMake 4.3.3, Python 3.12.12

## Canonical Fake execution

```bash
.venv/bin/python -m diskann_harness.cli execute \
  --config tests/fixtures/fake_pipeline/experiment.yaml \
  --workspace build/feature-001/fake-workspace \
  --runner build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner
```

Result: exit **0**. Summary:
`build/feature-001/fake-workspace/reports/summaries/fake-six-node-two-run-v1.json`.

Both retained runs report invocation counts `{partition: 1, local_build: 2, merge: 1}` and final
aggregate checksum
`443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe`.
Both payload checksums and deterministic evaluation projections match, while run IDs, report IDs,
timestamps, and provenance are distinct.

The exact adjacency is:

```text
0:[1,2] 1:[0,2] 2:[1,3,4] 3:[2,4] 4:[3,5] 5:[]
```

Evaluation reports record `logical_node_count=6`, `invalid_neighbor_count=0`,
`declared_max_degree=3`, `observed_max_degree=3`, and `passed=true`.

Representative paths for run 1:

- Artifact metadata: `normal/fake-six-node-two-run-v1-run-1/merge/artifact.json`
- Merge BuildManifest: `manifests/fake-six-node-two-run-v1-run-1/stages/merge.json`
- Merge validation evidence: `manifests/evidence/fake-six-node-two-run-v1-run-1-merge-valid.json`
- EvaluationReport: `reports/fake-six-node-two-run-v1-run-1-evaluation.json`

## Valid Partition reuse

```bash
.venv/bin/python -m diskann_harness.cli execute \
  --config tests/fixtures/fake_pipeline/reuse-partition.yaml \
  --workspace build/feature-001/fake-reuse-workspace \
  --runner build/feature-001/tests/cpp/fakes/diskann_pipeline_fake_runner
```

Result: exit **0**. Both retained runs report invocation counts
`{partition: 0, local_build: 2, merge: 1}`, the same final checksum/adjacency as the canonical run,
fresh validation evidence for the reused Partition, and a terminal `REUSED` Partition manifest.

No real DiskANN executable or dataset was invoked by either command.

