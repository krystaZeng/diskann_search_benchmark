# Implementation Plan: Modular cpp_main DiskANN Pipeline

**Branch**: `master` | **Date**: 2026-08-04 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-modular-cpp-diskann-pipeline/spec.md`

## Summary

Build a C++17 public core for immutable Artifact references, stage interfaces, static-factory
Registry resolution, capability preflight, contract validation, and the Partition → explicit
per-Partition Local Build → Merge lifecycle. JSON files carry Artifact metadata, BuildManifests,
EvaluationReports, and
normalized runner requests; YAML is accepted only as the human-authored experiment composition
format and is normalized by a Python 3.10+ Harness before invoking a C++ CLI. Test-only Fake
implementations prove the exact six-node pipeline, reuse, failure gates, and quarantine behavior
without linking or executing the real DiskANN Baseline.

**Implemented targets**: `diskann_pipeline_core` is the public C++17 library;
`diskann_pipeline_runner` is the installed production assembly with no registered stage in Feature
001; `diskann_pipeline_fake_stages` and the non-installed `diskann_pipeline_fake_runner` exist only
under `tests/cpp/fakes`. The Python entry point is `diskann-harness` / `python -m
diskann_harness.cli` with `build`, `execute`, `validate`, and `evaluate` lifecycles.

## Delivery Status

Feature 001 is frozen as an **infrastructure prototype / contract MVP**. It establishes and has
validated the public ID model, immutable Artifact references, Registry admission and resolution,
Artifact validation, the test-only Fake Pipeline, and the minimum Harness lifecycle needed to
exercise those contracts.

This delivery does not integrate or execute the real `cpp_main` Baseline. The installed production
runner currently has no formal Partition, Local Build, or Merge implementation registered, and
Feature 001 must not be described as a production-ready scientific pipeline. It produces neither a
Baseline result nor a paper-reproduction result. The deferred hardening items are recorded in
[deferred-hardening.md](deferred-hardening.md); they are not prerequisites for starting Feature B.
Whether any public contract should be adjusted will be decided from evidence obtained while
integrating the real Baseline, rather than by extending the Fake-only infrastructure in advance.

## Technical Context

**Language/Version**: Public core and module contracts: ISO C++17; Python Harness: Python 3.10+.
Isolated tools or future paper implementations may use a newer C++ standard only behind a C++17
boundary and may not expose C++20 types in public core headers.

**Primary Dependencies**: CMake 3.20+; C++ implementation-private `nlohmann_json` 3.11+ for JSON;
OpenSSL 3.x `libcrypto` EVP for SHA-256; Python `PyYAML` for safe YAML parsing and `jsonschema` for
Draft 2020-12 validation. No dynamic plugin loader and no C++ YAML dependency are included.

**Storage**: Local filesystem only. Immutable normal Artifacts, BuildManifests,
EvaluationReports, quarantine entries, and temporary work use distinct workspace roots. No
database, SQLite state store, or checkpoint persistence.

**Testing**: CTest runs C++ unit, contract, small-graph property, and Fake integration executables;
pytest runs Python CLI, YAML normalization, subprocess, schema, installation-surface, and
cross-language integration tests. YAML safety uses
duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation: one project subclass of
`yaml.SafeLoader` supplies a custom
mapping constructor; the result must be a top-level mapping with string keys and recursively contain
only `dict`, `list`, `str`, `bool`, `int`, finite `float`, and `None`. Tests reject duplicate keys,
top-level sequences, non-string keys, Python object and custom tags, unsafe constructors,
date/datetime, bytes, set, NaN, Infinity, and other non-JSON types while accepting ordinary valid
YAML. No second YAML parser is used; JSON Schema decides which concrete fields permit null or float.

Fake implementations are compiled only into test-support targets and a non-installed Fake runner.
Phase 2 validates Registry admission: an explicitly constructed `kProduction` Registry rejects
`test_only: true`, while `kTest` accepts both test-only and formal descriptors. Final Assembly
isolation has three independent evidence layers: the C++ production-surface test checks Registry
visibility and absence of production Adapter registration; a simple CMake target-property or
configure-time/CTest script assertion verifies target/source separation; and
`tests/python/test_production_assembly.py` installs to a temporary prefix and verifies that only the
production runner, not Fake targets or files, is installed. Registry enumeration alone is not link
isolation evidence.

The final C++ smoke targets are `fake_pipeline_e2e`, `negative_matrix_e2e`,
`production_surface`, and `production_target_isolation`. Python end-to-end and installation
coverage lives in `test_harness_fake_pipeline_e2e.py`, `test_harness_reuse_e2e.py`, and
`test_production_assembly.py`. The canonical Harness writes retained runner results and summaries
under the normalized `reports/` root while normal Artifacts, BuildManifests/evidence, quarantine,
and stage temporary data remain under their separate roots.

**Target Platform**: Linux is the normative experiment platform. macOS with a C++17-capable
AppleClang/Clang toolchain is supported for this Feature's unit tests and Fake Pipeline only.

**Project Type**: Mixed C++ library/CLI plus Python orchestration package.

**Study Class**: `infrastructure_validation`; no Baseline, paper-reproduction, or
cross-paper scientific result is produced.

**Baseline Reference**: `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, commit
`78256bbab4685e1774e78d331e081a153be26823`, reproduction mode `not-executed`. The gitlink and
`upstream/diskann-cpp-main` tree remain read-only.

**Compiler/Build Flags**: Normative Linux development/CI profile uses GCC 11+; supported macOS
development uses AppleClang/Clang with complete C++17 support. CMake sets
`CMAKE_CXX_STANDARD=17`, `CMAKE_CXX_STANDARD_REQUIRED=ON`, `CMAKE_CXX_EXTENSIONS=OFF`, and
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`. Project targets use `-Wall -Wextra -Wpedantic` on GCC/Clang;
Debug uses `-O0 -g`, and Release uses `-O3 -DNDEBUG`. The Harness records the actual compiler
path/version, CMake version, generator, build type, actual configure/build commands, effective
CMake definitions, and project compile options in each BuildManifest rather than treating these
defaults as sufficient provenance. Feature 001 does not require per-translation-unit compile-command
or linker-command capture.

**Dataset Identity**: `fake-six-node-v1`: six fixed two-dimensional vectors whose logical vector
type is `float32`, from FR-045, with exact L2, no query set or ground truth, two fixed overlapping
Partitions, and the exact adjacency from FR-046. The physical storage is UTF-8 JSON with no
trailing newline; this JSON payload is 49 bytes. Its SHA-256 is
`2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b`, and the checksum plus all
expected stage payload and aggregate checksums are authoritative in `contracts/fake-fixture.md`.
No SIFT dataset is read or executed.

**Performance Goals**: No ANN performance target. The complete deterministic Fake suite must run
locally as a small test workload; correctness, fail-closed behavior, and deterministic checksums
are the acceptance goals.

**Constraints**: Stage data crosses implementation boundaries only through `ArtifactRef` values,
never full graph objects. Local Build receives the aggregate PartitionArtifact reference plus an
explicit validated PartitionId and a C++17 `LocalBuildConfig`; the config refers to the immutable
normalized stage Manifest. Read-only Artifact collections use `const std::vector<ArtifactRef>&`;
public headers expose no `std::span`. Artifact data, BuildManifest, and EvaluationReport remain
separate. The Python workspace normalizer materializes confined normal, temporary, manifest,
report, and quarantine roots in the normalized RunRequest. Invalid output is isolated in
quarantine. Descriptor `requires.input_artifacts` is a C++17 collection of per-input
type/accepted-version/ID-space requirements; Merge declares both PartitionArtifact and
LocalGraphArtifact, while `provides` remains a single output. Every descriptor carries
`parameter_schema_path`, a stable repository-relative path to its implementation parameter JSON
Schema. Registry construction explicitly selects `kProduction` or `kTest`; no default construction
exists. Fake implementations are test-only. No real DiskANN integration, Loop Engine, SQLite,
parameter matrix, retry, or checkpoint scheduling is permitted.

**Scale/Scope**: One process and one machine; three stage interfaces; three stage Artifact types;
one explicit Registry; one C++ runner contract; one Python Harness; a canonical six-node fixture;
and the positive/negative tests required by FR-045 through FR-049.

## Constitution Check

*GATE: Passed before Phase 0 research. Re-check is required after Phase 1 design.*

- [x] **Baseline immutability**: All planned source, generated data, builds, and tests are outside
      `upstream/diskann-cpp-main`; the fixed gitlink is read-only and no upstream command runs.
- [x] **Pinned provenance**: The fixed Baseline identity is fully pinned and explicitly marked
      not executed. Feature BuildManifests capture actual source, compiler, flags, dataset checksum,
      and complete normalized parameters.
- [x] **Implementation coexistence**: Registry identities are immutable and distinct. This Feature
      adds only public infrastructure and test-only Fake factories, not a replacement Baseline or
      paper implementation.
- [x] **Node identity**: Global, Local, and Partition IDs are explicit; standard local output uses
      partition-scoped LocalNodeId and a complete LocalToGlobalIdMap.
- [x] **Three-layer outputs**: Artifact metadata/data, BuildManifest, and EvaluationReport have
      separate schemas, roots, references, and lifecycles.
- [x] **Compatibility gate**: Descriptors declare `requires`, `provides`, input/output ID spaces,
      Artifact versions, capabilities, and runtime services; preflight and Artifact validation are
      blocking.
- [x] **Study/report separation**: The Feature is classified as `infrastructure_validation` and
      produces no scientific comparison.
- [x] **Fair experiment protocol**: The only repeated runs are two retained deterministic runs
      over one frozen Fake configuration. Both use identical inputs/configuration and retain equal
      payload/aggregate checksums and deterministic EvaluationReport findings projections under
      `retain_all`, while preserving distinct run/report identities, timestamps, and provenance;
      there is no parameter search, run selection, query change, or performance comparison.
- [x] **Validation and approval**: C++ unit/contract/property/integration tests and Python Harness
      tests are planned. Baseline parity and SIFT1M are not applicable because no Baseline or
      algorithm semantics are integrated; experiments larger than SIFT1M remain prohibited without
      recorded approval.

**Pre-design gate result**: PASS. No exception or Complexity Tracking entry is required.

## Project Structure

### Documentation (this feature)

```text
specs/001-modular-cpp-diskann-pipeline/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── deferred-hardening.md     # frozen T092–T098 gaps and reassessment triggers
├── contracts/
│   ├── README.md
│   ├── adapter-policy.md
│   ├── checksum-canonicalization.md
│   ├── cli.md
│   ├── cpp-api.md
│   ├── fake-fixture.md
│   └── schemas/
└── tasks.md                  # implementation task plan
```

### Source Code (repository root)

```text
CMakeLists.txt
cmake/
include/diskann_pipeline/
├── artifact_ref.hpp
├── identifiers.hpp
├── implementation.hpp
├── pipeline.hpp
├── registry.hpp              # public ImplementationRegistry interface
├── runner_protocol.hpp
├── stage_context.hpp
├── stage_interfaces.hpp
└── validation.hpp
contracts/v1/
├── schemas/                  # runtime copies of the approved JSON schemas
├── checksum-canonicalization.md
├── adapter-policy.md
└── SOURCES.json              # generated source-to-runtime trace; not an authority
src/
├── core/
├── registry/
├── validation/
├── pipeline/                 # executor, validated reuse, evaluator
└── cli/                      # protocol + installed C++ runner; no Fake registration
harness/python/diskann_harness/
├── cli.py
├── config.py
├── manifests.py
├── runner.py
└── workspace.py
tests/
├── cmake/                    # simple production/Fake target-property isolation checks
├── cpp/
│   ├── unit/
│   ├── contract/
│   ├── property/
│   ├── integration/
│   └── fakes/                # Fake Partition/Local Build/Merge and Fake runner registration
├── python/
│   └── test_production_assembly.py  # temporary-prefix installation-surface check
└── fixtures/fake_pipeline/
pyproject.toml
upstream/
└── diskann-cpp-main/         # fixed read-only Baseline; never a change target
```

**Structure Decision**: Use one public C++17 core library and one production runner outside
`upstream/`, with JSON runtime schemas in `contracts/v1`. The Python package under `harness/`
normalizes YAML into JSON and drives the runner as a subprocess; it does not bind C++ objects in
process. Fake implementations live only under `tests/cpp/fakes`, link only into test targets and a
non-installed Fake runner, and therefore cannot appear as formal experiment implementations.
Loop Engine and production experiment implementation directories are intentionally untouched.

## Design Deliverables

- Phase 0 decisions: [research.md](research.md)
- Entity model and lifecycle: [data-model.md](data-model.md)
- C++ boundary, CLI protocol, schemas, and checksum rules: [contracts/](contracts/README.md)
- Runnable post-implementation validation guide: [quickstart.md](quickstart.md)

### Requirement-to-Design Trace

| Requirement group | Primary design evidence |
|---|---|
| FR-001–FR-005: ID semantics | `data-model.md` sections 1–3; Artifact schemas |
| FR-006–FR-013: Artifact/provenance separation | Artifact, BuildManifest, and EvaluationReport schemas; checksum contract |
| FR-014–FR-019: Registry/compatibility | `contracts/cpp-api.md`; implementation descriptor schema |
| FR-020–FR-030: validation/Harness | validation order, CLI exit contract, evidence and quarantine schemas |
| FR-031–FR-041: Fake Pipeline, StageContext, and reuse | `contracts/fake-fixture.md`; `contracts/cpp-api.md`; related schemas; `quickstart.md` |
| FR-042–FR-044: Adapter policy | `contracts/adapter-policy.md` (sole policy authority); Adapter declaration/evidence contract in `contracts/cpp-api.md`; existing format, ID-space, and capability declarations in the implementation descriptor schema |
| FR-045–FR-049: canonical fixture and negative matrix | `contracts/fake-fixture.md`; related schemas; `quickstart.md` |

## Post-Design Constitution Re-check

**Result**: PASS.

- The source tree, build layout, CLI, fixture, and runtime storage remain entirely outside the fixed
  upstream directory; no real DiskANN integration or execution path was introduced.
- All JSON schemas and the C++ API preserve explicit ID spaces, complete local maps, immutable
  references, three-layer output separation, stable implementation identities, and hard validation
  gates.
- Local Build consumes the validated aggregate PartitionArtifact together with one explicit,
  in-range PartitionId; no undefined Partition shard ArtifactRef or additional Artifact type is
  introduced.
- Registry and Assembly exclusion are validated at distinct layers. Phase 2 validates explicit
  `kProduction`/`kTest` Registry admission using the existing `test_only` field; final target and
  installation isolation are not asserted at that phase. Phase 4 combines Registry visibility,
  simple CMake target/source isolation assertions, and a temporary-prefix installation test. The
  installed production runner uses `kProduction`; the non-installed Fake runner uses `kTest` and
  remains outside the install surface.
- The canonical fixture freezes parameters, payload bytes, checksum projection, repeated-run rule,
  and expected output; it creates no Baseline, paper, or cross-paper comparison claim.
- Planned CTest/pytest suites cover unit, contract, small-graph, integration, reuse, independent
  during-stage cancellation for all three Fake stages, and quarantine behavior. Baseline parity and
  SIFT1M are correctly not applicable to a Feature that integrates no real algorithm; the
  larger-than-SIFT1M approval gate remains intact.

No design exception, validator bypass, Baseline mutation, or deferred clarification was found.

## Complexity Tracking

No Constitution violations or approved exceptions.
