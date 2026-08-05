<!--
Sync Impact Report
- Version change: unratified template → 1.0.0
- Modified principles:
  - Placeholder principles → I. Fixed and Read-Only Baseline
  - Placeholder principles → II. Coexisting Modular Implementations
  - Placeholder principles → III. Explicit Node Identity
  - Placeholder principles → IV. Three-Layer Artifact Provenance
  - Placeholder principles → V. Declared Capabilities and Validated Handoffs
  - Placeholder principles → VI. Separated and Fair Scientific Reporting
  - Placeholder principles → VII. Testable, Traceable, Repeatable Execution
- Added sections:
  - Baseline and Experiment Constraints
  - Development Workflow and Quality Gates
- Removed sections: none; placeholder sections were instantiated
- Dependent artifacts:
  - ✅ updated: .specify/templates/plan-template.md
  - ✅ updated: .specify/templates/spec-template.md
  - ✅ updated: .specify/templates/tasks-template.md
  - ✅ updated: .agents/skills/speckit-tasks/SKILL.md
  - ✅ updated: .agents/skills/speckit-implement/SKILL.md
  - ✅ reviewed, no change required: all other installed speckit command files
  - ✅ reviewed, already aligned: AGENTS.md
- Follow-up TODOs: none
-->
# DiskANN cpp_main Paper Reproduction Constitution

## Core Principles

### I. Fixed and Read-Only Baseline

`upstream/diskann-cpp-main` MUST remain a fixed, read-only Baseline. No implementation,
instrumentation, build output, generated file, or experiment patch may edit, add, delete,
format, or overwrite content in that tree. Builds and tests MUST use out-of-tree paths.
Changing the pinned submodule commit requires explicit human approval, a constitution-aware
migration record, refreshed source mapping, and a new Baseline Parity Gate.

Every valid Baseline run MUST bind repository, branch, full commit, compiler and version,
complete build flags, dataset checksum, and complete experiment parameters. Floating labels
such as "current cpp_main" MUST NOT identify a Baseline. This preserves a stable scientific
control and makes every reported result independently traceable.

### II. Coexisting Modular Implementations

Baseline implementations, paper implementations, and Adapters MUST coexist independently in
both code and configuration. They MUST NOT replace one another by overwriting or mutating an
existing implementation. All project-owned implementations and experiment code MUST live
outside `upstream/` and MUST have stable, distinct implementation identities.

Runtime experiments MAY select implementations through the Registry or explicit configuration
and MAY replace one or more pipeline stages. Selection changes composition only; it MUST NOT
change the stored Baseline or silently relabel a variant as the Baseline. This enables rollback,
single-stage ablation, coupled replacement, and cross-paper composition without losing provenance.

### III. Explicit Node Identity

`GlobalNodeId` MUST be unique and stable across the complete dataset and pipeline.
`LocalNodeId` MUST be interpreted only inside its owning Partition. The standard
`LocalGraphArtifact` MUST use local IDs by default and MUST preserve a complete
`LocalToGlobalIdMap` whose length equals `local_node_count`.

Any implementation using a different ID space MUST enter the standard pipeline through an
explicit, versioned Adapter with its own tests and deviation record. Final global graph output
MUST contain only valid GlobalNodeIds. These rules prevent replica ambiguity, cross-partition ID
collisions, and accidental interpretation of local topology as global topology.

### IV. Three-Layer Artifact Provenance

Artifact, BuildManifest, and EvaluationReport MUST remain separate schemas, objects, and stored
references. An Artifact describes immutable data. A BuildManifest records how an Artifact was
produced, including source identity, inputs, parameters, toolchain, resources, status, and failure
reason. An EvaluationReport records integrity, quality, performance, and comparison findings.

Evaluation metrics such as reachability, Recall, QPS, or latency MUST NOT be embedded as intrinsic
Artifact fields, and provenance MUST NOT be reconstructed only from an EvaluationReport. This
separation allows immutable reuse, reproducible generation, and evaluation under multiple protocols.

### V. Declared Capabilities and Validated Handoffs

Every Partition, Local Build, Merge, post-processing, and Adapter implementation MUST declare
`requires`, `provides`, input ID space, and output ID space. Declarations MUST cover Artifact type
and version, required and provided capabilities, and required runtime services.

Before execution, the Harness MUST prove that upstream `provides` satisfy downstream `requires`.
Every new or reused Artifact MUST pass validation of format, version, checksum, ID space, and
capabilities before downstream execution. Failed, missing, incompatible, or unvalidated Artifacts
MUST NOT enter downstream execution, evaluation, cache reuse, comparison, or result aggregation.
Validation is a hard gate, not an advisory warning.

### VI. Separated and Fair Scientific Reporting

Baseline results, single-paper reproduction results, and cross-paper composition results MUST be
reported as separate study classes. A cross-paper combination MUST NOT be presented as evidence
that any single paper was faithfully reproduced. Exact reproduction, faithful adaptation, and
inspired implementation MUST also remain explicitly distinguished, with all deviations recorded.

Experiments MUST NOT silently change frozen parameters, query sets, ground truth, distance metric,
Recall target, cache condition, resource budget, or timing boundary. Repeated runs MUST follow a
predeclared aggregation rule and MUST NOT report only the best run. Parameter search MUST be an
explicit experiment with a preserved search space, configuration, stopping rule, and complete
results, including failures and invalid runs. These controls are required for fair comparisons.

### VII. Testable, Traceable, Repeatable Execution

All code and experiment logic MUST be testable, traceable to a spec/contract/source mapping, and
repeatable from recorded manifests and configuration. After code changes, contributors MUST run
the repository tests that actually exist and apply to the change. Reports MUST separately list
tests executed, tests failed, and applicable tests not executed with reasons; incomplete validation
MUST NOT be described as passed.

Algorithm-semantic changes MUST pass applicable unit tests, small-graph property tests, SIFT1M
validation, and human diff review before SIFT100M execution. Experiments larger than SIFT1M require
explicit human approval. These ordered gates bound cost and prevent unreviewed semantic drift from
being amplified into large experiments.

## Baseline and Experiment Constraints

The ratified Baseline source identity is:

```yaml
repository: https://github.com/microsoft/DiskANN.git
branch: cpp_main
commit: 78256bbab4685e1774e78d331e081a153be26823
source_path: upstream/diskann-cpp-main
```

For this commit, descriptions of Baseline behavior MUST agree with both source and the
commit-bound Baseline source mapping. In particular, `merge_shards` maps local IDs to global IDs,
unions replica-local adjacency candidates, removes duplicates, randomly shuffles candidates, and
truncates to the configured maximum degree. It MUST NOT be described as distance pruning,
`RobustPrune`, global refinement, connectivity detection, or connectivity repair.

Upstream Faithful, Instrumented, and Deterministic Experimental modes MUST use distinct
implementation and reproduction-mode identities. Instrumentation requires parity evidence;
deterministic or semantic variants require explicit deviation records. A run missing required
identity, provenance, compatibility, or validation evidence is invalid and MUST NOT be aggregated.

## Development Workflow and Quality Gates

1. A feature specification MUST identify its study class, replacement scope, Baseline reference,
   ID-space obligations, Artifact flow, comparison protocol, dataset scale, and approval needs.
2. An implementation plan MUST pass the Constitution Check before research and again after design.
   Any exception requires written justification and explicit human approval; Baseline mutation and
   validator bypass are never justifiable exceptions.
3. Task lists for code or experiment-logic changes MUST include applicable unit, contract,
   small-graph, integration, parity, or SIFT1M validation work. Documentation-only changes MUST
   still name their applicable document validation and may mark code tests not applicable with a
   reason.
4. Contract changes MUST update the schema version, validator, Registry declarations, Adapters,
   tests, and affected specs together. Breaking changes MUST use a new format version.
5. Before any stage runs, the Harness MUST validate module compatibility and all input Artifacts.
   Before aggregation, it MUST validate run identity, manifest completeness, experiment-class
   separation, and the predeclared aggregation protocol.
6. Human approvals for Baseline migration, semantic review, or experiments larger than SIFT1M
   MUST be recorded in a review or experiment record; an unstored verbal assumption is not approval.

## Governance

This constitution is the highest-authority project governance document for specifications, plans,
tasks, implementations, experiments, and reports. `AGENTS.md` supplies compatible operational detail;
if project guidance conflicts with this constitution, work MUST stop until the conflict is resolved
through an explicit amendment rather than silently selecting the weaker rule.

Amendments require a documented rationale, affected-principle analysis, migration or compatibility
impact, updates to dependent templates and runtime guidance, and explicit human approval. Versioning
follows semantic versioning: MAJOR for removal or incompatible redefinition of governance principles,
MINOR for new principles or materially expanded obligations, and PATCH for non-semantic clarification.

Every plan MUST contain a Constitution Check, and every spec/task/review MUST demonstrate compliance
with applicable MUST rules. Constitution violations are blocking findings. Compliance reviewers MUST
also confirm that reports do not exceed the evidence from completed validation and that required
approvals are recorded.

**Version**: 1.0.0 | **Ratified**: 2026-08-03 | **Last Amended**: 2026-08-03
