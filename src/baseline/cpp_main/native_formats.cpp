#include "baseline/cpp_main/native_formats.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <string>

namespace diskann_pipeline::cpp_main {
namespace {

std::uint64_t exact_file_size(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "native payload is not a regular file: " +
                            path.string());
  }
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "cannot determine native payload length: " +
                            path.string());
  }
  return size;
}

std::uint32_t read_u32(std::istream& input, const char* field) {
  unsigned char bytes[4]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        std::string("truncated native field: ") + field);
  }
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint64_t read_u64(std::istream& input, const char* field) {
  unsigned char bytes[8]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        std::string("truncated native field: ") + field);
  }
  std::uint64_t result = 0;
  for (unsigned index = 0; index < 8; ++index) {
    result |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return result;
}

std::uint64_t checked_matrix_length(NodeCount rows, std::uint32_t dimension,
                                    std::uint64_t scalar_bytes) {
  if (!is_valid_count(rows) || dimension == 0) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "native matrix count and dimension must be positive");
  }
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  if (rows > maximum / dimension ||
      rows * static_cast<std::uint64_t>(dimension) > maximum / scalar_bytes) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native matrix byte length overflows uint64");
  }
  const auto payload = rows * static_cast<std::uint64_t>(dimension) *
                       scalar_bytes;
  if (payload > maximum - 8U) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native matrix envelope length overflows uint64");
  }
  return 8U + payload;
}

MatrixFacts read_float32_matrix(const std::filesystem::path& path,
                                NodeCount expected_rows,
                                std::uint32_t expected_dimension) {
  const auto actual_length = exact_file_size(path);
  std::ifstream input(path, std::ios::binary);
  const auto rows = read_u32(input, "npts");
  const auto dimension = read_u32(input, "dim");
  if (rows != expected_rows || dimension != expected_dimension) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "native matrix header differs from expected identity");
  }
  const auto expected_length =
      checked_matrix_length(expected_rows, expected_dimension, sizeof(float));
  if (actual_length != expected_length) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native float32 matrix has truncated or trailing bytes");
  }
  for (NodeCount row = 0; row < expected_rows; ++row) {
    for (std::uint32_t column = 0; column < expected_dimension; ++column) {
      const auto bits = read_u32(input, "float32 scalar");
      float scalar = 0.0F;
      std::memcpy(&scalar, &bits, sizeof(scalar));
      if (!std::isfinite(scalar)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "native float32 matrix contains a non-finite value");
      }
    }
  }
  return {expected_rows, expected_dimension, actual_length};
}

std::vector<GlobalNodeId> read_u32_vector(
    const std::filesystem::path& path, NodeCount expected_rows,
    NodeCount global_node_count, const char* label, bool require_unique) {
  const auto actual_length = exact_file_size(path);
  std::ifstream input(path, std::ios::binary);
  const auto rows = read_u32(input, "row_count");
  const auto dimension = read_u32(input, "dim");
  if (rows != expected_rows || dimension != 1) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        std::string(label) +
                            " header differs from expected count or dim=1");
  }
  const auto expected_length = checked_matrix_length(expected_rows, 1, 4U);
  if (actual_length != expected_length) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        std::string(label) +
                            " has truncated or trailing bytes");
  }
  std::vector<GlobalNodeId> values;
  values.reserve(static_cast<std::size_t>(expected_rows));
  std::set<GlobalNodeId> unique;
  for (NodeCount index = 0; index < expected_rows; ++index) {
    const auto value = read_u32(input, label);
    if (!is_valid_global_node_id(value, global_node_count)) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          std::string(label) + " contains an out-of-range ID");
    }
    if (require_unique && !unique.insert(value).second) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          std::string(label) + " contains a duplicate ID");
    }
    values.push_back(value);
  }
  return values;
}

GraphFacts validate_graph(const std::filesystem::path& path,
                          NodeCount node_count, std::uint32_t degree_bound,
                          bool local, bool header_is_observed_maximum) {
  if (!is_valid_count(node_count)) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "native graph node count is invalid");
  }
  const auto actual_length = exact_file_size(path);
  if (actual_length < 24U) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native graph header is truncated");
  }
  std::ifstream input(path, std::ios::binary);
  const auto declared_size = read_u64(input, "declared_file_size");
  const auto header_max = read_u32(input, "maximum_observed_degree");
  const auto entry_point = read_u32(input, "entry_point");
  const auto frozen = read_u64(input, "frozen_point_count");
  if (declared_size != actual_length) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native graph declared size differs from actual length");
  }
  if (header_max > degree_bound) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "native graph header degree exceeds Profile bound");
  }
  if (!is_valid_global_node_id(entry_point, node_count)) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "native graph entry point is outside its ID space");
  }
  if (frozen != 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "scoped unfiltered graph has frozen points");
  }
  std::uint32_t observed_max = 0;
  for (NodeCount node = 0; node < node_count; ++node) {
    const auto degree = read_u32(input, "degree");
    if (degree > degree_bound) {
      throw PipelineError(ErrorCategory::kDegreeExceeded,
                          "native graph degree exceeds Profile bound");
    }
    observed_max = std::max(observed_max, degree);
    for (std::uint32_t edge = 0; edge < degree; ++edge) {
      const auto neighbor = read_u32(input, "neighbor");
      const bool valid = local
                             ? is_valid_local_node_id(neighbor, node_count)
                             : is_valid_global_node_id(neighbor, node_count);
      if (!valid) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "native graph neighbor is outside its ID space");
      }
    }
  }
  if (input.peek() != std::char_traits<char>::eof()) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "native graph contains trailing bytes");
  }
  if (header_is_observed_maximum && observed_max != header_max) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "native graph header maximum differs from adjacency");
  }
  return {declared_size, header_max, entry_point, frozen, node_count,
          observed_max};
}

}  // namespace

MatrixFacts validate_float32_matrix(const std::filesystem::path& path,
                                    NodeCount expected_rows,
                                    std::uint32_t expected_dimension) {
  return read_float32_matrix(path, expected_rows, expected_dimension);
}

MatrixFacts validate_float32_dataset(const std::filesystem::path& path,
                                     NodeCount expected_rows,
                                     std::uint32_t expected_dimension,
                                     const std::string& expected_sha256) {
  const auto facts =
      read_float32_matrix(path, expected_rows, expected_dimension);
  if (!expected_sha256.empty() && sha256_file(path) != expected_sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "Dataset payload checksum differs from DatasetIdentity");
  }
  return facts;
}

std::vector<GlobalNodeId> validate_partition_id_map(
    const std::filesystem::path& path, NodeCount expected_rows,
    NodeCount global_node_count) {
  return read_u32_vector(path, expected_rows, global_node_count,
                         "Partition ID map", true);
}

MatrixFacts validate_centroids(const std::filesystem::path& path,
                               NodeCount partition_count,
                               std::uint32_t dataset_dimension) {
  return read_float32_matrix(path, partition_count, dataset_dimension);
}

GraphFacts validate_local_graph(const std::filesystem::path& path,
                                NodeCount local_node_count,
                                std::uint32_t degree_bound) {
  return validate_graph(path, local_node_count, degree_bound, true, true);
}

MatrixFacts validate_local_graph_data(const std::filesystem::path& path,
                                      NodeCount local_node_count,
                                      std::uint32_t dataset_dimension) {
  return read_float32_matrix(path, local_node_count, dataset_dimension);
}

GraphFacts validate_merged_global_graph(const std::filesystem::path& path,
                                        NodeCount global_node_count,
                                        std::uint32_t degree_bound) {
  // merge_shards writes the requested output width R in the header. Unlike
  // Index::save, that field is a bound and need not equal the maximum degree
  // observed in a small merged graph.
  return validate_graph(path, global_node_count, degree_bound, false, false);
}

std::vector<GlobalNodeId> validate_medoids(
    const std::filesystem::path& path, NodeCount partition_count,
    NodeCount global_node_count) {
  return read_u32_vector(path, partition_count, global_node_count, "medoids",
                         false);
}

}  // namespace diskann_pipeline::cpp_main
