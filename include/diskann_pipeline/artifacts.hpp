#pragma once

#include "diskann_pipeline/artifact_ref.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

inline constexpr const char* kArtifactSchemaVersion = "1.0.0";
inline constexpr const char* kArtifactCanonicalization =
    "diskann-artifact-c14n-v1";
inline constexpr const char* kPartitionCentroidsPayloadRole = "centroids";
inline constexpr const char* kPartitionCentroidsCapability =
    "partition.centroids/v1";
inline constexpr const char* kGlobalGraphMedoidsPayloadRole = "medoids";
inline constexpr const char* kGlobalGraphMedoidsCapability =
    "graph.medoids/v1";

struct PartitionRecord {
  PartitionId partition_id;
  NodeCount local_node_count;
  std::optional<std::string> shard_vectors_role;
  std::string local_to_global_map_role;
};

struct DatasetArtifactMetadata {
  DatasetIdentity dataset;
  std::vector<std::string> capabilities;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

struct PartitionArtifactMetadata {
  DatasetIdentity dataset;
  NodeCount partition_count;
  std::vector<PartitionRecord> partitions;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

struct LocalGraphArtifactMetadata {
  DatasetIdentity dataset;
  PartitionId partition_id;
  NodeCount local_node_count;
  std::uint32_t declared_max_degree;
  std::uint32_t observed_max_degree;
  std::string source_partition_checksum;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

struct GlobalGraphArtifactMetadata {
  DatasetIdentity dataset;
  NodeCount global_node_count;
  std::uint32_t declared_max_degree;
  std::uint32_t observed_max_degree;
  SourceArtifactIdentity source_partition_artifact;
  std::vector<SourceArtifactIdentity> source_local_graph_artifacts;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

struct PQArtifactMetadata {
  DatasetIdentity dataset;
  NodeCount point_count;
  std::uint32_t dimension;
  std::uint32_t centroid_count;
  std::uint32_t pq_chunks;
  bool opq;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

struct DiskLayoutArtifactMetadata {
  DatasetIdentity dataset;
  NodeCount global_node_count;
  std::uint32_t dimension;
  std::uint32_t declared_max_degree;
  std::uint32_t medoid_count;
  std::string centroid_policy;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::string aggregate_checksum;
};

}  // namespace diskann_pipeline
