#pragma once

#include "diskann_pipeline/identifiers.hpp"
#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/validation.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace diskann_pipeline::cpp_main {

inline constexpr const char* kFloat32DatasetMediaType =
    "application/vnd.diskann.cpp-main.float32-bin;version=1";
inline constexpr const char* kPartitionIdMapMediaType =
    "application/vnd.diskann.cpp-main.uint32-id-map;version=1";
inline constexpr const char* kCentroidsMediaType =
    "application/vnd.diskann.cpp-main.float32-centroids;version=1";
inline constexpr const char* kLocalGraphMediaType =
    "application/vnd.diskann.cpp-main.local-graph;version=1";
inline constexpr const char* kGlobalGraphMediaType =
    "application/vnd.diskann.cpp-main.global-graph;version=1";
inline constexpr const char* kMedoidsMediaType =
    "application/vnd.diskann.cpp-main.uint32-medoids;version=1";

// Only inspect facts already checked by Validator. Missing future inputs are
// deferred; each map/graph actually present is checked independently.
CompatibilityResult check_native_partition_maps(
    const std::vector<ValidatedArtifact>& known_inputs);
CompatibilityResult check_native_merge_inputs(
    const std::vector<ValidatedArtifact>& known_inputs);

struct MatrixFacts {
  NodeCount row_count;
  std::uint32_t dimension;
  std::uint64_t byte_length;
};

struct GraphFacts {
  std::uint64_t declared_file_size;
  std::uint32_t header_max_degree;
  GlobalNodeId entry_point;
  std::uint64_t frozen_point_count;
  NodeCount node_count;
  std::uint32_t observed_max_degree;
};

MatrixFacts validate_float32_matrix(const std::filesystem::path& path,
                                    NodeCount expected_rows,
                                    std::uint32_t expected_dimension);
MatrixFacts validate_float32_dataset(const std::filesystem::path& path,
                                     NodeCount expected_rows,
                                     std::uint32_t expected_dimension,
                                     const std::string& expected_sha256);
std::vector<GlobalNodeId> validate_partition_id_map(
    const std::filesystem::path& path, NodeCount expected_rows,
    NodeCount global_node_count);
MatrixFacts validate_centroids(const std::filesystem::path& path,
                               NodeCount partition_count,
                               std::uint32_t dataset_dimension);
GraphFacts validate_local_graph(const std::filesystem::path& path,
                                NodeCount local_node_count,
                                std::uint32_t degree_bound);
MatrixFacts validate_local_graph_data(const std::filesystem::path& path,
                                      NodeCount local_node_count,
                                      std::uint32_t dataset_dimension);
GraphFacts validate_merged_global_graph(const std::filesystem::path& path,
                                        NodeCount global_node_count,
                                        std::uint32_t degree_bound);
std::vector<GlobalNodeId> validate_medoids(
    const std::filesystem::path& path, NodeCount partition_count,
    NodeCount global_node_count);

}  // namespace diskann_pipeline::cpp_main
