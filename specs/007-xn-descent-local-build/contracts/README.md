# XN-Descent Feature-local Contracts

These contracts define only B1-specific parameters and execution semantics:

- `xn-descent-parameters.schema.json`: preserved pre-relaxation design baseline;
- `xn-descent-parameters-v1.1.0.schema.json`: planned revision-1.1.0 design authority with the
  profile degree restrictions removed;
- `xn-descent-execution-contract.md`: descriptor, Algorithm-1 round/termination behavior, native
  encoding boundary, and the admitted profiles' shared partitioned execution path.

They do not redefine LocalGraphArtifact/v1, BuildManifest, ValidationEvidence, identifiers,
capabilities, P0, B0, or M0. During T033, the revision-1.1.0 parameter schema is copied
byte-for-byte to
`contracts/papers/xn-graph-xn-descent-v1/schemas/xn-descent-parameters-v1.1.0.schema.json` and
accompanied by source provenance. Both copies use
`https://diskann-methods.local/specs/007-xn-descent-local-build/contracts/xn-descent-parameters-v1.1.0.schema.json`
as their shared `$id`; the Feature-local revision file remains the design authority. The old file
and historical evidence remain preserved.
