# Implementation Plan: XN-Descent Local Build

**Branch**: `feature/xn-descent-local-build` | **Date**: 2026-09-07 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/007-xn-descent-local-build/spec.md`

## Summary

Add an independently selectable B1 local-build implementation of XN-Graph Algorithm 1. B1 maps a
validated P0 Partition to the paper's set `S`, constructs deterministic `C`, `R`, and `G` state in
LocalNodeId space, and publishes the existing LocalGraphArtifact/v1 with its complete unchanged
LocalToGlobal map. The design preserves Equation (2), all cross-`TryInsert` effects, and within-node
working-`G` visibility, while using deterministic synchronous rounds, immediate full normalized
`C/R/G` fixed-point convergence, and proved three-round `C/G` stability across a possible `R`
two-cycle as faithful adaptations.

The minimal integration is an isolated `src/papers/xn_graph/xn_descent/` module. A Feature-local
runner preflights `pipeline-compatible` requests before invoking the existing P0 → B1 → M0
executor, and owns a separate local-only lifecycle for `paper-style lambda=64` because the generic
executor has no local-only mode. Existing public pipeline interfaces, Artifact schemas, P0, B0,
M0, production Registry, Harness, Loop Engine, and pinned upstream source remain unchanged.

## Technical Context

**Language/Version**: C++17; JSON Schema 2020-12 for the Feature-local parameter contract

**Primary Dependencies**: existing `diskann_pipeline_core`, nlohmann/json, OpenSSL SHA-256,
C++ standard library (`std::thread`); existing pinned cpp_main stages only for pipeline closure

**Storage**: existing filesystem workspace, JSON Artifact/BuildManifest/ValidationEvidence metadata,
reviewed cpp_main native local-graph and LocalToGlobal map payloads

**Testing**: existing CMake/CTest C++ unit, contract, paper semantic, and pipeline test pattern;
planned Feature runner validation for SIFT1M and Linux replay

**Target Platform**: macOS local development and Linux amd64 little-endian validation; no
macOS-specific implementation; native M0 closure is restricted to an admitted reviewed native
format environment

**Build execution boundary**: macOS `semantic-small` uses `DISKANN_XN_DESCENT=ON` with
`DISKANN_CPP_MAIN_BASELINE=OFF`; Linux `pipeline-compatible` uses both options `ON`. This corrects
the build dependency boundary only and does not modify XN-Descent algorithm semantics.

**Project Type**: modular C++ library plus Feature-local command-line validation runner

**Study Class**: `single_paper_reproduction`, component-level `faithful_adaptation`

**Baseline Reference**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`; Baseline remains `upstream_faithful`, while B1 has a
separate implementation identity and reproduction mode

**Compiler/Build Flags**: the repository's existing C++17 CMake flow; exact compiler path/version,
CMake version/generator, build type, definitions, compile commands, and link commands are
run-time provenance and must be recorded rather than inferred in this plan

**Dataset Identity**: deterministic small fixtures first; SIFT1M only after prior gates, with exact
Dataset/checksum and, when search evaluation applies, query set/ground truth/metric fixed in the
run declaration; admitted B1 distance is current squared L2

**Performance Goals**: semantic fidelity, exact within-environment replay, and budget compliance;
no construction-time, Recall, hop, latency, or QPS target is claimed by this Feature

**Constraints**: `alpha` finite and `>=1`; positive `omega`, `lambda`, cap, memory, and threads;
`C<=omega`, `G/out-degree<=lambda`; unbounded logical `R`; `pipeline-compatible lambda<=10`;
`paper-style lambda=64` local-only; cap without fixed point fails and publishes no Artifact

**Scale/Scope**: one selected P0 Partition resident for Algorithm 1; semantic fixtures through
SIFT1M validation; no BBP, Secondary Index, IMF Search, SSD layout, parameter sweep, or dataset
larger than SIFT1M

## Constitution Check — Before Phase 0

| Gate | Result | Evidence in this plan |
|---|---|---|
| Baseline immutability | PASS | All new code is project-owned; upstream is read-only and builds remain out-of-tree. |
| Pinned provenance | PASS | Full repository/branch/commit is fixed; complete toolchain, flags, Dataset checksum, and parameters are required in manifests. |
| Implementation coexistence | PASS | B1 has a distinct identity and a Feature-local Registry; B0 remains available and unchanged. |
| Node identity | PASS | B1 consumes one explicit PartitionId, operates in LocalNodeId, and copies the complete owning map. |
| Three-layer outputs | PASS | Artifact, BuildManifest/validation evidence, and EvaluationReport retain distinct lifecycles. |
| Compatibility gate | PASS | Descriptor requirements/provisions and validators are reused; the M0 degree relation is preflighted before stage invocation. |
| Study/report separation | PASS | Component faithful adaptation, Baseline, and any later cross-paper study remain separately identified. |
| Fair experiment protocol | PASS | No tuning is in scope; any later comparison must predeclare fixed inputs, resources, repeats, and aggregation. |
| Validation and approval | PASS | Unit, contract, oracle, small pipeline, SIFT1M, human review, and Linux replay are planned; larger runs remain gated. |

No gate exception is required.

## Technical Design

### Algorithm core

The core owns the XN parameter value object and the deterministic Algorithm-1 state machine. Each
round snapshots `C` and `R`, builds canonical candidates, mutates only node-owned working `G`, and
reduces cross-`C` and reverse-`R` proposals at the next boundary. The core exposes normalized round
evidence, full-state fixed-point status, consecutive stable-`C/G` count, and the selected
convergence condition, together with either a converged graph or a failure termination. It does
not know Artifact paths, M0, Registry, or experiment reporting.

The concrete PRNG, seed derivation, owner-relative `(distance, LocalNodeId)` order, cross-proposal
reduction, immediate full `C/R/G` fixed point, three-round `C/G` stability for a possible `R`
two-cycle, overflow-safe `alpha^2` comparison, and deterministic local medoid are frozen in
[research.md](./research.md). Private container and worker-chunk choices remain implementation
details only when they preserve that observable contract.

### B1 stage binding

`xn_graph_xn_descent_local_build_v1@1.0.0` implements the existing `LocalGraphBuilder`. It validates
the aggregate Partition and selected map, maps LocalNodeIds through the map for `VectorAccessor`,
and uses only the declared squared-L2 service. Before major allocation it checks known memory with
overflow-safe arithmetic; it continuously prevents thread-budget excess and performs exact
round-boundary capacity checks for unbounded `R`.

On convergence, the stage writes the existing native local graph in LocalNodeId space and copies
the selected native map unchanged. It sets:

- `structural.declared_max_degree = lambda`;
- `structural.observed_max_degree =` payload actual maximum;
- native header maximum = the same observed maximum;
- entry point = deterministic local medoid.

It returns a candidate only after producer-specific checks reject self edges, duplicate edges,
range errors, degree errors, or inconsistent entry point. The existing Validator still owns formal
Artifact admission. A stage-owned BuildManifest records effective policies, resources,
environment, source identities, per-round/final convergence evidence, and exact termination.

### Descriptor and contracts

The B1 descriptor requires:

- Dataset `diskann-float32-dataset/v1`, ID space `none`;
- Partition `partition-artifact/v1`, ID space `local`;
- capabilities `dataset.diskann_float32_bin/v1` and
  `partition.complete_local_to_global_map/v1`;
- services `vector_accessor/v1`, `distance_function/l2-v1`,
  `cancellation_token/v1`, and `metrics_sink/v1`.

It provides LocalGraph `local-graph-artifact/v1`, ID space `local`, and
`graph.local_adjacency/v1`. It does not require P0 centroids or ascending-map capability for the
algorithm. The Feature-local parameter schema is the normative design source; implementation later
copies it byte-for-byte to a stable `contracts/papers/xn-graph-xn-descent-v1/` path and records the
same source-provenance pattern as existing paper modules. No public Artifact, Manifest, ID, or
capability schema changes.

Because current M0 admits the reviewed cpp_main native local graph and map encodings, B1 uses that
byte layout. The B1 implementation remains a separate algorithm identity; its manifest separately
binds the project source and pinned DiskANN format-source identity. The Feature-local contract
narrows this use to the exact role/media/layout accepted by current validators and M0.

### Execution paths

`pipeline-compatible` uses a Feature-local runner that validates the complete request and exact
P0/B1/M0 composition before constructing the Registry or invoking `PipelineExecutor`. A request
with the current M0 and `lambda>10`, a non-pipeline profile, or mismatched identity fails with P0,
B1, and M0 invocation counts all zero. Accepted requests register existing P0, new B1, and existing
M0 only, then reuse the generic executor and its normal validation/publication lifecycle.

`semantic-small` validates the core and stage through unit, oracle, and contract fixtures.

`paper-style` uses a distinct Feature-local local-only runner operation accepting validated Dataset
and Partition references. It requires `lambda=64`, forbids a Merge selection, invokes only B1, and
reuses Validator and atomic-publication rules through a small Feature-local lifecycle owner. This
is the sole intentional runner-specific lifecycle code: current `PipelineExecutor` cannot express
local-only. No fake Merge, GlobalGraph, generic pipeline mode, or production Registry entry is
created.

### Failure and evidence

Invalid parameters/composition/input/budget fail before algorithm work. Cancellation, non-finite
distance, checked-size overflow, allocation failure, runtime budget exhaustion, or cap-limited
non-convergence returns an existing non-success `StageStatus` with no output candidate. A cap hit
uses `FAILED` plus stable `termination_reason: iteration_cap_nonconverged`; adding a public status is
unnecessary. Only the separate BuildManifest and scalar/digest diagnostics remain. Partial graph
state is never stored as a valid LocalGraphArtifact, passed to M0/evaluation, or cached.

## Project Structure

### Documentation created in this Phase

```text
specs/007-xn-descent-local-build/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── source-mapping.md
└── contracts/
    ├── README.md
    ├── xn-descent-execution-contract.md
    └── xn-descent-parameters.schema.json
```

`tasks.md` is intentionally absent and belongs to the later `/speckit-tasks` phase.

### Expected implementation changes after task approval

```text
CMakeLists.txt                                      # add one opt-in feature switch/subdirectory
src/CMakeLists.txt                                  # include the selected paper module only
src/papers/xn_graph/xn_descent/
├── CMakeLists.txt
├── stage_binding.hpp
├── descriptor.cpp
├── parameters.hpp/.cpp
├── xn_descent_algorithm.hpp/.cpp
├── xn_descent_local_build.cpp
└── xn_descent_experiment_runner.cpp

contracts/papers/xn-graph-xn-descent-v1/
├── SOURCES.json
└── schemas/xn-descent-parameters.schema.json       # byte-identical runtime copy

tests/CMakeLists.txt                                # add selected paper tests
tests/cpp/papers/xn_graph/
├── xn_descent_parameters_test.cpp
├── xn_descent_equation_try_insert_test.cpp
├── xn_descent_round_oracle_test.cpp
├── xn_descent_determinism_test.cpp
├── xn_descent_local_artifact_test.cpp
└── xn_descent_pipeline_test.cpp
```

The exact split among private `.hpp/.cpp` files may be consolidated during task generation; the
directories and responsibilities are the planned change surface. If the runner's isolated
local-only lifecycle is easier to test as a second private translation unit, it remains inside the
same paper module and does not become a public API.

**Structure decision**: Use the established paper-module, paper-contract-copy, Feature-local
runner, and `tests/cpp/papers/...` patterns. Do not add an Adapter, Artifact version, generic
pipeline/harness/loop abstraction, placeholder directory, or upstream file.

## Constitution Check — After Phase 1 Design

| Gate | Result | Design verification |
|---|---|---|
| Baseline immutability | PASS | Source mapping reads pinned upstream only; planned writers/builds live outside `upstream/`. |
| Pinned provenance | PASS | The native layout source and M0 are pinned by full commit; stage manifests require exact project/toolchain/input/parameter identities. |
| Implementation coexistence | PASS | B1 is additive, selectable, and Feature-local; B0 and all existing identities are unchanged. |
| Node identity | PASS | Data model fixes Partition-scoped LocalNodeId, complete copied map, and legal local medoid. |
| Three-layer outputs | PASS | Data model and execution contract forbid quality/performance data in Artifact or build provenance in EvaluationReport. |
| Compatibility gate | PASS | Descriptor and parameter contracts are explicit; candidate validation precedes publication/downstream; M0 numeric compatibility is runner-preflighted. |
| Study/report separation | PASS | All three profiles carry claim boundaries; `paper-style` cannot masquerade as the Baseline-compatible pipeline or full XN system. |
| Fair experiment protocol | PASS | Quickstart plans fixed replays and records platform differences; no parameter search or selective aggregation. |
| Validation and approval | PASS | Quickstart includes ordered semantic, contract, closure, SIFT1M, human-review, and Linux gates, with no larger run. |

Phase 1 introduces no constitution violation, so no complexity exception is tracked.
