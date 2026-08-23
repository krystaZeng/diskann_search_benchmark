#include "validation/validation_internal.hpp"

#include "diskann_pipeline/errors.hpp"

namespace diskann_pipeline::validation_detail {

const SchemaRule& schema_rule(ArtifactType artifact_type,
                              std::string_view format_version) {
  static const SchemaRule dataset{ArtifactType::kDataset,
                                  "fake-dataset-json/v1", IdSpace::kNone,
                                  "dataset.vector_access/v1"};
  static const SchemaRule cpp_main_dataset{
      ArtifactType::kDataset, "diskann-float32-dataset/v1", IdSpace::kNone,
      "dataset.diskann_float32_bin/v1"};
  static const SchemaRule partition{
      ArtifactType::kPartition, "partition-artifact/v1", IdSpace::kLocal,
      "partition.complete_local_to_global_map/v1"};
  static const SchemaRule local{ArtifactType::kLocalGraph,
                                "local-graph-artifact/v1", IdSpace::kLocal,
                                "graph.local_adjacency/v1"};
  static const SchemaRule global{ArtifactType::kGlobalGraph,
                                 "global-graph-artifact/v1", IdSpace::kGlobal,
                                 "graph.global_adjacency/v1"};
  static const SchemaRule pq{ArtifactType::kPQ, "pq-artifact/v1",
                             IdSpace::kNone, "search.pq_in_memory/v1"};
  static const SchemaRule disk_layout{
      ArtifactType::kDiskLayout, "disk-layout-artifact/v1", IdSpace::kGlobal,
      "search.disk_layout/v1"};
  switch (artifact_type) {
    case ArtifactType::kDataset:
      if (format_version == dataset.format_version) {
        return dataset;
      }
      if (format_version == cpp_main_dataset.format_version) {
        return cpp_main_dataset;
      }
      break;
    case ArtifactType::kPartition:
      if (format_version == partition.format_version) {
        return partition;
      }
      break;
    case ArtifactType::kLocalGraph:
      if (format_version == local.format_version) {
        return local;
      }
      break;
    case ArtifactType::kGlobalGraph:
      if (format_version == global.format_version) {
        return global;
      }
      break;
    case ArtifactType::kPQ:
      if (format_version == pq.format_version) {
        return pq;
      }
      break;
    case ArtifactType::kDiskLayout:
      if (format_version == disk_layout.format_version) {
        return disk_layout;
      }
      break;
  }
  throw PipelineError(ErrorCategory::kUnsupportedVersion,
                      "Artifact type has no registered schema");
}

}  // namespace diskann_pipeline::validation_detail
