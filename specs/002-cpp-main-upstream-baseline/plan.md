# Implementation Plan: cpp_main B0 Upstream Callable Wrapper

**Branch**: `feature/cpp-main-upstream-baseline` | **Date**: 2026-08-06 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification and completed source/probe evidence in
`specs/002-cpp-main-upstream-baseline/`.

## Summary

Implement only B0 as a project-owned orchestration wrapper around pinned, unchanged upstream
callables. The wrapper uses the validated Dataset-backed `StageContext::vector_accessor`, a
validated aggregate PartitionArtifact, an explicit PartitionId, the selected complete
LocalToGlobalIdMap, and frozen B0 parameters. It invokes
`retrieve_shard_data_from_ids<float>`, constructs upstream
write parameters with Profile `L`, `floor(2R/3)`, saturation false, Profile `T`, and upstream
defaults, then invokes `Index<float>::build/save` and publishes a validated LocalGraphArtifact.

The acceptance boundary is only `structural_and_contract`. It does not compare graph bytes,
process state, thread scheduling, or adjacency equality with the top-level native composite loop;
it adds no repeated-run statistics or formal Graph Parity. P0, M0, full Unified execution, and
`validated_graph_baseline` registration remain out of reach in this plan.

## Technical Context

**Language/Version**: C++17 at the public pipeline boundary and for the B0 wrapper

**Primary Dependencies**: fixed `upstream/diskann-cpp-main` commit
`78256bbab4685e1774e78d331e081a153be26823`; upstream CMake target `diskann_s`; existing
`diskann_pipeline::core`; upstream-resolved OpenMP, MKL, tcmalloc, and platform runtime dependencies

**Storage**: immutable filesystem Artifacts plus separate BuildManifest and ValidationEvidence;
all build, temporary, output, log, and test files outside `upstream/`

**Testing**: CTest for C++17 contract, input-gate, callable-wrapper, native-format, and structural
graph tests; one retained SIFT1M B0 structural validation after smaller tests pass; no repeated-run
or parity statistics

**Target Platform**: Linux amd64 is the Baseline validation platform; macOS may compile/run tests
that do not require the upstream Linux dependency surface but cannot establish Baseline validity

**Project Type**: C++ library implementation selected by the existing static Registry, with a
test-only stage driver for B0 validation and no new installed CLI

**Study Class**: `baseline_validation`

**Baseline Reference**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit
`78256bbab4685e1774e78d331e081a153be26823`, reproduction mode `upstream_faithful`, replacement
scope `none`

**Compiler/Build Flags**: implementation validation must record the actual compiler path/version,
CMake version/generator/build type, effective CMake definitions and project/upstream compile
options, actual configure/build commands, linked dependency identities, and wrapper/upstream binary
checksums. The successful Native Probe used GNU 11.4.0, CMake 3.22.1, Unix Makefiles, Release, and
the effective options recorded in `research.md`; those observed values are evidence, not a floating
substitute for the implementation BuildManifest.

**Dataset Identity**: repository-relative `sift1M_dataset/base_vector.bin`; little-endian float32,
`1,000,000 x 128`, L2, 512000008 bytes, SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`; query data is not used

**Performance Goals**: none; this plan produces no Recall, QPS, latency, memory-efficiency, or build
speed conclusion

**Constraints**: fixed `R=16`, `L=32`, `T=6` for the SIFT1M validation profile; local degree `10`;
float32/L2/unfiltered; labels, build PQ, and OPQ disabled; no upstream mutation; no Vamana copy; no
automatic parameter retry; no run larger than SIFT1M

**Scale/Scope**: one B0 invocation per explicit PartitionId; small fixtures for automated tests and
one SIFT1M-derived Partition for the non-parity structural gate

## B0 Boundary Decision

```yaml
b0:
  separability: separable
  binding_mode: upstream_callable_wrapper
  validation_scope: structural_and_contract
```

The wrapper composes exported upstream operations without reproducing their algorithm:

```text
validated Dataset-backed VectorAccessor + validated aggregate PartitionArtifact + explicit PartitionId
  -> resolve selected complete LocalToGlobalIdMap
  -> write native ID-map input in the unchanged map order
  -> upstream retrieve_shard_data_from_ids<float>
  -> validate reconstructed row count/order against Dataset + map
  -> upstream IndexWriteParametersBuilder(L, floor(2R/3))
       .with_filter_list_size(0) [resolves to L]
       .with_saturate_graph(false)
       .with_num_threads(T)
       [alpha/max occlusion remain upstream defaults]
  -> upstream Index<float>
  -> upstream Index::build(shard_filename, map_length)
  -> upstream Index::save(temporary_prefix)
  -> parse graph candidate and validate structure/lineage
  -> immutable LocalGraphArtifact publication
```

The selected map must be consumable in its existing order by the pinned retrieval callable.
Native P0 maps meet this condition because they are emitted in dataset traversal order. The
wrapper must not sort or relabel another map; a row-count or row-order mismatch fails closed.

## Minimal Contract Decision

The approved v1-compatible amendment leaves `LocalGraphBuilder::build` unchanged: it receives the
aggregate PartitionArtifact, explicit PartitionId, LocalBuildConfig, and StageContext. Each map is
mandatory, `shard_vectors_role` is optional, and the validated Dataset-backed
`StageContext::vector_accessor` supplies vectors when the selected record has no shard-vector
payload. Missing both sources fails before upstream work. Runtime materialization is B0 preparation
and is never represented as a P0 native output.

No shard ArtifactRef, inferred PartitionId, raw Dataset parameter path, process-global Dataset, or
upstream type is added to the public API. The descriptor may still declare the DatasetArtifact and
PartitionArtifact available-chain requirements plus `vector_accessor/v1`; execution uses the
existing service boundary. P0 and M0 representation are now compatible through optional centroids
and medoids payloads respectively.

The existing logical LocalGraphArtifact shape and aggregate-checksum projection remain unchanged.
The validator gains format-specific dispatch for the native float32 Dataset payload and the exact
`Index::save` local-graph payload; it does not convert the graph to Fake JSON or define another
algorithm. The map payload stays in the standard LocalToGlobal representation, while the temporary
native ID-map file is only an input to the upstream retrieval call.

The planned descriptor identity is
`cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1`, version `1.0.0`.
It is a formal, non-Fake B0 identity but is not assembled into the installed production runner by
this plan. Its parameter schema path is
`contracts/baseline-v1/schemas/cpp-main-b0-parameters.schema.json`.

## Constitution Check

*GATE: passed before design and re-checked after the contracts below.*

- [x] **Baseline immutability**: wrapper source and all outputs live outside
      `upstream/diskann-cpp-main`; upstream is only read and compiled out of tree.
- [x] **Pinned provenance**: every B0 admission binds the exact repository, branch, commit,
      upstream clean state, toolchain/build identity, Dataset checksum, Partition/map identity,
      and complete parameters.
- [x] **Implementation coexistence**: B0 has a stable cpp_main-specific identity and does not
      overwrite Fake, paper, Adapter, or upstream implementations.
- [x] **Node identity**: Dataset IDs are GlobalNodeIds; graph positions/neighbors are LocalNodeIds
      owned by one explicit PartitionId; the complete map is retained.
- [x] **Three-layer outputs**: LocalGraphArtifact, BuildManifest, and validation/evaluation evidence
      are distinct and separately referenced.
- [x] **Compatibility gate**: B0 declares the available Dataset/Partition requirements and
      `vector_accessor/v1`, then provides one LocalGraphArtifact; input and output validation
      surrounds factory/stage execution.
- [x] **Study/report separation**: all records remain `baseline_validation`; no paper or
      infrastructure-validation result is relabeled.
- [x] **Fair experiment protocol**: the only planned SIFT1M run is a retained structural gate with
      frozen inputs/parameters; no best-run selection, parameter search, repetitions, or parity
      inference is permitted.
- [x] **Validation and approval**: contract/unit/small-graph checks precede one SIFT1M structural
      validation; the reviewed public representation amendment changes no upstream behavior; no
      larger run is authorized.

Post-design result: **PASS** for planning. P0/M0 representation is compatible, while B0 native
format validation and Stage Binding remain unimplemented. There is no justified Constitution
violation.

## Project Structure

### Documentation (this Feature)

```text
specs/002-cpp-main-upstream-baseline/
├── spec.md
├── plan.md
├── research.md
├── source-mapping.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── b0-callable-wrapper.md
└── checklists/
    └── requirements.md
```

### Planned Source and Test Surface (repository root)

```text
upstream/
└── diskann-cpp-main/                  # fixed read-only source; never edited

cmake/
└── CppMainBaseline.cmake              # out-of-tree target wiring and identity checks

include/diskann_pipeline/
├── artifacts.hpp                      # optional shard-vector role in the v1 record
└── validation.hpp                     # vector-source precondition over validated input

src/baseline/cpp_main/
├── CMakeLists.txt
├── b0_callable_wrapper.cpp             # orchestration only; no Vamana implementation
├── b0_descriptor.cpp                   # Dataset + Partition requires; LocalGraph provides
└── native_format_reader.cpp             # DiskANN Dataset/local-graph structural reader

src/validation/
└── artifact_validator.cpp               # media-type dispatch to the reviewed native reader

contracts/baseline-v1/schemas/
├── cpp-main-b0-parameters.schema.json
└── diskann-float32-dataset-artifact.schema.json

tests/cpp/baseline/
├── test_b0_contract.cpp
├── test_b0_input_gate.cpp
├── test_b0_callable_wrapper.cpp
└── test_b0_structural_validation.cpp

experiments/configs/cpp-main-upstream-baseline/
└── b0-sift1m-structural.yaml           # one non-parity validation profile
```

**Structure Decision**: keep upstream source untouched and isolate all baseline-owned orchestration
under `src/baseline/cpp_main`. The CMake integration references the pinned source with an explicit
binary directory outside upstream and links the upstream `diskann_s` target; it does not add copied
Vamana source to a project target. Feature-specific native Dataset/parameter schemas live in a
separate runtime contract set. The shared v1 Artifact amendment is backward compatible and does not
alter the frozen Fake bytes, adjacency, or checksums.

## Minimal Delivery Sequence

### 1. Contract admission

- Use the reviewed optional-shard-vector and optional-centroids/medoids v1 representation; do not
  add another Local Build signature or raw Dataset path.
- Add a real float32 DiskANN-bin DatasetArtifact format and exact B0 parameter schema without
  modifying Feature 001's frozen Fake payload, adjacency, or checksums.
- Add reviewed native Dataset/local-graph media types and semantic parser dispatch while retaining
  the existing Artifact metadata shapes and checksum canonicalization.
- Declare B0 requires/provides, the ascending-map capability, stable parameter schema path, pinned
  source identity, and no production Adapter.
- Extend preflight so missing/invalid Dataset, Partition, PartitionId, map capability, version,
  checksum, ID space, or parameter set fails before factory creation.

### 2. Upstream callable wrapper

- Add read-only commit/cleanliness admission and out-of-tree upstream target wiring.
- Resolve the selected Partition record and exact map; never infer it from list position or path.
- Materialize all native temporary files only under StageContext temporary storage.
- Invoke the source-mapped upstream float retrieval and `Index<float>::build/save` sequence with
  exactly the frozen profile and upstream defaults.
- Check the existing cancellation token at stage start, after input validation, after shard
  validation, immediately after each non-interruptible upstream call returns, and before output
  publication. Do not patch upstream to inject an in-call cancellation hook.
- Treat the `Index::save` `.data` companion and reconstructed shard file as recorded temporary
  lifecycle files; publish only the standard map and local-graph payloads.

### 3. Structural and Contract gate

- Before B0: validate source/dataset/partition/map identity, map length/range/order compatibility,
  and complete parameters.
- After retrieval: validate native header, row count, dimension, finite float32 payload, and exact
  row-by-row equality to Dataset vectors selected by the unmodified map.
- After save: parse the native graph and validate node count, every LocalNodeId, and observed degree
  `<= floor(2R/3)`.
- Before publication: validate LocalGraphArtifact format/checksum/ID space/capabilities and exact
  Dataset/Partition/map lineage; invalid output is quarantined and cannot enter downstream work.

### 4. Minimal evidence

- Run C++17 public contract and preflight failure tests first.
- Run small-fixture wrapper and structural tests, including invalid PartitionId, incomplete/out-of-
  range/non-consumable map, wrong Dataset checksum, shard-order mismatch, invalid neighbor, wrong
  node count, degree overflow, and deterministic cancellation at wrapper-controlled checkpoints.
- After those pass, run exactly one retained SIFT1M-derived Partition B0 structural validation with
  `R=16`, `L=32`, `T=6`; record complete BuildManifest, Artifact, and ValidationEvidence.
- Report this only as B0 `structural_and_contract` evidence. Do not compare bytes or adjacency with
  Native Probe outputs, aggregate repetitions, run P0/M0, start formal Graph Parity, or register
  `validated_graph_baseline`.

## Requirement-to-Design Trace

| Requirement area | Design evidence |
|---|---|
| Fixed source/read-only build | `research.md`, `source-mapping.md`, out-of-tree CMake decision above |
| B0 separability and callable sequence | B0 Boundary Decision; `contracts/b0-callable-wrapper.md` |
| Dataset, PartitionId, map input and lineage | `data-model.md`; reviewed VectorAccessor fallback |
| Native parameters/defaults | `research.md` sections 3, 4, 10; wrapper contract |
| Structural validation | `data-model.md`; wrapper contract; quickstart expected findings |
| Artifact/Manifest/evidence separation | `data-model.md`; existing Feature 001 contracts |
| No parity/registration overclaim | Summary, Minimal Evidence, `quickstart.md` |

## Explicit Non-Goals

- P0 or M0 Stage Binding; their representation amendments are complete.
- Native-versus-wrapper byte, adjacency, process-state, or thread-schedule equivalence.
- Graph Parity protocol, repeated-run statistics, Recall, QPS, or latency.
- A new CLI, plugin loader, generic Adapter, copied Vamana implementation, or upstream patch.
- `validated_graph_baseline` registration or a complete Unified Graph Pipeline claim.

## Complexity Tracking

No Constitution violation is accepted. The backward-compatible v1 representation amendment keeps
the existing Local Build signature and uses its validated StageContext service boundary;
parameter-path and global-state alternatives remain rejected because they bypass validated
Artifact handoff.
