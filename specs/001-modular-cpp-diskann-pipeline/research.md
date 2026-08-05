# Phase 0 Research: Modular cpp_main DiskANN Pipeline

All user-supplied technology choices are treated as fixed inputs. This research resolves the
remaining implementation choices without expanding the Feature into real DiskANN integration or a
Loop Engine. All implementation choices required for Phase 1 are resolved.

## Decision 1: C++17 is the public compatibility floor

**Decision**: Set every public core target to C++17 with `CXX_STANDARD_REQUIRED=ON` and
`CXX_EXTENSIONS=OFF`. Public headers use standard C++17 library types only. In particular,
`GraphMerger` accepts local graph references as `const std::vector<ArtifactRef>&`, not
`std::span<const ArtifactRef>`.

**Rationale**: The first release has one concrete read-only collection shape, so a vector
reference is sufficient and avoids raising the common ABI/API floor solely for a view type. CMake
supports an explicit C++17 target standard and a required flag that prevents silent standard
downgrade ([CMake `CXX_STANDARD`](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html)).

**Alternatives considered**: Upgrade the common core to C++20 for `std::span`; rejected by the
provided compatibility requirement. Add a custom span immediately; deferred until multiple
container sources create an actual need. Copy `std::vector<ArtifactRef>` by value; rejected because
stage boundaries are read-only and potentially numerous.

## Decision 2: Newer-language implementations remain behind adapters/factories

**Decision**: A future isolated target may compile as C++20 or newer, but its factory and exported
module object must implement the C++17 public interfaces without leaking newer standard-library
types, ownership conventions, exceptions, or compiler-specific ABI objects across the boundary.

**Rationale**: Target-level CMake standard settings permit isolation while preserving one stable
core contract. This enables paper code to retain its required toolchain without forcing all
consumers to adopt it.

**Alternatives considered**: One repository-wide maximum standard; rejected because it couples
otherwise independent implementations. A C ABI or RPC boundary for v1; unnecessary for the
in-process static factory scope and substantially more complex.

## Decision 3: CMake owns C++ build metadata and CTest owns C++ test discovery

**Decision**: Require CMake 3.20+, export compile commands, define target-local warnings, and use
CTest to register all C++ unit, contract, property, and integration executables. Do not introduce a
separate C++ test framework in v1; small project-owned test helpers are sufficient for the exact
fixtures and failure categories.

**Rationale**: This keeps build/test dependencies narrow while providing machine-readable test
results and exact compiler invocations for BuildManifest capture. `CXX_STANDARD_REQUIRED` prevents
the standard from decaying to an older compiler mode.

**Alternatives considered**: Make/Ninja files maintained by hand; rejected because toolchain and
test metadata would fragment. GoogleTest/Catch2; useful later, but not required for the small v1
contract suite. Building inside upstream; prohibited.

## Decision 4: JSON is the machine contract; YAML is input sugar only

**Decision**: Artifact metadata, ArtifactRef, BuildManifest, EvaluationReport, implementation
descriptors, validation evidence, normalized run requests, and runner responses are JSON. Human
experiment composition is YAML parsed once through
duplicate-key-rejecting SafeLoader plus JSON-compatible tree validation. The project subclasses
`yaml.SafeLoader` only to provide a custom
mapping constructor that rejects duplicate keys. After construction, the Harness recursively
requires a top-level mapping, string mapping keys, and values limited to `dict`, `list`, `str`,
`bool`, `int`, finite `float`, and `None`; it rejects Python object or custom tags, unsafe
constructors, NaN, Infinity, date/datetime, bytes, set, and other non-JSON types. JSON Schema then
decides whether each concrete field permits null or float. The normalized result becomes JSON
before C++ execution, and no second YAML parser is used.

**Rationale**: One machine representation avoids cross-language YAML interpretation differences.
PyYAML's SafeLoader avoids the arbitrary-object behavior of unrestricted loading; the project-owned
mapping constructor and JSON-tree pass add deterministic duplicate-key and result-type constraints
without rewriting the remaining constructors
([PyYAML documentation](https://pyyaml.org/wiki/PyYAMLDocumentation)).

**Alternatives considered**: Parse YAML independently in C++ and Python; rejected because it
duplicates semantics. JSON-only authoring; valid but less usable for experiment composition.
Passing Python objects through bindings; rejected because the requested boundary is CLI plus JSON.

## Decision 5: JSON Schema Draft 2020-12 plus semantic validators

**Decision**: Publish strict Draft 2020-12 schemas with `additionalProperties: false` for the v1
JSON documents. Python applies schema validation. C++ performs explicit parse/type checks and all
semantic validation that JSON Schema cannot express cleanly: checksum recomputation, file length,
ID ranges, map completeness, graph degrees, dataset/source consistency, capability compatibility,
normal-vs-quarantine namespace, and lifecycle rules.

**Rationale**: Draft 2020-12 is a published, language-neutral schema vocabulary with explicit array
and unevaluated-property behavior
([JSON Schema Draft 2020-12](https://json-schema.org/draft/2020-12)). Schema validation and domain
validation remain separately testable; neither substitutes for the other.

**Alternatives considered**: Encode every invariant in JSON Schema; rejected because checksums and
cross-file graph properties require I/O and domain logic. C++-only validation; rejected because the
Python boundary must fail before launching C++ for malformed requests.

## Decision 6: Restricted canonical JSON and SHA-256 define Artifact identity

**Decision**: Define `diskann-artifact-c14n-v1`, a restricted canonical JSON projection containing
only UTF-8 strings, booleans, bounded integers, arrays, and objects. It uses lexicographically sorted
UTF-8 object keys, preserved array order, minimal JSON string escaping, base-10 integers without
leading zeros, no insignificant whitespace, and no trailing newline. Floating point and null are
forbidden in the aggregate projection. Every raw payload is hashed first; the aggregate SHA-256 is
then computed over the canonical projection required by FR-009.

**Rationale**: A narrow versioned profile is straightforward to implement identically in C++ and
Python and avoids serializer-dependent whitespace, map order, locale, or float formatting. SHA-256
uses OpenSSL 3.x EVP through CMake's `OpenSSL::Crypto`; CMake exposes that imported target and
OpenSSL documents SHA2-256 through EVP
([CMake `FindOpenSSL`](https://cmake.org/cmake/help/latest/module/FindOpenSSL.html),
[OpenSSL SHA2 EVP](https://docs.openssl.org/3.5/man7/EVP_MD-SHA2/)).

**Alternatives considered**: Hash the stored metadata file verbatim; rejected because paths and
excluded provenance fields would make identity location-dependent. Hash an ordinary JSON dump;
rejected because output is serializer-dependent. Invent a cryptographic implementation; rejected
as unnecessary risk.

## Decision 7: `nlohmann_json` stays implementation-private

**Decision**: Use the namespaced CMake target `nlohmann_json::nlohmann_json` in private source
targets. Public headers expose domain structs, strings, paths, vectors, enums, and interfaces—not
`nlohmann::json`. Disable implicit domain conversions and route all serialization through explicit
codec functions.

**Rationale**: The library provides a documented CMake imported target and requires only C++11 for
the ordinary header API
([nlohmann/json CMake integration](https://json.nlohmann.me/integration/cmake/)). Keeping it private
prevents a third-party JSON type from becoming part of the public compatibility contract.

**Alternatives considered**: Hand-written JSON parser; rejected as avoidable parsing risk. Expose
the JSON DOM publicly; rejected because it weakens typed contracts and couples consumers to a
serialization library. Experimental C++20 JSON modules; incompatible with the C++17 floor.

## Decision 8: Static factories are assembled explicitly

**Decision**: Registry entries are immutable descriptors plus typed factory callables registered by
an explicit assembly function. Every descriptor uses `parameter_schema_path`, a stable
repository-relative path to its implementation parameter JSON Schema. Duplicate identities fail
registration. There is no filesystem plugin discovery, shared-library loading, replacement
registration, or fallback selection. The existing descriptor field `test_only` is the sole
test-scope marker; no parallel visibility or registration-scope concept is introduced.

`ImplementationRegistry` has no default construction. Its explicit `RegistryAdmissionPolicy` is
`kProduction` or `kTest`. Production admission rejects `test_only: true`; test admission accepts
both test-only and formal descriptors. Either mode enforces duplicate/stage rejection, sealing, no
overwrite, and no fallback, and failed admission leaves Registry state unchanged. Phase 2 validates
this Registry policy but does not claim target linkage or installation isolation.

Phase 4 validates final Assembly isolation through three layers. First, the C++ production-surface
test verifies the `kProduction` Registry exposes no Fake identity and has no production Adapter
registration. Second, a simple CMake configure-time assertion, CTest CMake script, or equivalent
target-property check verifies that the production runner neither links a Fake target nor includes
Fake registration source, and that the Fake runner is independent. Third, a Python test installs to
a temporary prefix and verifies the production runner is installed while the Fake runner, Fake
library, fixtures, and Fake-only files are absent. No link map, symbol scanner, plugin loader, or
Graphviz layer is introduced, and Registry enumeration alone is not treated as link evidence.

**Rationale**: Explicit assembly is deterministic, inspectable, and supports preflight without the
ABI and deployment complexity of dynamic plugins. Registry policy provides an early fail-closed
check using the descriptor contract, while final test-only linking independently enforces that Fake
modules never become formal experiment implementations.

**Alternatives considered**: Static initialization side effects; rejected because initialization
order and duplicate diagnostics are less explicit. Dynamic plugins; out of scope for v1. Registry
overwrite; constitutionally prohibited.

## Decision 9: Stage boundaries use Artifact references and explicit Partition selection

**Decision**: Partition receives one dataset `ArtifactRef`. Partition output remains one aggregate
PartitionArtifact containing all Partition records and payload references. For every Local Build
invocation, the executor passes that validated aggregate `ArtifactRef`, one explicit `PartitionId`
that exists in the aggregate, a C++17 `LocalBuildConfig` referring to the immutable normalized JSON
stage Manifest, and a mutable `StageContext`. Merge receives the aggregate Partition `ArtifactRef`
and `const std::vector<ArtifactRef>&` local graphs. A `StageResult` returns status, the output
ArtifactRef, and a separate BuildManifest path. Graph payloads are opened lazily from validated
references; no `PartitionShardArtifact` or undefined shard ArtifactRef is introduced.

Descriptor compatibility mirrors these calls: `ContractRequirement.input_artifacts` is a C++17
collection of per-input Artifact type, accepted-version, and ID-space requirements. Merge declares
separate PartitionArtifact and LocalGraphArtifact requirements, and preflight must match both before
factory creation. Required capabilities/services remain on the enclosing requirement;
`ContractProvide` remains a single-output contract.

**Rationale**: This keeps full graphs out of the API, makes Partition selection unambiguous without
splitting the aggregate Artifact, enables immutable reuse, and makes every invocation replayable
from stored inputs and configuration.

**Alternatives considered**: Pass in-memory graph objects; rejected by the Feature requirement and
large-scale design. Embed configuration in virtual methods as library-specific objects; rejected
because it leaks implementation details and weakens manifest replay.

## Decision 10: Publication is atomic and quarantine is a separate resolver

**Decision**: A stage writes only inside its managed temporary directory. After validation, the
Harness atomically publishes the immutable Artifact under the normal content-addressed root. On
invalid/incomplete output, it atomically moves the candidate payload, metadata, failed
BuildManifest, and evidence into a quarantine record with a new quarantine identity. The normal
resolver is constructed without a quarantine root; diagnostics use a separate explicit resolver.

**Rationale**: Separating resolver capabilities makes accidental downstream discovery impossible
by construction. Atomic rename within one workspace filesystem prevents partial normal entries.
Corrected output is newly produced and never promotes a quarantine record in place.

**Alternatives considered**: A shared directory with a status flag; rejected because ordinary
enumeration can accidentally expose invalid data. Delete invalid output; rejected because the spec
requires diagnostic evidence retention. Revalidate and promote in place; prohibited.

## Decision 11: Python packaging is tool-neutral

**Decision**: A root `pyproject.toml` declares `requires-python = ">=3.10"`, the lightweight
Harness runtime dependencies, and a `test` optional-dependency group containing pytest and test
support. Use a standard build backend and document `python -m pip install -e '.[test]'`; uv may be
used by a developer but is neither required nor encoded as the sole workflow.

**Rationale**: `pyproject.toml` supports project metadata, minimum Python, and optional dependency
groups in standard packaging metadata
([Python Packaging User Guide](https://packaging.python.org/en/latest/guides/writing-pyproject-toml/));
pytest supports configuration in `pyproject.toml`
([pytest configuration](https://docs.pytest.org/en/stable/reference/customize.html)).

**Alternatives considered**: Requirements files only; rejected because the user selected
`pyproject.toml`. Mandatory uv lock/workflow; explicitly not required. A Python/C++ extension
module; rejected because subprocess isolation and manifest-driven execution are required.

## Decision 12: Validation scope and platform matrix remain intentionally small

**Decision**: Linux runs the normative GCC C++17 and Python 3.10+ matrix. macOS runs compilation,
CTest, pytest, and the Fake Pipeline with AppleClang/Clang. Tests cover exact fixture output,
contract failures, two retained deterministic runs under `retain_all` with distinct run provenance,
reuse, cancellation, and quarantine. No real DiskANN,
SIFT1M/SIFT100M, performance run, retry, parameter matrix, SQLite, or checkpoint scheduling is
planned.

**Rationale**: These tests prove the public contracts and hard gates without claiming algorithmic
parity or ANN performance. It matches the Feature's explicit scope and approval boundaries.

**Alternatives considered**: Integrate cpp_main immediately; explicitly excluded. Build a Loop
Engine early; explicitly excluded. Treat macOS as a standard experiment platform; rejected because
Linux is the normative environment.
