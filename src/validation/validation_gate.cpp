#include "validation/validation_internal.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace diskann_pipeline::validation_detail {
namespace {

using detail::Json;

bool valid_sha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

void require_sha256(const std::string& value, const char* field) {
  if (!valid_sha256(value)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string(field) + " is not lowercase SHA-256");
  }
}

void require_relative(const std::filesystem::path& value, const char* field) {
  if (value.empty() || value.is_absolute()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        std::string(field) + " must be relative");
  }
  for (const auto& component : value) {
    if (component == "..") {
      throw PipelineError(ErrorCategory::kPathEscape,
                          std::string(field) + " contains '..'");
    }
  }
}

DatasetIdentity parse_dataset(const Json& value) {
  detail::require_object_shape(
      value, {"dataset_id", "dataset_checksum", "global_node_count",
              "vector_dimension", "scalar_type", "distance_metric"});
  DatasetIdentity result{value.at("dataset_id").get<std::string>(),
                         value.at("dataset_checksum").get<std::string>(),
                         value.at("global_node_count").get<NodeCount>(),
                         value.at("vector_dimension").get<std::uint32_t>(),
                         value.at("scalar_type").get<std::string>(),
                         value.at("distance_metric").get<std::string>()};
  require_sha256(result.dataset_checksum, "dataset_checksum");
  if (result.dataset_id.empty() || result.global_node_count == 0 ||
      result.global_node_count > kMaximumNodeCount ||
      result.vector_dimension == 0 || result.scalar_type != "float32" ||
      result.distance_metric != "l2") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "DatasetIdentity violates v1 bounds or interpretation");
  }
  return result;
}

SourceArtifactIdentity parse_source(const Json& value) {
  detail::require_object_shape(
      value, {"artifact_type", "format_version", "aggregate_checksum"});
  SourceArtifactIdentity result{
      artifact_type_from_string(value.at("artifact_type").get<std::string>()),
      value.at("format_version").get<std::string>(),
      value.at("aggregate_checksum").get<std::string>()};
  require_sha256(result.aggregate_checksum, "source aggregate_checksum");
  if (result.format_version.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "source format_version is empty");
  }
  return result;
}

PayloadRef parse_payload(const Json& value) {
  detail::require_object_shape(
      value, {"role", "path", "media_type", "byte_length", "sha256"});
  PayloadRef result{value.at("role").get<std::string>(),
                    value.at("path").get<std::string>(),
                    value.at("media_type").get<std::string>(),
                    value.at("byte_length").get<std::uint64_t>(),
                    value.at("sha256").get<std::string>()};
  if (result.role.empty() || result.media_type.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "PayloadRef role or media_type is empty");
  }
  require_relative(result.path, "PayloadRef.path");
  require_sha256(result.sha256, "PayloadRef.sha256");
  return result;
}

void validate_structural_shape(ArtifactType type, const Json& structural) {
  switch (type) {
    case ArtifactType::kDataset:
      detail::require_object_shape(
          structural, {"global_node_count", "vector_dimension", "scalar_type",
                       "distance_metric"});
      break;
    case ArtifactType::kPartition:
      detail::require_object_shape(structural,
                                   {"partition_count", "partitions"});
      if (!structural.at("partitions").is_array()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "partitions must be an array");
      }
      for (const auto& record : structural.at("partitions")) {
        if (record.contains("shard_vectors_role")) {
          detail::require_object_shape(
              record,
              {"partition_id", "local_node_count", "shard_vectors_role",
               "local_to_global_map_role"});
        } else {
          detail::require_object_shape(
              record, {"partition_id", "local_node_count",
                       "local_to_global_map_role"});
        }
      }
      break;
    case ArtifactType::kLocalGraph:
      detail::require_object_shape(
          structural,
          {"partition_id", "local_node_count", "declared_max_degree",
           "observed_max_degree", "source_partition_checksum"});
      break;
    case ArtifactType::kGlobalGraph:
      detail::require_object_shape(
          structural,
          {"global_node_count", "declared_max_degree", "observed_max_degree",
           "source_partition_artifact", "source_local_graph_artifacts"});
      break;
  }
}

}  // namespace

AdmittedArtifact admit_artifact(const WorkspaceRef& workspace,
                                const ArtifactRef& reference) {
  try {
    require_relative(reference.metadata_path, "ArtifactRef.metadata_path");
    require_sha256(reference.aggregate_checksum,
                   "ArtifactRef.aggregate_checksum");
    const auto metadata_file = resolve_normal_artifact_metadata(
        workspace, reference.metadata_path);
    std::ifstream input(metadata_file, std::ios::binary);
    if (!input) {
      throw PipelineError(ErrorCategory::kPayloadMissing,
                          "Artifact metadata document is missing");
    }
    std::ostringstream bytes;
    bytes << input.rdbuf();
    const auto metadata = detail::parse_json_strict(bytes.str());
    detail::require_object_shape(
        metadata,
        {"schema_version", "canonicalization", "artifact_type",
         "format_version", "dataset", "id_space", "structural",
         "capabilities", "source_artifacts", "payloads",
         "aggregate_checksum"});
    if (metadata.at("schema_version") != "1.0.0" ||
        metadata.at("canonicalization") != "diskann-artifact-c14n-v1") {
      throw PipelineError(ErrorCategory::kUnsupportedVersion,
                          "Artifact metadata schema or canonicalization is unsupported");
    }
    const auto metadata_type = artifact_type_from_string(
        metadata.at("artifact_type").get<std::string>());
    if (metadata_type != reference.artifact_type) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "ArtifactRef type differs from metadata type");
    }
    const auto& rule =
        schema_rule(reference.artifact_type, reference.format_version);
    if (metadata.at("format_version").get<std::string>() !=
        reference.format_version) {
      throw PipelineError(ErrorCategory::kUnsupportedVersion,
                          "ArtifactRef version differs from metadata version");
    }
    const auto id_space =
        id_space_from_string(metadata.at("id_space").get<std::string>());
    if (id_space != rule.id_space) {
      throw PipelineError(ErrorCategory::kIdSpaceMismatch,
                          "metadata ID space differs from schema contract");
    }
    if (!metadata.at("capabilities").is_array() ||
        !metadata.at("source_artifacts").is_array() ||
        !metadata.at("payloads").is_array()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "Artifact collections must be arrays");
    }

    AdmittedArtifact result{reference,
                            metadata_file,
                            metadata,
                            parse_dataset(metadata.at("dataset")),
                            id_space,
                            {},
                            {},
                            {},
                            metadata.at("structural")};
    result.capabilities =
        metadata.at("capabilities").get<std::vector<std::string>>();
    if (std::any_of(result.capabilities.begin(), result.capabilities.end(),
                    [](const auto& capability) { return capability.empty(); })) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "Artifact capability ID is empty");
    }
    if (!std::is_sorted(result.capabilities.begin(), result.capabilities.end()) ||
        std::adjacent_find(result.capabilities.begin(), result.capabilities.end()) !=
            result.capabilities.end() ||
        std::find(result.capabilities.begin(), result.capabilities.end(),
                  rule.required_capability) == result.capabilities.end()) {
      throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                          "Artifact capabilities are not sorted, unique, and complete");
    }
    for (const auto& value : metadata.at("source_artifacts")) {
      result.source_artifacts.push_back(parse_source(value));
    }
    std::set<std::string> roles;
    for (const auto& value : metadata.at("payloads")) {
      auto parsed = parse_payload(value);
      if (!roles.insert(parsed.role).second) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "Artifact payload roles are duplicated");
      }
      result.payloads.push_back(std::move(parsed));
    }
    validate_structural_shape(metadata_type, result.structural);
    if (metadata_type == ArtifactType::kLocalGraph) {
      require_sha256(
          result.structural.at("source_partition_checksum").get<std::string>(),
          "source_partition_checksum");
    } else if (metadata_type == ArtifactType::kGlobalGraph) {
      parse_source(result.structural.at("source_partition_artifact"));
      const auto& local_sources =
          result.structural.at("source_local_graph_artifacts");
      if (!local_sources.is_array() || local_sources.empty()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "source_local_graph_artifacts must be non-empty");
      }
      for (const auto& source_value : local_sources) {
        parse_source(source_value);
      }
    }
    require_sha256(metadata.at("aggregate_checksum").get<std::string>(),
                   "metadata aggregate_checksum");
    return result;
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("Artifact metadata is invalid: ") + error.what());
  }
}

}  // namespace diskann_pipeline::validation_detail
