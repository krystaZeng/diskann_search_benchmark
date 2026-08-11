# Optional Stage Source Contract

## Current gap

`PipelineExecutionRequest` currently contains one `SourceIdentity source`, and every Partition,
Local Build and Merge BuildManifest receives that same value. `source_config_json` is the shared
request-level run/config identity containing only `yaml_sha256` and `normalized_json_sha256`; it is
not the manifest's authoritative source identity or stage-specific build provenance. No existing
equivalent stage-specific source mechanism exists.

## C++17 amendment

Add one aggregate type and one optional request member:

```cpp
struct StageSourceIdentities {
  SourceIdentity partition;
  SourceIdentity local_build;
  SourceIdentity merge;
};

struct PipelineExecutionRequest {
  // Existing fields remain in place.
  SourceIdentity source;
  std::optional<StageSourceIdentities> stage_sources;
};
```

No default constructor behavior, map, list, cardinality, source Registry or plugin is added.

## Resolution

| Request form | Partition manifest source | Local Build manifest source | Merge manifest source |
|---|---|---|---|
| `stage_sources` absent | `request.source` | `request.source` | `request.source` |
| `stage_sources` present | `stage_sources.partition` | `stage_sources.local_build` | `stage_sources.merge` |

The executor implements one private `source_for(StageKind)` and passes the result into the existing
`base_manifest`. Reused-stage manifests use the same stage resolution rule. BuildManifest schema
and codec remain unchanged because each manifest still contains exactly one SourceIdentity.

## Shared source_config remains unchanged

The public RunRequest source-config contract remains:

```json
{
  "yaml_sha256": "<sha256>",
  "normalized_json_sha256": "<sha256>"
}
```

The executor may include this identical shared value in every parent Stage BuildManifest. It must
not be repurposed for GP-ANN build, executable or subprocess evidence, and no stage-specific
source-config schema is added.

## Existing StageResult build-manifest transport

`StageResult.build_manifest_path` carries an optional stage-owned existing BuildManifest/v1:

- empty path preserves all legacy parent Manifest parameter behavior;
- non-empty path must be processed for a successful or failed outer StageResult before its parent
  Manifest is finalized, be workspace-confined, parse and validate as the current invocation's
  BuildManifest/v1, and have an internally valid terminal status/failure shape; the subordinate
  status may remain `SUCCEEDED` when later post-process validation makes the outer stage fail;
- the executor persists the exact subordinate bytes immutably in the current run manifest area,
  computes SHA-256, and adds only to the corresponding parent Manifest parameters:

```json
{
  "stage_execution_manifest": {
    "path": "<workspace-relative persisted path>",
    "sha256": "<sha256>"
  }
}
```

The reference is never propagated to another stage. Missing, malformed, mismatched or
path-escaping subordinate input fails closed. This changes neither the StageResult shape nor the
BuildManifest schema/codec and adds no metrics transport, Registry or generic provenance framework.

## OGP formal request

The Feature runner requires a complete present mapping:

- Partition: official GP-ANN commit `df5efb4a6b303011c795666ed267f29d994b558c`;
- Local Build: fixed DiskANN cpp_main commit
  `78256bbab4685e1774e78d331e081a153be26823`;
- Merge: the same fixed DiskANN identity.

The runner completes both source/build admissions before constructing its Registry or invoking the
executor. Source admission is not moved into Registry descriptors.

## Compatibility and failure rules

- Existing callers that omit `stage_sources` must produce byte/semantic-equivalent source fields
  to current behavior.
- Existing stages returning an empty `build_manifest_path` must preserve their current parent
  Manifest parameters and lifecycle.
- A present mapping with any missing field is invalid during request normalization.
- The OGP runner rejects a Partition source other than admitted GP-ANN or Local/Merge source other
  than admitted DiskANN before factory creation.
- The fixed cpp_main Baseline runner neither accepts nor needs `stage_sources` for OGP and retains
  its exact P0+B0+M0 admission gate.
