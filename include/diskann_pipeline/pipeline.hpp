#pragma once

#include "diskann_pipeline/evaluation.hpp"
#include "diskann_pipeline/registry.hpp"
#include "diskann_pipeline/stage_context.hpp"
#include "diskann_pipeline/validation.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

struct StageInvocationCounts {
  std::size_t partition{0};
  std::size_t local_build{0};
  std::size_t merge{0};
};

struct PipelineExecutionRequest {
  std::string run_id;
  ArtifactRef dataset;
  std::string partition_implementation_id;
  std::string partition_implementation_version{"1.0.0"};
  std::string local_implementation_id;
  std::string local_implementation_version{"1.0.0"};
  std::string merge_implementation_id;
  std::string merge_implementation_version{"1.0.0"};
  std::string partition_parameters_json{"{}"};
  std::string local_parameters_json{"{}"};
  std::string merge_parameters_json{"{}"};
  std::string source_config_json{"{}"};
  std::optional<ArtifactRef> reused_partition;
  SourceIdentity source;
  ToolchainIdentity toolchain;
  std::string started_at;
  std::string ended_at;
};

struct PipelineExecutionResult {
  StageStatus status{StageStatus::kFailed};
  std::optional<ArtifactRef> artifact;
  std::optional<ArtifactRef> partition_artifact;
  std::vector<ArtifactRef> local_artifacts;
  std::vector<std::filesystem::path> build_manifest_paths;
  std::vector<std::filesystem::path> validation_evidence_paths;
  std::optional<std::filesystem::path> evaluation_report_path;
  std::optional<std::string> quarantine_id;
  std::optional<FailureInfo> failure;
  StageInvocationCounts invocations;
  std::vector<StageKind> stage_order;
  std::vector<PartitionId> local_partition_ids;
};

class PipelineExecutor {
 public:
  PipelineExecutor(ImplementationRegistry& registry, WorkspaceRef workspace);

  PipelineExecutionResult execute(const PipelineExecutionRequest& request,
                                  StageContext& context) const;

 private:
  ImplementationRegistry& registry_;
  WorkspaceRef workspace_;
};

EvaluationReport evaluate_validated_global_graph(
    const WorkspaceRef& workspace, const ValidatedArtifact& artifact,
    const std::filesystem::path& validation_evidence_path,
    const std::string& report_id, const std::string& generated_at);

std::string deterministic_evaluation_projection(
    const EvaluationReport& report);

}  // namespace diskann_pipeline
