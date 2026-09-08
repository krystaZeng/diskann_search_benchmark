#pragma once

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

std::unique_ptr<LocalGraphBuilder> make_xn_descent_local_build();

}  // namespace diskann_pipeline::xn_descent
