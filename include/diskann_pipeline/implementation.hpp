#pragma once

#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/stage_interfaces.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace diskann_pipeline {

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
using MergeFactory = std::function<std::unique_ptr<GraphMerger>()>;

}  // namespace diskann_pipeline

