#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/validation.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace diskann_pipeline {
namespace {

CompatibilityResult reject(ErrorCategory category, std::string reason) {
  return {false, FailureInfo{category, std::move(reason), std::nullopt}};
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

CompatibilityResult dataset_binding(const VectorAccessor* accessor,
                                    const DatasetIdentity& expected) {
  const auto* bound = accessor ? accessor->bound_dataset() : nullptr;
  if (!bound) {
    return reject(ErrorCategory::kCapabilityUnsatisfied,
                  "runtime service vector_accessor/dataset-bound-v1: unknown Dataset binding");
  }
  if (bound->reference().artifact_type != ArtifactType::kDataset) {
    return reject(ErrorCategory::kSourceMismatch,
                  "runtime service vector_accessor/dataset-bound-v1: binding is not a Dataset");
  }
  const auto& observed = bound->dataset();
  const auto mismatch = [](const char* field, const auto& wanted, const auto& actual) {
    std::ostringstream message;
    message << "runtime service vector_accessor/dataset-bound-v1: " << field
            << " mismatch (expected " << wanted << ", observed " << actual << ')';
    return reject(ErrorCategory::kSourceMismatch, message.str());
  };
  if (observed.dataset_id != expected.dataset_id)
    return mismatch("dataset_id", expected.dataset_id, observed.dataset_id);
  if (observed.dataset_checksum != expected.dataset_checksum)
    return mismatch("dataset_checksum", expected.dataset_checksum, observed.dataset_checksum);
  if (observed.global_node_count != expected.global_node_count)
    return mismatch("global_node_count", expected.global_node_count, observed.global_node_count);
  if (observed.vector_dimension != expected.vector_dimension)
    return mismatch("vector_dimension", expected.vector_dimension, observed.vector_dimension);
  if (observed.scalar_type != expected.scalar_type)
    return mismatch("scalar_type", expected.scalar_type, observed.scalar_type);
  if (observed.distance_metric != expected.distance_metric)
    return mismatch("distance_metric", expected.distance_metric, observed.distance_metric);
  if (accessor->dimension() != expected.vector_dimension)
    return mismatch("accessor dimension", expected.vector_dimension, accessor->dimension());
  return {true, std::nullopt};
}

}  // namespace

RuntimeServiceCapabilities runtime_service_capabilities(
    const StageContext& context, const DatasetIdentity& dataset) {
  RuntimeServiceCapabilities result;
  auto& values = result.available_services;
  if (context.vector_accessor) {
    values.push_back("vector_accessor/v1");
    if (dataset_binding(context.vector_accessor, dataset).compatible)
      values.push_back("vector_accessor/dataset-bound-v1");
  }
  if (context.distance_function) {
    switch (context.distance_function->semantics()) {
      case DistanceSemantics::kSquaredL2:
        values.push_back("distance_function/squared-l2-v1");
        values.push_back("distance_function/l2-v1");
        break;
      case DistanceSemantics::kEuclideanL2:
        values.push_back("distance_function/euclidean-l2-v1");
        values.push_back("distance_function/l2-v1");
        break;
      case DistanceSemantics::kUnknown:
        break;
    }
  }
  if (context.metrics_sink) values.push_back("metrics_sink/v1");
  if (context.cancellation_token) values.push_back("cancellation_token/v1");
  result.checkpoint_store_available =
      context.checkpoint_store && context.checkpoint_store->available();
  if (result.checkpoint_store_available) values.push_back("checkpoint_store/v1");
  return result;
}

CompatibilityResult check_runtime_services(
    const ContractRequirement& consumer, const StageContext& context,
    const DatasetIdentity& dataset) {
  if (contains(consumer.required_services, "vector_accessor/dataset-bound-v1")) {
    const auto binding = dataset_binding(context.vector_accessor, dataset);
    if (!binding.compatible) return binding;
  }
  if (contains(consumer.required_services, "distance_function/squared-l2-v1") &&
      (!context.distance_function ||
       context.distance_function->semantics() != DistanceSemantics::kSquaredL2)) {
    const auto observed = context.distance_function &&
                                 context.distance_function->semantics() ==
                                     DistanceSemantics::kEuclideanL2
                             ? "euclidean_l2" : "unknown";
    return reject(ErrorCategory::kCapabilityUnsatisfied,
                  std::string("runtime service distance_function/squared-l2-v1: ") +
                      "expected squared_l2, observed " + observed);
  }
  ImplementationDescriptor services_only{};
  services_only.requires.required_services = consumer.required_services;
  return CompatibilityChecker{}.check(
      services_only, {}, runtime_service_capabilities(context, dataset));
}

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
