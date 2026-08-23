# Prebuilt search fixture

The executable contract test materializes this deterministic six-node Dataset/GlobalGraph and its
valid PQ/DiskLayout payload bytes into an isolated temporary workspace, then fresh-validates the
complete metadata, payload checksums, aggregate checksums, lineage, and ValidationEvidence before
search use. The fixed source identity is the test Dataset/GlobalGraph lineage declared by
`search_artifact_test_support.hpp`. No US1 production materializer is called by the US2 fixture.

The byte recipes below are reviewable source fixtures; the test writes their binary representation
only into its temporary test workspace.
