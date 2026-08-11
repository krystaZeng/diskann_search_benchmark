# OGP Feature Contracts

These files are the Feature 003 design authority for the selected OGP integration. They do not
define a new public Artifact or generic Adapter/backend framework.

- [official-partition-subprocess.md](official-partition-subprocess.md): exact selected official CLI
  boundary, file lifecycle and failure rules.
- [stage-sources.md](stage-sources.md): the only public request amendment, an optional typed
  Partition/Local Build/Merge source split with request-level fallback, plus the fixed semantics for
  using the already existing `StageResult.build_manifest_path` without changing its shape.
- [ogp-parameters.schema.json](ogp-parameters.schema.json): implementation parameter contract;
  epsilon is intentionally absent because the CLI fixes it.

The runtime implementation parameter schema is a byte-identical copy at
`contracts/papers/gp-ann-ogp-v1/schemas/ogp-parameters.schema.json`, referenced by the OGP
ImplementationDescriptor through a repository-relative `parameter_schema_path`. Copy provenance
records source, target and SHA-256. The existing `PartitionArtifact/v1`, BuildManifest,
ValidationEvidence and GlobalGraph EvaluationReport contracts remain authoritative and unchanged.
The MVP defines no OGP-specific Artifact, EvaluationReport, worker-provenance object or report
schema; effective Partition and shard sizes remain available from validated Artifact metadata.

The public `source_config` remains the existing shared two-digest run/config identity. Each actual
OGP subprocess uses an existing BuildManifest/v1 returned through
`StageResult.build_manifest_path`; the parent Partition BuildManifest references its immutable
persisted copy through `parameters.stage_execution_manifest {path, sha256}`. This introduces no new
BuildManifest field/schema/codec, StageResult field, report family or generic provenance framework.

Formal use of these contracts is conditional on a clean local checkout of official GP-ANN commit
`df5efb4a6b303011c795666ed267f29d994b558c` and its exact recursive dependency gitlinks. The
unauthenticated local research snapshot is not an admitted runtime source.
