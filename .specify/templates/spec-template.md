# Feature Specification: [FEATURE NAME]

**Feature Branch**: `[###-feature-name]`

**Created**: [DATE]

**Status**: Draft

**Input**: User description: "$ARGUMENTS"

## Reproduction and Experiment Governance *(mandatory)*

<!--
  Classify the work before defining requirements. Do not merge Baseline validation,
  a single-paper reproduction, and a cross-paper composition into one claim.
-->

**Study Class**: [Baseline validation / single-paper reproduction / cross-paper composition]

**Baseline Reference**: [repository, branch, full commit, reproduction mode]

**Replacement Scope**: [Partition / Local Build / Merge / other stages; list every replaced stage]

**Implementation Identities**: [selected Baseline, paper, and Adapter implementation IDs]

**ID-Space Contract**: [input and output ID spaces; required LocalToGlobalIdMap behavior]

**Artifact Flow**: [Artifact types/versions and separate BuildManifest/EvaluationReport outputs]

**Validation Gate**: [required format, version, checksum, ID-space, and capability checks]

**Experiment Protocol**: [dataset/checksum, scale, approval, controlled comparison variables,
repetitions, aggregation rule, and parameter-search declaration]

## User Scenarios & Testing *(mandatory)*

<!--
  IMPORTANT: User stories should be PRIORITIZED as user journeys ordered by importance.
  Each user story/journey must be INDEPENDENTLY TESTABLE - meaning if you implement just ONE of them,
  you should still have a viable MVP (Minimum Viable Product) that delivers value.

  Assign priorities (P1, P2, P3, etc.) to each story, where P1 is the most critical.
  Think of each story as a standalone slice of functionality that can be:
  - Developed independently
  - Tested independently
  - Deployed independently
  - Demonstrated to users independently
-->

### User Story 1 - [Brief Title] (Priority: P1)

[Describe this user journey in plain language]

**Why this priority**: [Explain the value and why it has this priority level]

**Independent Test**: [Describe how this can be tested independently - e.g., "Can be fully tested by [specific action] and delivers [specific value]"]

**Acceptance Scenarios**:

1. **Given** [initial state], **When** [action], **Then** [expected outcome]
2. **Given** [initial state], **When** [action], **Then** [expected outcome]

---

### User Story 2 - [Brief Title] (Priority: P2)

[Describe this user journey in plain language]

**Why this priority**: [Explain the value and why it has this priority level]

**Independent Test**: [Describe how this can be tested independently]

**Acceptance Scenarios**:

1. **Given** [initial state], **When** [action], **Then** [expected outcome]

---

### User Story 3 - [Brief Title] (Priority: P3)

[Describe this user journey in plain language]

**Why this priority**: [Explain the value and why it has this priority level]

**Independent Test**: [Describe how this can be tested independently]

**Acceptance Scenarios**:

1. **Given** [initial state], **When** [action], **Then** [expected outcome]

---

[Add more user stories as needed, each with an assigned priority]

### Edge Cases

<!--
  ACTION REQUIRED: The content in this section represents placeholders.
  Fill them out with the right edge cases.
-->

- What happens when an Artifact has an unsupported format/version or checksum mismatch?
- How does the pipeline reject an ID-space or `requires`/`provides` capability mismatch?
- What happens when an experiment requests a dataset larger than SIFT1M without recorded approval?
- How are incomplete, failed, or invalid repeated runs retained without entering aggregation?

## Requirements *(mandatory)*

<!--
  ACTION REQUIRED: The content in this section represents placeholders.
  Fill them out with the right functional requirements.
-->

### Functional Requirements

- **FR-001**: The feature MUST [state the specific pipeline or experiment capability].
- **FR-002**: The feature MUST preserve the fixed, read-only Baseline and coexist through a
  distinct implementation/Adapter identity.
- **FR-003**: Every affected implementation MUST declare `requires`, `provides`, input ID space,
  and output ID space.
- **FR-004**: Every produced or reused Artifact MUST be rejected from downstream use unless its
  format, version, checksum, ID space, and capabilities validate successfully.
- **FR-005**: The feature MUST keep Artifact, BuildManifest, and EvaluationReport separate and
  preserve sufficient provenance to repeat the run.
- **FR-006**: The experiment MUST preserve all predeclared configurations and repeated-run results
  and aggregate them using the declared protocol.

*Example of marking unclear requirements:*

- **FR-007**: The comparison MUST hold [NEEDS CLARIFICATION: parameters or resource budget to hold constant] fixed.
- **FR-008**: The implementation MUST consume [NEEDS CLARIFICATION: required upstream Artifact type/version and ID space].

### Key Entities *(include if feature involves data)*

- **Implementation**: Stable identity, stage, version, `requires`, `provides`, and input/output ID spaces.
- **Artifact**: Immutable stage data, type/version, checksum, ID space, and source references.
- **BuildManifest**: Complete generation provenance, inputs, parameters, environment, outputs, and status.
- **EvaluationReport**: Validation, quality, performance, and comparison findings for one declared study class.
- **ExperimentConfig**: Dataset identity, implementation selection, parameters, resources, repetitions,
  aggregation protocol, and approval record.

## Success Criteria *(mandatory)*

<!--
  ACTION REQUIRED: Define measurable success criteria.
  These must be technology-agnostic and measurable.
-->

### Measurable Outcomes

- **SC-001**: 100% of valid runs contain the required pinned source, build, dataset, and parameter provenance.
- **SC-002**: 100% of downstream stages reject invalid or incompatible input Artifacts before execution.
- **SC-003**: All standard local graph outputs contain a complete, valid LocalToGlobalIdMap.
- **SC-004**: Baseline, single-paper, and cross-paper results are emitted as separately identifiable reports.
- **SC-005**: [Feature-specific measurable quality, performance, or resource outcome].

## Assumptions

<!--
  ACTION REQUIRED: The content in this section represents placeholders.
  Fill them out with the right assumptions based on reasonable defaults
  chosen when the feature description did not specify certain details.
-->

- The pinned `cpp_main` Baseline and its source mapping are available but remain read-only.
- Standard local graph Artifacts use LocalNodeId and include a complete LocalToGlobalIdMap.
- Dataset files, query sets, ground truth, and checksums are available to the Harness.
- Any experiment larger than SIFT1M remains out of execution scope until human approval is recorded.
