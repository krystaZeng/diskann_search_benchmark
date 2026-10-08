#pragma once

#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/registry.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/stage_interfaces.hpp"

#include <memory>
#include <string>

namespace diskann_pipeline::xn_descent {

inline constexpr const char* kImplementationId =
    "xn_graph_xn_descent_local_build_v1";
inline constexpr const char* kImplementationVersion = "1.0.0";

enum class TerminationKind {
  kConverged,
  kInvalidInput,
  kCancelled,
  kResourceExhausted,
  kNumericFailure,
  kIterationCapNonconverged,
  kInternalFailure,
};

struct LocalOnlyExecutionRequest {
  ArtifactRef partition_artifact;
  PartitionId partition_id{0};
  std::string parameters_json;
};

struct ExperimentStageFactories {
  PartitionFactory partition;
  LocalBuildFactory local_build;
  MergeFactory merge;
};

struct ExperimentRunnerResult {
  int exit_code{4};
  PipelineExecutionResult execution;
  std::string result_json;
};

ImplementationDescriptor descriptor();
CompatibilityResult input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context);
std::unique_ptr<LocalGraphBuilder> make_xn_descent_local_build();
ExperimentRunnerResult execute_pipeline_experiment_request(
    const std::string& request_json,
    const ExperimentStageFactories* stage_factories = nullptr);
int xn_descent_experiment_runner_main(int argc, char** argv);

}  // namespace diskann_pipeline::xn_descent
