# Feature 006 Contract Boundary

Feature 006 adds one authoritative design source for an additive public Artifact format and four
Feature-local documents. It does not add an Artifact type, Registry, Pipeline stage, Adapter, or
generic certification system.

## Additive public contract source

`global-graph-artifact-v2.schema.json` is the complete authoritative design source for the future
runtime target:

```text
contracts/v1/schemas/global-graph-artifact-v2.schema.json
```

Implementation must add one `contracts/v1/SOURCES.json` entry whose source is this Feature 006 file.
Source and target remain byte-identical and source SHA-256 is verified by the existing source-sync
test. The existing v1 source and target are not edited or remapped.

The v2 format keeps `artifact_type=global_graph`, `id_space=global`, existing canonical Artifact
checksum semantics, and existing graph/medoids media types. Its only lineage mode is
`direct_native_final`; its sole source Artifact is the admitted Dataset. Generation provenance stays
in the independent BuildManifest, and validation facts stay in independent ValidationEvidence.

## Feature-local contracts

| File | Purpose | Public runtime schema |
|---|---|---|
| `native-reference-build-request.schema.json` | Exact immutable request for the fixed upstream native sharded Reference build and final-graph capture | No |
| `quality-parity-policy.schema.json` | Pre-search frozen policy binding exact graphs, layouts, shared inputs, protocols, controls, evaluator, and 0.010 rule | No |
| `quality-parity-evidence.schema.json` | Pure comparison evidence over existing Feature 005 reports and all attempted observations | No |
| `framework-baseline-certification-decision.schema.json` | Pure final `CERTIFIED | NOT_CERTIFIED | INCONCLUSIVE` evidence decision | No |
| `cli.md` | Feature-local process and human-approval boundaries | No |

These documents are not Artifacts or BuildManifests. They use path, byte length, SHA-256, existing
ArtifactRef identities, and document-specific canonical checksums where required. No certification
ID is introduced.

## Contracts reused unchanged

- Public DatasetArtifact, ArtifactRef, BuildManifest/v1, ValidationEvidence/v1, PQArtifact/v1, and
  DiskLayoutArtifact/v1.
- GlobalGraphArtifact/v1 for the existing Framework P0+B0+M0 graph.
- Feature 002 structural policy/report schemas and four-state helper.
- Feature 005 SearchProtocol/v1, SearchObservation/v1, SearchEvaluationReport/v1, QuerySetReference,
  GroundTruthReference, formal-search preflight, search runner, and evaluator.

DiskLayoutArtifact/v1 already permits a `global_graph` source identity without constraining that
source to v1. The future implementation changes only the fixed DiskLayout materializer descriptor's
accepted GlobalGraph versions from `{v1}` to `{v1,v2}` and tests the existing lineage validator with
a validated v2 source. No DiskLayout v2 or schema amendment is needed.
