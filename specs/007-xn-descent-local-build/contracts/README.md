# XN-Descent Feature-local Contracts

These contracts define only B1-specific parameters and execution semantics:

- `xn-descent-parameters.schema.json`: normalized effective parameters and validation profiles;
- `xn-descent-execution-contract.md`: descriptor, Algorithm-1 round/termination behavior, native
  encoding boundary, and the two admitted execution paths.

They do not redefine LocalGraphArtifact/v1, BuildManifest, ValidationEvidence, identifiers,
capabilities, P0, B0, or M0. During implementation, the parameter schema is copied byte-for-byte to
the established runtime paper-contract location and accompanied by source provenance; this source
file remains the design authority.
