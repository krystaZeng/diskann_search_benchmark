#pragma once

#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/provenance.hpp"

#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

struct AvailableArtifactContract {
  ArtifactType artifact_type;
  std::string format_version;
  IdSpace id_space;
  std::vector<std::string> capabilities;
};

struct RuntimeServiceCapabilities {
  std::vector<std::string> available_services;
  bool checkpoint_store_available{false};
};

struct CompatibilityResult {
  bool compatible{false};
  std::optional<FailureInfo> failure;
};

class CompatibilityChecker {
 public:
  CompatibilityResult check(
      const ImplementationDescriptor& consumer,
      const std::vector<AvailableArtifactContract>& available_artifacts,
      const RuntimeServiceCapabilities& services) const;

  CompatibilityResult check_chain(
      const std::vector<ImplementationDescriptor>& selected_chain,
      std::vector<AvailableArtifactContract> initial_artifacts,
      const RuntimeServiceCapabilities& services) const;
};

}  // namespace diskann_pipeline
