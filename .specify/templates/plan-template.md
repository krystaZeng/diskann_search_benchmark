# Implementation Plan: [FEATURE]

**Branch**: `[###-feature-name]` | **Date**: [DATE] | **Spec**: [link]

**Input**: Feature specification from `/specs/[###-feature-name]/spec.md`

**Note**: This template is filled in by the `/speckit-plan` command; its definition describes the execution workflow.

## Summary

[Extract from feature spec: primary requirement + technical approach from research]

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: [e.g., Python 3.11, Swift 5.9, Rust 1.75 or NEEDS CLARIFICATION]

**Primary Dependencies**: [e.g., FastAPI, UIKit, LLVM or NEEDS CLARIFICATION]

**Storage**: [if applicable, e.g., PostgreSQL, CoreData, files or N/A]

**Testing**: [e.g., pytest, XCTest, cargo test or NEEDS CLARIFICATION]

**Target Platform**: [e.g., Linux server, iOS 15+, WASM or NEEDS CLARIFICATION]

**Project Type**: [e.g., library/cli/web-service/mobile-app/compiler/desktop-app or NEEDS CLARIFICATION]

**Study Class**: [Baseline validation, single-paper reproduction, or cross-paper composition]

**Baseline Reference**: [repository, branch, full commit, reproduction mode]

**Compiler/Build Flags**: [compiler and version; complete build flags]

**Dataset Identity**: [dataset, checksum, query set, ground truth, distance metric]

**Performance Goals**: [domain-specific, e.g., 1000 req/s, 10k lines/sec, 60 fps or NEEDS CLARIFICATION]

**Constraints**: [domain-specific, e.g., <200ms p95, <100MB memory, offline-capable or NEEDS CLARIFICATION]

**Scale/Scope**: [domain-specific, e.g., 10k users, 1M LOC, 50 screens or NEEDS CLARIFICATION]

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- [ ] **Baseline immutability**: The plan does not modify `upstream/diskann-cpp-main`, its
      fixed gitlink, or generate files inside it; all builds are out-of-tree.
- [ ] **Pinned provenance**: Baseline use records repository, branch, full commit, compiler,
      build flags, dataset checksum, and complete parameters without floating references.
- [ ] **Implementation coexistence**: Baseline, paper implementations, and Adapters have
      distinct identities and paths; Registry/config selection replaces stages at runtime.
- [ ] **Node identity**: Input/output ID spaces are explicit, LocalNodeId is partition-scoped,
      and every standard local Artifact carries a complete LocalToGlobalIdMap.
- [ ] **Three-layer outputs**: Artifact, BuildManifest, and EvaluationReport remain separate.
- [ ] **Compatibility gate**: Every implementation declares `requires`, `provides`, input ID
      space, and output ID space; format/version/checksum/ID/capability validation blocks invalid
      downstream use.
- [ ] **Study/report separation**: Baseline, single-paper reproduction, and cross-paper
      composition are configured and reported separately.
- [ ] **Fair experiment protocol**: Frozen parameters and comparison conditions are explicit;
      repetitions, aggregation, exclusions, and any parameter search are predeclared.
- [ ] **Validation and approval**: Applicable unit, contract, small-graph, parity, and SIFT1M
      checks are planned; runs larger than SIFT1M require recorded human approval.

## Project Structure

### Documentation (this feature)

```text
specs/[###-feature]/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output (/speckit-plan command)
├── data-model.md        # Phase 1 output (/speckit-plan command)
├── quickstart.md        # Phase 1 output (/speckit-plan command)
├── contracts/           # Phase 1 output (/speckit-plan command)
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)
<!--
  ACTION REQUIRED: Replace or expand this tree with the concrete paths touched
  by the feature. Keep all project-owned code outside upstream/. Do not retain
  paths that the feature does not use.
-->

```text
upstream/
└── diskann-cpp-main/     # fixed read-only Baseline; never a change target

contracts/
├── identifiers/
├── artifacts/
├── manifests/
├── capabilities/
└── evaluations/

src/
├── pipeline/
├── partition/
├── local_build/
├── merge/
├── adapters/
└── registry/

harness/
├── build/
├── execute/
├── validate/
├── evaluate/
└── compare/

tests/
├── contract/
├── property/
├── integration/
├── parity/
└── unit/

experiments/
├── configs/
├── manifests/
├── artifacts/
├── reports/
└── results/
```

**Structure Decision**: [Document the selected structure and reference the real
directories captured above]

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| [e.g., 4th project] | [current need] | [why 3 projects insufficient] |
| [e.g., Repository pattern] | [specific problem] | [why direct DB access insufficient] |
