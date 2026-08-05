#pragma once

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace diskann_pipeline::test {

using Json = nlohmann::json;

inline constexpr const char* kDatasetPayloadChecksum =
    "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b";
inline constexpr const char* kDatasetAggregateChecksum =
    "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d";
inline constexpr const char* kPartitionAggregateChecksum =
    "a640037c2d07b2dcdff98cd22b82b5c4f5da7e6323fc8da2a61538c1cbbb2984";
inline constexpr const char* kLocal0AggregateChecksum =
    "31d93424d2d0a826891c5d2cc6d3b5b7da6f646a9c6f878603d532ddfae0c9c1";
inline constexpr const char* kLocal1AggregateChecksum =
    "ca02d15d4709d62aea8f404de57d2143be1205f6fa517b4d2d550fe2dab265f6";
inline constexpr const char* kGlobalAggregateChecksum =
    "443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe";

inline Json source(ArtifactType type, const char* format,
                   const char* checksum) {
  return {{"artifact_type", to_string(type)},
          {"format_version", format},
          {"aggregate_checksum", checksum}};
}

inline Json dataset_identity() {
  return {{"dataset_id", "fake-six-node-v1"},
          {"dataset_checksum", kDatasetPayloadChecksum},
          {"global_node_count", 6},
          {"vector_dimension", 2},
          {"scalar_type", "float32"},
          {"distance_metric", "l2"}};
}

inline Json payload(const char* role, const char* path, const char* media_type,
                    std::uint64_t length, const char* checksum) {
  return {{"role", role},
          {"path", path},
          {"media_type", media_type},
          {"byte_length", length},
          {"sha256", checksum}};
}

inline void overwrite_file(const std::filesystem::path& path,
                           const std::string& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("unable to write contract test fixture");
  }
}

inline Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return Json::parse(input);
}

inline std::filesystem::path metadata_path(const WorkspaceRef& workspace,
                                           const ArtifactRef& reference) {
  return resolve_normal_artifact_metadata(workspace, reference.metadata_path);
}

inline std::string recompute_aggregate(const Json& metadata) {
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

inline void mutate_metadata(const WorkspaceRef& workspace, ArtifactRef& reference,
                            const std::function<void(Json&)>& mutation,
                            bool recompute = true) {
  const auto path = metadata_path(workspace, reference);
  auto metadata = read_json(path);
  mutation(metadata);
  if (recompute) {
    metadata["aggregate_checksum"] = recompute_aggregate(metadata);
    reference.aggregate_checksum =
        metadata.at("aggregate_checksum").get<std::string>();
  }
  overwrite_file(path, metadata.dump());
}

inline void replace_payload(const WorkspaceRef& workspace, ArtifactRef& reference,
                            const std::string& role,
                            const std::string& content) {
  const auto path = metadata_path(workspace, reference);
  auto metadata = read_json(path);
  for (auto& item : metadata.at("payloads")) {
    if (item.at("role") == role) {
      overwrite_file(path.parent_path() /
                         item.at("path").get<std::string>(),
                     content);
      item["byte_length"] = content.size();
      item["sha256"] = sha256_bytes(content);
      metadata["aggregate_checksum"] = recompute_aggregate(metadata);
      reference.aggregate_checksum =
          metadata.at("aggregate_checksum").get<std::string>();
      overwrite_file(path, metadata.dump());
      return;
    }
  }
  throw std::runtime_error("fixture payload role not found");
}

class ContractArtifactFixture {
 public:
  ContractArtifactFixture() {
    static std::atomic<unsigned long long> sequence{0};
    const auto token = std::to_string(
                           std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()) +
                       "-" + std::to_string(sequence.fetch_add(1));
    root = std::filesystem::temp_directory_path() /
           ("diskann-pipeline-contract-" + token);
    workspace = create_workspace(root);
    materialize();
  }

  ContractArtifactFixture(const ContractArtifactFixture&) = delete;
  ContractArtifactFixture& operator=(const ContractArtifactFixture&) = delete;

  ~ContractArtifactFixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  std::filesystem::path root;
  WorkspaceRef workspace;
  ArtifactRef dataset{"dataset/artifact.json", ArtifactType::kDataset,
                      "fake-dataset-json/v1", kDatasetAggregateChecksum};
  ArtifactRef partition{"partition/artifact.json", ArtifactType::kPartition,
                        "partition-artifact/v1",
                        kPartitionAggregateChecksum};
  ArtifactRef local0{"local0/artifact.json", ArtifactType::kLocalGraph,
                     "local-graph-artifact/v1", kLocal0AggregateChecksum};
  ArtifactRef local1{"local1/artifact.json", ArtifactType::kLocalGraph,
                     "local-graph-artifact/v1", kLocal1AggregateChecksum};
  ArtifactRef global{"global/artifact.json", ArtifactType::kGlobalGraph,
                     "global-graph-artifact/v1", kGlobalAggregateChecksum};

 private:
  void write_artifact(const ArtifactRef& reference, const Json& metadata,
                      const std::vector<std::pair<std::string, std::string>>& files) {
    const auto path = metadata_path(workspace, reference);
    for (const auto& file : files) {
      overwrite_file(path.parent_path() / file.first, file.second);
    }
    overwrite_file(path, metadata.dump());
  }

  void materialize() {
    const auto dataset_source =
        source(ArtifactType::kDataset, "fake-dataset-json/v1",
               kDatasetAggregateChecksum);
    const auto partition_source =
        source(ArtifactType::kPartition, "partition-artifact/v1",
               kPartitionAggregateChecksum);
    const auto local0_source =
        source(ArtifactType::kLocalGraph, "local-graph-artifact/v1",
               kLocal0AggregateChecksum);
    const auto local1_source =
        source(ArtifactType::kLocalGraph, "local-graph-artifact/v1",
               kLocal1AggregateChecksum);

    write_artifact(
        dataset,
        {{"schema_version", "1.0.0"},
         {"canonicalization", "diskann-artifact-c14n-v1"},
         {"artifact_type", "dataset"},
         {"format_version", "fake-dataset-json/v1"},
         {"dataset", dataset_identity()},
         {"id_space", "none"},
         {"structural",
          {{"global_node_count", 6},
           {"vector_dimension", 2},
           {"scalar_type", "float32"},
           {"distance_metric", "l2"}}},
         {"capabilities", Json::array({"dataset.vector_access/v1"})},
         {"source_artifacts", Json::array()},
         {"payloads",
          Json::array({payload(
              "vectors", "vectors.json",
              "application/vnd.diskann.fake-vectors+json;version=1", 49,
              kDatasetPayloadChecksum)})},
         {"aggregate_checksum", kDatasetAggregateChecksum}},
        {{"vectors.json",
          "{\"vectors\":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]}"}});

    write_artifact(
        partition,
        {{"schema_version", "1.0.0"},
         {"canonicalization", "diskann-artifact-c14n-v1"},
         {"artifact_type", "partition"},
         {"format_version", "partition-artifact/v1"},
         {"dataset", dataset_identity()},
         {"id_space", "local"},
         {"structural",
          {{"partition_count", 2},
           {"partitions",
            Json::array(
                {{{"partition_id", 0},
                  {"local_node_count", 4},
                  {"shard_vectors_role", "partition/0/vectors"},
                  {"local_to_global_map_role",
                   "partition/0/local_to_global_map"}},
                 {{"partition_id", 1},
                  {"local_node_count", 4},
                  {"shard_vectors_role", "partition/1/vectors"},
                  {"local_to_global_map_role",
                   "partition/1/local_to_global_map"}}})}}},
         {"capabilities",
          Json::array({"partition.complete_local_to_global_map/v1",
                       "partition.overlap/v1"})},
         {"source_artifacts", Json::array({dataset_source})},
         {"payloads",
          Json::array(
              {payload("partition/0/vectors", "p0-vectors.json",
                       "application/json", 37,
                       "a61d72902e4771175bf0767a13cc9006dd129ea92439fc092d167b632d35437c"),
               payload("partition/0/local_to_global_map", "p0-map.json",
                       "application/json", 29,
                       "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c"),
               payload("partition/1/vectors", "p1-vectors.json",
                       "application/json", 37,
                       "119a82674838210f5d056a5f1b784d91e11e84bf2b4113afa236a70736abd242"),
               payload("partition/1/local_to_global_map", "p1-map.json",
                       "application/json", 29,
                       "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c")})},
         {"aggregate_checksum", kPartitionAggregateChecksum}},
        {{"p0-vectors.json", "{\"vectors\":[[0,0],[1,0],[2,0],[0,1]]}"},
         {"p0-map.json", "{\"local_to_global\":[0,1,2,3]}"},
         {"p1-vectors.json", "{\"vectors\":[[2,0],[0,1],[1,1],[2,1]]}"},
         {"p1-map.json", "{\"local_to_global\":[2,3,4,5]}"}});

    const auto local_graph =
        "{\"adjacency\":[[1,2],[0,2],[1,3],[]]}";
    const auto local_checksum =
        "e54ec8ef5014c15c861cef23ea8aa2edddd1a587b1a30614abd9b0a4defbeddd";
    const auto make_local = [&](ArtifactRef& reference, int partition_id,
                                const char* map_checksum,
                                const char* aggregate, const char* map_bytes) {
      write_artifact(
          reference,
          {{"schema_version", "1.0.0"},
           {"canonicalization", "diskann-artifact-c14n-v1"},
           {"artifact_type", "local_graph"},
           {"format_version", "local-graph-artifact/v1"},
           {"dataset", dataset_identity()},
           {"id_space", "local"},
           {"structural",
            {{"partition_id", partition_id},
             {"local_node_count", 4},
             {"declared_max_degree", 2},
             {"observed_max_degree", 2},
             {"source_partition_checksum", kPartitionAggregateChecksum}}},
           {"capabilities", Json::array({"graph.local_adjacency/v1"})},
           {"source_artifacts", Json::array({partition_source})},
           {"payloads",
            Json::array({payload("local_to_global_map", "map.json",
                                 "application/json", 29, map_checksum),
                         payload("local_graph", "graph.json", "application/json",
                                 36, local_checksum)})},
           {"aggregate_checksum", aggregate}},
          {{"map.json", map_bytes}, {"graph.json", local_graph}});
    };
    make_local(local0, 0,
               "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c",
               kLocal0AggregateChecksum,
               "{\"local_to_global\":[0,1,2,3]}");
    make_local(local1, 1,
               "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c",
               kLocal1AggregateChecksum,
               "{\"local_to_global\":[2,3,4,5]}");

    write_artifact(
        global,
        {{"schema_version", "1.0.0"},
         {"canonicalization", "diskann-artifact-c14n-v1"},
         {"artifact_type", "global_graph"},
         {"format_version", "global-graph-artifact/v1"},
         {"dataset", dataset_identity()},
         {"id_space", "global"},
         {"structural",
          {{"global_node_count", 6},
           {"declared_max_degree", 3},
           {"observed_max_degree", 3},
           {"source_partition_artifact", partition_source},
           {"source_local_graph_artifacts",
            Json::array({local0_source, local1_source})}}},
         {"capabilities", Json::array({"graph.global_adjacency/v1"})},
         {"source_artifacts",
          Json::array({partition_source, local0_source, local1_source})},
         {"payloads",
          Json::array({payload(
              "global_graph", "graph.json", "application/json", 50,
              "9cbfeade7ef24a5b7b1911da4ff7baead15b7444e338e3b0a5ee42ed780d622f")})},
         {"aggregate_checksum", kGlobalAggregateChecksum}},
        {{"graph.json",
          "{\"adjacency\":[[1,2],[0,2],[1,3,4],[2,4],[3,5],[]]}"}});
  }
};

}  // namespace diskann_pipeline::test
