#pragma once

#include "diskann_pipeline/stage_context.hpp"

// Runner-private service, exposed here only so behavior tests exercise the
// exact implementation assembled by the executable, not a copied test double.
namespace diskann_pipeline::hnsw_merger_vamana::runner_detail {
namespace dp = diskann_pipeline;

class L2Distance final : public dp::DistanceFunction {
 public:
  dp::DistanceSemantics semantics() const noexcept override {
    return dp::DistanceSemantics::kSquaredL2;
  }
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    if (lhs.size() != rhs.size()) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "distance dimensions differ");
    }
    double total = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      const auto delta = lhs[index] - rhs[index];
      total += delta * delta;
    }
    return total;
  }
};

}  // namespace diskann_pipeline::hnsw_merger_vamana::runner_detail
