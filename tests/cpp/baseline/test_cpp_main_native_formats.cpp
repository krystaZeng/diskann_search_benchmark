#include "baseline/cpp_main/native_formats.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;

namespace {

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    static std::atomic<unsigned long long> sequence{0};
    path = std::filesystem::temp_directory_path() /
           ("cpp-main-native-formats-" +
            std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(path);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::filesystem::path path;
};

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void append_float(std::vector<std::uint8_t>& bytes, float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  append_u32(bytes, bits);
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("unable to write native-format test fixture");
  }
}

std::vector<std::uint8_t> matrix(
    std::uint32_t rows, std::uint32_t dimension,
    const std::vector<float>& values) {
  std::vector<std::uint8_t> bytes;
  append_u32(bytes, rows);
  append_u32(bytes, dimension);
  for (const auto value : values) {
    append_float(bytes, value);
  }
  return bytes;
}

std::vector<std::uint8_t> uint32_matrix(
    const std::vector<std::uint32_t>& values) {
  std::vector<std::uint8_t> bytes;
  append_u32(bytes, static_cast<std::uint32_t>(values.size()));
  append_u32(bytes, 1);
  for (const auto value : values) {
    append_u32(bytes, value);
  }
  return bytes;
}

std::vector<std::uint8_t> graph(std::uint32_t header_max,
                                std::uint32_t entry_point,
                                const std::vector<std::vector<std::uint32_t>>& adjacency,
                                std::uint64_t frozen = 0) {
  std::vector<std::uint8_t> records;
  for (const auto& neighbors : adjacency) {
    append_u32(records, static_cast<std::uint32_t>(neighbors.size()));
    for (const auto neighbor : neighbors) {
      append_u32(records, neighbor);
    }
  }
  std::vector<std::uint8_t> bytes;
  append_u64(bytes, 24U + records.size());
  append_u32(bytes, header_max);
  append_u32(bytes, entry_point);
  append_u64(bytes, frozen);
  bytes.insert(bytes.end(), records.begin(), records.end());
  return bytes;
}

}  // namespace

int main() {
  TemporaryDirectory temporary;

  const auto dataset_path = temporary.path / "dataset.bin";
  write_bytes(dataset_path,
              matrix(3, 2, {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}));
  const auto dataset_sha = dp::sha256_file(dataset_path);
  const auto dataset =
      native::validate_float32_dataset(dataset_path, 3, 2, dataset_sha);
  DP_EXPECT_EQ(dataset.row_count, dp::NodeCount{3});
  DP_EXPECT_EQ(dataset.dimension, std::uint32_t{2});

  auto non_finite = matrix(1, 1, {std::numeric_limits<float>::infinity()});
  write_bytes(temporary.path / "non-finite.bin", non_finite);
  DP_EXPECT_THROW(native::validate_float32_dataset(
      temporary.path / "non-finite.bin", 1, 1, {}));

  auto truncated = matrix(2, 2, {0.0F, 1.0F, 2.0F, 3.0F});
  truncated.pop_back();
  write_bytes(temporary.path / "truncated.bin", truncated);
  DP_EXPECT_THROW(native::validate_float32_matrix(
      temporary.path / "truncated.bin", 2, 2));

  auto trailing = matrix(1, 1, {1.0F});
  trailing.push_back(0);
  write_bytes(temporary.path / "trailing.bin", trailing);
  DP_EXPECT_THROW(native::validate_float32_matrix(
      temporary.path / "trailing.bin", 1, 1));

  std::vector<std::uint8_t> overflow;
  append_u32(overflow, std::numeric_limits<std::uint32_t>::max());
  append_u32(overflow, std::numeric_limits<std::uint32_t>::max());
  write_bytes(temporary.path / "overflow.bin", overflow);
  DP_EXPECT_THROW(native::validate_float32_matrix(
      temporary.path / "overflow.bin",
      std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max()));

  const auto map_path = temporary.path / "map.bin";
  write_bytes(map_path, uint32_matrix({0, 2, 4}));
  const auto ids = native::validate_partition_id_map(map_path, 3, 6);
  DP_EXPECT_EQ(ids.size(), std::size_t{3});
  DP_EXPECT_EQ(ids[2], dp::GlobalNodeId{4});
  write_bytes(temporary.path / "map-duplicate.bin", uint32_matrix({0, 4, 4}));
  DP_EXPECT_THROW(native::validate_partition_id_map(
      temporary.path / "map-duplicate.bin", 3, 6));
  write_bytes(temporary.path / "map-range.bin", uint32_matrix({0, 2, 6}));
  DP_EXPECT_THROW(native::validate_partition_id_map(
      temporary.path / "map-range.bin", 3, 6));

  const auto centroids_path = temporary.path / "centroids.bin";
  write_bytes(centroids_path, matrix(2, 2, {0.0F, 1.0F, 2.0F, 3.0F}));
  DP_EXPECT_EQ(native::validate_centroids(centroids_path, 2, 2).row_count,
               dp::NodeCount{2});

  const auto local_path = temporary.path / "local.graph";
  write_bytes(local_path, graph(2, 0, {{1, 2}, {0}, {}}));
  const auto local = native::validate_local_graph(local_path, 3, 2);
  DP_EXPECT_EQ(local.observed_max_degree, std::uint32_t{2});
  DP_EXPECT_EQ(local.entry_point, dp::GlobalNodeId{0});

  write_bytes(temporary.path / "bad-entry.graph",
              graph(2, 3, {{1, 2}, {0}, {}}));
  DP_EXPECT_THROW(native::validate_local_graph(
      temporary.path / "bad-entry.graph", 3, 2));
  write_bytes(temporary.path / "bad-neighbor.graph",
              graph(2, 0, {{1, 3}, {0}, {}}));
  DP_EXPECT_THROW(native::validate_local_graph(
      temporary.path / "bad-neighbor.graph", 3, 2));
  write_bytes(temporary.path / "bad-degree.graph",
              graph(3, 0, {{1, 2}, {0}, {}}));
  DP_EXPECT_THROW(native::validate_local_graph(
      temporary.path / "bad-degree.graph", 3, 2));

  const auto data_path = temporary.path / "local.data";
  write_bytes(data_path, matrix(3, 2, {0, 1, 2, 3, 4, 5}));
  DP_EXPECT_EQ(native::validate_local_graph_data(data_path, 3, 2).dimension,
               std::uint32_t{2});

  const auto global_path = temporary.path / "global.graph";
  write_bytes(global_path, graph(2, 1, {{1}, {0, 2}, {1}}));
  DP_EXPECT_EQ(
      native::validate_merged_global_graph(global_path, 3, 2)
          .observed_max_degree,
      std::uint32_t{2});
  const auto sparse_global_path = temporary.path / "sparse-global.graph";
  write_bytes(sparse_global_path, graph(2, 1, {{1}, {0}, {1}}));
  DP_EXPECT_EQ(
      native::validate_merged_global_graph(sparse_global_path, 3, 2)
          .observed_max_degree,
      std::uint32_t{1});
  auto truncated_global = graph(2, 1, {{1}, {0, 2}, {1}});
  truncated_global.pop_back();
  write_bytes(temporary.path / "truncated-global.graph", truncated_global);
  DP_EXPECT_THROW(native::validate_merged_global_graph(
      temporary.path / "truncated-global.graph", 3, 2));
  auto trailing_global = graph(2, 1, {{1}, {0, 2}, {1}});
  trailing_global.push_back(0);
  write_bytes(temporary.path / "trailing-global.graph", trailing_global);
  DP_EXPECT_THROW(native::validate_merged_global_graph(
      temporary.path / "trailing-global.graph", 3, 2));
  write_bytes(temporary.path / "self-global.graph",
              graph(2, 1, {{0, 1}, {0}, {1}}));
  DP_EXPECT_THROW(native::validate_merged_global_graph(
      temporary.path / "self-global.graph", 3, 2));

  const auto medoids_path = temporary.path / "medoids.bin";
  write_bytes(medoids_path, uint32_matrix({0, 2}));
  const auto medoids = native::validate_medoids(medoids_path, 2, 3);
  DP_EXPECT_EQ(medoids.size(), std::size_t{2});
  write_bytes(temporary.path / "duplicate-medoids.bin",
              uint32_matrix({2, 2}));
  DP_EXPECT_EQ(native::validate_medoids(
                   temporary.path / "duplicate-medoids.bin", 2, 3)
                   .size(),
               std::size_t{2});
  write_bytes(temporary.path / "bad-medoids.bin", uint32_matrix({0, 3}));
  DP_EXPECT_THROW(native::validate_medoids(
      temporary.path / "bad-medoids.bin", 2, 3));

  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const auto metadata_dir = workspace.normal_root / "native-dataset";
  const auto artifact_dataset = metadata_dir / "vectors.bin";
  write_bytes(artifact_dataset,
              matrix(3, 2, {0.0F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F}));
  const auto artifact_dataset_sha = dp::sha256_file(artifact_dataset);
  const nlohmann::json structural{{"global_node_count", 3},
                                  {"vector_dimension", 2},
                                  {"scalar_type", "float32"},
                                  {"distance_metric", "l2"}};
  dp::AggregateIdentityProjection projection{
      dp::ArtifactType::kDataset,
      "diskann-float32-dataset/v1",
      artifact_dataset_sha,
      dp::IdSpace::kNone,
      {"dataset.diskann_float32_bin/v1"},
      {{"vectors", std::filesystem::file_size(artifact_dataset),
        artifact_dataset_sha}},
      {},
      structural.dump()};
  const auto aggregate = dp::aggregate_checksum(projection);
  nlohmann::json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "dataset"},
      {"format_version", "diskann-float32-dataset/v1"},
      {"dataset",
       {{"dataset_id", "native-three-node-v1"},
        {"dataset_checksum", artifact_dataset_sha},
        {"global_node_count", 3},
        {"vector_dimension", 2},
        {"scalar_type", "float32"},
        {"distance_metric", "l2"}}},
      {"id_space", "none"},
      {"structural", structural},
      {"capabilities",
       nlohmann::json::array({"dataset.diskann_float32_bin/v1"})},
      {"source_artifacts", nlohmann::json::array()},
      {"payloads",
       nlohmann::json::array(
           {{{"role", "vectors"},
             {"path", "vectors.bin"},
             {"media_type", native::kFloat32DatasetMediaType},
             {"byte_length", std::filesystem::file_size(artifact_dataset)},
             {"sha256", artifact_dataset_sha}}})},
      {"aggregate_checksum", aggregate}};
  std::ofstream(metadata_dir / "artifact.json", std::ios::binary)
      << metadata.dump();
  dp::ArtifactRef reference{"native-dataset/artifact.json",
                            dp::ArtifactType::kDataset,
                            "diskann-float32-dataset/v1", aggregate};
  dp::Validator validator(workspace);
  DP_EXPECT_TRUE(validator.validate(reference).valid());

  metadata["payloads"][0]["media_type"] = "application/octet-stream";
  std::ofstream(metadata_dir / "artifact.json",
                std::ios::binary | std::ios::trunc)
      << metadata.dump();
  DP_EXPECT_TRUE(!validator.validate(reference).valid());

  return dp::test::finish();
}
