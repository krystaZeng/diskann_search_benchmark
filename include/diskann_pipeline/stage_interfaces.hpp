#pragma once

#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace diskann_pipeline {

struct LocalBuildConfig {
  std::filesystem::path stage_manifest_path;
};

struct StageResult {
  StageStatus status;
  std::optional<ArtifactRef> output_candidate;
  std::filesystem::path build_manifest_path;
};

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
  virtual StageResult build(const ArtifactRef& partition_artifact,
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

}  // namespace diskann_pipeline

