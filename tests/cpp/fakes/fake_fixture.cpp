#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/checksum.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace diskann_pipeline::test {
namespace {

using Json = nlohmann::json;

constexpr const char* kDatasetPayloadChecksum =
    "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b";

Json dataset_identity() {
  return {{"dataset_id", "fake-six-node-v1"},
          {"dataset_checksum", kDatasetPayloadChecksum},
          {"global_node_count", 6},
          {"vector_dimension", 2},
          {"scalar_type", "float32"},
          {"distance_metric", "l2"}};
}

Json source(ArtifactType type, const std::string& format,
            const std::string& checksum) {
  return {{"artifact_type", to_string(type)},
          {"format_version", format},
          {"aggregate_checksum", checksum}};
}

Json payload(const std::string& role, const std::string& path,
             const std::string& media, std::uint64_t length,
             const std::string& checksum) {
  return {{"role", role}, {"path", path}, {"media_type", media},
          {"byte_length", length}, {"sha256", checksum}};
}

void write(const std::filesystem::path& path, const std::string& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to materialize Fake fixture");
  }
}

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "Fake source metadata is missing");
  }
  return Json::parse(input);
}

std::filesystem::path normal_metadata(const WorkspaceRef& workspace,
                                      const ArtifactRef& reference) {
  return resolve_normal_artifact_metadata(workspace, reference.metadata_path);
}

std::vector<GlobalNodeId> read_map(const WorkspaceRef& workspace,
                                   const ArtifactRef& local) {
  const auto metadata_path = normal_metadata(workspace, local);
  const auto metadata = read_json(metadata_path);
  const auto relative =
      metadata.at("payloads").at(0).at("path").get<std::string>();
  return read_json(metadata_path.parent_path() / relative)
      .at("local_to_global")
      .get<std::vector<GlobalNodeId>>();
}

std::vector<std::vector<LocalNodeId>> read_local_graph(
    const WorkspaceRef& workspace, const ArtifactRef& local) {
  const auto metadata_path = normal_metadata(workspace, local);
  const auto metadata = read_json(metadata_path);
  const auto relative =
      metadata.at("payloads").at(1).at("path").get<std::string>();
  return read_json(metadata_path.parent_path() / relative)
      .at("adjacency")
      .get<std::vector<std::vector<LocalNodeId>>>();
}

PartitionId local_partition_id(const WorkspaceRef& workspace,
                               const ArtifactRef& local) {
  return read_json(normal_metadata(workspace, local))
      .at("structural")
      .at("partition_id")
      .get<PartitionId>();
}

}  // namespace

ArtifactRef materialize_fake_dataset(const WorkspaceRef& workspace) {
  const ArtifactRef reference{"dataset/artifact.json", ArtifactType::kDataset,
                              "fake-dataset-json/v1",
                              kDatasetAggregateChecksum};
  const auto metadata_path =
      resolve_normal_artifact_metadata(workspace, reference.metadata_path);
  if (std::filesystem::exists(metadata_path)) {
    return reference;
  }
  const std::string vectors =
      "{\"vectors\":[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]}";
  write(metadata_path.parent_path() / "vectors.json", vectors);
  const Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "dataset"},
      {"format_version", "fake-dataset-json/v1"},
      {"dataset", dataset_identity()},
      {"id_space", "none"},
      {"structural", {{"global_node_count", 6}, {"vector_dimension", 2},
                      {"scalar_type", "float32"}, {"distance_metric", "l2"}}},
      {"capabilities", Json::array({"dataset.vector_access/v1"})},
      {"source_artifacts", Json::array()},
      {"payloads", Json::array({payload(
                       "vectors", "vectors.json",
                       "application/vnd.diskann.fake-vectors+json;version=1",
                       49, kDatasetPayloadChecksum)})},
      {"aggregate_checksum", kDatasetAggregateChecksum}};
  write(metadata_path, metadata.dump());
  return reference;
}

ArtifactRef materialize_partition_candidate(
    const std::filesystem::path& root, const ArtifactRef& dataset) {
  const ArtifactRef reference{"candidate/artifact.json",
                              ArtifactType::kPartition,
                              "partition-artifact/v1",
                              kPartitionAggregateChecksum};
  const auto directory = root / "candidate";
  const std::vector<std::pair<std::string, std::string>> files{
      {"p0-vectors.json", "{\"vectors\":[[0,0],[1,0],[2,0],[0,1]]}"},
      {"p0-map.json", "{\"local_to_global\":[0,1,2,3]}"},
      {"p1-vectors.json", "{\"vectors\":[[2,0],[0,1],[1,1],[2,1]]}"},
      {"p1-map.json", "{\"local_to_global\":[2,3,4,5]}"}};
  for (const auto& file : files) {
    write(directory / file.first, file.second);
  }
  const Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "partition"},
      {"format_version", "partition-artifact/v1"},
      {"dataset", dataset_identity()},
      {"id_space", "local"},
      {"structural",
       {{"partition_count", 2},
        {"partitions",
         Json::array({{{"partition_id", 0}, {"local_node_count", 4},
                       {"shard_vectors_role", "partition/0/vectors"},
                       {"local_to_global_map_role",
                        "partition/0/local_to_global_map"}},
                      {{"partition_id", 1}, {"local_node_count", 4},
                       {"shard_vectors_role", "partition/1/vectors"},
                       {"local_to_global_map_role",
                        "partition/1/local_to_global_map"}}})}}},
      {"capabilities", Json::array({"partition.complete_local_to_global_map/v1",
                                    "partition.overlap/v1"})},
      {"source_artifacts", Json::array({source(dataset.artifact_type,
                                               dataset.format_version,
                                               dataset.aggregate_checksum)})},
      {"payloads",
       Json::array({
           payload("partition/0/vectors", "p0-vectors.json", "application/json",
                   37, "a61d72902e4771175bf0767a13cc9006dd129ea92439fc092d167b632d35437c"),
           payload("partition/0/local_to_global_map", "p0-map.json",
                   "application/json", 29,
                   "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c"),
           payload("partition/1/vectors", "p1-vectors.json", "application/json",
                   37, "119a82674838210f5d056a5f1b784d91e11e84bf2b4113afa236a70736abd242"),
           payload("partition/1/local_to_global_map", "p1-map.json",
                   "application/json", 29,
                   "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c")})},
      {"aggregate_checksum", kPartitionAggregateChecksum}};
  write(directory / "artifact.json", metadata.dump());
  return reference;
}

ArtifactRef materialize_local_candidate(
    const std::filesystem::path& root, const WorkspaceRef&,
    const ArtifactRef& partition, PartitionId partition_id, bool corrupt) {
  if (partition_id > 1) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "Fake Local Build PartitionId is out of range");
  }
  const auto map = partition_id == 0
                       ? "{\"local_to_global\":[0,1,2,3]}"
                       : "{\"local_to_global\":[2,3,4,5]}";
  const auto map_checksum = partition_id == 0
                                ? "76fb4d67995b155a9755dd54977692290b7cdf416294229b4f320b81efd5ce7c"
                                : "90923604fcdf8dcfa013e82292ecdba7d015e4b77638cbb387c9a583450f493c";
  const auto aggregate = partition_id == 0 ? kLocal0AggregateChecksum
                                           : kLocal1AggregateChecksum;
  const ArtifactRef reference{"candidate/artifact.json",
                              ArtifactType::kLocalGraph,
                              "local-graph-artifact/v1", aggregate};
  const auto directory = root / "candidate";
  write(directory / "map.json", map);
  write(directory / "graph.json",
        "{\"adjacency\":[[1,2],[0,2],[1,3],[]]}");
  const Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "local_graph"},
      {"format_version", "local-graph-artifact/v1"},
      {"dataset", dataset_identity()},
      {"id_space", "local"},
      {"structural", {{"partition_id", partition_id},
                      {"local_node_count", 4},
                      {"declared_max_degree", 2},
                      {"observed_max_degree", 2},
                      {"source_partition_checksum",
                       partition.aggregate_checksum}}},
      {"capabilities", Json::array({"graph.local_adjacency/v1"})},
      {"source_artifacts", Json::array({source(partition.artifact_type,
                                               partition.format_version,
                                               partition.aggregate_checksum)})},
      {"payloads", Json::array({payload("local_to_global_map", "map.json",
                                        "application/json", 29, map_checksum),
                                payload("local_graph", "graph.json",
                                        "application/json", 36,
                                        "e54ec8ef5014c15c861cef23ea8aa2edddd1a587b1a30614abd9b0a4defbeddd")})},
      {"aggregate_checksum", aggregate}};
  write(directory / "artifact.json", metadata.dump());
  if (corrupt) {
    write(directory / "graph.json", std::string(36, 'x'));
  }
  return reference;
}

std::vector<std::vector<GlobalNodeId>> merge_local_graphs(
    const std::vector<std::vector<GlobalNodeId>>& maps,
    const std::vector<std::vector<std::vector<LocalNodeId>>>& local_graphs,
    std::uint32_t max_degree) {
  if (maps.size() != local_graphs.size()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        "Fake Merge map/graph count mismatch");
  }
  std::vector<std::vector<GlobalNodeId>> candidates(6);
  for (std::size_t partition = 0; partition < maps.size(); ++partition) {
    if (maps[partition].size() != local_graphs[partition].size()) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "Fake Merge owning map is incomplete");
    }
    for (std::size_t local = 0; local < local_graphs[partition].size(); ++local) {
      const auto global = maps[partition][local];
      for (const auto neighbor : local_graphs[partition][local]) {
        if (neighbor >= maps[partition].size()) {
          throw PipelineError(ErrorCategory::kIdRangeInvalid,
                              "Fake Merge local neighbor is out of range");
        }
        candidates[global].push_back(maps[partition][neighbor]);
      }
    }
  }
  for (auto& neighbors : candidates) {
    std::set<GlobalNodeId> seen;
    std::vector<GlobalNodeId> deduplicated;
    for (const auto neighbor : neighbors) {
      if (seen.insert(neighbor).second) {
        deduplicated.push_back(neighbor);
      }
    }
    if (deduplicated.size() > max_degree) {
      deduplicated.resize(max_degree);
    }
    neighbors = std::move(deduplicated);
  }
  return candidates;
}

ArtifactRef materialize_global_candidate(
    const std::filesystem::path& root, const WorkspaceRef& workspace,
    const ArtifactRef& partition, const std::vector<ArtifactRef>& locals,
    bool corrupt) {
  auto ordered = locals;
  std::sort(ordered.begin(), ordered.end(), [&](const auto& lhs, const auto& rhs) {
    return local_partition_id(workspace, lhs) < local_partition_id(workspace, rhs);
  });
  std::vector<std::vector<GlobalNodeId>> maps;
  std::vector<std::vector<std::vector<LocalNodeId>>> graphs;
  for (const auto& local : ordered) {
    maps.push_back(read_map(workspace, local));
    graphs.push_back(read_local_graph(workspace, local));
  }
  const auto adjacency = merge_local_graphs(maps, graphs, 3);
  const std::string graph = Json{{"adjacency", adjacency}}.dump();
  const ArtifactRef reference{"candidate/artifact.json",
                              ArtifactType::kGlobalGraph,
                              "global-graph-artifact/v1",
                              kGlobalAggregateChecksum};
  const auto directory = root / "candidate";
  write(directory / "graph.json", graph);
  Json local_sources = Json::array();
  for (const auto& local : ordered) {
    local_sources.push_back(source(local.artifact_type, local.format_version,
                                   local.aggregate_checksum));
  }
  const auto partition_source = source(partition.artifact_type,
                                       partition.format_version,
                                       partition.aggregate_checksum);
  Json all_sources = Json::array({partition_source});
  for (const auto& local_source : local_sources) {
    all_sources.push_back(local_source);
  }
  const Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "global_graph"},
      {"format_version", "global-graph-artifact/v1"},
      {"dataset", dataset_identity()},
      {"id_space", "global"},
      {"structural", {{"global_node_count", 6},
                      {"declared_max_degree", 3},
                      {"observed_max_degree", 3},
                      {"source_partition_artifact", partition_source},
                      {"source_local_graph_artifacts", local_sources}}},
      {"capabilities", Json::array({"graph.global_adjacency/v1"})},
      {"source_artifacts", all_sources},
      {"payloads", Json::array({payload(
                       "global_graph", "graph.json", "application/json", 50,
                       "9cbfeade7ef24a5b7b1911da4ff7baead15b7444e338e3b0a5ee42ed780d622f")})},
      {"aggregate_checksum", kGlobalAggregateChecksum}};
  write(directory / "artifact.json", metadata.dump());
  if (corrupt) {
    write(directory / "graph.json", std::string(50, 'x'));
  }
  return reference;
}

}  // namespace diskann_pipeline::test
