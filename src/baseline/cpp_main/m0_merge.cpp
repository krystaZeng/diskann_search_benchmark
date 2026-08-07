#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <immintrin.h>
#include "disk_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace diskann_pipeline::cpp_main {
namespace {

using Json = nlohmann::json;

struct PartitionInput {
  Json metadata;
  DatasetIdentity dataset;
  NodeCount count;
  std::vector<std::vector<GlobalNodeId>> maps;
  std::vector<std::filesystem::path> map_paths;
  std::vector<std::string> map_checksums;
};

struct LocalInput {
  ArtifactRef reference;
  PartitionId partition_id;
  std::filesystem::path graph_path;
  GraphFacts graph;
};

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "M0 input document is missing");
  }
  return detail::parse_json_strict(
      std::string(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>()));
}

DatasetIdentity parse_dataset(const Json& value) {
  detail::require_object_shape(
      value, {"dataset_id", "dataset_checksum", "global_node_count",
              "vector_dimension", "scalar_type", "distance_metric"});
  return {value.at("dataset_id").get<std::string>(),
          value.at("dataset_checksum").get<std::string>(),
          value.at("global_node_count").get<NodeCount>(),
          value.at("vector_dimension").get<std::uint32_t>(),
          value.at("scalar_type").get<std::string>(),
          value.at("distance_metric").get<std::string>()};
}

bool same_dataset(const DatasetIdentity& lhs, const DatasetIdentity& rhs) {
  return lhs.dataset_id == rhs.dataset_id &&
         lhs.dataset_checksum == rhs.dataset_checksum &&
         lhs.global_node_count == rhs.global_node_count &&
         lhs.vector_dimension == rhs.vector_dimension &&
         lhs.scalar_type == rhs.scalar_type &&
         lhs.distance_metric == rhs.distance_metric;
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

std::string recompute_aggregate(const Json& metadata) {
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
  for (const auto& payload : metadata.at("payloads")) {
    projection.payloads.push_back(
        {payload.at("role").get<std::string>(),
         payload.at("byte_length").get<std::uint64_t>(),
         payload.at("sha256").get<std::string>()});
  }
  for (const auto& source : metadata.at("source_artifacts")) {
    projection.source_artifacts.push_back(
        {artifact_type_from_string(source.at("artifact_type").get<std::string>()),
         source.at("format_version").get<std::string>(),
         source.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return aggregate_checksum(projection);
}

const Json& find_payload(const Json& metadata, const std::string& role) {
  const auto found = std::find_if(
      metadata.at("payloads").begin(), metadata.at("payloads").end(),
      [&](const auto& value) { return value.at("role") == role; });
  if (found == metadata.at("payloads").end()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "M0 required payload is missing: " + role);
  }
  return *found;
}

std::filesystem::path checked_payload_path(
    const std::filesystem::path& metadata_path, const Json& payload) {
  const auto path = resolve_confined_path(
      metadata_path.parent_path(), payload.at("path").get<std::string>());
  if (!std::filesystem::is_regular_file(path) ||
      std::filesystem::file_size(path) !=
          payload.at("byte_length").get<std::uint64_t>() ||
      sha256_file(path) != payload.at("sha256").get<std::string>()) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "M0 input payload identity does not match metadata");
  }
  return path;
}

void verify_reference(const ArtifactRef& reference, const Json& metadata,
                      ArtifactType type, const std::string& version,
                      const std::string& id_space) {
  if (reference.artifact_type != type || reference.format_version != version ||
      metadata.at("artifact_type") != to_string(type) ||
      metadata.at("format_version") != version ||
      metadata.at("id_space") != id_space ||
      metadata.at("aggregate_checksum") != reference.aggregate_checksum ||
      recompute_aggregate(metadata) != reference.aggregate_checksum) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "M0 Artifact reference/identity is incompatible");
  }
}

PartitionInput load_partition(const WorkspaceRef& workspace,
                              const ArtifactRef& reference) {
  const auto metadata_path =
      resolve_normal_artifact_metadata(workspace, reference.metadata_path);
  auto metadata = read_json(metadata_path);
  verify_reference(reference, metadata, ArtifactType::kPartition,
                   "partition-artifact/v1", "local");
  const auto capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  if (std::find(capabilities.begin(), capabilities.end(),
                "partition.complete_local_to_global_map/v1") ==
      capabilities.end()) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "M0 Partition map capability is missing");
  }
  PartitionInput result;
  result.metadata = metadata;
  result.dataset = parse_dataset(metadata.at("dataset"));
  result.count =
      metadata.at("structural").at("partition_count").get<NodeCount>();
  const auto& records = metadata.at("structural").at("partitions");
  if (result.count == 0 || records.size() != result.count) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "M0 Partition count is inconsistent");
  }
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records[index];
    if (record.at("partition_id").get<PartitionId>() != index) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "M0 Partition IDs are not contiguous");
    }
    const auto role =
        record.at("local_to_global_map_role").get<std::string>();
    const auto& payload = find_payload(metadata, role);
    if (payload.at("media_type") != kPartitionIdMapMediaType) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "M0 map media type is not reviewed cpp_main native");
    }
    const auto path = checked_payload_path(metadata_path, payload);
    const auto rows = record.at("local_node_count").get<NodeCount>();
    result.maps.push_back(validate_partition_id_map(
        path, rows, result.dataset.global_node_count));
    result.map_paths.push_back(path);
    result.map_checksums.push_back(payload.at("sha256").get<std::string>());
  }
  return result;
}

LocalInput load_local(const WorkspaceRef& workspace,
                      const ArtifactRef& reference,
                      const ArtifactRef& partition_reference,
                      const PartitionInput& partition) {
  const auto metadata_path =
      resolve_normal_artifact_metadata(workspace, reference.metadata_path);
  const auto metadata = read_json(metadata_path);
  verify_reference(reference, metadata, ArtifactType::kLocalGraph,
                   "local-graph-artifact/v1", "local");
  if (!same_dataset(parse_dataset(metadata.at("dataset")), partition.dataset) ||
      metadata.at("source_artifacts").size() != 1 ||
      metadata.at("source_artifacts").at(0) !=
          source_json(partition_reference)) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "M0 LocalGraph Dataset/source lineage is inconsistent");
  }
  const auto id =
      metadata.at("structural").at("partition_id").get<PartitionId>();
  if (id >= partition.count ||
      metadata.at("structural").at("source_partition_checksum") !=
          partition_reference.aggregate_checksum ||
      metadata.at("structural").at("local_node_count").get<NodeCount>() !=
          partition.maps[id].size()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "M0 LocalGraph ownership is inconsistent");
  }
  const auto& map_payload = find_payload(metadata, "local_to_global_map");
  const auto& graph_payload = find_payload(metadata, "local_graph");
  if (map_payload.at("media_type") != kPartitionIdMapMediaType ||
      graph_payload.at("media_type") != kLocalGraphMediaType ||
      map_payload.at("sha256") != partition.map_checksums[id]) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "M0 LocalGraph map/graph encoding is inconsistent");
  }
  checked_payload_path(metadata_path, map_payload);
  const auto graph_path = checked_payload_path(metadata_path, graph_payload);
  const auto declared = metadata.at("structural")
                            .at("declared_max_degree")
                            .get<std::uint32_t>();
  if (declared > 10) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "M0 LocalGraph exceeds the B0 degree bound");
  }
  const auto graph = validate_local_graph(
      graph_path, static_cast<NodeCount>(partition.maps[id].size()), declared);
  if (graph.observed_max_degree !=
      metadata.at("structural")
          .at("observed_max_degree")
          .get<std::uint32_t>()) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "M0 LocalGraph degree claim differs from payload");
  }
  return {reference, id, graph_path, graph};
}

bool cancelled(const StageContext& context) {
  return context.cancellation_token != nullptr &&
         context.cancellation_token->is_cancelled();
}

class CppMainM0 final : public GraphMerger {
 public:
  StageResult merge(
      const ArtifactRef& partition_artifact,
      const std::vector<ArtifactRef>& local_graphs,
      const std::filesystem::path& stage_manifest_path,
      StageContext& context) override {
    try {
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      const auto parameters = read_json(stage_manifest_path);
      detail::require_object_shape(parameters, {"R", "use_filters"});
      const auto r = parameters.at("R").get<std::uint32_t>();
      if (r != 16 || parameters.at("use_filters").get<bool>()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "M0 parameters differ from the approved Profile");
      }
      const auto partition =
          load_partition(context.workspace, partition_artifact);
      if (local_graphs.size() != partition.count) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "M0 requires exactly one LocalGraph per Partition");
      }
      std::map<PartitionId, LocalInput> inputs;
      for (const auto& reference : local_graphs) {
        auto input = load_local(context.workspace, reference,
                                partition_artifact, partition);
        if (!inputs.emplace(input.partition_id, std::move(input)).second) {
          throw PipelineError(ErrorCategory::kSourceMismatch,
                              "M0 received duplicate Partition ownership");
        }
      }
      for (PartitionId id = 0; id < partition.count; ++id) {
        if (inputs.count(id) == 0) {
          throw PipelineError(ErrorCategory::kSourceMismatch,
                              "M0 LocalGraph set is incomplete");
        }
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      const auto native = context.temporary_storage.stage_root / "native";
      std::filesystem::create_directories(native);
      const auto prefix = native / "shard-";
      for (PartitionId id = 0; id < partition.count; ++id) {
        std::filesystem::copy_file(partition.map_paths[id],
                                   prefix.string() + std::to_string(id) +
                                       ".ids");
        std::filesystem::copy_file(inputs.at(id).graph_path,
                                   prefix.string() + std::to_string(id) +
                                       ".index");
      }
      const auto output_graph = native / "merged.index";
      const auto output_medoids = native / "medoids.bin";
      context.metrics_sink->event("cpp_main.m0.merge_shards");
      if (diskann::merge_shards(prefix.string(), ".index", prefix.string(),
                                ".ids", partition.count, r,
                                output_graph.string(),
                                output_medoids.string(), false, "") != 0) {
        throw PipelineError(ErrorCategory::kInternal,
                            "merge_shards failed");
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      const auto global = validate_merged_global_graph(
          output_graph, partition.dataset.global_node_count, r);
      const auto medoids = validate_medoids(
          output_medoids, partition.count,
          partition.dataset.global_node_count);
      for (PartitionId id = 0; id < partition.count; ++id) {
        const auto expected =
            partition.maps[id][inputs.at(id).graph.entry_point];
        if (medoids[id] != expected) {
          throw PipelineError(ErrorCategory::kSourceMismatch,
                              "M0 medoid differs from mapped local entry point");
        }
      }
      if (std::find(medoids.begin(), medoids.end(), global.entry_point) ==
          medoids.end()) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "M0 global entry point is not a produced medoid");
      }

      const auto candidate =
          context.temporary_storage.stage_root / "candidate";
      std::filesystem::create_directories(candidate);
      const auto candidate_graph = candidate / "global.index";
      const auto candidate_medoids = candidate / "medoids.bin";
      std::filesystem::copy_file(output_graph, candidate_graph);
      std::filesystem::copy_file(output_medoids, candidate_medoids);
      Json source_locals = Json::array();
      Json all_sources = Json::array({source_json(partition_artifact)});
      std::vector<SourceArtifactIdentity> source_identities{
          {partition_artifact.artifact_type,
           partition_artifact.format_version,
           partition_artifact.aggregate_checksum}};
      for (PartitionId id = 0; id < partition.count; ++id) {
        source_locals.push_back(source_json(inputs.at(id).reference));
        all_sources.push_back(source_json(inputs.at(id).reference));
        source_identities.push_back(
            {inputs.at(id).reference.artifact_type,
             inputs.at(id).reference.format_version,
             inputs.at(id).reference.aggregate_checksum});
      }
      Json output{
          {"schema_version", "1.0.0"},
          {"canonicalization", "diskann-artifact-c14n-v1"},
          {"artifact_type", "global_graph"},
          {"format_version", "global-graph-artifact/v1"},
          {"dataset", dataset_json(partition.dataset)},
          {"id_space", "global"},
          {"structural",
           {{"global_node_count", partition.dataset.global_node_count},
            {"declared_max_degree", r},
            {"observed_max_degree", global.observed_max_degree},
            {"source_partition_artifact", source_json(partition_artifact)},
            {"source_local_graph_artifacts", source_locals}}},
          {"capabilities",
           Json::array({"graph.global_adjacency/v1", "graph.medoids/v1"})},
          {"source_artifacts", all_sources},
          {"payloads",
           Json::array({payload_json("global_graph", "global.index",
                                     kGlobalGraphMediaType,
                                     candidate_graph),
                        payload_json("medoids", "medoids.bin",
                                     kMedoidsMediaType,
                                     candidate_medoids)})}};
      output["aggregate_checksum"] = recompute_aggregate(output);
      std::ofstream(candidate / "artifact.json", std::ios::binary)
          << output.dump();
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }
      return {StageStatus::kSucceeded,
              ArtifactRef{"candidate/artifact.json",
                          ArtifactType::kGlobalGraph,
                          "global-graph-artifact/v1",
                          output.at("aggregate_checksum").get<std::string>()},
              {}};
    } catch (const std::exception& error) {
      std::cerr << "cpp_main M0 failed: " << error.what() << '\n';
      return {StageStatus::kFailed, std::nullopt, {}};
    }
  }
};

}  // namespace

std::unique_ptr<GraphMerger> make_m0_merge() {
  return std::make_unique<CppMainM0>();
}

}  // namespace diskann_pipeline::cpp_main
