#include "papers/xn_graph/xn_descent/stage_binding.hpp"

namespace diskann_pipeline::xn_descent {

ImplementationDescriptor descriptor() {
  return {kImplementationId,
          kImplementationVersion,
          StageKind::kLocalBuild,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
             IdSpace::kNone},
            {ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal}},
           {"dataset.diskann_float32_bin/v1",
            "partition.complete_local_to_global_map/v1"},
           {"vector_accessor/v1", "distance_function/l2-v1",
            "cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kLocalGraph,
           "local-graph-artifact/v1",
           IdSpace::kLocal,
           {"graph.local_adjacency/v1"}},
          "contracts/papers/xn-graph-xn-descent-v1/schemas/"
          "xn-descent-parameters.schema.json",
          false};
}

}  // namespace diskann_pipeline::xn_descent
