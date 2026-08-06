# Quickstart: Validate the Planned B0 Callable Wrapper

This is a post-implementation validation guide, not evidence that the wrapper currently exists.
Run nothing from this guide until the planned targets and native format validators are implemented.
The v1 representation amendment is complete. All commands use out-of-tree build/output paths.

## 1. Admission checks

```sh
git ls-files --stage upstream/diskann-cpp-main
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
```

Expected: gitlink and submodule HEAD both equal
`78256bbab4685e1774e78d331e081a153be26823`; upstream status is empty. Any mismatch stops the run.

Verify the immutable SIFT1M input through the implemented DatasetArtifact admission command. The
expected identity is `1000000 x 128` little-endian float32, 512000008 bytes, SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`.
Do not read `sift1M_dataset/selected_query.csv` for B0.

## 2. Configure and build outside upstream

```sh
cmake -S . -B build/cpp-main-b0 -DCMAKE_BUILD_TYPE=Release \
  -DDISKANN_CPP_MAIN_BASELINE=ON
cmake --build build/cpp-main-b0 --target cpp_main_b0_tests --parallel
```

Expected: the fixed upstream source is configured with an explicit binary directory below
`build/cpp-main-b0/`; no generated file appears under `upstream/diskann-cpp-main`. The BuildManifest
records actual configure/build commands, toolchain, effective definitions/options, linked runtime
identities, and binary checksums.

## 3. Run contract and small-fixture tests first

```sh
ctest --test-dir build/cpp-main-b0 --output-on-failure \
  -R '^cpp_main_b0_(contract|input_gate|callable_wrapper|structural)$'
```

Expected positive findings:

- the descriptor admits the Dataset/Partition requirements, receives an explicit PartitionId, and
  uses the Dataset-backed VectorAccessor when the selected Partition has no shard vectors;
- the pinned upstream shard-retrieval and `Index<float>::build/save` call path is used;
- reconstructed shard rows equal Dataset rows selected by the unchanged map;
- LocalGraph node count equals map length, neighbors are valid LocalNodeIds, and maximum degree is
  at most `floor(2R/3)`;
- Dataset, Partition, and map lineage are exact.

Expected negative cases fail before factory/build/publication as appropriate: invalid PartitionId,
missing Partition record, incomplete/out-of-range/non-consumable map, Dataset mismatch, invalid
parameters, shard-order mismatch, invalid neighbor, wrong node count, and degree overflow. No
invalid candidate enters downstream execution.

## 4. Run one SIFT1M structural gate

After the smaller tests pass, run the planned non-installed B0 stage driver once with:

```text
experiments/configs/cpp-main-upstream-baseline/b0-sift1m-structural.yaml
```

The frozen profile is float32/L2/unfiltered, `R=16`, `L=32`, `T=6`, build PQ/OPQ/labels disabled,
and one explicit validated PartitionId/map. The run must retain a schema-valid terminal
BuildManifest, one validated LocalGraphArtifact, and ValidationEvidence. It must not automatically
change a parameter or retry on failure.

Expected terminal scope:

```yaml
study_class: baseline_validation
stage: B0
separability: separable
binding_mode: upstream_callable_wrapper
validation_scope: structural_and_contract
formal_graph_parity: not_reached
validated_graph_baseline_registered: false
```

Do not compare LocalGraph bytes or adjacency with Native Probe captures. Do not run repetitions,
P0, M0, formal Graph Parity, Recall/QPS/latency evaluation, or a dataset larger than SIFT1M.

## 5. Post-run immutability check

```sh
git -C upstream/diskann-cpp-main rev-parse HEAD
git -C upstream/diskann-cpp-main status --porcelain=v1
git diff --check
```

Expected: the pinned upstream HEAD is unchanged and clean. Report executed, failed, and unexecuted
tests separately; an incomplete SIFT1M gate must not be described as passed.
