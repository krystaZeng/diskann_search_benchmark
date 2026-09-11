# XN-Descent B1 Execution Contract

## 1. Identity and claim boundary

```yaml
implementation_id: xn_graph_xn_descent_local_build_v1
implementation_version: 1.0.0
stage: local_build
study_class: single_paper_reproduction
reproduction_mode: faithful_adaptation
replacement_scope: local_build_only
```

This identity reproduces the XN-Descent component over a current P0 Partition. It does not claim
the paper's BBP partitioning, Secondary Index, IMF Search, SSD layout/cache, or end-to-end results.

## 2. Descriptor

```yaml
requires:
  input_artifacts:
    - artifact_type: dataset
      accepted_format_versions: [diskann-float32-dataset/v1]
      input_id_space: none
    - artifact_type: partition
      accepted_format_versions: [partition-artifact/v1]
      input_id_space: local
  capabilities:
    - dataset.diskann_float32_bin/v1
    - partition.complete_local_to_global_map/v1
  runtime_services:
    - vector_accessor/v1
    - distance_function/l2-v1
    - cancellation_token/v1
    - metrics_sink/v1
provides:
  artifact_type: local_graph
  format_version: local-graph-artifact/v1
  output_id_space: local
  capabilities:
    - graph.local_adjacency/v1
parameter_schema_path: contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters-v1.1.0.schema.json
test_only: false
```

The descriptor keeps its existing implementation identity and version. Its exact schema path and
the shared Feature/runtime XN `$id`
`https://diskann-methods.local/specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json`
select parameter-contract revision `1.1.0`; this value is distinct from the historical XN `$id`,
and the JSON Schema dialect URI does not select a contract revision. The normalized parameter document remains
`schema_version: xn-descent-parameters/v1`. The Feature source at
`specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json` and the
runtime schema above are byte-identical and their real SHA-256 mapping is recorded in
`contracts/papers/xn-graph-xn-descent-v1/SOURCES.json`. Existing schema paths and historical
evidence remain preserved as the pre-relaxation baseline.

The stage accepts an aggregate PartitionArtifact plus one explicit PartitionId. It resolves vectors
by applying the selected LocalToGlobal map before calling `VectorAccessor`. It does not require a
P0 centroid or ascending-map capability.

### 2.1 XN runner request wire versions and source provenance

The Feature-local `xn_descent_experiment_runner` accepts two request wire versions. They do not
change the B1 implementation identity/version or the XN parameter document's
`schema_version: xn-descent-parameters/v1`.

- `request_version: 1.0.0` retains the historical input shape. The caller must provide exactly
  `source_config.yaml_sha256` and `source_config.normalized_json_sha256`; after validation that
  object is preserved as the execution source configuration.
- `request_version: 1.1.0` is the JSON-only input shape. The caller must omit `source_config`.
  The runner derives the source configuration from the exact request text it received before
  creating the workspace, Registry, factories, or stages.

For `1.1.0`, the generated source configuration is:

```json
{
  "source_format": "json",
  "raw_request_sha256": "<SHA-256 of the exact input bytes>",
  "normalized_json_sha256": "<SHA-256 of xn-json-compact-v1 bytes>",
  "normalization_policy": "xn-json-compact-v1"
}
```

`xn-json-compact-v1` first uses the existing strict parser, which rejects invalid JSON and
duplicate object keys. It then uses the parsed `nlohmann::json` value's ordered object keys and
compact `dump()` bytes: no trailing newline, no default-field injection, and no array reordering.
This is a Feature-local serialization rule, not RFC 8785. It does not use
`canonicalize_restricted_json`, because valid XN requests include floating-point fields such as
`alpha`.

The runner supplies the resulting object through the existing
`PipelineExecutionRequest.source_config_json`. `PipelineExecutor` places it in each parent stage
BuildManifest's `parameters.source_config`; no second Manifest or publication lifecycle exists.
Because a `1.1.0` input contains no `source_config`, hashing the complete raw and parsed request
does not create a self-reference. The separate SHA-256 of the final request file remains an
approval-time integrity check in `quickstart.md`.

## 3. Validation ownership

Before construction, the execution owner must:

1. validate Dataset and Partition Artifact versions, identity, payload checksums, lineage, ID
   spaces, and required capabilities;
2. validate the complete effective parameter document with both schema shape and semantic checks;
3. bind a squared-L2 DistanceFunction and a matching vector service;
4. validate positive memory and thread budgets;
5. reject a known mandatory-memory excess with checked arithmetic before major allocation.

The builder repeats B1 parameter and selected-Partition checks. No validation failure may be
converted into an empty or truncated valid graph.

## 4. Round semantics

For round `t`:

1. Candidate membership comes only from snapshot `C_t[i] union R_t[i]` and is traversed by
   `(d2(i,u),u)`.
2. A reverse proposal `R_(t+1)[u] <- i` is emitted for every candidate before its acceptance
   outcome.
3. `flag` is local to the current `(i,u)` check.
4. `G_t[i]` is copied to node-owned working `G[i]`; changes caused by earlier candidates are visible
   to later candidates for that same owner.
5. For every current `v != u` encountered in working `G[i]`, both cross-`TryInsert` proposals are
   emitted. Finding an occluder does not end this scan.
6. Occlusion is true only when both strict comparisons hold:

   ```text
   d2(i,v) < d2(i,u)
   alpha^2 * d2(v,u) < d2(i,u)
   ```

7. If `u` is not occluded, all current `v` occluded by `u` are removed before
   `TryInsert(u,G[i],lambda)`. Removal uses the role-reversed strict predicates
   `d2(i,u) < d2(i,v)` and `alpha^2 * d2(u,v) < d2(i,v)`. A later capacity eviction does not
   restore removed members.
8. All cross-`C` proposals and reverse proposals are deterministically reduced, and `C/R/G` commit
   together at the next boundary. Cross-`C` retains the closest `omega`; `R` is not capped.

The distance/tie total order cannot turn equality in Equation (2) into occlusion. The scale-safe
comparator implements `alpha^2`, never `alpha`, on squared distances.

## 5. Convergence and termination

After a full round, canonical `C`, `R`, and `G` are directly compared with their boundary snapshot.
If every list for every node is equal, convergence is immediate with `fixed_point=true` and
`convergence_condition=full_state_fixed_point`. Digest equality is retained evidence but does not
replace direct comparison.

If full-state equality is absent, three consecutive complete rounds with both normalized `C` and
normalized `G` unchanged also converge, with `fixed_point=false`, `stable_c_g_rounds>=3`, and
`convergence_condition=three_round_c_g_stability`. Any `C` or `G` change resets the count. This
exact rule covers both phases of `R_(t+1)=reverse(C union R_t)` once `C` is fixed; it is a faithful
adaptation because Algorithm 1 does not define `Converged`. Stable `G` alone is insufficient.

The cap counts complete rounds. Either condition found on the last permitted round succeeds. If
neither condition holds on that round:

- the stage terminates unsuccessfully with existing `FAILED` status;
- the precise reason is `iteration_cap_nonconverged`;
- no output candidate or valid LocalGraphArtifact is created;
- BuildManifest retains cap, completed rounds, final state digests, and exact change counts;
- no partial graph is eligible for M0, evaluation, cache, comparison, or aggregation.

Cancellation, numeric failure, checked-size overflow, allocation failure, or resource exhaustion
has the same no-output publication policy with its own reason.

## 6. LocalGraph and native encoding

Successful B1 output reuses LocalGraphArtifact/v1. Its selected P0 map is copied byte-for-byte. The
adjacency is serialized using the reviewed cpp_main local-graph layout and exact local-graph media
type required by current M0:

```text
uint64 declared_file_size
uint32 actual_maximum_observed_degree
uint32 deterministic_local_entry_point
uint64 frozen_point_count = 0
for each LocalNodeId in ascending order:
  uint32 degree
  uint32 neighbor[degree]
```

All scalars are emitted in the admitted little-endian native contract. The Feature-local producer
binds this serializer to the pinned DiskANN layout source
`78256bbab4685e1774e78d331e081a153be26823`, while the algorithm remains the distinct B1 project
implementation. The BuildManifest records both identities.

Required claims are:

- `declared_max_degree == lambda`;
- `observed_max_degree ==` actual payload maximum;
- native header maximum `== observed_max_degree <= lambda`;
- local entry point is the deterministic local medoid and is in range;
- every adjacency is LocalNodeId, in range, unique, self-free, and degree-bounded;
- selected map bytes/checksum and `source_partition_checksum` match the owning Partition.

The existing Artifact validator still owns format/checksum/range/header admission. Producer and
paper-specific checks additionally enforce self-free and duplicate-free adjacency because the
current native reader does not check those properties.

## 7. Execution profiles

### 7.1 Complete partitioned execution

Before constructing the Registry or invoking P0, a Feature-local runner requires:

- exact current P0 identity;
- exact B1 identity above;
- exact current M0 identity;
- profile `pipeline-compatible` or `paper-style`;
- explicit positive B1 `lambda`;
- explicit M0 `merge.R` as an integer in `1..UINT32_MAX`, independent of `lambda`.

The Feature-local runner checks `merge.R` before Registry/factory construction, expensive
allocation, workspace/runtime-service creation, or stage dispatch. Missing, boolean, string,
fractional, negative, zero, and greater-than-`UINT32_MAX` values fail with a
`merge.R`-specific reason; factory and P0/B1/M0 invocation counts remain zero. Signed and unsigned
JSON integers are range-checked before uint32 conversion and no `double` comparison is used. The
public `ParameterValidator` remains structural/type/constant-only, and M0 repeats the range check
at runtime.

Invalid request fields fail before stage dispatch. Success uses the existing `PipelineExecutor`,
produces and validates one B1 LocalGraph for every Partition, and passes only the complete valid
collection to the existing M0. M0 calls pinned `merge_shards` with `merge.R`.

The frozen Feature 002 Baseline Profile remains `R=16` with B0 local degree 10. Parameterizing the
reusable M0 does not change those runner values or create a new Merge identity or algorithm.

Because the reusable M0 revision accepts positive values other than 16, the fixed Baseline runner
must independently require integer merge R=16 and use_filters=false at its request boundary before
workspace, runtime-service, Registry, or stage creation. The direct executable-entry regression is
owned by tests/cpp/baseline/test_cpp_main_pipeline.cpp, with only the actual
cpp_main_baseline_runner target path supplied by tests/cpp/baseline/CMakeLists.txt. The existing
Python profile-normalization test remains applicable but does not replace the direct runner test.

### 7.2 paper-style parameter profile

`paper-style` may identify a paper-derived parameter selection such as `lambda=64`, but 64 is not
required and it does not select a different lifecycle. It must provide explicit `merge.R` and use
the same complete P0,
per-Partition B1, reusable M0, and GlobalGraph path. It cannot be labeled Baseline-compatible or
full-system reproduction.

### 7.3 semantic-small

This profile is admitted only by unit/oracle/contract validation entry points. It cannot be used as
evidence of SIFT1M completion or paper-scale/system reproduction.

## 8. Replay guarantee

For identical validated inputs, effective parameter document, seed, implementation, squared-L2
behavior, platform, architecture, compiler/flags, and declared numerical environment, the accepted
graph and termination evidence are exact-replay outputs. Any valid thread count and scheduling are
result-invariant. macOS and Linux each satisfy this guarantee independently; equality between their
graph payloads is recorded when observed but is not required.
