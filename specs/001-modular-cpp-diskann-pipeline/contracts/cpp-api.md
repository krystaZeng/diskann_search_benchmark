# C++17 Public API Contract

This document fixes the public shape for Feature 001. Namespaces and include partitioning may be
refined during implementation, but no public header may require a language mode newer than C++17
or expose the JSON library.

## 1. Core Value Types

```cpp
namespace diskann_pipeline {

using GlobalNodeId = std::uint32_t;
using LocalNodeId = std::uint32_t;
using PartitionId = std::uint32_t;

enum class ArtifactType { kDataset, kPartition, kLocalGraph, kGlobalGraph };
enum class IdSpace { kNone, kLocal, kGlobal };
enum class StageKind { kPartition, kLocalBuild, kMerge };
enum class StageStatus {
  kPlanned,
  kRunning,
  kSucceeded,
  kFailed,
  kCancelled,
  kInvalidOutput,
  kReused,
};

struct ArtifactRef {
  std::filesystem::path metadata_path;
  ArtifactType artifact_type;
  std::string format_version;
  std::string aggregate_checksum;
};

struct LocalBuildConfig {
  std::filesystem::path stage_manifest_path;
};

struct StageResult {
  StageStatus status;
  std::optional<ArtifactRef> output_candidate;
  std::filesystem::path build_manifest_path;
};

struct PartitionRecord {
  PartitionId partition_id;
  NodeCount local_node_count;
  std::optional<std::string> shard_vectors_role;
  std::string local_to_global_map_role;
};

}  // namespace diskann_pipeline
```

`ArtifactRef.metadata_path` resolves only to an immutable Artifact metadata document; it never
points to a normalized stage Manifest, BuildManifest, or EvaluationReport. `ArtifactRef` does not
load payloads. `output_candidate` remains inside managed temporary storage until the Harness
validates and publishes or quarantines it. `kFailed` and `kCancelled` carry no candidate. A
pipeline-level invalid-output result exposes a quarantine diagnostic ID separately, never as a
normal ArtifactRef.

## 2. StageContext Services

The public service interfaces use C++17 containers and pointers only. Representative signatures:

```cpp
class ValidatedArtifact;

class VectorAccessor {
 public:
  virtual ~VectorAccessor() = default;
  virtual std::size_t dimension() const noexcept = 0;
  virtual void read(GlobalNodeId id, std::vector<float>& destination) const = 0;
  virtual const ValidatedArtifact* bound_dataset() const noexcept { return nullptr; }
};

enum class DistanceSemantics { kUnknown, kSquaredL2, kEuclideanL2 };

class DistanceFunction {
 public:
  virtual ~DistanceFunction() = default;
  virtual double evaluate(const std::vector<float>& lhs,
                          const std::vector<float>& rhs) const = 0;
  virtual DistanceSemantics semantics() const noexcept { return DistanceSemantics::kUnknown; }
};

class MetricsSink {
 public:
  virtual ~MetricsSink() = default;
  virtual void event(const std::string& name) = 0;
  virtual void increment(const std::string& counter, std::uint64_t amount) = 0;
};

class CancellationToken {
 public:
  virtual ~CancellationToken() = default;
  virtual bool is_cancelled() const noexcept = 0;
};

class CheckpointStore {
 public:
  virtual ~CheckpointStore() = default;
  virtual bool available() const noexcept = 0;
  virtual void reject_unavailable_operation() const = 0;
};

struct StageContext {
  WorkspaceRef workspace;
  std::uint64_t memory_budget_bytes;
  std::uint32_t thread_budget;
  TemporaryStorageRef temporary_storage;
  VectorAccessor* vector_accessor;
  DistanceFunction* distance_function;
  CheckpointStore* checkpoint_store;
  MetricsSink* metrics_sink;
  CancellationToken* cancellation_token;
};
```

All pointers are non-owning and non-null for a running stage. The unavailable CheckpointStore object
is real, reports `false`, and throws/reports a stable unavailable-service error on use. The Registry
does not advertise its capability.

### Consumer-scoped runtime admission

`vector_accessor/dataset-bound-v1` requires a Dataset `ValidatedArtifact` retained by the
accessor from the actual validated input used to construct its storage. The borrowed pointer
must remain valid for the accessor's lifetime (which covers its StageContext use). Forwarding
wrappers forward that real binding. The shared `check_runtime_services` checks all existing
`DatasetIdentity` fields: ID, payload checksum, node count, dimension, scalar type and metric,
plus accessor dimension. It does not require matching paths, rescan vectors, or invent a second
identity. A correct in-memory accessor remains supported. Missing binding is
`capability_unsatisfied`; a known mismatching binding is `source_mismatch`.

`distance_function/squared-l2-v1` and `distance_function/euclidean-l2-v1` distinguish squared
and ordinary L2. Only an explicit `semantics()` declaration supplies either capability. The
legacy `distance_function/l2-v1` denotes the known L2 family, never proof of squared L2; a
non-null pointer with unknown semantics supplies none of these distance capabilities.

Executor and CLI execution/preflight derive capabilities from actual StageContext services and
reuse the common check, enabled only by each consumer's `requires.required_services`. Generated
chains check all selected consumers before Partition factory creation; Partition reuse checks
remaining Local/Merge against validated inputs; merge-only checks Merge before its factory.
Services are rechecked at handoffs before downstream factories. Service incompatibility is a
run failure, not Artifact corruption: already validated/published inputs retain VALID evidence
and successful producer manifests, without quarantine. Other output and handoff checks remain
unchanged. CLI validate/evaluate and generic `validate_stage_context` do not acquire these new
semantic requirements. Direct consumers also defend their own algorithm entry.

The two default virtual methods preserve source compatibility, not binary ABI compatibility:
recompile the core, service implementations, stages, runners and tests together; mixing old and
new binaries is unsupported. Consumers opting into these capabilities intentionally reject old
implementations that provide no evidence. No Artifact/schema/request/parameter format or
algorithm implementation version changes. Implementers are responsible for truthful binding
and behavior; targeted tests check the known distance implementations. Runtime admission
prevents mismatched assembly, not dishonesty by arbitrary third-party implementations.

## 3. Stage Interfaces

```cpp
class PartitionStrategy {
 public:
  virtual ~PartitionStrategy() = default;
  virtual StageResult run(
      const ArtifactRef& dataset,
      const std::filesystem::path& stage_manifest_path,
      StageContext& context) = 0;
};

class LocalGraphBuilder {
 public:
  virtual ~LocalGraphBuilder() = default;
  virtual StageResult build(
      const ArtifactRef& partition_artifact,
      PartitionId partition_id,
      const LocalBuildConfig& config,
      StageContext& context) = 0;
};

class GraphMerger {
 public:
  virtual ~GraphMerger() = default;
  virtual StageResult merge(
      const ArtifactRef& partition_artifact,
      const std::vector<ArtifactRef>& local_graphs,
      const std::filesystem::path& stage_manifest_path,
      StageContext& context) = 0;
};
```

Hard API rules:

- No stage accepts or returns a complete in-memory graph.
- No public interface exposes `std::span`, ranges, concepts, modules, coroutine types, or
  `nlohmann::json`.
- Input refs are validated before invocation. Output candidates are validated after invocation.
- `partition_artifact` is the aggregate PartitionArtifact containing all Partition records.
  `partition_id` is mandatory, must satisfy `0 <= partition_id < partition_count`, and must match a
  record present in that validated Artifact. No `PartitionShardArtifact` or shard ArtifactRef exists.
- `LocalBuildConfig.stage_manifest_path` identifies an immutable normalized JSON document containing
  complete Local Build parameters; it is not a BuildManifest, Artifact metadata path, in-memory
  configuration, or graph object.
- Every LocalGraphArtifact output records the selected owning PartitionId and the aggregate checksum
  of the exact PartitionArtifact supplied to the invocation.
- `PartitionRecord.local_to_global_map_role` is mandatory and
  `PartitionRecord.shard_vectors_role` is optional. Local Build uses the Artifact payload when it is
  present; otherwise an implementation that needs vectors must use the validated Dataset-backed
  `StageContext::vector_accessor` in map order. Missing both sources is rejected. Runtime
  materialization is Local Build preparation and cannot be published as a P0 output.
- An optional aggregate Partition payload role `centroids` is paired with
  `partition.centroids/v1`. A GlobalGraphArtifact always contains `global_graph` and may contain a
  second `medoids` payload paired with `graph.medoids/v1`. Descriptor output matching makes the
  latter mandatory for any implementation, including cpp_main M0, that declares that capability.
- Other stage Manifest paths likewise identify immutable normalized JSON parameter documents.
- Fake Partition, Local Build, and Merge check cancellation at stage start, after input
  resolution/validation, and before output publication; future implementations declare and test
  equivalent bounded cooperative checkpoints appropriate to their work.

## 4. Descriptor and Static Factory Registry

```cpp
struct ArtifactRequirement {
  ArtifactType artifact_type;
  std::vector<std::string> accepted_format_versions;
  IdSpace input_id_space;
};

struct ContractRequirement {
  std::vector<ArtifactRequirement> input_artifacts;
  std::vector<std::string> required_capabilities;
  std::vector<std::string> required_services;
};

struct ContractProvide {
  ArtifactType artifact_type;
  std::string format_version;
  IdSpace output_id_space;
  std::vector<std::string> capabilities;
};

struct ImplementationDescriptor {
  std::string implementation_id;
  std::string implementation_version;
  StageKind stage;
  ContractRequirement requires;
  ContractProvide provides;
  std::filesystem::path parameter_schema_path;
  bool test_only;
};

using PartitionFactory =
    std::function<std::unique_ptr<PartitionStrategy>()>;
using LocalBuildFactory =
    std::function<std::unique_ptr<LocalGraphBuilder>()>;
using MergeFactory =
    std::function<std::unique_ptr<GraphMerger>()>;

enum class RegistryAdmissionPolicy {
  kProduction,
  kTest,
};

class ImplementationRegistry {
 public:
  explicit ImplementationRegistry(
      RegistryAdmissionPolicy admission_policy);
  ImplementationRegistry() = delete;
};
```

`ArtifactRequirement` is the C++17 representation of one JSON `requires.input_artifacts[]` item.
`ContractRequirement.input_artifacts` is non-empty. Required capabilities and services remain on
the enclosing requirement, and `ContractProvide` remains a single-output contract. A Merge
descriptor contains one PartitionArtifact requirement and one LocalGraphArtifact requirement. No
v1 descriptor adds cardinality, multiplicity, optional-input, or fallback fields. Preflight matches
every required input's type, accepted version, and ID space; a missing input or any incompatibility
rejects the chain before factory creation or stage invocation.

`ImplementationDescriptor.parameter_schema_path` is a stable repository-relative path to the
implementation parameter JSON Schema.

Registry registration is explicit and returns an error on duplicate identity, mismatched factory
type, empty contracts, or mutation after sealing. Resolution is by exact identity and exact stage.
`kProduction` rejects `test_only == true`; `kTest` accepts both test-only and formal descriptors.
Both policies apply duplicate identity, stage, sealing, no-overwrite, and no-fallback checks. There
is no default construction, and failed admission does not change Registry state. Preflight
validates the complete selected chain and StageContext capabilities before creating or invoking any
implementation.

Production assembly and test assembly are separate link targets. The installed production runner
constructs its Registry with `kProduction`; the non-installed Fake runner uses `kTest`. Only the
test assembly calls:

```cpp
void register_fake_implementations(ImplementationRegistry& registry);
```

### 4.1 Adapter declaration and evidence boundary

Feature 001 registers no production Adapter and adds no Adapter factory. For policy validation only,
an Adapter declaration uses the same ImplementationDescriptor contracts: source Artifact types,
accepted format versions, and input ID spaces are declared through `requires.input_artifacts`;
produced type, format version, output ID space, and capabilities are declared through `provides`.
The associated evidence must retain the complete source-to-target trace, round-trip or canonical
logical-equivalence evidence, and both source and output checksums. The allowed conversions and
semantic-change rejection rules are defined only by `adapter-policy.md`; this API description does
not add or broaden them.

## 5. Validation and Resolution

The normal validator accepts a WorkspaceRef and ArtifactRef and returns a typed result containing
ValidationEvidence. It never repairs metadata or payloads. The normal resolver has no API accepting
a quarantine ID. A separate `DiagnosticQuarantineResolver`, absent from all stage constructors,
supports read-only diagnostic access and explicit deletion workflows.

Reuse calls the same validator version and compatibility checker as new output. A cached Artifact
does not retain validation privilege across a new consumer request.

## 6. Error Contract

Library operations return or throw typed project errors with a stable category:

- `schema_invalid`
- `unsupported_version`
- `path_escape`
- `payload_missing`
- `payload_length_mismatch`
- `checksum_mismatch`
- `id_space_mismatch`
- `id_range_invalid`
- `mapping_incomplete`
- `degree_exceeded`
- `node_count_mismatch`
- `capability_unsatisfied`
- `duplicate_implementation`
- `reuse_mismatch`
- `cancelled`
- `checkpoint_unavailable`

The CLI maps these categories to the runner-result contract. Human messages add context but do not
replace the stable category.
