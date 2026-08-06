#pragma once

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace diskann_pipeline::cpp_main::test {

using Json = nlohmann::json;

class TemporaryDirectory {
 public:
  explicit TemporaryDirectory(const std::string& label) {
    static std::atomic<unsigned long long> sequence{0};
    path = std::filesystem::temp_directory_path() /
           ("cpp-main-" + label + "-" +
            std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(path);
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  std::filesystem::path path;
};

inline void append_u32(std::vector<std::uint8_t>& bytes,
                       std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

inline void append_u64(std::vector<std::uint8_t>& bytes,
                       std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

inline void append_float(std::vector<std::uint8_t>& bytes, float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  append_u32(bytes, bits);
}

inline void write_bytes(const std::filesystem::path& path,
                        const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("unable to write cpp_main fixture");
}

inline void write_text(const std::filesystem::path& path,
                       const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) throw std::runtime_error("unable to write cpp_main fixture");
}

inline Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to read cpp_main fixture");
  return Json::parse(input);
}

inline std::vector<std::uint8_t> float_matrix(
    std::uint32_t rows, std::uint32_t dimension,
    const std::vector<float>& values) {
  std::vector<std::uint8_t> bytes;
  append_u32(bytes, rows);
  append_u32(bytes, dimension);
  for (const auto value : values) append_float(bytes, value);
  return bytes;
}

inline std::vector<std::uint8_t> id_matrix(
    const std::vector<GlobalNodeId>& ids) {
  std::vector<std::uint8_t> bytes;
  append_u32(bytes, static_cast<std::uint32_t>(ids.size()));
  append_u32(bytes, 1);
  for (const auto id : ids) append_u32(bytes, id);
  return bytes;
}

inline std::vector<std::uint8_t> native_graph(
    std::uint32_t entry_point,
    const std::vector<std::vector<std::uint32_t>>& adjacency) {
  std::vector<std::uint8_t> records;
  std::uint32_t maximum = 0;
  for (const auto& neighbors : adjacency) {
    maximum = std::max(maximum,
                       static_cast<std::uint32_t>(neighbors.size()));
    append_u32(records, static_cast<std::uint32_t>(neighbors.size()));
    for (const auto neighbor : neighbors) append_u32(records, neighbor);
  }
  std::vector<std::uint8_t> bytes;
  append_u64(bytes, 24U + records.size());
  append_u32(bytes, maximum);
  append_u32(bytes, entry_point);
  append_u64(bytes, 0);
  bytes.insert(bytes.end(), records.begin(), records.end());
  return bytes;
}

inline Json source(const ArtifactRef& reference) {
  return {{"artifact_type", to_string(reference.artifact_type)},
          {"format_version", reference.format_version},
          {"aggregate_checksum", reference.aggregate_checksum}};
}

inline Json payload(const std::string& role, const std::string& path,
                    const std::string& media,
                    const std::filesystem::path& absolute) {
  return {{"role", role},
          {"path", path},
          {"media_type", media},
          {"byte_length", std::filesystem::file_size(absolute)},
          {"sha256", sha256_file(absolute)}};
}

inline std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection;
  projection.artifact_type = artifact_type_from_string(
      metadata.at("artifact_type").get<std::string>());
  projection.format_version = metadata.at("format_version").get<std::string>();
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space =
      id_space_from_string(metadata.at("id_space").get<std::string>());
  projection.capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  for (const auto& item : metadata.at("payloads")) {
    projection.payloads.push_back(
        {item.at("role").get<std::string>(),
         item.at("byte_length").get<std::uint64_t>(),
         item.at("sha256").get<std::string>()});
  }
  for (const auto& item : metadata.at("source_artifacts")) {
    projection.source_artifacts.push_back(
        {artifact_type_from_string(item.at("artifact_type").get<std::string>()),
         item.at("format_version").get<std::string>(),
         item.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return aggregate_checksum(projection);
}

struct NativeDatasetFixture {
  explicit NativeDatasetFixture(const WorkspaceRef& target,
                                std::uint32_t rows = 96,
                                std::uint32_t dimension = 4)
      : workspace(target) {
    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(rows) * dimension);
    for (std::uint32_t row = 0; row < rows; ++row) {
      for (std::uint32_t column = 0; column < dimension; ++column) {
        values.push_back(static_cast<float>((row * 7 + column * 13) % 101) /
                         17.0F);
      }
    }
    const auto directory = workspace.normal_root / "dataset";
    const auto vectors = directory / "vectors.bin";
    write_bytes(vectors, float_matrix(rows, dimension, values));
    const auto checksum = sha256_file(vectors);
    identity = {"cpp-main-small-native-v1", checksum, rows, dimension,
                "float32", "l2"};
    const Json dataset_json{{"dataset_id", identity.dataset_id},
                            {"dataset_checksum", identity.dataset_checksum},
                            {"global_node_count", identity.global_node_count},
                            {"vector_dimension", identity.vector_dimension},
                            {"scalar_type", "float32"},
                            {"distance_metric", "l2"}};
    Json metadata{
        {"schema_version", "1.0.0"},
        {"canonicalization", "diskann-artifact-c14n-v1"},
        {"artifact_type", "dataset"},
        {"format_version", "diskann-float32-dataset/v1"},
        {"dataset", dataset_json},
        {"id_space", "none"},
        {"structural",
         {{"global_node_count", rows},
          {"vector_dimension", dimension},
          {"scalar_type", "float32"},
          {"distance_metric", "l2"}}},
        {"capabilities", Json::array({"dataset.diskann_float32_bin/v1"})},
        {"source_artifacts", Json::array()},
        {"payloads", Json::array({payload("vectors", "vectors.bin",
                                            kFloat32DatasetMediaType,
                                            vectors)})}};
    metadata["aggregate_checksum"] = aggregate_for(metadata);
    write_text(directory / "artifact.json", metadata.dump());
    reference = {"dataset/artifact.json", ArtifactType::kDataset,
                 "diskann-float32-dataset/v1",
                 metadata.at("aggregate_checksum").get<std::string>()};
  }

  WorkspaceRef workspace;
  DatasetIdentity identity;
  ArtifactRef reference;
};

inline ArtifactRef materialize_partition(
    const WorkspaceRef& workspace, const NativeDatasetFixture& dataset,
    const std::vector<std::vector<GlobalNodeId>>& maps,
    bool include_centroids = false) {
  const auto directory = workspace.normal_root / "partition";
  Json records = Json::array();
  Json payloads = Json::array();
  for (std::size_t index = 0; index < maps.size(); ++index) {
    const auto filename = "map-" + std::to_string(index) + ".bin";
    const auto path = directory / filename;
    write_bytes(path, id_matrix(maps[index]));
    const auto role = "partition/" + std::to_string(index) +
                      "/local_to_global_map";
    records.push_back({{"partition_id", index},
                       {"local_node_count", maps[index].size()},
                       {"local_to_global_map_role", role}});
    payloads.push_back(payload(role, filename, kPartitionIdMapMediaType, path));
  }
  std::vector<std::string> capabilities{
      "partition.complete_local_to_global_map/v1",
      "partition.local_to_global_ascending/v1", "partition.overlap/v1"};
  if (include_centroids) {
    const auto path = directory / "centroids.bin";
    std::vector<float> values(maps.size() * dataset.identity.vector_dimension,
                              0.0F);
    write_bytes(path,
                float_matrix(static_cast<std::uint32_t>(maps.size()),
                             dataset.identity.vector_dimension, values));
    payloads.push_back(payload(kPartitionCentroidsPayloadRole,
                               "centroids.bin", kCentroidsMediaType, path));
    capabilities.insert(capabilities.begin(), kPartitionCentroidsCapability);
  }
  const Json dataset_json{{"dataset_id", dataset.identity.dataset_id},
                          {"dataset_checksum", dataset.identity.dataset_checksum},
                          {"global_node_count", dataset.identity.global_node_count},
                          {"vector_dimension", dataset.identity.vector_dimension},
                          {"scalar_type", "float32"},
                          {"distance_metric", "l2"}};
  Json metadata{{"schema_version", "1.0.0"},
                {"canonicalization", "diskann-artifact-c14n-v1"},
                {"artifact_type", "partition"},
                {"format_version", "partition-artifact/v1"},
                {"dataset", dataset_json},
                {"id_space", "local"},
                {"structural", {{"partition_count", maps.size()},
                                {"partitions", records}}},
                {"capabilities", capabilities},
                {"source_artifacts", Json::array({source(dataset.reference)})},
                {"payloads", payloads}};
  metadata["aggregate_checksum"] = aggregate_for(metadata);
  write_text(directory / "artifact.json", metadata.dump());
  return {"partition/artifact.json", ArtifactType::kPartition,
          "partition-artifact/v1",
          metadata.at("aggregate_checksum").get<std::string>()};
}

inline ArtifactRef materialize_local(
    const WorkspaceRef& workspace, const NativeDatasetFixture& dataset,
    const ArtifactRef& partition, PartitionId partition_id,
    const std::vector<GlobalNodeId>& map,
    const std::vector<std::vector<std::uint32_t>>& adjacency) {
  const auto name = "local-" + std::to_string(partition_id);
  const auto directory = workspace.normal_root / name;
  const auto map_path = directory / "map.bin";
  const auto graph_path = directory / "graph.index";
  write_bytes(map_path, id_matrix(map));
  write_bytes(graph_path, native_graph(0, adjacency));
  std::uint32_t observed = 0;
  for (const auto& neighbors : adjacency) {
    observed = std::max(observed,
                        static_cast<std::uint32_t>(neighbors.size()));
  }
  const Json dataset_json{{"dataset_id", dataset.identity.dataset_id},
                          {"dataset_checksum", dataset.identity.dataset_checksum},
                          {"global_node_count", dataset.identity.global_node_count},
                          {"vector_dimension", dataset.identity.vector_dimension},
                          {"scalar_type", "float32"},
                          {"distance_metric", "l2"}};
  Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "local_graph"},
      {"format_version", "local-graph-artifact/v1"},
      {"dataset", dataset_json},
      {"id_space", "local"},
      {"structural",
       {{"partition_id", partition_id},
        {"local_node_count", map.size()},
        {"declared_max_degree", observed},
        {"observed_max_degree", observed},
        {"source_partition_checksum", partition.aggregate_checksum}}},
      {"capabilities", Json::array({"graph.local_adjacency/v1"})},
      {"source_artifacts", Json::array({source(partition)})},
      {"payloads",
       Json::array({payload("local_to_global_map", "map.bin",
                            kPartitionIdMapMediaType, map_path),
                    payload("local_graph", "graph.index",
                            kLocalGraphMediaType, graph_path)})}};
  metadata["aggregate_checksum"] = aggregate_for(metadata);
  write_text(directory / "artifact.json", metadata.dump());
  return {name + "/artifact.json", ArtifactType::kLocalGraph,
          "local-graph-artifact/v1",
          metadata.at("aggregate_checksum").get<std::string>()};
}

class TestMetrics final : public MetricsSink {
 public:
  void event(const std::string& name) override { events.push_back(name); }
  void increment(const std::string& name, std::uint64_t amount) override {
    counters[name] += amount;
  }
  bool saw(const std::string& name) const {
    return std::find(events.begin(), events.end(), name) != events.end();
  }
  std::vector<std::string> events;
  std::map<std::string, std::uint64_t> counters;
};

class TestCancellation final : public CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return cancelled; }
  bool cancelled{false};
};

class TestDistance final : public DistanceFunction {
 public:
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    double total = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      const auto difference = lhs[index] - rhs[index];
      total += difference * difference;
    }
    return total;
  }
};

struct TestContext {
  explicit TestContext(const WorkspaceRef& workspace,
                       const std::string& stage)
      : context{workspace,
                1024ULL * 1024ULL * 1024ULL,
                6,
                create_temporary_storage(workspace, stage),
                nullptr,
                &distance,
                &checkpoints,
                &metrics,
                &cancellation} {}
  TestMetrics metrics;
  TestCancellation cancellation;
  TestDistance distance;
  UnavailableCheckpointStore checkpoints;
  StageContext context;
};

inline std::filesystem::path write_parameters(
    const TemporaryStorageRef& temporary, const std::string& json) {
  const auto path = temporary.stage_root / "parameters.json";
  write_text(path, json);
  return path;
}

inline WorkspaceRef candidate_workspace(const StageContext& context) {
  auto workspace = context.workspace;
  workspace.normal_root = context.temporary_storage.stage_root;
  return workspace;
}

}  // namespace diskann_pipeline::cpp_main::test
