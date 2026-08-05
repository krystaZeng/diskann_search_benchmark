#include "fakes/fake_pipeline.hpp"

namespace diskann_pipeline::test {

namespace {

const std::filesystem::path kParameterSchema =
    "contracts/v1/schemas/fake-parameters.schema.json";

}  // namespace

ImplementationDescriptor fake_partition_descriptor() {
  return {"fake_partition_v1",
          "1.0.0",
          StageKind::kPartition,
          {{{ArtifactType::kDataset, {"fake-dataset-json/v1"},
             IdSpace::kNone}},
           {"dataset.vector_access/v1"},
           {"vector_accessor/v1", "metrics_sink/v1",
            "cancellation_token/v1"}},
          {ArtifactType::kPartition, "partition-artifact/v1", IdSpace::kLocal,
           {"partition.complete_local_to_global_map/v1",
            "partition.overlap/v1"}},
          kParameterSchema,
          true};
}

ImplementationDescriptor fake_local_graph_descriptor() {
  return {"fake_local_graph_v1",
          "1.0.0",
          StageKind::kLocalBuild,
          {{{ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal}},
           {"partition.complete_local_to_global_map/v1"},
           {"metrics_sink/v1", "cancellation_token/v1"}},
          {ArtifactType::kLocalGraph, "local-graph-artifact/v1",
           IdSpace::kLocal, {"graph.local_adjacency/v1"}},
          kParameterSchema,
          true};
}

ImplementationDescriptor fake_merge_descriptor() {
  return {"fake_merge_v1",
          "1.0.0",
          StageKind::kMerge,
          {{{ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal},
            {ArtifactType::kLocalGraph, {"local-graph-artifact/v1"},
             IdSpace::kLocal}},
           {"partition.complete_local_to_global_map/v1",
            "graph.local_adjacency/v1"},
           {"metrics_sink/v1", "cancellation_token/v1"}},
          {ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
           IdSpace::kGlobal, {"graph.global_adjacency/v1"}},
          kParameterSchema,
          true};
}

void register_fake_implementations(ImplementationRegistry& registry) {
  registry.register_partition(fake_partition_descriptor(), make_fake_partition);
  registry.register_local_build(fake_local_graph_descriptor(),
                                make_fake_local_graph);
  registry.register_merge(fake_merge_descriptor(), make_fake_merge);
}

}  // namespace diskann_pipeline::test
