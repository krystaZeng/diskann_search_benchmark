#include "validation/validation_internal.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace diskann_pipeline::validation_detail {
namespace {

bool same_dataset(const DatasetIdentity& lhs, const DatasetIdentity& rhs) {
  return lhs.dataset_id == rhs.dataset_id &&
         lhs.dataset_checksum == rhs.dataset_checksum &&
         lhs.global_node_count == rhs.global_node_count &&
         lhs.vector_dimension == rhs.vector_dimension &&
         lhs.scalar_type == rhs.scalar_type &&
         lhs.distance_metric == rhs.distance_metric;
}

bool same_source(const SourceArtifactIdentity& source,
                 const ArtifactRef& reference) {
  return source.artifact_type == reference.artifact_type &&
         source.format_version == reference.format_version &&
         source.aggregate_checksum == reference.aggregate_checksum;
}

const PayloadRef& require_payload(const AdmittedArtifact& artifact,
                                  const std::string& role) {
  const auto found = std::find_if(
      artifact.payloads.begin(), artifact.payloads.end(),
      [&](const PayloadRef& payload) { return payload.role == role; });
  if (found == artifact.payloads.end()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "required payload role is missing: " + role);
  }
  return *found;
}

std::filesystem::path payload_path(const AdmittedArtifact& artifact,
                                   const PayloadRef& payload) {
  return resolve_confined_path(artifact.metadata_file.parent_path(),
                               payload.path);
}

void validate_sources(
    const AdmittedArtifact& artifact,
    const std::vector<ValidatedArtifact>& validated_sources) {
  if (artifact.source_artifacts.size() != validated_sources.size()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "validated source count differs from source_artifacts");
  }
  for (std::size_t index = 0; index < artifact.source_artifacts.size(); ++index) {
    if (!same_source(artifact.source_artifacts[index],
                     validated_sources[index].reference()) ||
        !same_dataset(artifact.dataset, validated_sources[index].dataset())) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "source Artifact identity or dataset does not match");
    }
  }
}

void validate_vector_payload(const AdmittedArtifact& artifact,
                             const PayloadRef& payload, NodeCount expected) {
  std::ifstream input(payload_path(artifact, payload), std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  try {
    const auto json = detail::parse_json_strict(bytes.str());
    detail::require_object_shape(json, {"vectors"});
    const auto& vectors = json.at("vectors");
    if (!vectors.is_array() || vectors.size() != expected) {
      throw PipelineError(ErrorCategory::kNodeCountMismatch,
                          "vector payload count does not match metadata");
    }
    for (const auto& vector : vectors) {
      if (!vector.is_array() || vector.size() != artifact.dataset.vector_dimension) {
        throw PipelineError(ErrorCategory::kNodeCountMismatch,
                            "vector dimension does not match DatasetIdentity");
      }
      for (const auto& scalar : vector) {
        if (!scalar.is_number()) {
          throw PipelineError(ErrorCategory::kSchemaInvalid,
                              "vector scalar is not numeric");
        }
      }
    }
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("invalid vector payload: ") + error.what());
  }
}

NodeCount partition_count(const ValidatedArtifact& partition) {
  const auto structural = detail::parse_json_strict(partition.structural_json());
  return structural.at("partition_count").get<NodeCount>();
}

}  // namespace

void validate_payload_identity(const WorkspaceRef&, AdmittedArtifact& artifact) {
  for (const auto& payload : artifact.payloads) {
    const auto path = payload_path(artifact, payload);
    if (!std::filesystem::is_regular_file(path)) {
      throw PipelineError(ErrorCategory::kPayloadMissing,
                          "Artifact payload file is missing");
    }
    if (std::filesystem::file_size(path) != payload.byte_length) {
      throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                          "Artifact payload byte length differs from metadata");
    }
    if (sha256_file(path) != payload.sha256) {
      throw PipelineError(ErrorCategory::kChecksumMismatch,
                          "Artifact payload SHA-256 differs from metadata");
    }
  }

  AggregateIdentityProjection projection{
      artifact.reference.artifact_type,
      artifact.reference.format_version,
      artifact.dataset.dataset_checksum,
      artifact.id_space,
      artifact.capabilities,
      {},
      artifact.source_artifacts,
      detail::dump_json(artifact.structural)};
  for (const auto& payload : artifact.payloads) {
    projection.payloads.push_back(
        {payload.role, payload.byte_length, payload.sha256});
  }
  const auto recomputed = aggregate_checksum(projection);
  const auto metadata_checksum =
      artifact.metadata.at("aggregate_checksum").get<std::string>();
  if (recomputed != metadata_checksum ||
      recomputed != artifact.reference.aggregate_checksum) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "Artifact aggregate checksum does not match exact identity");
  }
}

void validate_artifact_semantics(
    const WorkspaceRef&, AdmittedArtifact& artifact,
    const std::vector<ValidatedArtifact>& validated_sources) {
  validate_sources(artifact, validated_sources);
  const auto& structural = artifact.structural;
  if (artifact.reference.artifact_type == ArtifactType::kDataset) {
    if (!validated_sources.empty() || artifact.dataset.dataset_id != "fake-six-node-v1" ||
        artifact.dataset.dataset_checksum !=
            "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b" ||
        artifact.dataset.global_node_count != 6 ||
        artifact.dataset.vector_dimension != 2 ||
        structural.at("global_node_count").get<NodeCount>() != 6 ||
        structural.at("vector_dimension").get<std::uint32_t>() != 2 ||
        structural.at("scalar_type") != "float32" ||
        structural.at("distance_metric") != "l2" ||
        artifact.payloads.size() != 1 || artifact.payloads[0].role != "vectors" ||
        artifact.payloads[0].media_type !=
            "application/vnd.diskann.fake-vectors+json;version=1" ||
        artifact.payloads[0].byte_length != 49 ||
        artifact.payloads[0].sha256 != artifact.dataset.dataset_checksum) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "DatasetArtifact does not match its versioned contract");
    }
    validate_vector_payload(artifact, artifact.payloads[0], 6);
    return;
  }

  if (artifact.reference.artifact_type == ArtifactType::kPartition) {
    if (validated_sources.size() != 1 ||
        validated_sources[0].reference().artifact_type != ArtifactType::kDataset) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "PartitionArtifact requires one validated DatasetArtifact");
    }
    const auto count = structural.at("partition_count").get<NodeCount>();
    const auto& records = structural.at("partitions");
    if (!is_valid_count(count) || records.size() != count) {
      throw PipelineError(ErrorCategory::kNodeCountMismatch,
                          "partition_count does not match Partition records");
    }
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto& record = records[index];
      const auto id = record.at("partition_id").get<PartitionId>();
      const auto local_count = record.at("local_node_count").get<NodeCount>();
      if (!is_valid_partition_id(id, count) || id != index ||
          !is_valid_count(local_count)) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "PartitionId is absent, non-contiguous, or out of range");
      }
      const auto vector_role =
          record.at("shard_vectors_role").get<std::string>();
      const auto map_role =
          record.at("local_to_global_map_role").get<std::string>();
      const auto& vectors = require_payload(artifact, vector_role);
      const auto& map = require_payload(artifact, map_role);
      validate_vector_payload(artifact, vectors, local_count);
      validate_local_to_global_map(payload_path(artifact, map), local_count,
                                   artifact.dataset.global_node_count);
      const auto expected_offset = index * 2;
      if (expected_offset + 1 >= artifact.payloads.size() ||
          artifact.payloads[expected_offset].role != vector_role ||
          artifact.payloads[expected_offset + 1].role != map_role) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "Partition shard/map payload order mismatches PartitionId");
      }
    }
    return;
  }

  if (artifact.reference.artifact_type == ArtifactType::kLocalGraph) {
    if (validated_sources.size() != 1 ||
        validated_sources[0].reference().artifact_type != ArtifactType::kPartition) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "LocalGraph requires its validated aggregate PartitionArtifact");
    }
    const auto id = structural.at("partition_id").get<PartitionId>();
    const auto local_count = structural.at("local_node_count").get<NodeCount>();
    const auto source_checksum =
        structural.at("source_partition_checksum").get<std::string>();
    const auto source_count = partition_count(validated_sources[0]);
    if (!is_valid_partition_id(id, source_count)) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "owning PartitionId is outside PartitionArtifact range");
    }
    if (source_checksum != validated_sources[0].reference().aggregate_checksum) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "source_partition_checksum does not match source Artifact");
    }
    const auto source_structural =
        detail::parse_json_strict(validated_sources[0].structural_json());
    const auto& records = source_structural.at("partitions");
    const auto record = std::find_if(records.begin(), records.end(), [&](const auto& value) {
      return value.at("partition_id").template get<PartitionId>() == id;
    });
    if (record == records.end()) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "selected Partition record does not exist");
    }
    if (record->at("local_node_count").get<NodeCount>() != local_count) {
      throw PipelineError(ErrorCategory::kNodeCountMismatch,
                          "LocalGraph count differs from selected Partition record");
    }
    const auto source_map_role =
        record->at("local_to_global_map_role").get<std::string>();
    const auto source_map = std::find_if(
        validated_sources[0].payloads().begin(), validated_sources[0].payloads().end(),
        [&](const auto& payload) { return payload.role == source_map_role; });
    if (source_map == validated_sources[0].payloads().end()) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "selected Partition map payload is missing");
    }
    if (artifact.payloads.size() != 2 ||
        artifact.payloads[0].role != "local_to_global_map" ||
        artifact.payloads[1].role != "local_graph" ||
        artifact.payloads[0].sha256 != source_map->sha256) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "LocalGraph map does not match selected PartitionId map");
    }
    validate_local_to_global_map(payload_path(artifact, artifact.payloads[0]),
                                 local_count,
                                 artifact.dataset.global_node_count);
    const auto declared =
        structural.at("declared_max_degree").get<std::uint32_t>();
    const auto observed_claim =
        structural.at("observed_max_degree").get<std::uint32_t>();
    const auto observed = validate_adjacency_graph(
        payload_path(artifact, artifact.payloads[1]), local_count, local_count,
        declared);
    if (observed != observed_claim) {
      throw PipelineError(ErrorCategory::kDegreeExceeded,
                          "observed degree does not match graph payload");
    }
    return;
  }

  if (validated_sources.size() < 2 ||
      validated_sources[0].reference().artifact_type != ArtifactType::kPartition) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GlobalGraph requires Partition and LocalGraph sources");
  }
  const auto global_count = structural.at("global_node_count").get<NodeCount>();
  if (global_count != artifact.dataset.global_node_count) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "GlobalGraph node count differs from DatasetIdentity");
  }
  const auto expected_locals = partition_count(validated_sources[0]);
  if (validated_sources.size() != expected_locals + 1) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GlobalGraph does not contain one LocalGraph per Partition");
  }
  std::set<PartitionId> owning_partitions;
  for (std::size_t index = 1; index < validated_sources.size(); ++index) {
    if (validated_sources[index].reference().artifact_type !=
            ArtifactType::kLocalGraph ||
        !validated_sources[index].partition_id() ||
        !owning_partitions.insert(*validated_sources[index].partition_id()).second) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "GlobalGraph LocalGraph source ownership is invalid");
    }
  }
  if (artifact.payloads.size() != 1 ||
      artifact.payloads[0].role != "global_graph") {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "GlobalGraph payload role is missing");
  }
  const auto declared =
      structural.at("declared_max_degree").get<std::uint32_t>();
  const auto observed_claim =
      structural.at("observed_max_degree").get<std::uint32_t>();
  const auto observed = validate_adjacency_graph(
      payload_path(artifact, artifact.payloads[0]), global_count, global_count,
      declared);
  if (observed != observed_claim) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "observed global degree does not match graph payload");
  }
  const auto& source_partition = structural.at("source_partition_artifact");
  const auto& source_locals = structural.at("source_local_graph_artifacts");
  if (source_partition.at("aggregate_checksum") !=
          validated_sources[0].reference().aggregate_checksum ||
      !source_locals.is_array() ||
      source_locals.size() != expected_locals) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GlobalGraph structural source identities are inconsistent");
  }
  for (std::size_t index = 0; index < source_locals.size(); ++index) {
    if (source_locals[index].at("aggregate_checksum") !=
        validated_sources[index + 1].reference().aggregate_checksum) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "GlobalGraph LocalGraph source checksum differs");
    }
  }
}

}  // namespace diskann_pipeline::validation_detail
