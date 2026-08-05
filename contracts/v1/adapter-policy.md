# Adapter Policy: Representation-Preserving Conversion v1

This document is the sole design authority for Feature 001 Adapter policy. It records only the
semantics already approved by the Adapter clarification and FR-042 through FR-044. It does not add
a production Adapter or authorize an algorithmic transformation.

## Permitted conversions

An operation may be classified as an Adapter only when it is demonstrably lossless and limited to
one or more of the following representation changes:

- serialization, container, storage-layout, or endianness conversion;
- integer-width conversion proven range-safe for every converted value;
- physical record relocation that preserves observable logical record order and adjacency order;
- explicit LocalNodeId-to-GlobalNodeId or GlobalNodeId-to-LocalNodeId relabeling through a complete,
  unambiguous map in which every source occurrence has exactly one valid target.

Replicas must remain distinguishable. A permitted Adapter cannot aggregate replica occurrences or
change their logical identity.

## Required evidence

Every proposed Adapter conversion must declare its accepted and produced format versions and ID
spaces and retain all of the following evidence:

- a complete source-to-target trace for every converted logical record and ID occurrence;
- either round-trip evidence or canonical logical-equivalence evidence;
- the source checksum and the output checksum, even when the physical representation makes them
  differ.

Evidence must be sufficient to verify that logical record order, adjacency order, adjacency
content, node identity, and replica distinctions are preserved.

## Rejection and semantic boundary

The policy validator must reject Adapter classification when the trace is absent, incomplete,
ambiguous, or out of range; equivalence evidence is absent; integer conversion can overflow; only
the output checksum is retained; observable record order changes; adjacency content or order
changes; or replicas are aggregated.

The following are algorithm-semantic changes, not Adapter operations: adding, removing,
deduplicating, sorting, reordering, truncating, or symmetrizing adjacency; merging or splitting
nodes; inventing IDs; recomputing distances or changing the metric; quantizing or mutating vectors;
or changing entry points, centroids, partition ownership, candidate sets, degree rules, parameters,
randomness, pruning, refinement, or connectivity behavior. Such a change requires a separate
implementation with its own `requires`, `provides`, tests, and declared deviation; Feature 001 does
not implement one.
