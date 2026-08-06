# Contract Gap Status: cpp_main P0/B0/M0

This record applies to fixed upstream commit
`78256bbab4685e1774e78d331e081a153be26823`. It records only the reviewed v1 representation
amendment; it does not implement a Stage Binding, execute Graph Parity, or register a Baseline.

| Stage | Confirmed gap | Minimal amendment | Gate A final status |
|---|---|---|---|
| P0 | Native P0 emits complete ID maps and centroids but no shard-vector files. | Keep Dataset lineage, PartitionId, ownership, and every complete LocalToGlobal map mandatory; make `shard_vectors_role` optional; permit one aggregate `centroids` payload paired with `partition.centroids/v1`. | `separability: separable`; `contract_status: compatible`; `availability: planned` |
| B0 preparation | B0 needs vectors even when P0 did not emit shard vectors. | Use the selected Artifact vectors when present; otherwise require the validated Dataset-backed `StageContext::vector_accessor` and materialize in exact map order. Treat temporary vectors as B0 preparation, never P0 output. | `separability: separable`; `contract_status: compatible`; `availability: planned`; `binding_mode: upstream_callable_wrapper`; `validation_scope: structural_and_contract` |
| M0 | Native M0 emits a merged graph and a separate medoids file. | Keep `global_graph` required; permit one optional `medoids` payload; pair it with `graph.medoids/v1`; include both payload identities in the existing aggregate checksum projection. | `separability: separable`; `contract_status: compatible`; `availability: planned` |

The generic GlobalGraph contract does not require medoids. A future cpp_main M0 descriptor must
declare `graph.medoids/v1`; output validation then rejects either a missing medoids payload or an
undeclared medoids payload. No `MedoidsArtifact`, sidecar abstraction, Contract v2, or Adapter is
introduced.

Existing Fake PartitionArtifacts retain their shard-vector roles and exact frozen checksums and
remain valid. Existing graph-only Fake GlobalGraphArtifacts also remain valid.

Gate A concludes:

```yaml
native_reference_reachable: yes
unified_graph_parity_reachable: yes
formal_gate_status: not_reached
baseline_registration: prohibited
```

Incomplete exact-byte capture is retained as an input to the later frozen Parity Protocol's
comparison strategy. It is not a P0, B0, or M0 implementation blocker and does not require the B0
wrapper to be byte-identical to the native top-level loop.

Remaining work is implementation rather than approval to alter algorithms: native float32 Dataset,
centroids, local graph, merged graph, and medoids format-specific parsers/validators; pinned
P0/B0/M0 Stage Bindings; build/provenance integration; and a later, separately frozen Graph Parity
Protocol. Unified Graph Parity is design-reachable, but the formal gate remains `not_reached` until
those bindings exist and the formal protocol is executed; `validated_graph_baseline` remains
unregistered and registration is prohibited.
