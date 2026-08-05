#pragma once

#include "diskann_pipeline/identifiers.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace diskann_pipeline {

struct ArtifactRef {
  std::filesystem::path metadata_path;
  ArtifactType artifact_type;
  std::string format_version;
  std::string aggregate_checksum;
};

struct PayloadRef {
  std::string role;
  std::filesystem::path path;
  std::string media_type;
  std::uint64_t byte_length;
  std::string sha256;
};

struct DatasetIdentity {
  std::string dataset_id;
  std::string dataset_checksum;
  NodeCount global_node_count;
  std::uint32_t vector_dimension;
  std::string scalar_type;
  std::string distance_metric;
};

struct SourceArtifactIdentity {
  ArtifactType artifact_type;
  std::string format_version;
  std::string aggregate_checksum;
};

}  // namespace diskann_pipeline

