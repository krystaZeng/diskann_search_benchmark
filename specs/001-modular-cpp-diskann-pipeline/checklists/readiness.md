# Implementation Readiness Requirements Checklist: Modular cpp_main DiskANN Pipeline

**Purpose**: Review whether the Feature's specification, plan, and contracts are complete, clear,
consistent, measurable, and ready for task generation without testing an implementation.
**Created**: 2026-08-04
**Feature**: [spec.md](../spec.md) | [plan.md](../plan.md)

**Audience/Timing**: Reviewer, standard-depth review before `tasks.md` generation.

**Note**: Each item evaluates requirements writing and design-contract quality, not whether code
currently exists or behaves correctly.

## Requirement Completeness

- [x] CHK001 Are the ownership domain, legal range, count boundary, and cross-stage stability
  requirements complete for GlobalNodeId, LocalNodeId, and PartitionId? [Completeness, Spec
  §FR-001–FR-005]
- [x] CHK002 Are required metadata fields, payload roles/order, source relationships, ID spaces,
  capabilities, and integrity fields documented for Dataset, Partition, Local Graph, and Global
  Graph Artifacts, including aggregate PartitionArtifact input plus explicit Local Build
  PartitionId selection? [Completeness, Spec §FR-006–FR-009, Data Model §2–§3]
- [x] CHK003 Are Artifact, BuildManifest, EvaluationReport, ValidationEvidence, and quarantine
  identity, storage, reference, mutability, and lifecycle requirements independently specified?
  [Completeness, Spec §FR-010–FR-013 and §FR-023–FR-024]
- [x] CHK004 Are Registry descriptors and every StageContext service fully covered, including
  required/provided Artifact contracts, ID spaces, runtime capabilities, real services, and the
  unavailable CheckpointStore? [Completeness, Spec §FR-014–FR-019 and §FR-040–FR-041]
- [x] CHK005 Are build, preflight, execute, validate, evaluate, reuse, cancellation, and diagnostic
  outcomes specified with their required inputs, outputs, manifests, and refusal conditions?
  [Completeness, Spec §FR-025–FR-030 and §FR-036–FR-037, Contract `cli.md` §2–§6]
- [x] CHK006 Are the exact `infrastructure_validation` study class, pinned-but-unexecuted Baseline,
  frozen Fake configuration, two-run `retain_all` rule with distinct provenance, prohibited
  parameter search, and dataset-scale limits all stated as requirements rather than implementation
  assumptions? [Completeness, Spec §Reproduction and Experiment Governance, Constitution §I and
  §VI–§VII]

## Requirement Clarity

- [x] CHK007 Is `ArtifactRef.metadata_path` unambiguously defined as the immutable Artifact metadata
  document and distinguished from normalized stage Manifest, BuildManifest, and EvaluationReport
  paths wherever references and stage interfaces are described? [Ambiguity, Spec §FR-009–FR-013,
  Contract `cpp-api.md` §1–§3]
- [x] CHK008 Is the checksum contract precise about exact payload bytes, canonical projection
  fields, recursive key ordering, array ordering, exclusions, invalid value types, profile version,
  and source/payload role ordering? [Clarity, Spec §FR-009 and §FR-020, Contract
  `checksum-canonicalization.md`]
- [x] CHK009 Are exact compatibility rules defined for Artifact type/version, input/output ID
  space, capability sets, runtime services, version matching, and the absence of implicit fallback
  or Adapter insertion? [Clarity, Spec §FR-014–FR-019, Data Model §5]
- [x] CHK010 Is the separation between the installed production runner/Registry and the
  non-installed test-only Fake runner/Registry stated clearly enough to prevent Fake identities
  from being treated as formal experiment implementations? [Clarity, Spec §FR-031 and §FR-038,
  Plan §Project Structure]

## Requirement Consistency

- [x] CHK011 Are the C++17 compatibility floor, prohibition on public C++20 types, use of
  `const std::vector<ArtifactRef>&`, and isolation of higher-standard implementations consistent
  across plan, public API contract, and architectural source guidance? [Consistency, Plan
  §Technical Context, Research §Decision 1–§Decision 2, Contract `cpp-api.md`]
- [x] CHK012 Are Fake Merge ordering/deduplication/truncation requirements consistently identified
  as test-only semantics and never conflated with the fixed cpp_main `merge_shards` behavior?
  [Consistency, Spec §FR-046, Contract `fake-fixture.md` §5, Constitution §Baseline and Experiment
  Constraints]
- [x] CHK013 Are Artifact, BuildManifest, EvaluationReport, and ValidationEvidence fields
  consistently assigned across the data model, JSON schemas, checksum exclusions, and CLI result
  references? [Consistency, Spec §FR-009–FR-013, Contracts `schemas/`]
- [x] CHK014 Are the Adapter preservation rules consistent with the explicit exclusion of any
  production Adapter from this Feature, so requirements do not imply an Adapter implementation
  deliverable? [Consistency, Spec §FR-042–FR-044 and §Out of Scope, Plan §Constraints]

## Acceptance Criteria Quality

- [x] CHK015 Does the negative-fixture acceptance coverage map every required failure in FR-048 to
  an objective failure category, enforcement gate, and unauthorized-invocation count? [Acceptance
  Criteria, Spec §FR-048–FR-049 and §SC-003–SC-007]
- [x] CHK016 Are determinism criteria objectively grounded in frozen raw bytes, payload checksums,
  aggregate checksums, exact adjacency, two retained deterministic runs, equal deterministic
  evaluation projections, distinct run/report identities, timestamps and provenance, and
  `retain_all`? [Measurability, Spec §SC-001–SC-002 and §SC-012, Contract `fake-fixture.md`]
- [x] CHK017 Is quarantine isolation measurable across every named discovery and consumption path,
  including resolution, evaluation, reuse, comparison, and aggregation? [Acceptance Criteria, Spec
  §FR-023–FR-024 and §SC-007]
- [x] CHK018 Are the upstream immutability and scale-bound criteria objectively measurable through
  the fixed gitlink, clean upstream path, out-of-tree outputs, Fake-only dataset identity, and zero
  real DiskANN/SIFT invocations? [Measurability, Spec §SC-010, Plan §Baseline Reference and
  §Dataset Identity]

## Scenario Coverage

- [x] CHK019 Are primary-flow requirements complete from validated Dataset Artifact through all
  three stage handoffs, including aggregate PartitionArtifact plus explicit in-range PartitionId
  for each Local Build, normal publication, final validation, and separate evaluation? [Coverage,
  Spec §User Story 1 and §FR-025–FR-029]
- [x] CHK020 Are alternate reuse requirements complete for successful current revalidation,
  producer non-invocation, provenance recording, dataset/configuration mismatch, and preservation
  of the cached Artifact after rejection? [Coverage, Alternate Flow, Spec §User Story 3 and
  §FR-036–FR-037, §FR-049]
- [x] CHK021 Are preflight rejection, implementation failure, per-stage cooperative cancellation
  checkpoints and independent Partition/Local Build/Merge cancellation cases,
  INVALID_OUTPUT, explicit validation rejection, and evaluation refusal distinguished with
  mutually consistent lifecycle and downstream-blocking requirements? [Coverage, Exception Flow,
  Data Model §8, Contract `cli.md` §4–§6]
- [x] CHK022 Are recovery/diagnostic requirements complete for retained invalid evidence, explicit
  diagnostic reads and deletion, prohibition on in-place promotion, and new identity creation for
  corrected output? [Coverage, Recovery Flow, Spec §FR-023–FR-024]

## Edge Case Coverage

- [x] CHK023 Are zero nodes, zero Partitions, empty Partitions, present zero-neighbor lists, absent
  graph payloads, and partially written payloads distinguished with an explicit validity outcome?
  [Edge Cases, Spec §Edge Cases and §FR-039]
- [x] CHK024 Are maximum uint32 ID/count boundaries, out-of-range map/neighbor values, integer-width
  conversion, overflow, replica multiplicity, and ambiguous mappings addressed without relying on
  unstated implementation limits? [Edge Cases, Spec §FR-001–FR-005 and §FR-042–FR-043]
- [x] CHK025 Are path traversal, absolute stored paths, symlink escape, cross-filesystem atomicity,
  pre-existing temporary contents, and interrupted publication either specified or explicitly
  excluded from v1? [Gap, Edge Cases, Contract `cli.md` §7, Research §Decision 10]

## Non-Functional Requirements

- [x] CHK026 Is it clear whether memory/thread budgets are enforceable limits, advisory inputs, or
  provenance-only values, and are breach/cancellation outcomes specified consistently? [Ambiguity,
  Non-Functional, Spec §FR-012 and §FR-040]
- [x] CHK027 Are cross-platform determinism requirements bounded for Linux versus macOS, including
  float/L2 behavior, canonical UTF-8 serialization, filesystem ordering, and compiler-dependent
  results? [Non-Functional, Spec §SC-002, Plan §Target Platform, Research §Decision 12]

## Dependencies & Assumptions

- [x] CHK028 Are minimum/supported versions, acquisition expectations, and provenance obligations
  documented for CMake, compiler, OpenSSL, nlohmann_json, PyYAML, jsonschema, and pytest without
  silently depending on network access? [Dependency, Plan §Primary Dependencies and
  §Compiler/Build Flags]
- [x] CHK029 Are Python 3.10+, `pyproject.toml`, the optional test dependency group, standard pip
  workflow, and non-mandatory uv policy stated consistently as requirements or explicit planning
  assumptions? [Assumption, Plan §Technical Context, Research §Decision 11, Quickstart §2]

## Ambiguities & Conflicts

- [x] CHK030 Is the actual Feature branch identity consistently recorded as `master` in both the
  specification and implementation plan, with no stale generated branch label? [Consistency, Spec
  §Feature Branch, Plan §Branch]

## Notes

- Check items off as requirements are reviewed and findings are resolved: `[x]`.
- Record findings inline and link any resulting spec/plan/contract edits.
- Unchecked items indicate unreviewed requirements quality, not failed implementation behavior.
- Implementation-pre consistency audit on 2026-08-04 reviewed CHK001–CHK030 and resolved the
  remaining repetition/projection, per-stage cancellation, workspace-normalization, and
  FR-035/FR-048 authority wording residuals before marking the checklist complete.
