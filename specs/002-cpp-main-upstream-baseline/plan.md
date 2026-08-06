# Implementation Plan: cpp_main Upstream P0+B0+M0 Graph Closure

**Branch**: feature/cpp-main-upstream-baseline | **Date**: 2026-08-06 | **Spec**: [spec.md](spec.md)

**Input**: the Feature specification, the approved Gate A decision, the commit-bound
[source mapping](source-mapping.md), and the completed Native Probe evidence in
[research.md](research.md).

## Summary

Implement the smallest Unified graph-build path that calls the pinned, unchanged cpp_main
implementation for all three stages:

~~~text
DiskANN float32 Dataset
  -> P0 partition_with_ram_budget<float>
  -> map-only PartitionArtifact with centroids
  -> B0 per explicit PartitionId:
       retrieve_shard_data_from_ids<float>
       -> Index<float>::build
       -> Index<float>::save
  -> complete LocalGraphArtifact set
  -> M0 merge_shards
  -> GlobalGraphArtifact with merged graph and medoids
~~~

All algorithm calls link the real upstream target built outside the source tree. Project-owned code
is limited to Registry descriptors, orchestration, immutable Artifact publication, source/build
identity capture, and cpp_main-specific native-format parsing and structural validation. It does
not copy Vamana or merge logic, modify upstream control flow or randomness, define a generic
Adapter, or change the public C++17 contracts approved in Feature 001.

This first implementation round is a structural-and-contract closure, not formal Graph Parity. It
ends when the public Pipeline can execute P0, every B0 selected by explicit PartitionId, and M0 on
the fixed SIFT1M Dataset under the unchanged draft Profile; the sharded condition is satisfied, at least two Partitions are produced,
the final graph and medoids form a valid GlobalGraphArtifact, and upstream remains clean.
Formal parity remains not reached and validated_graph_baseline registration remains prohibited.

## Gate A Final Decision

~~~yaml
p0:
  separability: separable
  contract_status: compatible
  availability: planned
b0:
  separability: separable
  contract_status: compatible
  availability: planned
  binding_mode: upstream_callable_wrapper
  validation_scope: structural_and_contract
m0:
  separability: separable
  contract_status: compatible
  availability: planned
native_reference_reachable: yes
unified_graph_parity_reachable: yes
formal_gate_status: not_reached
baseline_registration: prohibited
~~~

Reachability is an architectural admission conclusion. This plan implements Stage Bindings and
structural evidence only. It does not freeze or execute a Graph Parity Protocol. Incomplete
exact-byte Native Probe capture is therefore a later comparison-strategy question, not a stage
implementation blocker. The YAML block above is retained Gate A design evidence; its status fields
are not fields of `unified-sharded-report.json`.

## Technical Context

**Language/Version**: C++17 for the public Pipeline and all Feature-owned binding code.

**Primary Dependencies**: fixed upstream source at
https://github.com/microsoft/DiskANN.git, branch cpp_main, commit
78256bbab4685e1774e78d331e081a153be26823; upstream CMake target diskann_s; the
existing diskann_pipeline core, Registry, preflight, Artifact validator, StageContext, and Pipeline
executor; upstream-resolved OpenMP, MKL, tcmalloc, aio, and platform runtime dependencies.

**Storage**: immutable filesystem Artifacts, separate build/stage BuildManifests, structural
ValidationEvidence, a separate graph-integrity EvaluationReport, immutable normalized requests,
and stage-temporary native files. Every build, temporary, output, log, and test path is outside
upstream/diskann-cpp-main.

**Native Formats**: Feature-local readers for the exact pinned layouts of the DiskANN float32
Dataset binary, uint32 Partition ID map, local graph, local graph .data companion, merged global
graph, centroids, and medoids. These readers are selected only for cpp_main media types; they do
not add a public Contract version, general sidecar model, or Adapter API.

**Testing**: CTest for source/build admission, native-format readers, descriptor/preflight gates,
P0, B0, M0, the narrow cpp_main native graph-integrity evaluator path, and a small complete
Pipeline; pytest for the Feature-local preflight/execute lifecycle and closure report; one retained
SIFT1M Unified structural run after targeted tests pass. No repeated-run statistics or parity
comparison is planned.

**Target Platform**: Linux amd64 is normative. macOS may run tests that do not require the upstream
Linux dependency surface but cannot establish the SIFT1M Baseline structural gate.

**Project Type**: three formal implementation factories assembled into one Feature-local,
non-installed cpp_main Baseline runner. The runner constructs a Production-admission Registry and
invokes the existing public Pipeline executor. A Feature-local Python entrypoint reuses the existing
safe YAML loader and workspace primitives. Its `preflight` mode validates one baseline_validation
request against a Feature-local schema and writes immutable normalized JSON without loading the
runner; its `execute --request` mode invokes that runner once. It does not change the Feature 001
infrastructure-validation request contract. The assembly is not the validated Baseline catalog and
is not installed as a production scientific pipeline.

**Study Class**: baseline_validation.

**Source Identity**: repository, branch, full commit, parent gitlink, submodule HEAD, and
before/after upstream clean state are mandatory admission inputs, never a floating cpp_main label.

**Build Identity**: record actual compiler path/version, CMake version/generator/build type,
effective CMake definitions, project and upstream compile options, actual configure/build commands,
linked dependency identities, relevant OpenMP/MKL conditions, and checksums of the linked upstream
library and Feature-owned runner.

**Dataset Identity**: sift1M_dataset/base_vector.bin; little-endian float32; 1,000,000 rows by 128
dimensions; L2; 512,000,008 bytes; SHA-256
8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e. Query data is not read.

**SIFT1M Draft Graph Profile**: `experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml`
with `status: draft`; R=16, L=32, M=0.6 GiB, T=6, replication factor=2,
P0 sample probability=min(1, 256000/N)=0.256, local degree=floor(2R/3)=10,
float32/L2/unfiltered, labels disabled, build PQ disabled, disk PQ disabled, OPQ disabled.
The Native Reference also records B=0.03 and QD=0, but those top-level PQ-preparation inputs do not
become P0, B0, or M0 algorithm parameters.

**Sharded Admission**: recompute the source-verified strict predicate using the actual compiled ABI,
fixed Dataset identity, and draft Profile values. The known probe identity gives estimated full-index RAM
707,519,996.643066406 bytes versus M budget 644,245,094.4 bytes, so the strict single-build
predicate is false. Unified P0 must additionally return at least two Partitions; the observed Native
Probe value of three is evidence, not a hardcoded expected count for a random P0 run.

**Performance Goals**: none. No Recall, QPS, latency, throughput, memory-efficiency, or build-speed
claim is produced.

**Scale/Scope**: automated tests use small deterministic format fixtures where algorithm output
identity is not asserted. The only full-data execution in this round is SIFT1M; no larger run is
authorized.

## Constitution Check

*GATE: passed before design and re-checked after the Phase 1 artifacts below.*

- [x] **Read-only fixed Baseline**: upstream is admitted only at the exact gitlink/HEAD/commit and
      compiled with an explicit external binary directory. No Feature output targets upstream.
- [x] **Complete provenance**: source, toolchain, build options, Dataset checksum, complete stage
      parameters, environment, commands, inputs, outputs, timestamps, and status are retained.
- [x] **Coexisting implementations**: P0, B0, and M0 use stable cpp_main-specific identities and do
      not overwrite Fake, paper, Adapter, or upstream implementations.
- [x] **ID spaces and maps**: Dataset IDs and M0 neighbors are GlobalNodeIds. B0 node positions and
      neighbors are LocalNodeIds owned by one explicit PartitionId. Every Partition retains its
      complete LocalToGlobalIdMap.
- [x] **Separate record layers**: Artifact payload/metadata, BuildManifest generation provenance,
      and structural validation evidence remain separate immutable records.
- [x] **Requires/provides and validation gate**: all descriptors declare their exact multi-input
      requirements, output, versions, ID spaces, capabilities, services, and parameter schema.
      Preflight rejects incompatible chains before factory creation; invalid outputs cannot reach
      the next stage.
- [x] **Faithful stage semantics**: the wrappers invoke partition_with_ram_budget,
      retrieve_shard_data_from_ids plus Index build/save, and merge_shards directly. M0 is not
      described as pruning, RobustPrune, refinement, or connectivity repair.
- [x] **Fair scope**: one retained structural run uses the unchanged draft Profile without automatic retry,
      parameter search, best-run selection, or parity inference.
- [x] **Test and scale gates**: native-format, unit, small-pipeline, and failure tests precede
      SIFT1M. No experiment beyond SIFT1M is planned.

Post-design result: **PASS**. No public Contract amendment or Constitution exception is required.
The formal Graph Parity gate remains not_reached until a later protocol is explicitly frozen and
executed.

## Architectural Boundaries

### Existing public contract remains unchanged

- P0 implements the existing Partitioner interface and consumes a Dataset ArtifactRef.
- B0 implements the existing LocalGraphBuilder interface and consumes an aggregate
  PartitionArtifact ArtifactRef, explicit PartitionId, LocalBuildConfig, and StageContext.
- M0 implements the existing GraphMerger interface and consumes the validated aggregate
  PartitionArtifact plus the complete LocalGraphArtifact collection.
- Existing ArtifactRef, DatasetIdentity, PartitionArtifact, LocalGraphArtifact,
  GlobalGraphArtifact, StageResult, StageContext, Registry, capability checker, validator,
  checksum, quarantine, and executor contracts remain authoritative.
- The already-approved optional shard-vector, optional centroids, and optional medoids
  representation is used as-is. No Contract v2 is created.

### Baseline-local format dispatch

The internal validator may dispatch by the existing artifact type/format version and payload media
type to a fixed cpp_main reader. The dispatch table contains only reviewed Feature 001 formats and
the explicitly listed cpp_main formats. It is not a plugin loader or a general Adapter mechanism.
The logical Artifact formats for Partition, LocalGraph, and GlobalGraph remain their public v1
formats; only their payload encodings are cpp_main native bytes.

The local .data file from Index::save is validated as temporary B0 lifecycle evidence and recorded
in the BuildManifest. It is not added as a LocalGraphArtifact payload because M0 does not consume
it. Likewise, a shard-vector file materialized by B0 is temporary preparation and is never
published as P0 output.

The Feature-local runner resolves the already validated Dataset Artifact once and constructs a
private cpp_main float32 VectorAccessor bound to that exact native payload and checksum. P0 resolves
the same payload from its Dataset ArtifactRef. B0 verifies the private native-backed accessor and
uses its confined read-only path only as the filename argument to
retrieve_shard_data_from_ids. No native-path method is added to the public VectorAccessor, and no
path is accepted through stage parameters or process-global state.

### Minimal execution assembly

The three formal descriptors are registered only in the Feature-local cpp_main runner using
RegistryAdmissionPolicy::kProduction. The runner is non-installed during this structural round and
drives the existing Pipeline executor. Static factory registration proves implementation
selection; it does not register validated_graph_baseline. No new plugin, loader, scheduler, retry
engine, state database, IPC protocol, or public CLI abstraction is introduced. The baseline-local
normalizer reuses the established duplicate-key-rejecting SafeLoader and JSON-compatible tree
rules; it neither reuses nor weakens Feature 001's Fake-only run-request schema.

### Narrow cpp_main native graph-integrity evaluator path

The current public executor's `fake_graph_integrity_evaluator` path reads the `global_graph`
payload as a JSON adjacency document and therefore cannot read cpp_main's native binary merged
graph. The existing Fake JSON path and its evaluator identity remain unchanged.

The existing public internal evaluator implementation in `src/pipeline/evaluator.cpp` adds one
explicit, closed cpp_main conditional branch. It is not runner-private code, although only the
Feature-local cpp_main runner's successful path produces matching input. The branch writes the
following evaluator object:

~~~yaml
implementation_id: cpp_main_native_graph_integrity_evaluator
version: 1.0.0
~~~

The branch accepts only a native GlobalGraphArtifact that has already passed the cpp_main Artifact
validator, has GlobalNodeId space and the required global-adjacency capability, and declares the
single exact reviewed cpp_main global-graph media type. Fixed source identity is guaranteed before
this branch by source admission, normalized request identity, and the corresponding BuildManifest.
It generates a separate EvaluationReport from the ValidatedArtifact structural claims and their
corresponding ValidationEvidence; it does not reparse the native graph payload or implement another
native parser. The report retains graph integrity, logical node count, invalid-neighbor count,
declared and observed degree bounds, and the terminal structural result established by that
validated evidence.

An unvalidated Artifact or an unknown/mismatched media type fails closed before report creation.
This is one closed condition in the existing public internal evaluator implementation, not an
evaluator Registry, plugin API, generic evaluator dispatch, generic media-type dispatcher, or
reusable native-evaluation framework. The existing `fake_graph_integrity_evaluator` identity,
version, JSON parsing path, and behavior remain unchanged. The cpp_main branch computes no Recall,
QPS, latency, Graph Parity, or performance result.

### Feature-local preflight and execute lifecycle

`cpp_main_baseline preflight` is a non-executing admission command. It validates the draft Profile,
fixed Source/Build Identity, and SIFT1M Dataset; creates and validates the confined immutable
DatasetArtifact plus its ValidationEvidence; recomputes the source-derived sharded-path predicate;
and writes the immutable normalized request at
`build/cpp-main-baseline/runs/sift1m-unified/requests/unified-sharded-request.json`. Its terminal
response returns the Dataset ArtifactRef and that repository-relative request path. Preflight does
not start the runner, create a Stage factory, or invoke P0, B0, or M0; all three invocation counts
are exactly zero.

`cpp_main_baseline execute` accepts that request only through `--request`, verifies its immutable
identity and referenced validated DatasetArtifact, and then invokes the runner once. It does not
accept `--config` as an implicit second admission path and does not rewrite the normalized request.

### Feature-local structural closure report

The run writes `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json`. This file is a
Feature-local structural closure summary, not a public Artifact, Graph Parity Report, registration
record, or reusable reporting framework. It references rather than embeds the separate Artifacts,
BuildManifests, ValidationEvidence, and graph-integrity EvaluationReport.

Its minimum fields are:

- `run_id`;
- `run_class: unified_sharded_closure`;
- `formal_graph_parity: false`;
- `validated_graph_baseline_registered: false`;
- `profile_ref: experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml`;
- `profile_checksum`, computed over the exact referenced draft YAML bytes;
- `profile_status: draft`;
- `upstream_commit`;
- `dataset_checksum`;
- `implementation_identities`, containing the exact P0, B0, and M0 identities;
- `partition_count`;
- `stage_results`, ordered as P0, per-Partition B0, then M0;
- `artifact_references`;
- `structural_validation`, whose value is `PASS` or `FAIL`;
- `upstream_clean_before` and `upstream_clean_after`.

No public schema or generic report abstraction is created for this file.

## Project Structure

### Documentation for this Feature

~~~text
specs/002-cpp-main-upstream-baseline/
├── spec.md
├── plan.md
├── research.md
├── source-mapping.md
├── contract-gap.md
├── data-model.md
├── quickstart.md
├── unified-sharded-report.json           # generated Feature-local structural closure summary
├── contracts/
│   ├── b0-callable-wrapper.md
│   ├── native-formats.md
│   └── stage-bindings.md
└── checklists/
    └── requirements.md
~~~

### Planned source and test surface

~~~text
upstream/
└── diskann-cpp-main/                    # fixed read-only source

cmake/
└── CppMainBaseline.cmake                # fixed identity and external binary-tree wiring

src/baseline/cpp_main/
├── CMakeLists.txt
├── native_formats.hpp                   # private fixed-format declarations
├── native_formats.cpp                   # Dataset/map/graph/.data/centroid/medoid readers
├── cpp_main_vector_accessor.cpp          # private native-backed VectorAccessor for the runner
├── source_build_identity.hpp             # private source/build admission declarations
├── source_build_identity.cpp             # gitlink, clean-state, toolchain and binary evidence
├── p0_partition.cpp                      # thin partition_with_ram_budget binding
├── b0_local_build.cpp                    # thin retrieval -> Index build/save binding
├── m0_merge.cpp                          # thin merge_shards binding
├── descriptors.cpp                       # three formal static factories
└── cpp_main_baseline_runner.cpp          # non-installed existing-Pipeline assembly

harness/python/diskann_harness/
└── cpp_main_baseline.py                  # non-running preflight -> immutable request -> one execute call

src/validation/
├── schema_catalog.cpp                    # fixed Feature 001 + cpp_main format dispatch
└── artifact_validator.cpp                # delegates native semantic checks; no conversion

src/pipeline/
├── evaluator.cpp                         # existing Fake JSON path plus one closed cpp_main branch
└── pipeline_executor.cpp                 # record declared Dataset + Partition B0 manifest inputs

contracts/baseline-v1/schemas/
├── cpp-main-dataset-artifact.schema.json
├── cpp-main-run-request.schema.json
├── cpp-main-p0-parameters.schema.json
├── cpp-main-b0-parameters.schema.json
└── cpp-main-m0-parameters.schema.json

tests/cpp/baseline/
├── CMakeLists.txt
├── test_cpp_main_link_smoke.cpp
├── test_cpp_main_source_build_admission.cpp
├── test_cpp_main_native_formats.cpp
├── test_cpp_main_descriptors_preflight.cpp
├── test_cpp_main_p0.cpp
├── test_cpp_main_b0.cpp
├── test_cpp_main_m0.cpp
└── test_cpp_main_pipeline.cpp

tests/cmake/
└── check_cpp_main_baseline_build.cmake

tests/python/
└── test_cpp_main_baseline_config.py

experiments/configs/cpp-main-upstream-baseline/
└── baseline-run-profile.yaml              # status: draft
~~~

No planned file contains copied partitioning, Vamana, or merge algorithm source. Baseline-local
schemas describe implementation parameters and the native Dataset metadata accepted by this
Feature; they do not replace or version the public Artifact contract.

## Minimal Delivery Sequence

### 1. Source/build admission and native formats

1. Verify parent gitlink mode/object, submodule HEAD, full fixed commit, repository/branch identity,
   and clean upstream state before configure, before execution, and after execution.
2. Configure upstream with an explicit binary directory outside upstream and link the actual
   diskann_s target. Record effective source/build identity and exact commands; fail closed if any
   identity differs.
3. Implement only the format readers enumerated in native-formats.md. Every reader checks exact
   header fields, overflow-safe expected length, file length, scalar/ID range, and structural
   termination; Dataset and centroid float32 values must be finite.
4. Add narrow internal validator dispatch for the reviewed cpp_main media types. Continue to run
   existing metadata, payload checksum, aggregate checksum, source-lineage, ID-space, and
   capability checks before native semantic checks.
5. Normalize the Feature-local YAML with the existing safe loader, validate the baseline-specific
   request schema, admit and validate the Dataset, and write the immutable preflight request at the
   fixed path above without starting the runner or creating a Stage factory. Execute later consumes
   only that request and invokes the baseline runner exactly once. Do not modify Feature 001's
   infrastructure_validation or two-run retain_all request contract.
6. Resolve the validated Dataset payload once, construct the private native-backed VectorAccessor,
   and bind its exact Dataset identity/checksum into StageContext and every consuming manifest.

### 2. P0 Stage Binding

1. Preflight a validated native float32/L2 Dataset, fixed source/profile identity, P0 parameters,
   writable stage-temporary paths, and cancellation.
2. Call pinned partition_with_ram_budget<float> with Dataset, derived sample probability, M=0.6,
   local degree 10, replication factor 2, and a temporary prefix. Do not copy partitioning code or
   override native random sources.
3. Resolve the returned Partition count and exact native filenames. Reject fewer than two
   Partitions, missing/non-contiguous IDs, missing maps, malformed map headers, out-of-range IDs,
   incomplete Dataset ownership, or invalid centroids.
4. Publish one immutable aggregate, map-only PartitionArtifact. Each record carries explicit
   PartitionId, local count, ownership and complete LocalToGlobalIdMap; the aggregate carries
   Dataset lineage and centroids. It contains no shard_vectors_role.
5. Validate the complete candidate and aggregate checksum before allowing any B0 factory
   invocation.

### 3. B0 Stage Binding

1. Iterate the validated Partition records by their explicit PartitionId, not by filename, list
   position, or implicit current Partition. Resolve and verify the selected exact map.
2. Stage the selected map unchanged in its native uint32 matrix form and call pinned
   retrieve_shard_data_from_ids<float>. The resulting shard vector file is B0 preparation under
   temporary storage.
3. Validate shard header/count/dimension/finiteness and row i byte-equivalence to Dataset row
   LocalToGlobalIdMap[i]. Reject order, range, or identity mismatch before constructing Index.
4. Construct upstream IndexWriteParameters using L=32, local degree=10, filter-list setter zero
   resolving to L, saturation false, T=6, and unchanged alpha/max-occlusion defaults. Construct
   Index<float>, call Index::build(filename,count), and call Index::save(prefix).
5. Validate both graph and .data outputs. Publish a LocalGraphArtifact containing the exact native
   graph plus exact selected map and lineage; retain the .data checksum and lifecycle only in the
   BuildManifest. Enforce node count, LocalNodeId range, and degree at most 10.
6. Record both the DatasetArtifact and aggregate PartitionArtifact as B0 BuildManifest inputs even
   though the public method receives Dataset vectors through the bound VectorAccessor service.
7. Validate every LocalGraphArtifact before proceeding. A failed or cancelled B0 publishes no
   normal Artifact and prevents M0 invocation.

### 4. M0 Stage Binding

1. Preflight one validated aggregate PartitionArtifact and exactly one validated
   LocalGraphArtifact for every PartitionId. Reject missing, duplicate, extra, wrong-owner,
   wrong-map, wrong-Dataset, wrong-source-checksum, version, ID-space, or capability entries before
   creating the M0 implementation.
2. Stage the immutable native maps and local graphs byte-for-byte under M0 temporary names matching
   the pinned callable's filename convention. Physical staging is recorded provenance and performs
   no logical conversion or adjacency reorder.
3. Call pinned merge_shards with the complete input set, returned Partition count, R=16, temporary
   merged-graph path, and temporary medoids path. Do not add pruning, refinement, distance
   computation, or connectivity work.
4. Validate the merged graph header, exact node-record count, every GlobalNodeId, degree at most
   R, and complete file consumption. Validate medoids as a uint32 matrix with one row per
   Partition, in-range GlobalNodeIds, and consistency with mapped local medoids.
5. Publish one immutable GlobalGraphArtifact with required global_graph and required-for-cpp_main
   medoids payloads. Both exact payload identities participate in the aggregate checksum.

### 5. Pipeline and evidence closure

1. Execute contract/preflight and native-format tests, then small P0/B0/M0 tests, then a complete
   small Pipeline with negative gates. Invalid input/output must stop before the next stage.
2. Run `cpp_main_baseline preflight` once with the unchanged draft Profile. Require the immutable
   normalized request, Dataset ArtifactRef, ValidationEvidence, source/build admission, sharded
   predicate, and zero P0/B0/M0 invocations before execution is allowed.
3. Run `cpp_main_baseline execute --request` once through the Feature-local runner using that exact
   request. Do not automatically change M, R, L, T, or any other value, rerun admission through
   `--config`, or retry with a modified profile.
4. Require source-derived sharded admission, at least two P0 Partitions, one valid B0 output per
   explicit PartitionId, one valid M0 output, and unchanged upstream HEAD/clean state.
5. Retain all stage Artifact metadata/payloads, one schema-valid build-stage BuildManifest, the
   existing schema-valid BuildManifest for every stage invocation, structural ValidationEvidence,
   the separate graph-integrity EvaluationReport produced by
   `cpp_main_native_graph_integrity_evaluator`, logs, complete configuration, and failure evidence.
   The EvaluationReport is not a Graph Parity Report or performance report. Write the
   Feature-local `specs/002-cpp-main-upstream-baseline/unified-sharded-report.json` with the fields
   defined above; do not create a baseline-registration record.

## Validation Matrix

| Boundary | Positive evidence | Required fail-closed cases |
|---|---|---|
| source/build | exact gitlink, HEAD, clean state, diskann_s linkage, full build identity | wrong commit/gitlink, dirty upstream, missing target, incomplete Build Identity |
| Dataset | exact little-endian float32 header, finite payload, length and SHA-256 | truncation, overflow, wrong type/dimension/count, non-finite value, checksum mismatch |
| P0 | Partition count at least two, centroids valid, maps complete and in range | missing map/centroids, invalid ID, inconsistent count, incomplete ownership, any shard-vector claim |
| B0 preparation | shard rows exactly follow the selected map | missing Dataset access, invalid PartitionId, map mismatch, wrong row/order/dimension/value |
| LocalGraph | node count equals map length, LocalNodeIds in range, degree at most floor(2R/3) | malformed/trailing graph bytes, invalid neighbor, wrong count, degree overflow, wrong lineage |
| M0 input set | exact PartitionId coverage and matching Dataset/map/source identity | missing/duplicate/extra local graph, incompatible version/ID space/capability/checksum |
| GlobalGraph | one record per Dataset node, GlobalNodeIds in range, degree at most R, medoids present | missing medoids, invalid medoid, malformed graph, wrong node count/neighbor/lineage/checksum |
| native graph evaluation | validated native claims/evidence produce a separate report with cpp_main evaluator identity | unvalidated Artifact, Fake/unknown/mismatched media type, or missing ValidationEvidence |
| pipeline | P0 then all B0 then M0; no invalid Artifact crosses a gate | preflight failure creates no factory; stage failure/cancellation invokes no downstream stage |

Cancellation is checked at stage start, after wrapper-controlled input validation, after each
non-interruptible upstream call returns, and before output publication. This plan does not modify
upstream to add in-call cancellation.

## First-Round Exit Gate

The implementation round is complete only when all of the following are true:

1. P0, B0, and M0 descriptors pass existing Registry and multi-input preflight checks.
2. The public Pipeline executes P0, then B0 once for every explicit PartitionId, then M0.
3. The unchanged draft SIFT1M Profile is admitted as sharded by the source formula and P0 produces at least
   two Partitions.
4. Every map, local graph, merged graph, and medoids file passes its cpp_main format and structural
   validator plus the existing Artifact identity/lineage validator.
5. The terminal GlobalGraphArtifact is valid and its aggregate checksum covers graph and medoids.
6. The build-stage and per-stage BuildManifests collectively contain complete
   Source/Build/Dataset/configuration/stage provenance without implementing Feature 001's deferred
   generic terminal-manifest hardening.
7. upstream/diskann-cpp-main remains at the pinned commit and clean before and after the run.
8. `unified-sharded-report.json` records `formal_graph_parity: false`,
   `validated_graph_baseline_registered: false`, `profile_status: draft`, and a terminal structural
   validation result without becoming a public Artifact or Graph Parity Report.

Passing this exit gate changes stage availability from planned to implemented-and-structurally-
validated in implementation evidence only. It does not change formal_gate_status, does not yield a
Graph Parity result, and does not authorize validated_graph_baseline registration.

## Requirement-to-Design Trace

| Requirement area | Design evidence |
|---|---|
| fixed source and out-of-tree build | Technical Context; source/build admission sequence; source-mapping.md |
| cpp_main native formats | contracts/native-formats.md; Validation Matrix |
| P0 callable and map-only output | contracts/stage-bindings.md; data-model.md P0 entities |
| B0 callable wrapper | contracts/b0-callable-wrapper.md; contracts/stage-bindings.md |
| M0 callable and required medoids | contracts/stage-bindings.md; data-model.md M0 entities |
| complete requires/provides and hard gates | Architectural Boundaries; contracts/stage-bindings.md |
| public Pipeline closure | Pipeline and evidence closure; quickstart.md |
| Feature-local structural closure report | Feature-local structural closure report; data-model.md |
| no parity or registration overclaim | Summary; First-Round Exit Gate; Explicit Non-Goals |

## Explicit Non-Goals

- Formal Native-versus-Unified Graph Parity, byte-identical graph output, adjacency equality, or
  repeated-run statistical protocol.
- Recall, QPS, search latency, complete DiskANN build/search parity, or performance conclusions.
- Loop Engine, parameter matrix, automatic retry, checkpoint scheduler, or automatic tuning.
- Generic Adapter, general format plugin, sidecar framework, production Adapter, or Contract v2.
- Copied or rewritten partitioning, Vamana, or merge algorithms; upstream patches or instrumentation.
- Instrumented Baseline, deterministic Baseline variant, paper implementation, or online update.
- Feature 001 deferred hardening.
- Installation as a production-ready scientific pipeline or registration of validated_graph_baseline.

## Complexity Tracking

No Constitution violation is accepted. The only non-common code is a fixed-commit build binding,
three thin stage bindings, fixed native readers, and one non-installed assembly. Native file
staging exists solely because pinned callables consume filenames; it is byte-preserving and
stage-temporary. Any need for a public API change, a generic conversion layer, or rewritten
algorithm behavior stops this plan and returns to explicit Contract review.
