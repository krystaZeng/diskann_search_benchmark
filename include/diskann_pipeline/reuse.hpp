#pragma once

#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/validation.hpp"

#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

struct ArtifactReuseResult {
  StageStatus status{StageStatus::kFailed};
  ValidationEvidence validation_evidence;
  std::optional<ValidatedArtifact> validated_artifact;
  std::optional<FailureInfo> failure;
  ReuseInfo provenance;
};

class ArtifactReuseService {
 public:
  explicit ArtifactReuseService(const Validator& validator);

  ArtifactReuseResult resolve(
      const ArtifactRef& cached_artifact,
      const ArtifactRequirement& consumer_requirement,
      const std::vector<std::string>& required_capabilities,
      const std::vector<ValidatedArtifact>& validated_sources = {}) const;

 private:
  const Validator& validator_;
};

}  // namespace diskann_pipeline
