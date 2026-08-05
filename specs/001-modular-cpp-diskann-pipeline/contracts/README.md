# Contract Set: Modular cpp_main DiskANN Pipeline v1

This directory, `specs/001-modular-cpp-diskann-pipeline/contracts/`, is the authoritative design
source for Feature 001 contracts. Repository-root `contracts/v1/` contains byte-identical runtime
copies of the approved schemas, checksum canonicalization contract, and Adapter policy contract.
`contracts/v1/SOURCES.json` is a generated source-tracking inventory for those copies; it is not a
second authority and is not itself copied byte-for-byte from another source file.

## Contract Inventory

| Contract | Purpose |
|---|---|
| [cpp-api.md](cpp-api.md) | C++17 public module, Registry, StageContext, and validator boundary |
| [cli.md](cli.md) | Python Harness commands and JSON-driven C++ runner protocol |
| [adapter-policy.md](adapter-policy.md) | Sole design authority for representation-preserving Adapter classification and evidence |
| [checksum-canonicalization.md](checksum-canonicalization.md) | Exact payload and aggregate SHA-256 input |
| [fake-fixture.md](fake-fixture.md) | Frozen six-node bytes, topology, and golden checksums |
| [examples/fake-experiment.yaml](examples/fake-experiment.yaml) | Feature-local two-run Fake experiment contract example |
| [schemas/common.schema.json](schemas/common.schema.json) | Shared JSON definitions |
| [schemas/artifact-ref.schema.json](schemas/artifact-ref.schema.json) | Standalone immutable Artifact reference |
| [schemas/dataset-artifact.schema.json](schemas/dataset-artifact.schema.json) | Validated Fake input dataset Artifact |
| [schemas/partition-artifact.schema.json](schemas/partition-artifact.schema.json) | Partition stage output metadata |
| [schemas/local-graph-artifact.schema.json](schemas/local-graph-artifact.schema.json) | Local Build output metadata |
| [schemas/global-graph-artifact.schema.json](schemas/global-graph-artifact.schema.json) | Merge output metadata |
| [schemas/build-manifest.schema.json](schemas/build-manifest.schema.json) | Generation/reuse provenance |
| [schemas/evaluation-report.schema.json](schemas/evaluation-report.schema.json) | Independent validation/evaluation findings |
| [schemas/validation-evidence.schema.json](schemas/validation-evidence.schema.json) | Machine-readable contract checks and failures |
| [schemas/quarantine-record.schema.json](schemas/quarantine-record.schema.json) | Isolated INVALID_OUTPUT retention record |
| [schemas/implementation-descriptor.schema.json](schemas/implementation-descriptor.schema.json) | Registry `requires`/`provides` declaration |
| [schemas/fake-parameters.schema.json](schemas/fake-parameters.schema.json) | Exact test-only Fake stage parameter contracts |
| [schemas/run-request.schema.json](schemas/run-request.schema.json) | YAML-normalized runner request |
| [schemas/runner-result.schema.json](schemas/runner-result.schema.json) | Stable C++ CLI outcome and exit classification |

## Runtime Source Inventory

`contracts/v1/SOURCES.json` has this minimum format:

```json
{
  "format_version": "diskann-contract-sources-v1",
  "entries": [
    {
      "source": "specs/001-modular-cpp-diskann-pipeline/contracts/adapter-policy.md",
      "target": "contracts/v1/adapter-policy.md",
      "sha256": "<SHA-256 of the exact source-file bytes>"
    }
  ]
}
```

`source` and `target` are repository-relative POSIX paths. Each `sha256` is calculated over the
exact bytes of the source file, and the copied target must be byte-identical to that source. The
`entries` array is stably sorted by `target`. `SOURCES.json` is generated after copying and does not
require a source entry for itself.

## Version and Compatibility Rules

- Schema documents use JSON Schema Draft 2020-12 and reject unknown fields unless a field is an
  explicitly implementation-specific parameter object.
- `schema_version` and Artifact `format_version` are independent. A schema describes metadata;
  `format_version` identifies the logical Artifact/payload interpretation.
- V1 accepts exact versions only. There is no implicit downgrade, migration, Adapter insertion, or
  capability alias.
- `ImplementationDescriptor.requires.input_artifacts[]` contains one type, accepted-version list,
  and input ID space per required input. Merge declares separate PartitionArtifact and
  LocalGraphArtifact entries; requirement-level capabilities/services remain outside those items,
  and `provides` remains a single output. `parameter_schema_path` is the stable
  repository-relative path to the implementation parameter JSON Schema.
- `ImplementationRegistry` is explicitly constructed with `kProduction` or `kTest`; production
  rejects `test_only: true`, test accepts both descriptor classes, and neither policy weakens
  duplicate/stage/sealing/no-overwrite/no-fallback checks.
- Breaking changes create a new format/schema version and update validator, Registry declaration,
  tests, and this Feature's design together.
- JSON Schema validates document shape. C++ semantic validators remain mandatory for paths,
  checksums, file lengths, graph/map contents, cross-document identity, capability compatibility,
  resolver isolation, and state transitions.

`ArtifactRef` always contains `artifact_type`, `format_version`, `aggregate_checksum`, and
`metadata_path`. The path resolves only to the immutable Artifact metadata document. It is never a
normalized stage Manifest, BuildManifest, or EvaluationReport path; those documents retain separate
references and lifecycles.

`run-request.schema.json` is Feature-001-specific: it requires
`study_class: infrastructure_validation` and exactly two repetitions under `retain_all`. A future
Feature that actually executes a Baseline must publish its own versioned request contract that can
use `baseline_validation`; it must not reuse or relabel this infrastructure-only schema.

Human YAML supplies the workspace root and budgets. The normalized RunRequest additionally records
the confined normal, temporary, manifest, report, and quarantine roots derived by `workspace.py`;
these run-location fields remain outside Artifact identity.

Partition output is one aggregate PartitionArtifact. Local Build consumes its validated
ArtifactRef plus an explicit in-range PartitionId and does not consume a separate shard ArtifactRef.
The resulting LocalGraphArtifact records the owning PartitionId and the aggregate source
PartitionArtifact checksum.

Determinism comparison applies to the EvaluationReport findings projection defined in
`data-model.md`, not to byte-for-byte equality of complete report documents. Report identity,
timestamps, paths, run provenance, and non-deterministic metrics remain independent.

## Validation Boundary

The order is fail-closed:

```text
duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation
→ normalized RunRequest schema → Registry preflight
→ input Artifact schema → payload/checksum/semantic validator
→ stage execution in temporary storage → output validator
→ normal publication OR quarantine → optional evaluation
```

No schema success alone grants downstream capability. Only the contract validator can return a
validated normal ArtifactRef.
