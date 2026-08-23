#include "validation/validation_internal.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "baseline/cpp_main/native_formats.hpp"
#include "search/diskann/native_search_formats.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace diskann_pipeline::validation_detail {

bool same_artifact_identity(const SourceArtifactIdentity& source,
                            const ArtifactRef& reference) noexcept {
  return source.artifact_type == reference.artifact_type &&
         source.format_version == reference.format_version &&
         source.aggregate_checksum == reference.aggregate_checksum;
}

namespace {

bool same_dataset(const DatasetIdentity& lhs, const DatasetIdentity& rhs) {
  return lhs.dataset_id == rhs.dataset_id &&
         lhs.dataset_checksum == rhs.dataset_checksum &&
         lhs.global_node_count == rhs.global_node_count &&
         lhs.vector_dimension == rhs.vector_dimension &&
         lhs.scalar_type == rhs.scalar_type &&
         lhs.distance_metric == rhs.distance_metric;
}

SourceArtifactIdentity structural_source_identity(const detail::Json& source) {
  return {artifact_type_from_string(
              source.at("artifact_type").get<std::string>()),
          source.at("format_version").get<std::string>(),
          source.at("aggregate_checksum").get<std::string>()};
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
    if (!same_artifact_identity(artifact.source_artifacts[index],
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

bool is_native_media(const PayloadRef& payload, const char* expected) {
  return payload.media_type == expected;
}

void validate_map_payload(const AdmittedArtifact& artifact,
                          const PayloadRef& payload, NodeCount local_count) {
  if (is_native_media(payload, cpp_main::kPartitionIdMapMediaType)) {
    cpp_main::validate_partition_id_map(payload_path(artifact, payload),
                                        local_count,
                                        artifact.dataset.global_node_count);
    return;
  }
  if (payload.media_type != "application/json") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unsupported LocalToGlobal map media type");
  }
  validate_local_to_global_map(payload_path(artifact, payload), local_count,
                               artifact.dataset.global_node_count);
}

void validate_graph_payload(const AdmittedArtifact& artifact,
                            const PayloadRef& payload, NodeCount node_count,
                            std::uint32_t degree_bound, bool local,
                            std::uint32_t observed_claim) {
  std::uint32_t observed = 0;
  const char* native_media =
      local ? cpp_main::kLocalGraphMediaType : cpp_main::kGlobalGraphMediaType;
  if (is_native_media(payload, native_media)) {
    const auto facts = local
                           ? cpp_main::validate_local_graph(
                                 payload_path(artifact, payload), node_count,
                                 degree_bound)
                           : cpp_main::validate_merged_global_graph(
                                 payload_path(artifact, payload), node_count,
                                 degree_bound);
    observed = facts.observed_max_degree;
  } else {
    if (payload.media_type != "application/json") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "unsupported graph payload media type");
    }
    observed = validate_adjacency_graph(payload_path(artifact, payload),
                                        node_count, node_count, degree_bound);
  }
  if (observed != observed_claim) {
    throw PipelineError(ErrorCategory::kDegreeExceeded,
                        "observed degree does not match graph payload");
  }
}

NodeCount partition_count(const ValidatedArtifact& partition) {
  const auto structural = detail::parse_json_strict(partition.structural_json());
  return structural.at("partition_count").get<NodeCount>();
}

bool has_capability(const AdmittedArtifact& artifact,
                    const std::string& capability) {
  return std::find(artifact.capabilities.begin(), artifact.capabilities.end(),
                   capability) != artifact.capabilities.end();
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
    if (artifact.reference.format_version == "diskann-float32-dataset/v1") {
      if (!validated_sources.empty() || artifact.payloads.size() != 1 ||
          artifact.payloads[0].role != "vectors" ||
          !is_native_media(artifact.payloads[0],
                           cpp_main::kFloat32DatasetMediaType) ||
          artifact.payloads[0].sha256 != artifact.dataset.dataset_checksum ||
          structural.at("global_node_count").get<NodeCount>() !=
              artifact.dataset.global_node_count ||
          structural.at("vector_dimension").get<std::uint32_t>() !=
              artifact.dataset.vector_dimension ||
          structural.at("scalar_type") != "float32" ||
          structural.at("distance_metric") != "l2") {
        throw PipelineError(
            ErrorCategory::kSchemaInvalid,
            "cpp_main DatasetArtifact metadata does not match native contract");
      }
      cpp_main::validate_float32_dataset(
          payload_path(artifact, artifact.payloads[0]),
          artifact.dataset.global_node_count,
          artifact.dataset.vector_dimension,
          artifact.dataset.dataset_checksum);
      return;
    }
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
    std::vector<std::string> expected_payload_roles;
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto& record = records[index];
      const auto id = record.at("partition_id").get<PartitionId>();
      const auto local_count = record.at("local_node_count").get<NodeCount>();
      if (!is_valid_partition_id(id, count) || id != index ||
          !is_valid_count(local_count)) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "PartitionId is absent, non-contiguous, or out of range");
      }
      const auto map_role =
          record.at("local_to_global_map_role").get<std::string>();
      if (map_role == kPartitionCentroidsPayloadRole) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "centroids is reserved for the aggregate P0 payload");
      }
      if (record.contains("shard_vectors_role")) {
        const auto vector_role =
            record.at("shard_vectors_role").get<std::string>();
        if (vector_role == kPartitionCentroidsPayloadRole) {
          throw PipelineError(
              ErrorCategory::kSchemaInvalid,
              "centroids is reserved for the aggregate P0 payload");
        }
        const auto& vectors = require_payload(artifact, vector_role);
        if (is_native_media(vectors, cpp_main::kFloat32DatasetMediaType)) {
          cpp_main::validate_float32_matrix(payload_path(artifact, vectors),
                                            local_count,
                                            artifact.dataset.vector_dimension);
        } else {
          validate_vector_payload(artifact, vectors, local_count);
        }
        expected_payload_roles.push_back(vector_role);
      }
      const auto& map = require_payload(artifact, map_role);
      validate_map_payload(artifact, map, local_count);
      expected_payload_roles.push_back(map_role);
    }
    const bool centroids_present =
        artifact.payloads.size() == expected_payload_roles.size() + 1 &&
        artifact.payloads.back().role == kPartitionCentroidsPayloadRole;
    const bool centroids_declared =
        has_capability(artifact, kPartitionCentroidsCapability);
    if (centroids_present != centroids_declared) {
      throw PipelineError(
          centroids_declared ? ErrorCategory::kPayloadMissing
                             : ErrorCategory::kCapabilityUnsatisfied,
          "Partition centroids payload and capability must be declared together");
    }
    if (centroids_present) {
      const auto& centroids = artifact.payloads.back();
      if (is_native_media(centroids, cpp_main::kCentroidsMediaType)) {
        cpp_main::validate_centroids(payload_path(artifact, centroids), count,
                                     artifact.dataset.vector_dimension);
      }
      expected_payload_roles.push_back(kPartitionCentroidsPayloadRole);
    }
    if (artifact.payloads.size() != expected_payload_roles.size() ||
        !std::equal(expected_payload_roles.begin(),
                    expected_payload_roles.end(), artifact.payloads.begin(),
                    [](const std::string& expected, const PayloadRef& actual) {
                      return expected == actual.role;
                    })) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "Partition payload order must follow each optional shard vector, "
          "required map, then optional centroids");
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
    validate_map_payload(artifact, artifact.payloads[0], local_count);
    const auto declared =
        structural.at("declared_max_degree").get<std::uint32_t>();
    const auto observed_claim =
        structural.at("observed_max_degree").get<std::uint32_t>();
    validate_graph_payload(artifact, artifact.payloads[1], local_count,
                           declared, true, observed_claim);
    return;
  }

  if (artifact.reference.artifact_type == ArtifactType::kPQ) {
    if (validated_sources.size() != 1 ||
        validated_sources[0].reference().artifact_type !=
            ArtifactType::kDataset ||
        artifact.payloads.size() != 2 ||
        artifact.payloads[0].role != "pq_pivots" ||
        artifact.payloads[1].role != "pq_compressed_vectors" ||
        artifact.payloads[0].media_type != search::kPqPivotsMediaType ||
        artifact.payloads[1].media_type != search::kPqCodesMediaType ||
        !has_capability(artifact, "search.pqflash_loadable/v1")) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "PQArtifact requires one Dataset source and exact native payloads");
    }
    const auto point_count = structural.at("point_count").get<NodeCount>();
    const auto dimension = structural.at("dimension").get<std::uint32_t>();
    const auto centroid_count =
        structural.at("centroid_count").get<std::uint32_t>();
    const auto chunks = structural.at("pq_chunks").get<std::uint32_t>();
    const auto opq = structural.at("opq").get<bool>();
    if (point_count != artifact.dataset.global_node_count ||
        dimension != artifact.dataset.vector_dimension ||
        structural.at("scalar_type") != "float32" ||
        structural.at("distance_metric") != "l2" || centroid_count != 256 ||
        chunks == 0 || chunks > dimension || chunks > 512 || opq ||
        structural.at("compressed_code_scalar") != "uint8") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "PQArtifact structural facts differ from Dataset");
    }
    search::validate_pq_native_payloads(
        payload_path(artifact, artifact.payloads[0]),
        payload_path(artifact, artifact.payloads[1]), point_count, dimension,
        chunks, opq);
    return;
  }

  if (artifact.reference.artifact_type == ArtifactType::kDiskLayout) {
    if (validated_sources.size() != 2 ||
        validated_sources[0].reference().artifact_type !=
            ArtifactType::kDataset ||
        validated_sources[1].reference().artifact_type !=
            ArtifactType::kGlobalGraph ||
        artifact.payloads.size() < 2 || artifact.payloads.size() > 3 ||
        artifact.payloads[0].role != "disk_index" ||
        artifact.payloads[1].role != "medoids" ||
        artifact.payloads[0].media_type != search::kDiskIndexMediaType ||
        artifact.payloads[1].media_type != search::kUint32MatrixMediaType ||
        !has_capability(artifact, "search.pqflash_loadable/v1") ||
        !has_capability(artifact, "search.disk_vectors_full_precision/v1") ||
        !has_capability(artifact, "graph.global_ids/v1") ||
        !has_capability(artifact, "search.entry_end_to_end/v1")) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "DiskLayoutArtifact requires ordered Dataset/GlobalGraph sources and exact payloads");
    }
    const auto global_count =
        structural.at("global_node_count").get<NodeCount>();
    const auto dimension = structural.at("dimension").get<std::uint32_t>();
    const auto declared_degree =
        structural.at("declared_max_degree").get<std::uint32_t>();
    const auto medoid_count =
        structural.at("medoid_count").get<std::uint32_t>();
    const auto frozen_count =
        structural.at("frozen_point_count").get<std::uint64_t>();
    const auto reorder = structural.at("reorder").get<bool>();
    const auto centroid_policy =
        structural.at("centroid_policy").get<std::string>();
    const auto source_global = detail::parse_json_strict(
        validated_sources[1].structural_json());
    if (global_count != artifact.dataset.global_node_count ||
        global_count !=
            source_global.at("global_node_count").get<NodeCount>() ||
        dimension != artifact.dataset.vector_dimension ||
        declared_degree !=
            source_global.at("declared_max_degree").get<std::uint32_t>() ||
        structural.at("scalar_type") != "float32" ||
        structural.at("distance_metric") != "l2" ||
        structural.at("disk_vector_encoding") != "float32_full_precision" ||
        structural.at("pq_disk_bytes").get<std::uint32_t>() != 0 || reorder ||
        frozen_count != 0 || structural.at("entry_policy") != "end_to_end" ||
        medoid_count == 0) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "DiskLayoutArtifact structural facts differ from its inputs");
    }
    const auto source_medoid = std::find_if(
        validated_sources[1].payloads().begin(),
        validated_sources[1].payloads().end(),
        [](const PayloadRef& payload) {
          return payload.role == kGlobalGraphMedoidsPayloadRole;
        });
    if (source_medoid == validated_sources[1].payloads().end() ||
        source_medoid->sha256 != artifact.payloads[1].sha256) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "DiskLayout medoids do not match GlobalGraph lineage");
    }
    if (centroid_policy == "medoid_vector_fallback") {
      if (artifact.payloads.size() != 2) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "medoid fallback forbids a centroid payload");
      }
    } else if (centroid_policy == "declared_centroids") {
      if (artifact.payloads.size() != 3 ||
          artifact.payloads[2].role != "centroids" ||
          artifact.payloads[2].media_type != search::kFloat32MatrixMediaType) {
        throw PipelineError(ErrorCategory::kPayloadMissing,
                            "declared centroids require the centroid payload");
      }
      const auto source_centroid = std::find_if(
          validated_sources[1].payloads().begin(),
          validated_sources[1].payloads().end(),
          [](const PayloadRef& payload) { return payload.role == "centroids"; });
      if (source_centroid == validated_sources[1].payloads().end() ||
          source_centroid->sha256 != artifact.payloads[2].sha256) {
        throw PipelineError(
            ErrorCategory::kSourceMismatch,
            "DiskLayout centroids are not traceable to GlobalGraph lineage");
      }
      search::validate_centroids_native_payload(
          payload_path(artifact, artifact.payloads[2]), medoid_count,
          dimension);
    } else {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "unknown DiskLayout centroid policy");
    }
    search::validate_disk_layout_native_payload(
        payload_path(artifact, artifact.payloads[0]), global_count, dimension,
        declared_degree, reorder, frozen_count);
    search::validate_medoids_native_payload(
        payload_path(artifact, artifact.payloads[1]), medoid_count,
        global_count);
    return;
  }

  if (artifact.reference.artifact_type != ArtifactType::kGlobalGraph) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unsupported Artifact semantic validator");
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
  if (artifact.payloads.empty() || artifact.payloads.size() > 2 ||
      artifact.payloads[0].role != "global_graph") {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "GlobalGraph payload role is missing");
  }
  const bool medoids_present =
      artifact.payloads.size() == 2 &&
      artifact.payloads[1].role == kGlobalGraphMedoidsPayloadRole;
  const bool medoids_declared =
      has_capability(artifact, kGlobalGraphMedoidsCapability);
  if (artifact.payloads.size() == 2 && !medoids_present) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "GlobalGraph optional second payload must be medoids");
  }
  if (medoids_present != medoids_declared) {
    throw PipelineError(
        medoids_declared ? ErrorCategory::kPayloadMissing
                         : ErrorCategory::kCapabilityUnsatisfied,
        "GlobalGraph medoids payload and capability must be declared together");
  }
  const auto declared =
      structural.at("declared_max_degree").get<std::uint32_t>();
  const auto observed_claim =
      structural.at("observed_max_degree").get<std::uint32_t>();
  validate_graph_payload(artifact, artifact.payloads[0], global_count,
                         declared, false, observed_claim);
  if (medoids_present &&
      is_native_media(artifact.payloads[1], cpp_main::kMedoidsMediaType)) {
    cpp_main::validate_medoids(payload_path(artifact, artifact.payloads[1]),
                               expected_locals, global_count);
  }
  const auto& source_partition = structural.at("source_partition_artifact");
  const auto& source_locals = structural.at("source_local_graph_artifacts");
  if (!same_artifact_identity(structural_source_identity(source_partition),
                              validated_sources[0].reference()) ||
      !source_locals.is_array() ||
      source_locals.size() != expected_locals) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GlobalGraph structural source identities are inconsistent");
  }
  for (std::size_t index = 0; index < source_locals.size(); ++index) {
    if (!same_artifact_identity(
            structural_source_identity(source_locals[index]),
            validated_sources[index + 1].reference())) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "GlobalGraph LocalGraph source identity differs");
    }
  }
}

}  // namespace diskann_pipeline::validation_detail
