#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <immintrin.h>
#include "defaults.h"
#include "index.h"
#include "parameters.h"
#include "partition.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace diskann_pipeline::cpp_main {
namespace {

using Json = nlohmann::json;

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "B0 input document is missing");
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
      ArtifactType::kLocalGraph,
      "local-graph-artifact/v1",
      metadata.at("dataset").at("dataset_checksum").get<std::string>(),
      IdSpace::kLocal,
      metadata.at("capabilities").get<std::vector<std::string>>(),
      {},
      {{ArtifactType::kPartition,
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

void validate_parameters(const Json& parameters) {
  detail::require_object_shape(
      parameters,
      {"L", "R", "alpha", "build_pq_bytes", "max_occlusion_size",
       "num_threads", "saturation", "use_opq"},
      {"partition_id"});
  if (parameters.at("L").get<std::uint32_t>() != 32 ||
      parameters.at("R").get<std::uint32_t>() != 16 ||
      std::abs(parameters.at("alpha").get<double>() - 1.2) > 1e-6 ||
      parameters.at("build_pq_bytes").get<std::uint32_t>() != 0 ||
      parameters.at("max_occlusion_size").get<std::uint32_t>() != 750 ||
      parameters.at("num_threads").get<std::uint32_t>() != 6 ||
      parameters.at("saturation").get<bool>() ||
      parameters.at("use_opq").get<bool>()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "B0 parameters differ from pinned native rules");
  }
}

void verify_shard_rows(const CppMainVectorAccessor& accessor,
                       const std::vector<GlobalNodeId>& map,
                       const std::filesystem::path& shard) {
  validate_float32_matrix(shard, static_cast<NodeCount>(map.size()),
                          static_cast<std::uint32_t>(accessor.dimension()));
  std::ifstream dataset(accessor.native_payload_path(), std::ios::binary);
  std::ifstream selected(shard, std::ios::binary);
  dataset.seekg(8);
  selected.seekg(8);
  const auto row_bytes = accessor.dimension() * sizeof(float);
  std::vector<char> dataset_row(row_bytes);
  std::vector<char> selected_row(row_bytes);
  std::size_t selected_index = 0;
  for (GlobalNodeId global_id = 0;
       global_id < accessor.dataset_identity().global_node_count;
       ++global_id) {
    dataset.read(dataset_row.data(),
                 static_cast<std::streamsize>(dataset_row.size()));
    if (!dataset) {
      throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                          "B0 Dataset ended during shard row verification");
    }
    if (selected_index < map.size() && map[selected_index] == global_id) {
      selected.read(selected_row.data(),
                    static_cast<std::streamsize>(selected_row.size()));
      if (!selected ||
          std::memcmp(dataset_row.data(), selected_row.data(), row_bytes) != 0) {
        throw PipelineError(
            ErrorCategory::kSourceMismatch,
            "B0 shard row order differs from LocalToGlobalIdMap");
      }
      ++selected_index;
    }
  }
  if (selected_index != map.size() ||
      selected.peek() != std::char_traits<char>::eof()) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "B0 shard row order differs from LocalToGlobalIdMap");
  }
}

class CppMainB0 final : public LocalGraphBuilder {
 public:
  StageResult build(const ArtifactRef& partition_artifact,
                    PartitionId partition_id,
                    const LocalBuildConfig& config,
                    StageContext& context) override {
    try {
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      const auto* accessor =
          dynamic_cast<const CppMainVectorAccessor*>(context.vector_accessor);
      if (accessor == nullptr) {
        throw PipelineError(
            ErrorCategory::kCapabilityUnsatisfied,
            "B0 requires the private validated cpp_main Dataset binding");
      }
      const auto partition_validation = Validator(context.workspace).validate(
          partition_artifact, {accessor->validated_dataset()});
      if (!partition_validation.valid()) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "B0 PartitionArtifact failed fresh validation");
      }
      const auto parameters = read_json(config.stage_manifest_path);
      validate_parameters(parameters);
      if (parameters.contains("partition_id") &&
          parameters.at("partition_id").get<PartitionId>() != partition_id) {
        throw PipelineError(
            ErrorCategory::kSourceMismatch,
            "B0 manifest PartitionId differs from the explicit interface input");
      }

      const auto metadata_path = resolve_normal_artifact_metadata(
          context.workspace, partition_artifact.metadata_path);
      const auto metadata = read_json(metadata_path);
      const auto& records = metadata.at("structural").at("partitions");
      const auto record = std::find_if(
          records.begin(), records.end(), [&](const auto& value) {
            return value.at("partition_id").template get<PartitionId>() ==
                   partition_id;
          });
      if (record == records.end() ||
          partition_id >=
              metadata.at("structural").at("partition_count").get<NodeCount>()) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "B0 explicit PartitionId does not exist");
      }
      const auto map_role =
          record->at("local_to_global_map_role").get<std::string>();
      const auto payload_record = std::find_if(
          metadata.at("payloads").begin(), metadata.at("payloads").end(),
          [&](const auto& value) { return value.at("role") == map_role; });
      if (payload_record == metadata.at("payloads").end() ||
          payload_record->at("media_type") != kPartitionIdMapMediaType) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "B0 selected native ID map is absent");
      }
      const auto local_count =
          record->at("local_node_count").get<NodeCount>();
      const auto map_path = resolve_confined_path(
          metadata_path.parent_path(),
          payload_record->at("path").get<std::string>());
      const auto map = validate_partition_id_map(
          map_path, local_count,
          accessor->dataset_identity().global_node_count);
      if (!std::is_sorted(map.begin(), map.end())) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "B0 map must retain ascending Dataset order");
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      const auto preparation =
          context.temporary_storage.stage_root / "preparation";
      const auto native = context.temporary_storage.stage_root / "native";
      std::filesystem::create_directories(preparation);
      std::filesystem::create_directories(native);
      const auto shard = preparation / "shard.bin";
      context.metrics_sink->event("cpp_main.b0.retrieve");
      if (retrieve_shard_data_from_ids<float>(
              accessor->native_payload_path().string(), map_path.string(),
              shard.string()) != 0) {
        throw PipelineError(ErrorCategory::kInternal,
                            "retrieve_shard_data_from_ids failed");
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      verify_shard_rows(*accessor, map, shard);

      const auto write_parameters =
          diskann::IndexWriteParametersBuilder(32, 10)
              .with_filter_list_size(0)
              .with_saturate_graph(false)
              .with_num_threads(6)
              .build();
      if (write_parameters.alpha != diskann::defaults::ALPHA ||
          write_parameters.max_occlusion_size !=
              diskann::defaults::MAX_OCCLUSION_SIZE ||
          write_parameters.max_degree != 10 ||
          write_parameters.search_list_size != 32 ||
          write_parameters.filter_list_size != 32) {
        throw PipelineError(ErrorCategory::kInternal,
                            "B0 native parameter construction drifted");
      }
      diskann::Index<float> index(
          diskann::Metric::L2, accessor->dimension(), local_count,
          std::make_shared<diskann::IndexWriteParameters>(write_parameters),
          nullptr, diskann::defaults::NUM_FROZEN_POINTS_STATIC, false, false,
          false, false, 0, false);
      context.metrics_sink->event("cpp_main.b0.index_build");
      index.build(shard.string().c_str(), local_count);
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      const auto index_prefix = native / "local.index";
      context.metrics_sink->event("cpp_main.b0.index_save");
      index.save(index_prefix.string().c_str());
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      const auto graph_facts =
          validate_local_graph(index_prefix, local_count, 10);
      validate_local_graph_data(index_prefix.string() + ".data", local_count,
                                accessor->dataset_identity().vector_dimension);
      if (sha256_file(shard) !=
          sha256_file(index_prefix.string() + ".data")) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "B0 saved .data differs from prepared shard vectors");
      }

      const auto candidate =
          context.temporary_storage.stage_root / "candidate";
      std::filesystem::create_directories(candidate);
      const auto candidate_map = candidate / "map.bin";
      const auto candidate_graph = candidate / "graph.index";
      std::filesystem::copy_file(map_path, candidate_map);
      std::filesystem::copy_file(index_prefix, candidate_graph);
      Json output{
          {"schema_version", "1.0.0"},
          {"canonicalization", "diskann-artifact-c14n-v1"},
          {"artifact_type", "local_graph"},
          {"format_version", "local-graph-artifact/v1"},
          {"dataset", dataset_json(accessor->dataset_identity())},
          {"id_space", "local"},
          {"structural",
           {{"partition_id", partition_id},
            {"local_node_count", local_count},
            {"declared_max_degree", 10},
            {"observed_max_degree", graph_facts.observed_max_degree},
            {"source_partition_checksum",
             partition_artifact.aggregate_checksum}}},
          {"capabilities", Json::array({"graph.local_adjacency/v1"})},
          {"source_artifacts",
           Json::array({source_json(partition_artifact)})},
          {"payloads",
           Json::array({payload_json("local_to_global_map", "map.bin",
                                     kPartitionIdMapMediaType, candidate_map),
                        payload_json("local_graph", "graph.index",
                                     kLocalGraphMediaType,
                                     candidate_graph)})}};
      output["aggregate_checksum"] = aggregate_for(output);
      std::ofstream(candidate / "artifact.json", std::ios::binary)
          << output.dump();
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      return {StageStatus::kSucceeded,
              ArtifactRef{"candidate/artifact.json",
                          ArtifactType::kLocalGraph,
                          "local-graph-artifact/v1",
                          output.at("aggregate_checksum").get<std::string>()},
              {}};
    } catch (const std::exception&) {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
  }
};

}  // namespace

std::unique_ptr<LocalGraphBuilder> make_b0_local_build() {
  return std::make_unique<CppMainB0>();
}

}  // namespace diskann_pipeline::cpp_main
