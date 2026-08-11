# Implementation Plan: OGP Partition Adaptation

**Branch**: `feature/opg-partition-adaptation` (the current Git branch's historical spelling; the
Feature short name and directory remain `ogp-partition-adaptation`) | **Date**: 2026-08-10 |
**Spec**: [spec.md](spec.md)

**Input**: the reduced Feature specification, the existing Pipeline and reviewed cpp_main B0/M0
bindings, and the source conclusions in [research.md](research.md) and
[source-mapping.md](source-mapping.md).

## Summary

Deliver one narrow vertical slice without creating another framework:

```text
validated DiskANN float32 DatasetArtifact
  -> pinned official GP-ANN Partition CLI (`OGP`)
  -> strict cluster validation and ascending native uint32 maps
  -> existing validated PartitionArtifact/v1
  -> existing cpp_main B0 once per PartitionId
  -> existing validated LocalGraphArtifact/v1[]
  -> existing cpp_main M0 once
  -> existing validated GlobalGraphArtifact/v1
  -> existing graph-integrity EvaluationReport
```

The official `Partition` subprocess is the only OGP integration. GP-ANN remains a private,
out-of-tree C++20 build; Feature-owned/public code remains C++17. The only public request-shape
change is an optional complete `StageSourceIdentities` value so the Partition manifest names GP-ANN
while Local Build and Merge manifests name DiskANN. The existing request-level source remains the
backward-compatible fallback. Stage-owned execution provenance uses the already existing
`StageResult.build_manifest_path`; its shape does not change.

The Feature does not introduce a callable/backend abstraction, new Artifact or report family,
detailed worker-observation model, new executor, OGP branch in B0/M0, generic Adapter, query
evaluator, parameter sweep, or dataset above SIFT1M.

## Technical Context

**Language/Version**: C++17 for project-owned public and wrapper code; the official GP-ANN build
privately uses C++20; Python 3.10+ for existing Harness configuration and runner invocation.

**Primary Dependencies**: existing `diskann_pipeline` Registry, validators, `PipelineExecutor`,
workspace/evidence stores, cpp_main vector accessor, B0 and M0; official GP-ANN repository
`https://github.com/larsgottesbueren/gp-ann.git`, branch `main`, commit
`df5efb4a6b303011c795666ed267f29d994b558c` and recursive dependencies in
`source-mapping.md`; fixed DiskANN `cpp_main` commit
`78256bbab4685e1774e78d331e081a153be26823`.

**Storage**: existing immutable Dataset/Partition/LocalGraph/GlobalGraph Artifacts and separate
BuildManifest, ValidationEvidence, runner result/quarantine record, and existing GlobalGraph
EvaluationReport. Each real OGP invocation writes one existing subordinate BuildManifest/v1 and
retained stdout/stderr/raw process evidence under confined workspace locations; the parent
Partition BuildManifest refers to the persisted subordinate manifest by path and SHA-256. No
Feature-local structural report or new evidence family is added.

**Testing**: CTest for public source fallback, legacy-empty and non-empty
`StageResult.build_manifest_path`, confined subordinate-manifest ingestion, OGP
source/parameter/cluster behavior (including Partition payload-checksum corruption), a small real
official Partition, and a small composed pipeline; pytest must launch the built
`ogp_experiment_runner` for the small composed run rather than reproduce orchestration; applicable
existing Pipeline/Baseline regressions include the C++ `fake_pipeline_e2e` before SIFT1M; one
frozen SIFT1M structural configuration follows only after all smaller gates pass.

**Target Platform**: Linux amd64 is normative for formal GP-ANN/KaMinPar execution. Other platforms
may run source-independent tests but cannot establish the formal real-process gate unless the exact
dependency build succeeds.

**Project Type**: one private Partition implementation plus one non-installed thin experiment
runner. The runner performs source admission, registration and runtime setup, then invokes the
existing executor exactly once.

**Study Class**: `single_paper_reproduction`; `reproduction_mode: faithful_adaptation`;
`replacement_scope: partition`.

**Baseline Reference**: Microsoft DiskANN `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`. Existing P0, B0, M0, and the fixed Baseline runner
remain unchanged.

**Dataset Identity**: formal SIFT1M input is `sift1M_dataset/base_vector.bin`, little-endian
float32, 1,000,000 rows, 128 dimensions, L2, 512,000,008 bytes, SHA-256
`8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`. Query data is outside
this Feature. The official CLI receives a confined byte-identical `.fbin` view because its reader
dispatches by suffix.

**Parameter Contract**: `requested_num_clusters`, finite non-negative `requested_overlap`, and
`partitioning_mode: default|strong`. Dynamic admission requires
`1 <= requested_num_clusters <= global_node_count` and overflow-safe overlap arithmetic. Epsilon is
not user-configurable; formal provenance records the source-fixed `0.05` value.

**Frozen SIFT1M Configuration**: one configuration is created only after the clean source build and
small real gates pass. Candidate OGP values are 40 requested clusters, overlap 0.2, and `default`
mode; Feature 002 B0/M0 parameters remain unchanged. An infrastructure retry preserves all frozen
fields, uses a new run ID, and records the earlier attempt and retry reason.

**Performance Goals**: none. The Feature makes no Recall, QPS, latency, throughput, resource
efficiency, Graph Parity, or query-performance claim.

**Scale/Scope**: fixtures → small real official OGP → small real OGP+B0+M0 → one frozen SIFT1M
configuration. No parameter sweep, changed-config retry, preferred-result selection, or larger
Dataset is authorized.

## Source Admission Gate

Formal build or execution requires an independent clean recursive checkout with:

```yaml
repository: https://github.com/larsgottesbueren/gp-ann.git
branch: main
commit: df5efb4a6b303011c795666ed267f29d994b558c
parlaylib: ea671f6fbae17e6d6cd25fe9b48134d3c0601787
kaminpar: a9e4b1f6d92b4110d4d4a31ee2e7dee5a0761b05
hnswlib: 359b2ba87358224963986f709e593d799064ace6
message_queue: f9b0856dde02248cc91a63077f7b6d5267004f25
```

Admission verifies root and recursive identities, clean state, out-of-tree configure/build paths,
actual toolchain/options, and official `Partition` executable checksum before OGP factory creation.
The unauthenticated research snapshot remains read-only evidence only. If the clean pinned source
cannot build without an upstream edit, the formal gate fails and records the blocker; no automatic
patch or PATH substitute is authorized.

The DiskANN gitlink, commit and clean state are checked independently before composing B0/M0. A
moving branch label never replaces either full commit.

### One admitted GP-ANN build topology

`cmake/GpAnnOgp.cmake` owns the Feature's sole official GP-ANN `Partition` out-of-tree build and
admission path. Source admission tests, the small real Partition test, the real Feature runner and
SIFT1M execution must consume that same admitted executable identity; a separately built or
PATH-resolved substitute is prohibited.

The canonical parent configure is:

```sh
cmake -S . -B build/ogp-partition-adaptation \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDISKANN_CPP_MAIN_BASELINE=ON \
  -DDISKANN_GP_ANN_OGP=ON \
  -DGPANN_SOURCE_DIR="$GPANN_SOURCE_ROOT"
```

Initial wiring registers only targets whose source files already exist. Whenever a later task
creates a Feature source, test or runner, that task updates the corresponding `CMakeLists.txt`,
registers its target/test, and reruns this canonical configure before the target's first build.

## Constitution Check

*GATE: PASS for the reduced design. Formal real-process work remains conditional on the external
GP-ANN source/build admission above.*

- [x] **Fixed Baseline**: `upstream/diskann-cpp-main` remains fixed/read-only and every build/output
      is out of tree.
- [x] **Coexisting implementations**: OGP has stable identity `gp_ann_ogp_partition_v1`; P0/B0/M0
      remain registered and unchanged.
- [x] **Explicit IDs**: GP-ANN point IDs are Dataset GlobalNodeIds; each map defines a complete
      Partition-local ID space; M0 emits only GlobalNodeIds.
- [x] **Three-layer evidence**: Artifacts, BuildManifests and the existing final EvaluationReport
      remain separate; ValidationEvidence remains the contract-validation record.
- [x] **Capabilities and handoffs**: OGP declares `requires`/`provides`; common gates validate every
      Artifact before downstream use.
- [x] **Scientific reporting**: the run remains a faithful single-paper Partition adaptation and
      cannot be reported as Baseline validation, full GP-ANN reproduction, or performance evidence.
- [x] **Ordered validation**: unit/contract tests precede small real tests and SIFT1M; no task or
      approval authorizes SIFT100M+.
- [x] **Fair attempts**: one SIFT1M configuration is frozen; all attempts are retained and a retry
      cannot change experimental fields.

Post-design result: **PASS with external source/build prerequisite**. There is no Constitution
exception, Baseline mutation, validator bypass, public format change, new report family, or new
execution flow.

## Architectural Boundaries

### 1. Official subprocess only

Launch the admitted executable without a shell:

```text
Partition INPUT.fbin OUTPUT_PREFIX REQUESTED_CLUSTERS OGP default|strong OVERLAP_TOKEN
```

Retain exact argv, source/build/executable identity, configured `PARLAY_NUM_THREADS`, stdout,
stderr, exit status, staged-input checksum, and expected-output checksum in the existing
subordinate BuildManifest/v1 and its retained evidence files. Do not claim observed Parlay or
KaMinPar worker counts because the selected CLI does not expose them. A nonzero exit, path escape,
missing/ambiguous output, or malformed file fails the Partition stage.

The wrapper accepts no epsilon field and records `effective_epsilon=0.05` with source
`upstream_cli_constant`. No direct callable or generic process backend is implemented.

### 2. Minimal optional stage sources

Add to `include/diskann_pipeline/pipeline.hpp`:

```cpp
struct StageSourceIdentities {
  SourceIdentity partition;
  SourceIdentity local_build;
  SourceIdentity merge;
};

std::optional<StageSourceIdentities> stage_sources;
```

One private selector in `pipeline_executor.cpp` chooses the stage member when present and existing
`request.source` when absent. BuildManifest schema/codecs do not change because each stage manifest
still contains one ordinary SourceIdentity. Partial mappings, generic maps, Registries and plugins
are prohibited.

`stage_sources` is authoritative only for per-stage `SourceIdentity`; it does not model or replace
toolchains. `source_config_json` retains its existing request-level run/config identity and exact
public shape:

```json
{
  "yaml_sha256": "<sha256>",
  "normalized_json_sha256": "<sha256>"
}
```

The same shared `source_config` may therefore appear in the parent Partition, B0 and M0
BuildManifests. It is not stage-specific build provenance and never contains the GP-ANN external
build identity. This Feature adds no stage-specific source-config schema,
`StageToolchainIdentities`, BuildManifest schema/codec change, or generic build-provenance
framework.

### 3. Existing stage-owned BuildManifest transport

The existing `StageResult.build_manifest_path` is the only transport for a stage-owned
implementation/execution BuildManifest:

- When empty, the executor preserves legacy behavior byte-for-byte at the parent Manifest
  parameter level; no `stage_execution_manifest` member is added.
- When non-empty, the path must be workspace-confined and identify an existing BuildManifest/v1
  produced by the current stage invocation. For either a successful or failed outer StageResult,
  the executor reads it before finalizing the parent Manifest, validates its current run/stage
  binding plus internally valid terminal status/failure shape, immutably persists its exact bytes
  into the current run manifest area, and computes SHA-256 over those persisted bytes. The
  subordinate status need not equal the outer status: subprocess `SUCCEEDED` followed by
  post-process rejection is explicitly valid.
- Only the corresponding parent Stage BuildManifest receives this additional member inside its
  existing object-valued `parameters`:

```json
{
  "stage_execution_manifest": {
    "path": "<workspace-relative persisted path>",
    "sha256": "<sha256>"
  }
}
```

- The reference never propagates to another stage. A malformed, missing, non-BuildManifest,
  mismatched or path-escaping subordinate document fails closed before downstream consumption.

This is a small generic executor helper for an existing StageResult field, not an OGP branch,
Registry, report type, metrics channel, lifecycle system or new provenance framework. Existing
stages returning an empty path, including B0 and M0, remain unchanged.

For OGP, the subordinate BuildManifest source is admitted GP-ANN and its toolchain is the actual
external C++20 build that produced the admitted `Partition` executable. It records the exact
source/dependency commits, compiler/version, CMake/generator/build type, effective options and
executable path/SHA-256. B0/M0 parent BuildManifests select DiskANN through `stage_sources`, retain
their existing cpp_main provenance, and do not reference the OGP subordinate manifest.

### 4. OGP Partition conversion

Resolve the already validated float32 Dataset payload, create a confined hard-link or exact-byte
copy ending `.fbin`, and recheck length/SHA-256 before launch. Parse one official output line as one
Partition using strict decimal uint32 tokens.

Before sorting, reject:

- an empty shard or malformed/trailing token;
- an ID outside `[0, global_node_count)`;
- a duplicate ID in the same shard;
- any Dataset GlobalNodeId missing from the union.

Then retain cross-shard replicas, sort each shard ascending, assign contiguous effective
PartitionIds, and write the existing cpp_main native uint32 maps. Publish one map-only
`PartitionArtifact/v1` with capabilities:

- `partition.complete_local_to_global_map/v1`;
- `partition.local_to_global_ascending/v1`;
- `partition.overlap/v1`.

Do not emit shard vectors or centroids. OGP-specific pre-publication checks do not replace or modify
the common Validator, which remains the formal publication gate.

### 5. Existing executor and Baseline isolation

The non-installed Feature runner registers existing P0/B0/M0 plus OGP in a production-admission
Registry, selects OGP+B0+M0, provides GP-ANN/DiskANN/DiskANN stage sources, configures existing
runtime services, and calls `PipelineExecutor` once. It does not implement a stage loop.

The executor validates the Dataset and chain before factory creation, validates OGP output before
B0, invokes B0 once per explicit effective PartitionId, validates every LocalGraph before M0, then
validates/evaluates the GlobalGraph. The installed fixed Baseline runner is unchanged and still
admits only P0+B0+M0.

### 6. Evidence without a new report family

Requested parameters and the fixed epsilon belong to existing run/BuildManifest provenance.
Effective Partition count and shard sizes are already in validated Partition Artifact metadata;
assignment totals and amplification are derivable from those values when later evaluation needs
them. Configured subprocess threads are recorded as configured resources; unavailable observations
are simply not claimed.

For every subprocess actually started, the authoritative argv/stdout/stderr/exit/output evidence is
the OGP subordinate BuildManifest plus its confined retained files. That existing BuildManifest/v1
records at least exact argv, `effective_epsilon=0.05`, configured `PARLAY_NUM_THREADS`, staged input
path/checksum, stdout and stderr paths/checksums, admitted executable path/SHA-256, exit status, and
the expected output path/checksum when it exists. The parent Partition BuildManifest binds it only
through `parameters.stage_execution_manifest {path, sha256}`.

Lifecycle separation is fixed:

1. Exit zero produces a subordinate `SUCCEEDED` manifest; later successful parsing/publication is
   recorded by the parent Partition Manifest.
2. A nonzero process exit produces a subordinate `FAILED` manifest with the actual
   `failure.exit_code`; stdout/stderr remain retained and the parent Partition stage fails.
3. Exit zero followed by cluster parsing, OGP invariant or Artifact validation failure leaves the
   subprocess manifest `SUCCEEDED`, while the parent Partition Manifest records `FAILED` or
   `INVALID_OUTPUT` as appropriate.

Source admission failure before process launch creates no subordinate subprocess Manifest and uses
the existing admission/runner failure evidence.

The MVP adds no `OgpStructuralReport`, worker completeness state, StageResult metrics, or public
EvaluationReport extension. After M0, the existing GlobalGraph EvaluationReport remains the only
evaluation document produced by this Feature.

### 7. SIFT1M attempts

One immutable experiment configuration binds Dataset checksum, both source/build identities,
implementations, all parameters, resource budget, retention rule and output locations. Each attempt
adds a unique run ID and terminal evidence without mutating the configuration. A retry is allowed
only for a recorded infrastructure/external execution failure and must preserve all experimental
fields. Failed/invalid attempts remain excluded from successful evidence and aggregation.

## Validation Sequence

1. **Source/build admission**: exact clean source/dependency identities, out-of-tree build, actual
   executable identity, and unchanged DiskANN gitlink.
2. **Contract and parser tests**: descriptor/parameters, stage-source fallback/split, unchanged
   shared two-digest `source_config`, empty/non-empty stage-owned Manifest transport,
   path-escape/malformed subordinate rejection, strict cluster parsing, duplicate-before-sort,
   range, coverage, order, overlap, Dataset staging, and an Artifact payload-checksum corruption
   case rejected by the existing Validator before B0/M0.
3. **Small real Partition**: prove the admitted official CLI created a common-validator-accepted
   `PartitionArtifact/v1` and invalid output cannot start B0/M0.
4. **Small real composed closure**: build and launch the real `ogp_experiment_runner` through the
   Python Harness and prove one official OGP, B0 per effective Partition, one M0,
   validation-before-consumption, final lineage and fixed Baseline runner isolation. A test-only
   in-process chain is supporting coverage, not a substitute for this runner path.
5. **Applicable regressions**: public C++17 request contract, Registry/preflight, Artifact
   validation, Pipeline failure gates, Feature 002 B0/M0/pipeline, C++ `fake_pipeline_e2e`, and
   Python safe config/Fake Harness/assembly. These are blocking before SIFT1M.
6. **SIFT1M**: freeze one configuration, retain each attempt, validate the complete successful
   chain if produced, and report no query/performance conclusion.
7. **Human diff review**: review semantic changes and SIFT1M evidence before any future separately
   authorized SIFT100M work; this Feature contains no larger-scale task.

## Project Structure

### Documentation for this Feature

```text
specs/003-ogp-partition-adaptation/
├── spec.md
├── plan.md
├── research.md
├── source-mapping.md
├── data-model.md
├── quickstart.md
├── contracts/
│   ├── README.md
│   ├── official-partition-subprocess.md
│   ├── stage-sources.md
│   └── ogp-parameters.schema.json
└── checklists/
    └── requirements.md
```

### Planned source and test surface

```text
upstream/diskann-cpp-main/                 # fixed read-only Baseline

cmake/GpAnnOgp.cmake                      # out-of-tree official CLI build/admission

include/diskann_pipeline/pipeline.hpp      # optional StageSourceIdentities only
src/pipeline/pipeline_executor.cpp         # private source selector + existing subordinate-Manifest ingestion

src/papers/gp_ann/ogp/
├── CMakeLists.txt
├── stage_binding.hpp
├── descriptor.cpp
├── source_build_identity.hpp
├── source_build_identity.cpp
├── official_partition_process.cpp
├── cluster_output.cpp
├── ogp_partition.cpp
└── ogp_experiment_runner.cpp

harness/python/diskann_harness/
└── ogp_partition_adaptation.py

contracts/papers/gp-ann-ogp-v1/
├── SOURCES.json
└── schemas/ogp-parameters.schema.json

experiments/configs/ogp-partition-adaptation/
└── sift1m-structural.yaml

tests/cpp/integration/test_stage_source_provenance.cpp
tests/cpp/papers/gp_ann/
├── CMakeLists.txt
├── test_ogp_partition.cpp               # descriptor, admission, parser, small real OGP
└── test_ogp_pipeline.cpp                # small composed closure and isolation
tests/python/test_ogp_partition_adaptation.py
```

No planned file copies GP/OGP/KaMinPar/Vamana/Merge algorithm code or writes under either admitted
source tree. Public Artifact, BuildManifest, EvaluationReport and Validator schemas remain
unchanged.

## Minimal Delivery Sequence

1. Admit and build the exact official GP-ANN `Partition` executable out of tree.
2. Add and test the optional complete stage-source set and existing stage-owned Manifest transport.
3. Implement the OGP descriptor, parameter schema and subprocess boundary.
4. Implement strict cluster conversion and existing Partition Artifact publication.
5. Prove the small real official Partition independently.
6. Register OGP with existing P0/B0/M0 and prove the small composed closure.
7. Run applicable regressions.
8. Freeze and execute the bounded SIFT1M structural configuration, retaining every attempt.

The MVP is steps 1–6. SIFT1M validates the completed MVP; it does not compensate for missing
small-data correctness.

## Requirement-to-Design Trace

| Requirements | Design evidence |
|---|---|
| FR-001–FR-004 | Source Admission Gate; official subprocess boundary; source mapping |
| FR-005–FR-006 | Parameter contract; descriptor and stage-source contract |
| FR-007–FR-009 | OGP Partition conversion; existing publication/compatibility gates |
| FR-010–FR-011 | Existing executor/Baseline isolation; minimal stage sources |
| FR-012 | Per-stage SourceIdentity selection; existing subordinate BuildManifest transport; evidence-without-new-report decision |
| FR-013 | Validation Sequence, real runner closure, checksum negative case and pre-SIFT regression surface |
| FR-014–FR-015 | SIFT1M attempt model; explicit exclusions |

## Complexity Tracking

The reduced design adds one paper Partition implementation, one official subprocess boundary, one
backward-compatible optional source aggregate, and a private executor helper that activates the
already existing `StageResult.build_manifest_path`. Every other behavior reuses an existing
contract, validator, executor, runner protocol, B0/M0 binding, evidence store or evaluator.

Deferred until a real second consumer exists: callable/backend hierarchy, generic Adapter/source
framework, public Partition EvaluationReport, OGP structural report, worker completeness model,
automatic retry/sweep scheduler, query pipeline and datasets above SIFT1M. Reintroducing any of
these requires a separate spec/contract decision rather than an incidental implementation task.
