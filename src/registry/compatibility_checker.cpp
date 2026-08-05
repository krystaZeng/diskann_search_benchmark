#include "diskann_pipeline/compatibility.hpp"

#include <algorithm>
#include <set>

namespace diskann_pipeline {
namespace {

CompatibilityResult reject(ErrorCategory category, std::string reason) {
  return {false, FailureInfo{category, std::move(reason), std::nullopt}};
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

CompatibilityResult CompatibilityChecker::check(
    const ImplementationDescriptor& consumer,
    const std::vector<AvailableArtifactContract>& available_artifacts,
    const RuntimeServiceCapabilities& services) const {
  std::set<std::string> capabilities;
  for (const auto& available : available_artifacts) {
    capabilities.insert(available.capabilities.begin(),
                        available.capabilities.end());
  }

  for (const auto& requirement : consumer.requires.input_artifacts) {
    const auto same_type = std::find_if(
        available_artifacts.begin(), available_artifacts.end(),
        [&](const auto& available) {
          return available.artifact_type == requirement.artifact_type;
        });
    if (same_type == available_artifacts.end()) {
      return reject(ErrorCategory::kCapabilityUnsatisfied,
                    "required input Artifact type is missing");
    }
    const auto same_version = std::find_if(
        available_artifacts.begin(), available_artifacts.end(),
        [&](const auto& available) {
          return available.artifact_type == requirement.artifact_type &&
                 contains(requirement.accepted_format_versions,
                          available.format_version);
        });
    if (same_version == available_artifacts.end()) {
      return reject(ErrorCategory::kUnsupportedVersion,
                    "input Artifact format version is not accepted");
    }
    const auto exact = std::find_if(
        available_artifacts.begin(), available_artifacts.end(),
        [&](const auto& available) {
          return available.artifact_type == requirement.artifact_type &&
                 contains(requirement.accepted_format_versions,
                          available.format_version) &&
                 available.id_space == requirement.input_id_space;
        });
    if (exact == available_artifacts.end()) {
      return reject(ErrorCategory::kIdSpaceMismatch,
                    "input Artifact ID space is incompatible");
    }
  }

  for (const auto& required : consumer.requires.required_capabilities) {
    if (capabilities.count(required) == 0) {
      return reject(ErrorCategory::kCapabilityUnsatisfied,
                    "required Artifact capability is unavailable: " + required);
    }
  }
  for (const auto& required : consumer.requires.required_services) {
    if (required == "checkpoint_store/v1" &&
        !services.checkpoint_store_available) {
      return reject(ErrorCategory::kCheckpointUnavailable,
                    "checkpoint service is unavailable");
    }
    if (!contains(services.available_services, required)) {
      return reject(ErrorCategory::kCapabilityUnsatisfied,
                    "required StageContext service is unavailable: " + required);
    }
  }
  return {true, std::nullopt};
}

CompatibilityResult CompatibilityChecker::check_chain(
    const std::vector<ImplementationDescriptor>& selected_chain,
    std::vector<AvailableArtifactContract> available,
    const RuntimeServiceCapabilities& services) const {
  for (const auto& descriptor : selected_chain) {
    const auto result = check(descriptor, available, services);
    if (!result.compatible) {
      return result;
    }
    available.push_back({descriptor.provides.artifact_type,
                         descriptor.provides.format_version,
                         descriptor.provides.output_id_space,
                         descriptor.provides.capabilities});
  }
  return {true, std::nullopt};
}

}  // namespace diskann_pipeline
