#include "validation/validation_internal.hpp"

#include "diskann_pipeline/errors.hpp"

namespace diskann_pipeline::validation_detail {

const SchemaRule& schema_rule(ArtifactType artifact_type) {
  static const SchemaRule dataset{ArtifactType::kDataset,
                                  "fake-dataset-json/v1", IdSpace::kNone,
                                  "dataset.vector_access/v1"};
  static const SchemaRule partition{
      ArtifactType::kPartition, "partition-artifact/v1", IdSpace::kLocal,
      "partition.complete_local_to_global_map/v1"};
  static const SchemaRule local{ArtifactType::kLocalGraph,
                                "local-graph-artifact/v1", IdSpace::kLocal,
                                "graph.local_adjacency/v1"};
  static const SchemaRule global{ArtifactType::kGlobalGraph,
                                 "global-graph-artifact/v1", IdSpace::kGlobal,
                                 "graph.global_adjacency/v1"};
  switch (artifact_type) {
    case ArtifactType::kDataset:
      return dataset;
    case ArtifactType::kPartition:
      return partition;
    case ArtifactType::kLocalGraph:
      return local;
    case ArtifactType::kGlobalGraph:
      return global;
  }
  throw PipelineError(ErrorCategory::kUnsupportedVersion,
                      "Artifact type has no registered schema");
}

}  // namespace diskann_pipeline::validation_detail
