# Feature 005 contract designs

These are Phase 1 design contracts. They do not implement or publish runtime support.

| Contract | Scope | Runtime disposition |
|---|---|---|
| `common.schema.json` | public additive source authority | full runtime v1 schema with only `pq` and `disk_layout` added to `artifactType` |
| `build-manifest.schema.json` | public additive source authority | full runtime v1 schema with only `search` added to the document `stage` enum |
| `pq-artifact.schema.json` | public reusable data | source for a new runtime `PQArtifact/v1` schema |
| `disk-layout-artifact.schema.json` | public reusable data | source for a new runtime `DiskLayoutArtifact/v1` schema |
| `search-protocol.schema.json` | Feature-local immutable request | remains Feature-local |
| `search-observation.schema.json` | Feature-local raw evidence, one point/document | remains Feature-local |
| `search-evaluation-report.schema.json` | Feature-local evaluation report | remains Feature-local |
| `cli.md` | fixed materialize/search/evaluate boundary | Feature-local runner/Harness contract |

SearchProtocol binds the normalized QuerySet/GroundTruth preparation evidence, exact admitted
runner binary and BuildManifest, and the single frozen cache input `requested_nodes`.
Source-derived requested/effective primary cache bytes belong to admission/runtime evidence, not
the request controls. SearchObservation binds the actual runner binary, its separate search
BuildManifest, raw environment fingerprint, and raw QueryStats without eligibility decisions.
SearchEvaluationReport independently derives quality/performance eligibility and references Search
Runner Parity and P0/B0/M0 non-reexecution evidence without turning either into a public report
family or a graph-certification claim.

Implementation must list all four public schema sources explicitly in `contracts/v1/SOURCES.json`.
The `common.schema.json` and `build-manifest.schema.json` runtime targets must change authority to
the full Feature 005 sources above; PQArtifact and DiskLayoutArtifact receive their new Feature 005
source mappings. Every other existing runtime target continues to point to its original Feature 001
source. Each runtime target has exactly one source, remains byte-identical to that source, and
verifies the source SHA-256. `test_contract_source_sync.py` may remove only the obsolete assumption
that every source is under Feature 001; unique target ownership, confined safe paths, source/target
existence, byte identity, and checksum verification remain mandatory. This is not an overlay,
patch, or multi-source composition mechanism. The designs must not be copied into Feature 001 and
do not add a fourth Pipeline stage. Feature-local schemas define a six-type ArtifactRef projection
so they can refer to the two planned public types before the approved runtime amendment is applied.
