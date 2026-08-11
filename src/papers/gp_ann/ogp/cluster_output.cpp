#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace diskann_pipeline::gp_ann_ogp {
namespace {

using Json = nlohmann::json;

GlobalNodeId parse_global_id(const std::string& token) {
  if (token.empty() ||
      !std::all_of(token.begin(), token.end(), [](unsigned char value) {
        return value >= static_cast<unsigned char>('0') &&
               value <= static_cast<unsigned char>('9');
      })) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "GP-ANN cluster membership is not decimal uint32");
  }
  std::uint64_t parsed = 0;
  const auto result =
      std::from_chars(token.data(), token.data() + token.size(), parsed);
  if (result.ec != std::errc{} ||
      result.ptr != token.data() + token.size() ||
      parsed > std::numeric_limits<GlobalNodeId>::max()) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "GP-ANN cluster membership exceeds uint32 range");
  }
  return static_cast<GlobalNodeId>(parsed);
}

Json dataset_json(const DatasetIdentity& dataset) {
  return {{"dataset_id", dataset.dataset_id},
          {"dataset_checksum", dataset.dataset_checksum},
          {"global_node_count", dataset.global_node_count},
          {"vector_dimension", dataset.vector_dimension},
          {"scalar_type", dataset.scalar_type},
          {"distance_metric", dataset.distance_metric}};
}

Json source_json(const ArtifactRef& source) {
  return {{"artifact_type", to_string(source.artifact_type)},
          {"format_version", source.format_version},
          {"aggregate_checksum", source.aggregate_checksum}};
}

void write_u32(std::ofstream& output, std::uint32_t value) {
  const std::uint8_t bytes[4]{
      static_cast<std::uint8_t>(value & 0xffU),
      static_cast<std::uint8_t>((value >> 8U) & 0xffU),
      static_cast<std::uint8_t>((value >> 16U) & 0xffU),
      static_cast<std::uint8_t>((value >> 24U) & 0xffU)};
  output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void write_native_map(const std::filesystem::path& path,
                      const std::vector<GlobalNodeId>& ids) {
  if (ids.empty() || ids.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        "OGP map row count is invalid");
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to create OGP native ID map");
  }
  write_u32(output, static_cast<std::uint32_t>(ids.size()));
  write_u32(output, 1U);
  for (const auto id : ids) write_u32(output, id);
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to write OGP native ID map");
  }
}

Json payload_json(const std::string& role, const std::string& relative,
                  const std::filesystem::path& path) {
  return {{"role", role},
          {"path", relative},
          {"media_type", cpp_main::kPartitionIdMapMediaType},
          {"byte_length", std::filesystem::file_size(path)},
          {"sha256", sha256_file(path)}};
}

std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection{
      ArtifactType::kPartition,
      "partition-artifact/v1",
      metadata.at("dataset").at("dataset_checksum").get<std::string>(),
      IdSpace::kLocal,
      metadata.at("capabilities").get<std::vector<std::string>>(),
      {},
      {{ArtifactType::kDataset,
        metadata.at("source_artifacts").at(0).at("format_version")
            .get<std::string>(),
        metadata.at("source_artifacts").at(0).at("aggregate_checksum")
            .get<std::string>()}},
      metadata.at("structural").dump()};
  for (const auto& payload : metadata.at("payloads")) {
    projection.payloads.push_back(
        {payload.at("role").get<std::string>(),
         payload.at("byte_length").get<std::uint64_t>(),
         payload.at("sha256").get<std::string>()});
  }
  return aggregate_checksum(projection);
}

}  // namespace

std::vector<std::vector<GlobalNodeId>> parse_and_canonicalize_clusters(
    const std::filesystem::path& cluster_output,
    NodeCount global_node_count) {
  if (!is_valid_count(global_node_count)) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "OGP Dataset node count is invalid");
  }
  std::ifstream input(cluster_output, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "GP-ANN cluster output is missing");
  }

  std::vector<std::vector<GlobalNodeId>> clusters;
  std::vector<bool> covered(static_cast<std::size_t>(global_node_count), false);
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream tokens(line);
    std::vector<GlobalNodeId> cluster;
    std::set<GlobalNodeId> unique;
    std::string token;
    while (tokens >> token) {
      const auto id = parse_global_id(token);
      if (!is_valid_global_node_id(id, global_node_count)) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "GP-ANN cluster contains an out-of-range GlobalNodeId");
      }
      if (!unique.insert(id).second) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "GP-ANN cluster contains a same-shard duplicate");
      }
      cluster.push_back(id);
      covered[id] = true;
    }
    if (cluster.empty()) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "GP-ANN emitted an empty shard");
    }
    std::sort(cluster.begin(), cluster.end());
    clusters.push_back(std::move(cluster));
  }
  if (!input.eof()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unable to read GP-ANN cluster output");
  }
  if (clusters.empty() ||
      clusters.size() > std::numeric_limits<PartitionId>::max()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        "GP-ANN emitted no valid Partition records");
  }
  if (std::find(covered.begin(), covered.end(), false) != covered.end()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        "GP-ANN output does not cover every GlobalNodeId");
  }
  return clusters;
}

ArtifactRef materialize_partition_candidate(
    const std::filesystem::path& stage_root,
    const ArtifactRef& dataset,
    const DatasetIdentity& dataset_identity,
    const std::vector<std::vector<GlobalNodeId>>& clusters) {
  if (dataset.artifact_type != ArtifactType::kDataset || clusters.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP candidate inputs are incomplete");
  }
  const auto candidate = stage_root / "candidate";
  if (std::filesystem::exists(candidate)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "OGP candidate directory already exists");
  }
  std::filesystem::create_directories(candidate);

  Json records = Json::array();
  Json payloads = Json::array();
  for (std::size_t index = 0; index < clusters.size(); ++index) {
    if (clusters[index].empty() ||
        !std::is_sorted(clusters[index].begin(), clusters[index].end())) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "OGP candidate requires non-empty ascending maps");
    }
    const auto filename =
        "partition-" + std::to_string(index) + "-map.bin";
    const auto path = candidate / filename;
    write_native_map(path, clusters[index]);
    const auto role = "partition/" + std::to_string(index) +
                      "/local_to_global_map";
    records.push_back(
        {{"partition_id", index},
         {"local_node_count", clusters[index].size()},
         {"local_to_global_map_role", role}});
    payloads.push_back(payload_json(role, filename, path));
  }

  Json metadata{
      {"schema_version", "1.0.0"},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "partition"},
      {"format_version", "partition-artifact/v1"},
      {"dataset", dataset_json(dataset_identity)},
      {"id_space", "local"},
      {"structural",
       {{"partition_count", clusters.size()}, {"partitions", records}}},
      {"capabilities",
       Json::array({"partition.complete_local_to_global_map/v1",
                    "partition.local_to_global_ascending/v1",
                    "partition.overlap/v1"})},
      {"source_artifacts", Json::array({source_json(dataset)})},
      {"payloads", payloads}};
  metadata["aggregate_checksum"] = aggregate_for(metadata);
  std::ofstream output(candidate / "artifact.json",
                       std::ios::binary | std::ios::trunc);
  output << metadata.dump();
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to write OGP PartitionArtifact metadata");
  }
  return {"candidate/artifact.json", ArtifactType::kPartition,
          "partition-artifact/v1",
          metadata.at("aggregate_checksum").get<std::string>()};
}

}  // namespace diskann_pipeline::gp_ann_ogp
