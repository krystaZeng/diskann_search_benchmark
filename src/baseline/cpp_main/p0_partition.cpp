#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <immintrin.h>
#include "partition.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

namespace diskann_pipeline::cpp_main {
namespace {

using Json = nlohmann::json;

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "P0 parameter document is missing");
  }
  return detail::parse_json_strict(
      std::string(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>()));
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

Json payload_json(const std::string& role, const std::string& relative,
                  const std::string& media,
                  const std::filesystem::path& path) {
  return {{"role", role},
          {"path", relative},
          {"media_type", media},
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
        metadata.at("source_artifacts").at(0).at("format_version").get<std::string>(),
        metadata.at("source_artifacts").at(0).at("aggregate_checksum").get<std::string>()}},
      metadata.at("structural").dump()};
  for (const auto& payload : metadata.at("payloads")) {
    projection.payloads.push_back(
        {payload.at("role").get<std::string>(),
         payload.at("byte_length").get<std::uint64_t>(),
         payload.at("sha256").get<std::string>()});
  }
  return aggregate_checksum(projection);
}

bool cancelled(const StageContext& context) {
  return context.cancellation_token != nullptr &&
         context.cancellation_token->is_cancelled();
}

class CppMainP0 final : public PartitionStrategy {
 public:
  StageResult run(const ArtifactRef& dataset,
                  const std::filesystem::path& stage_manifest_path,
                  StageContext& context) override {
    try {
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      CppMainVectorAccessor binding(context.workspace, dataset);
      const auto parameters = read_json(stage_manifest_path);
      validate_p0_parameters(parameters.dump(), binding.dataset_identity().global_node_count);
      const auto r = parameters.at("R").get<std::uint32_t>();
      const auto budget = parameters.at("build_DRAM_budget_gib").get<double>();
      const auto replication = parameters.at("replication_factor").get<std::uint32_t>();
      const auto probability = parameters.at("sample_probability").get<double>();
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      const auto native_directory =
          context.temporary_storage.stage_root / "native";
      std::filesystem::create_directories(native_directory);
      const auto output_prefix = native_directory / "p0";
      context.metrics_sink->event(
          "cpp_main.p0.partition_with_ram_budget");
      const auto partition_count = partition_with_ram_budget<float>(
          binding.native_payload_path().string(), probability, budget,
          2U * r / 3U, output_prefix.string(), replication);
      if (partition_count < 2) {
        throw PipelineError(ErrorCategory::kNodeCountMismatch,
                            "P0 did not produce a sharded Partition set");
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      const auto& identity = binding.dataset_identity();
      const auto centroids = output_prefix.string() + "_centroids.bin";
      validate_centroids(centroids, static_cast<NodeCount>(partition_count),
                         identity.vector_dimension);
      std::vector<std::vector<GlobalNodeId>> maps;
      maps.reserve(static_cast<std::size_t>(partition_count));
      std::vector<std::uint32_t> ownership(
          static_cast<std::size_t>(identity.global_node_count), 0);
      for (int partition = 0; partition < partition_count; ++partition) {
        const auto map_path =
            output_prefix.string() + "_subshard-" +
            std::to_string(partition) + "_ids_uint32.bin";
        std::ifstream header(map_path, std::ios::binary);
        std::uint32_t rows = 0;
        std::uint32_t dimension = 0;
        header.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        header.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
        if (!header || dimension != 1 || rows == 0) {
          throw PipelineError(ErrorCategory::kMappingIncomplete,
                              "P0 emitted an invalid or empty ID map header");
        }
        auto ids = validate_partition_id_map(
            map_path, rows, identity.global_node_count);
        if (!std::is_sorted(ids.begin(), ids.end())) {
          throw PipelineError(ErrorCategory::kMappingIncomplete,
                              "P0 ID map is not in ascending Dataset order");
        }
        for (const auto id : ids) ++ownership[id];
        maps.push_back(std::move(ids));
      }
      if (std::any_of(ownership.begin(), ownership.end(),
                      [replication](std::uint32_t count) {
                        return count != replication;
                      })) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "P0 ownership does not match replication factor 2");
      }

      const auto candidate =
          context.temporary_storage.stage_root / "candidate";
      std::filesystem::create_directories(candidate);
      Json records = Json::array();
      Json payloads = Json::array();
      for (int partition = 0; partition < partition_count; ++partition) {
        const auto source =
            output_prefix.string() + "_subshard-" +
            std::to_string(partition) + "_ids_uint32.bin";
        const auto filename =
            "partition-" + std::to_string(partition) + "-map.bin";
        const auto destination = candidate / filename;
        std::filesystem::copy_file(source, destination);
        const auto role = "partition/" + std::to_string(partition) +
                          "/local_to_global_map";
        records.push_back(
            {{"partition_id", partition},
             {"local_node_count", maps[partition].size()},
             {"local_to_global_map_role", role}});
        payloads.push_back(payload_json(role, filename,
                                        kPartitionIdMapMediaType,
                                        destination));
      }
      const auto centroids_destination = candidate / "centroids.bin";
      std::filesystem::copy_file(centroids, centroids_destination);
      payloads.push_back(payload_json(kPartitionCentroidsPayloadRole,
                                      "centroids.bin", kCentroidsMediaType,
                                      centroids_destination));
      Json metadata{
          {"schema_version", "1.0.0"},
          {"canonicalization", "diskann-artifact-c14n-v1"},
          {"artifact_type", "partition"},
          {"format_version", "partition-artifact/v1"},
          {"dataset", dataset_json(identity)},
          {"id_space", "local"},
          {"structural",
           {{"partition_count", partition_count}, {"partitions", records}}},
          {"capabilities",
           Json::array({"partition.centroids/v1",
                        "partition.complete_local_to_global_map/v1",
                        "partition.local_to_global_ascending/v1",
                        "partition.overlap/v1"})},
          {"source_artifacts", Json::array({source_json(dataset)})},
          {"payloads", payloads}};
      metadata["aggregate_checksum"] = aggregate_for(metadata);
      std::ofstream(candidate / "artifact.json", std::ios::binary)
          << metadata.dump();
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      return {StageStatus::kSucceeded,
              ArtifactRef{"candidate/artifact.json",
                          ArtifactType::kPartition,
                          "partition-artifact/v1",
                          metadata.at("aggregate_checksum").get<std::string>()},
              {}};
    } catch (const std::exception&) {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
  }
};

}  // namespace

void validate_p0_parameters(const std::string& json, NodeCount global_node_count) {
  const auto parameters = detail::parse_json_strict(json);
  detail::require_object_shape(
      parameters,
      {"R", "build_DRAM_budget_gib", "replication_factor",
       "sample_probability"});
  const auto r = parameters.at("R").get<std::uint32_t>();
  const auto budget =
      parameters.at("build_DRAM_budget_gib").get<double>();
  const auto replication =
      parameters.at("replication_factor").get<std::uint32_t>();
  const auto probability =
      parameters.at("sample_probability").get<double>();
  const auto expected_probability = std::min(
      1.0, 256000.0 /
               static_cast<double>(
                   global_node_count));
  if (r != 16 || std::abs(budget - 0.6) > 1e-12 || replication != 2 ||
      probability <= 0.0 || probability > 1.0 ||
      std::abs(probability - expected_probability) > 1e-12) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "P0 parameters differ from the approved Profile derivation");
  }
}

std::unique_ptr<PartitionStrategy> make_p0_partition() {
  return std::make_unique<CppMainP0>();
}

}  // namespace diskann_pipeline::cpp_main
