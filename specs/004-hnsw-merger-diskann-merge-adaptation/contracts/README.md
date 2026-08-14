# Feature 004 Design Contracts

These contracts are authoritative only for the HNSW-Merger DiskANN Merge Adaptation design. They
do not create new public Artifact or EvaluationReport families.

| Contract | Purpose |
|---|---|
| `hnsw-merger-vamana-parameters.schema.json` | fixed M1 implementation parameter document |
| `merge-only-execution.md` | minimal optional C++17 executor entry plus the Feature-private one-request/one-runner comparison wire contract |
| `same-input-structural-comparison.schema.json` | Feature-local evidence that M0 and M1 consumed the same input identities and independently passed structural validation |

Implementation will copy the parameter schema byte-for-byte to
`contracts/papers/hnsw-merger-v1/schemas/`. The Merge-only contract amends only the C++ request;
the existing public RunRequest JSON schema remains unchanged. The comparison schema is not copied
into `contracts/v1` and is not a public lifecycle/status/report contract.
