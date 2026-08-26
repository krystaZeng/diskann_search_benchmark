# Feature Specification: Framework Baseline Certification

**Feature Branch**: `feature/framework-baseline-certification`

**Created**: 2026-08-25

**Status**: Draft

**Input**: User description: "Establish the minimum certification closure between the fixed upstream DiskANN native sharded Reference and the modular Framework P0+B0+M0 candidate for SIFT1M/L2 static indexing, reusing Feature 002 structural policy and Feature 005 search-quality evidence semantics."

## Reproduction and Experiment Governance *(mandatory)*

**Study Class**: `baseline_validation`. This Feature answers only whether the current fixed Framework P0+B0+M0 composition has enough structural, provenance, search-semantic, and quality evidence to serve as the trusted DiskANN Baseline for later paper Features.

**Baseline Reference**: Repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit `78256bbab4685e1774e78d331e081a153be26823`, source location `upstream/diskann-cpp-main`, role `cpp_main_native_sharded_reference_v1`, and reproduction mode `upstream_faithful`. The source tree remains read-only and clean.

**Replacement Scope**: `none`. The Reference uses the fixed native sharded build path. The candidate uses the existing modular P0+B0+M0 composition with role `cpp_main_p0_b0_m0_framework_candidate_v1`. This Feature evaluates those fixed roles; it does not replace, reinterpret, or extend P0, B0, M0, search, or evaluation algorithms.

**Implementation Identities**: Reference role `cpp_main_native_sharded_reference_v1`; Framework role `cpp_main_p0_b0_m0_framework_candidate_v1`; instrumented search implementation `cpp_main_pqflash_search_instrumented_v1`. The reusable Search Runner Parity evidence records `PASS` for zero-cache and fixed-cache on the evidence-bearing fast admission path and is bound to Project commit `10f8520d8617fdaf8de3abf8a87842346ae4c96f`, runner SHA-256 `97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`, and the fixed DiskANN source identity. These are role and implementation identities, not certification IDs or new Registry identities.

**ID-Space Contract**: Both final graphs contain exactly the admitted Dataset's GlobalNodeIds. Each valid search result contains complete ordered top-K GlobalNodeIds. Partition-scoped LocalNodeIds and LocalToGlobalIdMaps remain governed by Features 001 and 002 and are not redefined here. Cross-graph ordered result equality is not required.

**Artifact Flow**: The fixed SIFT1M DatasetArtifact feeds an independently generated native Reference GlobalGraph and the existing Framework P0+B0+M0 GlobalGraph. Each graph feeds its own DiskLayout. Both search paths reuse one byte-identical PQArtifact and the same immutable QuerySet and exact GroundTruth. Artifact data, BuildManifest provenance, ValidationEvidence, SearchObservation, SearchEvaluationReport, structural parity evidence, quality comparison evidence, and the Feature-local certification decision remain separate.

**Validation Gate**: Every admitted source, Artifact, evidence document, search protocol, observation, and report must pass its existing identity, format, version, checksum, ID-space, lineage, and compatibility checks. The Feature 002 Structural/Contract Parity Policy remains frozen and solely derives `PASS`, `FAIL`, `INCONCLUSIVE`, or `NOT_REACHED`. Missing evidence is never inferred as passing.

**Experiment Protocol**: Scope is fixed to the existing SIFT1M float32 Dataset, L2, static unfiltered indexing, labels disabled, and no experiment above SIFT1M. Reference and Framework searches use separate graph and DiskLayout identities but the same byte-identical PQArtifact, `sift1M_dataset/selected_query.csv`, exact-L2 GroundTruth, K=100, ordered L-search declaration, beamwidth, threads, IO limit, warmup, query order, timing boundary, zero-cache and fixed-cache conditions, three repetitions, end-to-end entry policy, eligibility rules, and median-of-eligible aggregation inherited from Feature 005. Formal SIFT1M operations require immutable requests and separate human approval. Mac Docker quality results may support formal Recall decisions when quality-eligible; performance metrics remain informational and non-certifying.

## Clarifications

### Session 2026-08-25

- Q: What maximum Framework-versus-Reference shortfall is permitted for Recall@10 and Recall@100? → A: For each metric, the Framework aggregate may be at most 1.0 absolute percentage point (`0.010` on a `[0,1]` recall scale) below the corresponding Reference aggregate; a Framework value equal to or above the Reference is within tolerance.
- Q: How must the Recall tolerances be applied across cache conditions and L-search aggregates? → A: Quality Parity passes only when both Recall@10 and Recall@100 satisfy the frozen tolerance at every declared zero-cache and fixed-cache L-search aggregate; no cross-point averaging or compensation is allowed.
- Q: How do quality failures, insufficient evidence, and Search Runner Parity gaps map to the final decision? → A: Conclusive negative evidence—a completed Quality Parity threshold violation, an exact-current-identity Search Runner Parity `FAIL`, or complete valid evidence of a mandatory non-structural admission incompatibility—maps to `NOT_CERTIFIED`; an unevaluable condition caused by insufficient eligible repetitions or a missing aggregate, absent/stale/identity-mismatched parity evidence without a current explicit `FAIL`, or missing, invalid, incomplete, or ambiguous admission evidence maps to `INCONCLUSIVE`.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Capture a complete native Reference graph (Priority: P1)

As a Baseline maintainer, I need one complete, immutable final GlobalGraph from the fixed upstream native sharded build so the existing structural Gate can be reached without reconstructing missing native evidence.

**Why this priority**: The current Feature 002 Gate is `NOT_REACHED` because the earlier native probe did not retain the final merged Reference graph and matching structural evidence. No final certification decision is possible until that evidence gap is closed.

**Independent Test**: Execute an approved SIFT1M native Reference request and verify that the final merged GlobalGraph, its independent BuildManifest, its ValidationEvidence, source identity, Dataset identity, profile controls, terminal status, and upstream clean-state evidence are complete and mutually consistent.

**Acceptance Scenarios**:

1. **Given** the fixed clean upstream commit and admitted SIFT1M Dataset, **When** the approved native sharded Reference build succeeds, **Then** one immutable Reference GlobalGraph and all required generation and validation evidence are retained outside upstream.
2. **Given** the native build produces temporary P0/B0/M0 intermediates that are not retained, **When** certification admission evaluates the run, **Then** the complete final Reference GlobalGraph and actual available native evidence are sufficient inputs; no missing intermediate payload is fabricated or made newly mandatory.
3. **Given** a source, Dataset, profile, output, checksum, lineage, validation, or clean-state mismatch, **When** Reference admission runs, **Then** the Reference graph is rejected from the structural Gate and quality comparison, while terminal failure evidence is retained.

---

### User Story 2 - Re-derive the frozen structural Gate (Priority: P2)

As a Baseline maintainer, I need the existing Feature 002 Structural/Contract Parity Policy applied to the newly complete Reference evidence and the existing Framework evidence so structural compatibility is decided without changing the policy to fit the result.

**Why this priority**: Structural and contract parity is necessary but not sufficient for certification. Its four-state result must be known before a certification conclusion can be derived.

**Independent Test**: Supply the exact frozen policy and the complete admitted Reference and Framework evidence, reproduce every required check, and verify that the Gate result is derived as one of its existing four states with no search or quality criterion added to the policy.

**Acceptance Scenarios**:

1. **Given** all mandatory Feature 002 checks are present and pass, **When** the Gate is re-derived, **Then** it returns `PASS` and claims only structural-and-contract parity.
2. **Given** complete evidence with a failed mandatory check, **When** the Gate is re-derived, **Then** it returns `FAIL`, and the final certification result is necessarily `NOT_CERTIFIED`.
3. **Given** reached but incomplete or ambiguous mandatory evidence, **When** the Gate is re-derived, **Then** it returns `INCONCLUSIVE`, and the final certification result is `INCONCLUSIVE`.
4. **Given** a required execution or final-graph evidence is unavailable, **When** the Gate is re-derived, **Then** it returns `NOT_REACHED`, and the final certification result is `INCONCLUSIVE`.
5. **Given** any Gate result including `PASS`, **When** the evidence is reported, **Then** Search Runner Parity, Search Semantics, Quality Parity, and final certification remain separate claims.

---

### User Story 3 - Decide fixed-scope Framework certification (Priority: P3)

As a maintainer of later paper Features, I need a predeclared, auditable quality comparison and one bounded certification decision so I know whether P0+B0+M0 is a trustworthy fixed DiskANN Baseline without treating performance or cross-graph byte equality as certification evidence.

**Why this priority**: OGP and HNSW-Merger comparisons need a credible baseline. The decision must combine the structural Gate, exact runner parity identity, controlled search semantics, and Recall evidence without expanding into a general certification platform.

**Independent Test**: Using independently validated Reference and Framework DiskLayouts, the shared PQArtifact, the same QuerySet/GroundTruth, and two control-identical search schedules, retain every repetition, evaluate both sides offline, apply the predeclared quality and decision rules, and verify a single Feature-local result of `CERTIFIED`, `NOT_CERTIFIED`, or `INCONCLUSIVE`.

**Acceptance Scenarios**:

1. **Given** a Feature 002 Gate `PASS`, exact reusable Search Runner Parity identity, compatible inputs, identical controls, complete eligible Recall evidence, and a passing frozen Quality Parity rule, **When** the final decision is derived, **Then** it is `CERTIFIED` only for the fixed Framework P0+B0+M0 identity and scope.
2. **Given** the certification search uses a different runner source commit, binary SHA-256, implementation identity, or DiskANN source identity, **When** Search Runner Parity admission runs, **Then** the old parity evidence is rejected and fresh parity evidence is required before certification can proceed.
3. **Given** Reference and Framework searches use different final graphs but identical declared controls, **When** quality evaluation runs, **Then** their ordered result IDs and distances may differ; each side is assessed through complete top-K validity and Recall@10/Recall@100 rather than cross-graph exact-result equality.
4. **Given** a complete quality-eligible Mac Docker observation, **When** evaluation runs, **Then** Recall may enter the certification evidence while QPS, latency, and Mean IOs are at most `informational_non_formal` and never affect certification.
5. **Given** any failed, invalid, cancelled, identity-mismatched, or excluded run, **When** the final decision is prepared, **Then** its identity, status, and reason remain auditable and it contributes no metric or aggregate for which it is ineligible.
6. **Given** complete valid evidence of any Quality Parity threshold violation, an exact-current-identity Search Runner Parity `FAIL`, or a mandatory non-structural admission incompatibility, **When** the final decision is derived, **Then** it is `NOT_CERTIFIED`.
7. **Given** insufficient eligible repetitions or a missing aggregate, absent/stale/identity-mismatched parity evidence without a current explicit `FAIL`, or missing, invalid, incomplete, or ambiguous admission evidence that prevents a condition from being evaluated, **When** the final decision is derived, **Then** it is `INCONCLUSIVE`.
8. **Given** a final decision, **When** a later paper Feature consumes it, **Then** it certifies neither OGP, HNSW-Merger, changed controls, another Dataset, another metric, dynamic indexing, nor a larger scale.

### Edge Cases

- The upstream checkout matches the commit but is dirty before or after the native Reference build.
- The native Reference process succeeds but the final merged graph, BuildManifest, or ValidationEvidence is missing, invalid, or checksum-inconsistent.
- Native intermediate files are cleaned up even though the final Reference graph and required final evidence are complete.
- The Reference and Framework graphs bind different Dataset identities, profile controls, GlobalNodeId ranges, or maximum-degree contracts.
- The shared PQArtifact has the correct logical identity but differs by one byte between the two search inputs.
- One DiskLayout points to the wrong graph lineage or silently substitutes the other graph's medoid/entry lineage.
- A QuerySet, GroundTruth, cache, L-search list, repetition schedule, timing boundary, or evaluator differs across the two quality runs.
- A valid query returns fewer than K=100 result IDs or distances, duplicate positions are lost, or any result is outside GlobalNodeId range.
- The runner binary exists but its source commit, implementation identity, binary checksum, BuildManifest, or DiskANN source identity differs from the reused parity evidence.
- A repetition fails or is invalid after other repetitions for the same cache/L group succeed.
- The Structural/Contract Gate is `PASS`, but quality evidence is missing, incompatible, or fails the frozen Quality Parity rule.
- Mac Docker exposes complete quality evidence but does not satisfy formal Linux performance admission.
- A consumer interprets `CERTIFIED` as adjacency equality, exact ordered-result equality across graphs, performance certification, paper-algorithm certification, or support beyond SIFT1M/L2/static indexing.

## Requirements *(mandatory)*

### Functional Requirements

#### Fixed identities and evidence reuse

- **FR-001**: The Feature MUST bind the Reference source to repository `https://github.com/microsoft/DiskANN.git`, branch `cpp_main`, full commit `78256bbab4685e1774e78d331e081a153be26823`, role `cpp_main_native_sharded_reference_v1`, and upstream-faithful reproduction mode.
- **FR-002**: The Feature MUST bind the Framework candidate to the existing P0+B0+M0 composition and role `cpp_main_p0_b0_m0_framework_candidate_v1`; it MUST remain `candidate` until a final `CERTIFIED` decision is validly derived.
- **FR-003**: The Feature MUST NOT modify, generate files in, change the gitlink of, or accept a dirty `upstream/diskann-cpp-main` tree.
- **FR-004**: The Feature MUST reuse without redefinition the frozen Feature 002 Structural/Contract Parity Policy, its Reference/Framework role identities, its mandatory checks, and its four-state Gate semantics.
- **FR-005**: The Feature MUST reuse without redefinition Feature 005 Dataset, PQArtifact, QuerySet, GroundTruth, SearchProtocol, SearchObservation, SearchEvaluationReport, metric, eligibility, timing, repetition, exclusion, and aggregation semantics.
- **FR-006**: The current zero-cache and fixed-cache Search Runner Parity `PASS` on the evidence-bearing fast admission path MAY be reused only when the certification search uses Project commit `10f8520d8617fdaf8de3abf8a87842346ae4c96f`, runner SHA-256 `97acef83e042949071230691789b493231d8ab09de22d8af86363f9c180ec856`, implementation `cpp_main_pqflash_search_instrumented_v1`, and the fixed DiskANN source identity.
- **FR-007**: Any change to the identity set in FR-006 MUST invalidate reuse of that parity evidence and require a fresh Search Runner Parity `PASS` before certification may proceed.
- **FR-008**: Search Runner Parity MUST remain a same-input-graph instrumented-versus-upstream-oracle check and MUST NOT be interpreted as Graph Parity, cross-graph Quality Parity, or final certification.

#### Native Reference and structural Gate

- **FR-009**: One human-approved SIFT1M native sharded Reference build MUST retain the complete final merged Reference GlobalGraph as an independent immutable Artifact.
- **FR-010**: The Reference GlobalGraph MUST bind the fixed source, admitted Dataset, full effective profile and controls, native sharded path, terminal status, and clean upstream state.
- **FR-011**: Reference Artifact data, its BuildManifest, ValidationEvidence, structural evidence, and later search-quality evidence MUST remain independent objects and references.
- **FR-012**: The Reference build MUST retain actual failure and cancellation evidence and MUST NOT publish a normal Reference GlobalGraph when its output or provenance is incomplete or invalid.
- **FR-013**: Certification MUST NOT require native intermediate B0 or M0 payloads that the fixed native build does not retain, provided the complete final Reference GlobalGraph and all evidence required by the frozen Feature 002 Policy are available.
- **FR-014**: Missing native intermediate evidence MUST NOT be fabricated, reconstructed from the final graph, or represented as an observed native Artifact.
- **FR-015**: The Reference and Framework GlobalGraphs MUST bind the same admitted SIFT1M Dataset identity, profile/control identity, global node count, GlobalNodeId space, and structural contract required by Feature 002.
- **FR-016**: The Feature MUST re-derive the Structural/Contract Gate only from the exact frozen Policy and referenced evidence; it MUST NOT modify the Policy or its checks in response to observed results.
- **FR-017**: The Structural/Contract Gate result MUST remain exactly one of `PASS`, `FAIL`, `INCONCLUSIVE`, or `NOT_REACHED` with Feature 002 semantics.
- **FR-018**: Gate `FAIL` MUST force final result `NOT_CERTIFIED`; Gate `INCONCLUSIVE` or `NOT_REACHED` MUST force final result `INCONCLUSIVE`; only Gate `PASS` may allow `CERTIFIED` when every other certification condition also passes.
- **FR-019**: Gate `PASS` alone MUST claim only scoped structural-and-contract parity and MUST NOT upgrade the Framework candidate or imply Search Semantics, Quality Parity, Recall parity, full Native-versus-Framework parity, or certification.

#### Controlled search and quality evidence

- **FR-020**: The Reference and Framework GlobalGraphs MUST each have a separately identified, separately validated DiskLayout bound to that graph's own checksum and end-to-end entry/medoid lineage.
- **FR-021**: Reference and Framework certification searches MUST consume one byte-identical validated PQArtifact bound to the same Dataset; a logical match without byte identity is insufficient.
- **FR-022**: Both searches MUST use the repository's existing `sift1M_dataset/selected_query.csv`, zero-based `query_row_index` alignment, opaque `source_query_id`, no ID-based self-match exclusion, and the same compatible exact-L2 GroundTruth.
- **FR-023**: Both searches MUST use K=100, identical ordered L-search controls, beamwidth, thread count, IO limit, warmup, query order, timing boundary, storage declaration, cache-specific controls, three repetitions, end-to-end entry policy, and evaluator identity.
- **FR-024**: Zero-cache and fixed-cache MUST remain separate protocol and report identities; the fixed-cache condition MUST use the same requested-node control on both graphs.
- **FR-025**: The exact ordered L-search list and every other certification search control MUST be frozen before either formal search executes and MUST NOT change in response to observed Recall, runtime, failure, or resource usage.
- **FR-026**: Every complete query MUST retain exactly 100 ordered GlobalNodeIds and 100 corresponding distances, with all IDs in Dataset range; incomplete or contract-invalid observations MUST be retained but excluded from metrics and aggregates.
- **FR-027**: Each `(graph role, cache condition, L-search, repetition index)` point MUST remain independently identified and retained; no failed, slow, or low-Recall repetition may be overwritten, silently dropped, or replaced after results are observed.
- **FR-028**: Recall@10 and Recall@100 MUST be calculated from each eligible K=100 observation and aggregated separately using Feature 005's median-of-eligible rule.
- **FR-029**: Cross-graph quality comparison MUST NOT require graph-byte equality, adjacency equality, graph-checksum equality, ordered result-ID equality, or result-distance equality.
- **FR-030**: Mac Docker QPS, mean latency, P99.9 latency, and Mean IOs MAY be retained only as `informational_non_formal`; they MUST NOT contribute to Quality Parity, certification, or formal performance claims.
- **FR-031**: Quality eligibility MUST remain independent from normative Linux performance admission and Search Runner Parity's performance-gating role, while formal Recall MUST NOT be described as Search Semantics Parity, Quality Parity by itself, or final certification.
- **FR-032**: The Quality Parity policy MUST bind exact Reference/Framework Artifact identities, protocol identities, eligible repetition rules, evaluator identity, Recall metrics, and terminal decision rules before either quality search runs.
- **FR-033**: For Recall@10 and Recall@100 separately, the Quality Parity policy MUST require `Reference aggregate - Framework aggregate <= 0.010`, meaning the Framework may be at most 1.0 absolute percentage point below the corresponding Reference aggregate; equal or higher Framework Recall is within tolerance. This threshold MUST be frozen before execution and MUST NOT be adjusted after observing results.
- **FR-034**: Quality Parity MUST pass only when Recall@10 and Recall@100 each satisfy FR-033 at every declared `(cache condition, L-search)` aggregate for both zero-cache and fixed-cache. The decision MUST be the conjunction of all such point-and-metric findings; a passing point or metric MUST NOT compensate for a failing one, and no additional cross-L or cross-cache average may replace this rule.

#### Final certification decision

- **FR-035**: The final Feature-local decision MUST be exactly one of `CERTIFIED`, `NOT_CERTIFIED`, or `INCONCLUSIVE`.
- **FR-036**: `CERTIFIED` MUST require Structural/Contract Gate `PASS`, an exact-identity Search Runner Parity `PASS`, compatible and control-identical Reference/Framework searches, a passing frozen Quality Parity rule, and complete audit evidence for every attempted run.
- **FR-037**: The final decision MUST be `NOT_CERTIFIED` when complete valid evidence establishes any Quality Parity threshold violation, an exact-current-identity Search Runner Parity `FAIL`, or a mandatory non-structural admission incompatibility with the frozen scope or controls. It MUST be `INCONCLUSIVE` when a required condition cannot be evaluated because a cache/L group has insufficient eligible repetitions or no aggregate, the required Search Runner Parity `PASS` is absent, stale, or identity-mismatched without a current explicit `FAIL`, or required non-structural admission evidence is missing, invalid, incomplete, or ambiguous. Structural Gate outcomes continue to use the unchanged mapping in FR-018.
- **FR-038**: No result may be `CERTIFIED` when a required input, identity, validation, control, observation, aggregate, policy, or approval is missing, incompatible, invalid, or ambiguous.
- **FR-039**: The Feature-local certification decision MUST reference the frozen structural policy and result, exact runner parity evidence, both graph and DiskLayout identities, shared PQ identity, QuerySet/GroundTruth identities, all four SearchProtocols, all SearchObservations, all four EvaluationReports, Quality Parity findings, exclusions, and terminal rationale.
- **FR-040**: Artifact data, BuildManifest provenance, ValidationEvidence, SearchObservation, SearchEvaluationReport, structural result, Quality Parity evidence, and final certification decision MUST remain separate; certification state MUST NOT be written into a GlobalGraph, PQArtifact, or DiskLayoutArtifact.
- **FR-041**: `CERTIFIED` MUST apply only to the exact SIFT1M/L2/static/unfiltered Framework role, source, Dataset, profile, graph, search runner, PQ, query, GroundTruth, search controls, and decision policy identities named by the evidence.
- **FR-042**: Certification MUST NOT automatically apply to OGP, HNSW-Merger, another P/B/M composition, changed parameters, another Dataset, another metric, dynamic indexing, filtered/range search, or a larger scale.
- **FR-043**: The final decision MUST explicitly state that it is not Feature 002 Graph-byte/adjacency equality, complete Native-versus-Framework equivalence, Framework-wide certification, or performance certification.

#### Execution and scope boundaries

- **FR-044**: Specify, Clarify, Plan, Tasks, and Implement MUST NOT automatically execute the native SIFT1M Reference build, a formal SIFT1M DiskLayout materialization, either formal quality search, or a Linux formal performance experiment.
- **FR-045**: Every formal SIFT1M operation MUST use an exact immutable request and separate human approval bound to inputs, controls, binaries, environment, and output paths.
- **FR-046**: The Feature MUST NOT execute or authorize an experiment larger than SIFT1M.
- **FR-047**: The Feature MUST NOT add a public Registry, certification ID, claim-eligibility subsystem, public S0 Pipeline stage, performance-certification framework, or generic Graph/Search certification framework.
- **FR-048**: The Feature MUST NOT perform parameter search, topology statistics, generic comparison sweeps, or a complete performance comparison of paper-generated graphs.
- **FR-049**: Failed, invalid, cancelled, identity-mismatched, excluded, and incomplete runs MUST be retained with their identity, path, checksum when available, terminal status, and reason, and MUST NOT contribute to an ineligible metric, aggregate, or favorable decision.

### Scope Boundaries

This Feature includes only the fixed SIFT1M float32/L2/static/unfiltered upstream native sharded Reference, the exact current Framework P0+B0+M0 candidate, one complete final Reference GlobalGraph, reuse of the existing Framework graph and shared Feature 005 search assets, the unchanged Feature 002 structural Gate, controlled zero/fixed-cache Recall comparison, and one Feature-local three-state certification decision.

This Feature excludes public certification infrastructure, new Pipeline stages or Registry entries, performance certification, Linux formal performance measurement, parameter search, topology statistics, graph/adjacency/result equality across different graphs, paper-algorithm certification, dynamic indexing, filtering, range search, disk PQ, reorder, other datasets or metrics, and any experiment above SIFT1M.

### Key Entities

- **Reference Baseline Role**: Existing Feature-local role `cpp_main_native_sharded_reference_v1` for the fixed native sharded graph-build path.
- **Framework Baseline Candidate Role**: Existing Feature-local role `cpp_main_p0_b0_m0_framework_candidate_v1`; candidate until the bounded final decision is validly derived.
- **Reference GlobalGraphArtifact**: Immutable final native merged graph bound to source, Dataset, profile, GlobalNodeId space, structural contract, and independent provenance.
- **Framework GlobalGraphArtifact**: Existing validated P0+B0+M0 final graph, unchanged by this Feature.
- **Structural/Contract Parity Policy and Result**: Existing frozen Feature 002 policy and its four-state, evidence-derived Gate result.
- **Search Runner Parity Evidence**: Same-input-graph runner/oracle evidence bound to exact Project, implementation, runner binary, runner BuildManifest, and DiskANN source identities.
- **Certification Search Input Set**: The two graph-specific DiskLayouts, one byte-identical shared PQArtifact, one QuerySet, one exact GroundTruth, and two control-identical cache-specific protocol families.
- **Quality Comparison Evidence**: Feature-local references to eligible per-repetition Recall results, cache/L aggregates, exclusions, and the frozen Quality Parity decision rule; it is not a public metric framework.
- **Framework Baseline Certification Decision**: Feature-local report of `CERTIFIED`, `NOT_CERTIFIED`, or `INCONCLUSIVE` with complete evidence references and bounded claims; it is not an Artifact, Registry record, certification ID, or public certification subsystem.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: 100% of admitted Reference and Framework evidence binds the fixed source, Dataset, profile/control, role, checksum, lineage, and clean-state identities; every mismatch is rejected before comparison.
- **SC-002**: One complete final Reference GlobalGraph and its independent terminal BuildManifest and valid ValidationEvidence are retained, while zero unobserved native intermediate payloads are fabricated or made mandatory.
- **SC-003**: 100% of Structural/Contract Gate outcomes reproduce the unchanged Feature 002 four-state policy, including the fixed mapping from `FAIL`, `INCONCLUSIVE`, and `NOT_REACHED` to the final decision.
- **SC-004**: Reference and Framework quality runs use two graph-specific validated DiskLayouts and one PQArtifact whose bytes and SHA-256 are identical on both paths.
- **SC-005**: 100% of compared search points use identical QuerySet, exact GroundTruth, K, L-search declaration, beamwidth, threads, cache-specific controls, IO/warmup/timing/query-order controls, repetitions, entry-policy type, and evaluator identity.
- **SC-006**: 100% of quality-eligible queries return exactly 100 ordered in-range GlobalNodeIds and corresponding distances, and Recall@10 and Recall@100 are reported separately for every eligible repetition.
- **SC-007**: Every declared graph/cache/L group retains exactly three predeclared outcomes and produces a median only from eligible repetitions; zero result-dependent replacement repetitions occur.
- **SC-008**: After Clarify freezes the Quality Parity rule, 100% of cache/L Recall findings are assessed by that unchanged rule, and no threshold or decision mapping changes after either formal search starts.
- **SC-009**: 100% of certification attempts end in exactly one reproducible `CERTIFIED`, `NOT_CERTIFIED`, or `INCONCLUSIVE` decision with all failed, invalid, cancelled, mismatched, and excluded evidence retained.
- **SC-010**: A runner source, binary, implementation, BuildManifest, or DiskANN identity mismatch causes 100% rejection of the reused Search Runner Parity evidence.
- **SC-011**: Zero QPS, latency, Mean IOs, topology statistics, adjacency equality, or cross-graph ordered-result equality findings contribute to the certification decision.
- **SC-012**: Zero outputs from this Feature certify OGP, HNSW-Merger, another composition, changed parameters, another Dataset, another metric, a larger scale, or formal performance.
- **SC-013**: 100% of formal SIFT1M operations remain unexecuted until an exact immutable request receives separate human approval, and no operation above SIFT1M is executed.
- **SC-014**: Upstream remains at the fixed clean commit before and after every approved operation, with all generated files stored outside the upstream tree.

## Assumptions

- The existing SIFT1M DatasetArtifact is `sift1m-base-float32-v1`, with 1,000,000 float32 vectors, dimension 128, L2, aggregate checksum `4935b25f2cb2af2a05c7a657c3beb9c1823f45d1cf067f1d298169bb721f377e`, and vector payload SHA-256 `8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e`.
- Feature 002's frozen Structural/Contract Policy remains authoritative and currently reports `NOT_REACHED` because a complete immutable Reference merged GlobalGraph and matching structural evidence were not retained.
- The existing Framework P0+B0+M0 graph remains an immutable candidate input and will be freshly validated before reuse; this Feature does not rebuild it automatically.
- Feature 005's formal QuerySet remains `sift1M_dataset/selected_query.csv`, with zero-based `query_row_index`, opaque `source_query_id`, and no ID-based self-match exclusion; its compatible exact-L2 GroundTruth is available or will be admitted before evaluation.
- Reference and Framework entry policies are both `end_to_end` and retain their own graph-specific medoid/entry lineage; this is not a shared-entry or adjacency-only controlled comparison.
- The exact L-search list and resource/environment placement are frozen before formal execution through the admitted protocols; there is no optimizer, early stop, or best-result selection.
- Mac Docker may support quality-eligible Recall but is not a normative Linux performance environment; later formal Linux performance work requires a new search execution and remains outside certification.

## Dependencies

- Feature 002 policy, schema, current report, source mapping, existing Framework P0+B0+M0 evidence, and role identities remain available and unchanged.
- Feature 005 Artifact and evidence contracts, exact QuerySet/GroundTruth semantics, search/evaluation semantics, and retained Search Runner Parity evidence remain available and compatible.
- The fixed upstream source checkout and the admitted SIFT1M inputs remain readable and checksum-stable.
- Human approval is available later for each exact formal SIFT1M request; this specification itself grants no execution approval.
