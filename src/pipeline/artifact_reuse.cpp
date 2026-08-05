#include "diskann_pipeline/reuse.hpp"

#include <algorithm>

namespace diskann_pipeline {
namespace {

ArtifactReuseResult reject(const ArtifactValidationResult& validation,
                           ErrorCategory category, std::string reason,
                           const ArtifactRef& cached) {
  return {StageStatus::kFailed,
          validation.evidence,
          std::nullopt,
          FailureInfo{category, reason, std::nullopt},
          ReuseInfo{false, std::move(reason), cached}};
}

}  // namespace

ArtifactReuseService::ArtifactReuseService(const Validator& validator)
    : validator_(validator) {}

ArtifactReuseResult ArtifactReuseService::resolve(
    const ArtifactRef& cached,
    const ArtifactRequirement& requirement,
    const std::vector<std::string>& required_capabilities,
    const std::vector<ValidatedArtifact>& validated_sources) const {
  const auto validation = validator_.validate(cached, validated_sources);
  if (!validation.valid()) {
    const auto category = validation.evidence.failure_category.value_or(
        ErrorCategory::kReuseMismatch);
    return reject(validation, category,
                  "cached Artifact failed fresh contract validation", cached);
  }
  if (cached.artifact_type != requirement.artifact_type ||
      std::find(requirement.accepted_format_versions.begin(),
                requirement.accepted_format_versions.end(),
                cached.format_version) ==
          requirement.accepted_format_versions.end() ||
      validation.validated_artifact->id_space() != requirement.input_id_space) {
    return reject(validation, ErrorCategory::kReuseMismatch,
                  "cached Artifact type, version, or ID space is incompatible",
                  cached);
  }
  for (const auto& capability : required_capabilities) {
    if (!validation.validated_artifact->has_capability(capability)) {
      return reject(validation, ErrorCategory::kReuseMismatch,
                    "cached Artifact lacks a required consumer capability",
                    cached);
    }
  }
  return {StageStatus::kReused,
          validation.evidence,
          validation.validated_artifact,
          std::nullopt,
          ReuseInfo{true, "freshly validated immutable Artifact reuse", cached}};
}

}  // namespace diskann_pipeline
