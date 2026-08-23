#include "search/diskann/native_search_formats.hpp"

#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace diskann_pipeline::search {
namespace {

template <typename T>
T read_scalar(std::ifstream& input, std::uint64_t offset,
              const char* description) {
  T value{};
  input.seekg(static_cast<std::streamoff>(offset));
  input.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (!input) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("truncated native payload: ") + description);
  }
  return value;
}

std::uint64_t native_file_size(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "native payload is not a readable regular file");
  }
  return size;
}

void require_matrix(const std::filesystem::path& path,
                    std::uint32_t expected_rows,
                    std::uint32_t expected_columns,
                    std::uint64_t scalar_bytes) {
  std::ifstream input(path, std::ios::binary);
  const auto rows = read_scalar<std::uint32_t>(input, 0, "matrix rows");
  const auto columns = read_scalar<std::uint32_t>(input, 4, "matrix columns");
  if (rows != expected_rows || columns != expected_columns ||
      native_file_size(path) !=
          8ULL + static_cast<std::uint64_t>(rows) * columns * scalar_bytes) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "native matrix header or byte length is inconsistent");
  }
}

}  // namespace

void validate_pq_native_payloads(const std::filesystem::path& pivots,
                                 const std::filesystem::path& compressed,
                                 NodeCount expected_points,
                                 std::uint32_t expected_dimension,
                                 std::uint32_t expected_chunks,
                                 bool opq) {
  if (expected_points > std::numeric_limits<std::uint32_t>::max() ||
      expected_chunks == 0 || expected_chunks > expected_dimension) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ dimensions exceed pinned native header bounds");
  }
  std::ifstream pivot_input(pivots, std::ios::binary);
  const auto offsets_rows =
      read_scalar<std::uint32_t>(pivot_input, 0, "PQ offset rows");
  const auto offsets_columns =
      read_scalar<std::uint32_t>(pivot_input, 4, "PQ offset columns");
  if (offsets_rows != 4 || offsets_columns != 1) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "PQ pivot offset table must contain four offsets");
  }
  std::vector<std::size_t> offsets(offsets_rows);
  pivot_input.seekg(8);
  pivot_input.read(reinterpret_cast<char*>(offsets.data()),
                   static_cast<std::streamsize>(offsets.size() * sizeof(std::size_t)));
  if (!pivot_input || !std::is_sorted(offsets.begin(), offsets.end()) ||
      offsets.front() < 8 + offsets.size() * sizeof(std::size_t) ||
      offsets.back() != native_file_size(pivots)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "PQ pivot offsets are truncated or inconsistent");
  }
  const auto table_rows = read_scalar<std::uint32_t>(
      pivot_input, offsets[0], "PQ centroid rows");
  const auto table_dimension = read_scalar<std::uint32_t>(
      pivot_input, offsets[0] + 4, "PQ centroid dimension");
  const auto mean_dimension = read_scalar<std::uint32_t>(
      pivot_input, offsets[1], "PQ mean dimension");
  const auto mean_columns = read_scalar<std::uint32_t>(
      pivot_input, offsets[1] + 4, "PQ mean columns");
  const auto chunk_rows = read_scalar<std::uint32_t>(
      pivot_input, offsets[2], "PQ chunk rows");
  const auto chunk_columns = read_scalar<std::uint32_t>(
      pivot_input, offsets[2] + 4, "PQ chunk columns");
  if (table_rows != 256 || table_dimension != expected_dimension ||
      mean_dimension != expected_dimension || mean_columns != 1 ||
      chunk_rows != expected_chunks + 1 || chunk_columns != 1) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ pivot contents differ from Artifact metadata");
  }
  std::uint32_t prior = 0;
  for (std::uint32_t index = 0; index < chunk_rows; ++index) {
    const auto boundary = read_scalar<std::uint32_t>(
        pivot_input, offsets[2] + 8 + index * sizeof(std::uint32_t),
        "PQ chunk boundary");
    if ((index == 0 && boundary != 0) || boundary < prior ||
        boundary > expected_dimension) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "PQ chunk boundaries are invalid");
    }
    prior = boundary;
  }
  if (prior != expected_dimension) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ chunks do not cover the declared dimension");
  }

  std::ifstream codes_input(compressed, std::ios::binary);
  const auto points =
      read_scalar<std::uint32_t>(codes_input, 0, "PQ code point count");
  const auto chunks =
      read_scalar<std::uint32_t>(codes_input, 4, "PQ code chunk count");
  if (points != expected_points || chunks != expected_chunks ||
      native_file_size(compressed) !=
          8ULL + static_cast<std::uint64_t>(points) * chunks) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ compressed header or byte length is inconsistent");
  }
  if (!opq &&
      std::filesystem::exists(pivots.string() + "_rotation_matrix.bin")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OPQ rotation payload is forbidden when opq=false");
  }
}

void validate_disk_layout_native_payload(
    const std::filesystem::path& disk_index, NodeCount expected_points,
    std::uint32_t expected_dimension, std::uint32_t expected_max_degree,
    bool reorder, std::uint64_t frozen_point_count) {
  constexpr std::uint64_t kSectorBytes = 4096;
  std::ifstream input(disk_index, std::ios::binary);
  const auto metadata_rows =
      read_scalar<std::uint32_t>(input, 0, "disk metadata rows");
  const auto metadata_columns =
      read_scalar<std::uint32_t>(input, 4, "disk metadata columns");
  if (metadata_rows != 9 || metadata_columns != 1) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "full-precision disk metadata must contain nine values");
  }
  std::vector<std::uint64_t> metadata(metadata_rows);
  input.seekg(8);
  input.read(reinterpret_cast<char*>(metadata.data()),
             static_cast<std::streamsize>(metadata.size() * sizeof(std::uint64_t)));
  const auto actual_bytes = native_file_size(disk_index);
  const auto expected_record_bytes =
      static_cast<std::uint64_t>(expected_dimension) * sizeof(float) +
      (static_cast<std::uint64_t>(expected_max_degree) + 1) *
          sizeof(std::uint32_t);
  if (!input || metadata[0] != expected_points ||
      metadata[1] != expected_dimension || metadata[3] != expected_record_bytes ||
      metadata[4] != kSectorBytes / expected_record_bytes ||
      metadata[5] != frozen_point_count || metadata[7] != (reorder ? 1U : 0U) ||
      metadata[8] != actual_bytes || actual_bytes < 2 * kSectorBytes ||
      actual_bytes % kSectorBytes != 0 || reorder || frozen_point_count != 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "disk layout header violates full-precision MVP semantics");
  }
  if (!is_valid_global_node_id(static_cast<GlobalNodeId>(metadata[2]),
                               expected_points)) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "disk layout medoid is outside GlobalNodeId range");
  }
  const auto nodes_per_sector = metadata[4];
  for (NodeCount node = 0; node < expected_points; ++node) {
    const auto record = kSectorBytes + (node / nodes_per_sector) * kSectorBytes +
                        (node % nodes_per_sector) * expected_record_bytes;
    for (std::uint32_t dimension = 0; dimension < expected_dimension;
         ++dimension) {
      const auto scalar = read_scalar<float>(
          input, record + dimension * sizeof(float), "disk vector scalar");
      if (!std::isfinite(scalar)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "disk vector contains a non-finite value");
      }
    }
    const auto degree = read_scalar<std::uint32_t>(
        input, record + expected_dimension * sizeof(float), "disk degree");
    if (degree == 0 || degree > expected_max_degree) {
      throw PipelineError(ErrorCategory::kDegreeExceeded,
                          "disk graph degree is zero or exceeds the declaration");
    }
    std::set<GlobalNodeId> neighbors;
    for (std::uint32_t edge = 0; edge < degree; ++edge) {
      const auto neighbor = read_scalar<GlobalNodeId>(
          input,
          record + expected_dimension * sizeof(float) + sizeof(std::uint32_t) +
              edge * sizeof(GlobalNodeId),
          "disk neighbor");
      if (!is_valid_global_node_id(neighbor, expected_points) ||
          neighbor == node || !neighbors.insert(neighbor).second) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "disk graph neighbor is invalid");
      }
    }
  }
}

void validate_medoids_native_payload(const std::filesystem::path& medoids,
                                     std::uint32_t expected_count,
                                     NodeCount global_node_count) {
  require_matrix(medoids, expected_count, 1, sizeof(std::uint32_t));
  std::ifstream input(medoids, std::ios::binary);
  std::set<GlobalNodeId> values;
  for (std::uint32_t index = 0; index < expected_count; ++index) {
    const auto value = read_scalar<GlobalNodeId>(
        input, 8 + index * sizeof(GlobalNodeId), "medoid");
    if (!is_valid_global_node_id(value, global_node_count) ||
        !values.insert(value).second) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "medoid is duplicated or outside GlobalNodeId range");
    }
  }
}

void validate_centroids_native_payload(const std::filesystem::path& centroids,
                                       std::uint32_t expected_count,
                                       std::uint32_t dimension) {
  require_matrix(centroids, expected_count, dimension, sizeof(float));
}

}  // namespace diskann_pipeline::search
