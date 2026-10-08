#include "diskann_pipeline/registry.hpp"

#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <tuple>
#include <variant>

namespace diskann_pipeline {
namespace {

using Factory = std::variant<PartitionFactory, LocalBuildFactory, MergeFactory>;

struct Entry {
  ImplementationDescriptor descriptor;
  Factory factory;
  InputAdmissionCheck admission;
};

bool same_identity(const ImplementationDescriptor& descriptor,
                   const std::string& id, const std::string& version) {
  return descriptor.implementation_id == id &&
         descriptor.implementation_version == version;
}

void validate_descriptor(const ImplementationDescriptor& descriptor,
                         StageKind expected_stage) {
  if (descriptor.stage != expected_stage) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "registered factory does not match descriptor stage");
  }
  if (descriptor.implementation_id.empty() ||
      descriptor.implementation_version.empty() ||
      descriptor.parameter_schema_path.empty() ||
      descriptor.parameter_schema_path.is_absolute() ||
      descriptor.requires.input_artifacts.empty() ||
      descriptor.provides.format_version.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "implementation descriptor has an empty contract field");
  }
  for (const auto& component : descriptor.parameter_schema_path) {
    if (component == "..") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "parameter_schema_path must be repository-relative");
    }
  }
  for (const auto& input : descriptor.requires.input_artifacts) {
    if (input.accepted_format_versions.empty()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "ArtifactRequirement accepts no format version");
    }
  }
}

}  // namespace

struct ImplementationRegistry::Impl {
  explicit Impl(RegistryAdmissionPolicy value) : policy(value) {}
  RegistryAdmissionPolicy policy;
  bool is_sealed{false};
  std::vector<Entry> entries;
};

ImplementationRegistry::ImplementationRegistry(
    RegistryAdmissionPolicy admission_policy)
    : impl_(std::make_unique<Impl>(admission_policy)) {}

ImplementationRegistry::~ImplementationRegistry() = default;
ImplementationRegistry::ImplementationRegistry(ImplementationRegistry&&) noexcept =
    default;
ImplementationRegistry& ImplementationRegistry::operator=(
    ImplementationRegistry&&) noexcept = default;

namespace {

template <typename FactoryType>
void register_entry(ImplementationRegistry::Impl& impl,
                    ImplementationDescriptor descriptor, FactoryType factory,
                    StageKind expected_stage,
                    InputAdmissionCheck admission = {}) {
  if (impl.is_sealed) {
    throw PipelineError(ErrorCategory::kRegistrySealed,
                        "implementation Registry is sealed");
  }
  validate_descriptor(descriptor, expected_stage);
  if (!factory) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "implementation factory is empty");
  }
  if (impl.policy == RegistryAdmissionPolicy::kProduction &&
      descriptor.test_only) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "production Registry rejects test-only descriptor");
  }
  const auto duplicate = std::find_if(
      impl.entries.begin(), impl.entries.end(), [&](const Entry& entry) {
        return same_identity(entry.descriptor, descriptor.implementation_id,
                             descriptor.implementation_version);
      });
  if (duplicate != impl.entries.end()) {
    throw PipelineError(ErrorCategory::kDuplicateImplementation,
                        "duplicate implementation identity");
  }
  impl.entries.push_back({std::move(descriptor), std::move(factory),
                          std::move(admission)});
}

const Entry& resolve_entry(const ImplementationRegistry::Impl& impl,
                           const std::string& id, const std::string& version,
                           StageKind expected_stage) {
  const auto found = std::find_if(
      impl.entries.begin(), impl.entries.end(), [&](const Entry& entry) {
        return same_identity(entry.descriptor, id, version) &&
               entry.descriptor.stage == expected_stage;
      });
  if (found == impl.entries.end()) {
    throw PipelineError(ErrorCategory::kImplementationNotFound,
                        "no exact implementation identity and stage match");
  }
  return *found;
}

}  // namespace

void ImplementationRegistry::register_partition(
    ImplementationDescriptor descriptor, PartitionFactory factory) {
  register_entry(*impl_, std::move(descriptor), std::move(factory),
                 StageKind::kPartition);
}

void ImplementationRegistry::register_local_build(
    ImplementationDescriptor descriptor, LocalBuildFactory factory) {
  register_entry(*impl_, std::move(descriptor), std::move(factory),
                 StageKind::kLocalBuild);
}

void ImplementationRegistry::register_merge(ImplementationDescriptor descriptor,
                                            MergeFactory factory) {
  register_entry(*impl_, std::move(descriptor), std::move(factory),
                 StageKind::kMerge);
}

void ImplementationRegistry::register_local_build(
    ImplementationDescriptor descriptor, LocalBuildFactory factory,
    InputAdmissionCheck admission) {
  register_entry(*impl_, std::move(descriptor), std::move(factory),
                 StageKind::kLocalBuild, std::move(admission));
}

void ImplementationRegistry::register_merge(
    ImplementationDescriptor descriptor, MergeFactory factory,
    InputAdmissionCheck admission) {
  register_entry(*impl_, std::move(descriptor), std::move(factory),
                 StageKind::kMerge, std::move(admission));
}

CompatibilityResult ImplementationRegistry::check_known_inputs(
    const ImplementationDescriptor& consumer,
    const std::vector<ValidatedArtifact>& known_inputs,
    const StageContext& context) const {
  const auto& entry = resolve_entry(*impl_, consumer.implementation_id,
                                   consumer.implementation_version, consumer.stage);
  return entry.admission ? entry.admission(known_inputs, context)
                         : CompatibilityResult{true, std::nullopt};
}

void ImplementationRegistry::seal() { impl_->is_sealed = true; }
bool ImplementationRegistry::sealed() const noexcept { return impl_->is_sealed; }
std::size_t ImplementationRegistry::size() const noexcept {
  return impl_->entries.size();
}
RegistryAdmissionPolicy ImplementationRegistry::admission_policy() const noexcept {
  return impl_->policy;
}

std::vector<ImplementationDescriptor> ImplementationRegistry::enumerate() const {
  std::vector<ImplementationDescriptor> result;
  result.reserve(impl_->entries.size());
  for (const auto& entry : impl_->entries) {
    result.push_back(entry.descriptor);
  }
  std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.implementation_id, lhs.implementation_version) <
           std::tie(rhs.implementation_id, rhs.implementation_version);
  });
  return result;
}

const ImplementationDescriptor& ImplementationRegistry::resolve_descriptor(
    const std::string& implementation_id,
    const std::string& implementation_version,
    StageKind expected_stage) const {
  return resolve_entry(*impl_, implementation_id, implementation_version,
                       expected_stage)
      .descriptor;
}

PartitionFactory ImplementationRegistry::resolve_partition_factory(
    const std::string& id, const std::string& version) const {
  return std::get<PartitionFactory>(
      resolve_entry(*impl_, id, version, StageKind::kPartition).factory);
}

LocalBuildFactory ImplementationRegistry::resolve_local_build_factory(
    const std::string& id, const std::string& version) const {
  return std::get<LocalBuildFactory>(
      resolve_entry(*impl_, id, version, StageKind::kLocalBuild).factory);
}

MergeFactory ImplementationRegistry::resolve_merge_factory(
    const std::string& id, const std::string& version) const {
  return std::get<MergeFactory>(
      resolve_entry(*impl_, id, version, StageKind::kMerge).factory);
}

}  // namespace diskann_pipeline
