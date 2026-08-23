#pragma once

#include "diskann_pipeline/identifiers.hpp"

#include <cstdint>
#include <filesystem>

namespace diskann_pipeline::search {

inline constexpr const char* kPqPivotsMediaType =
    "application/vnd.diskann.pq-pivots+binary;version=1";
inline constexpr const char* kPqCodesMediaType =
    "application/vnd.diskann.pq-codes+binary;version=1";
inline constexpr const char* kDiskIndexMediaType =
    "application/vnd.diskann.pqflash-disk-index+binary;version=1";
inline constexpr const char* kUint32MatrixMediaType =
    "application/vnd.diskann.uint32-matrix+binary;version=1";
inline constexpr const char* kFloat32MatrixMediaType =
    "application/vnd.diskann.float32-matrix+binary;version=1";

void validate_pq_native_payloads(const std::filesystem::path& pivots,
                                 const std::filesystem::path& compressed,
                                 NodeCount expected_points,
                                 std::uint32_t expected_dimension,
                                 std::uint32_t expected_chunks,
                                 bool opq);

void validate_disk_layout_native_payload(
    const std::filesystem::path& disk_index, NodeCount expected_points,
    std::uint32_t expected_dimension, std::uint32_t expected_max_degree,
    bool reorder, std::uint64_t frozen_point_count);

void validate_medoids_native_payload(const std::filesystem::path& medoids,
                                     std::uint32_t expected_count,
                                     NodeCount global_node_count);

void validate_centroids_native_payload(const std::filesystem::path& centroids,
                                       std::uint32_t expected_count,
                                       std::uint32_t dimension);

}  // namespace diskann_pipeline::search
