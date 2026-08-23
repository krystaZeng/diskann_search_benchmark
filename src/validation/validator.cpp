#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"

#include "validation/validation_internal.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace diskann_pipeline {

bool ValidatedArtifact::has_capability(std::string_view capability) const {
  return std::find(capabilities_.begin(), capabilities_.end(), capability) !=
         capabilities_.end();
}

ValidatedArtifact::ValidatedArtifact(
    ArtifactRef reference, DatasetIdentity dataset, IdSpace id_space,
    std::vector<std::string> capabilities,
    std::vector<SourceArtifactIdentity> source_artifacts,
    std::vector<PayloadRef> payloads, std::string structural_json,
    std::optional<PartitionId> partition_id)
    : reference_(std::move(reference)),
      dataset_(std::move(dataset)),
      id_space_(id_space),
      capabilities_(std::move(capabilities)),
      source_artifacts_(std::move(source_artifacts)),
      payloads_(std::move(payloads)),
      structural_json_(std::move(structural_json)),
      partition_id_(partition_id) {}

PartitionVectorSource resolve_partition_vector_source(
    const ValidatedArtifact& partition_artifact, PartitionId partition_id) {
  if (partition_artifact.reference().artifact_type != ArtifactType::kPartition) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "partition vector source requires a PartitionArtifact");
  }
  const auto structural =
      detail::parse_json_strict(partition_artifact.structural_json());
  const auto& records = structural.at("partitions");
  const auto record =
      std::find_if(records.begin(), records.end(), [&](const auto& value) {
        return value.at("partition_id").template get<PartitionId>() ==
               partition_id;
      });
  if (record == records.end()) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "selected Partition record does not exist");
  }
  return record->contains("shard_vectors_role")
             ? PartitionVectorSource::kArtifactPayload
             : PartitionVectorSource::kVectorAccessor;
}

void require_partition_vector_source(
    const ValidatedArtifact& partition_artifact, PartitionId partition_id,
    const VectorAccessor* vector_accessor) {
  if (resolve_partition_vector_source(partition_artifact, partition_id) ==
      PartitionVectorSource::kArtifactPayload) {
    return;
  }
  if (vector_accessor == nullptr ||
      vector_accessor->dimension() !=
          partition_artifact.dataset().vector_dimension) {
    throw PipelineError(
        ErrorCategory::kCapabilityUnsatisfied,
        "Partition has no shard vectors and no matching Dataset VectorAccessor");
  }
}

Validator::Validator(WorkspaceRef workspace) : workspace_(std::move(workspace)) {}

ArtifactValidationResult Validator::validate(
    const ArtifactRef& reference,
    const std::vector<ValidatedArtifact>& validated_sources) const {
  ValidationEvidence evidence;
  evidence.validator_version = "artifact-contract-validator/1.1.0";
  evidence.artifact_candidate = {reference.artifact_type,
                                 reference.format_version,
                                 reference.metadata_path,
                                 reference.aggregate_checksum};
  try {
    auto admitted = validation_detail::admit_artifact(workspace_, reference);
    validation_detail::validate_payload_identity(workspace_, admitted);
    validation_detail::validate_artifact_semantics(workspace_, admitted,
                                                   validated_sources);
    evidence.checks.push_back(
        {"artifact.contract/v1", true,
         "metadata, payload, identity, source, and semantic checks passed",
         std::nullopt, std::nullopt});
    evidence.result = ValidationResult::kValid;
    std::optional<PartitionId> owning_partition;
    if (reference.artifact_type == ArtifactType::kLocalGraph) {
      owning_partition =
          admitted.structural.at("partition_id").get<PartitionId>();
    }
    ValidatedArtifact grant{reference,
                            admitted.dataset,
                            admitted.id_space,
                            admitted.capabilities,
                            admitted.source_artifacts,
                            admitted.payloads,
                            detail::dump_json(admitted.structural),
                            owning_partition};
    return {std::move(evidence), std::move(grant)};
  } catch (const PipelineError& error) {
    evidence.checks.push_back({"artifact.contract/v1", false, error.what(),
                               std::nullopt, std::nullopt});
    evidence.result = ValidationResult::kInvalid;
    evidence.failure_category = error.category();
    return {std::move(evidence), std::nullopt};
  } catch (const std::exception& error) {
    evidence.checks.push_back({"artifact.contract/v1", false, error.what(),
                               std::nullopt, std::nullopt});
    evidence.result = ValidationResult::kInvalid;
    evidence.failure_category = ErrorCategory::kSchemaInvalid;
    return {std::move(evidence), std::nullopt};
  }
}

ArtifactValidationResult Validator::validate_with_lineage(
    const ArtifactRef& reference) const {
  struct CatalogEntry {
    ArtifactRef reference;
    detail::Json metadata;
  };
  std::vector<CatalogEntry> entries;
  std::error_code error;
  if (std::filesystem::exists(workspace_.normal_root)) {
    for (std::filesystem::recursive_directory_iterator iterator(
             workspace_.normal_root, error),
         end;
         iterator != end && !error; iterator.increment(error)) {
      if (!iterator->is_regular_file() ||
          iterator->path().filename() != "artifact.json") {
        continue;
      }
      try {
        std::ifstream input(iterator->path(), std::ios::binary);
        std::ostringstream bytes;
        bytes << input.rdbuf();
        const auto metadata = detail::parse_json_strict(bytes.str());
        const auto relative =
            std::filesystem::relative(iterator->path(), workspace_.normal_root);
        entries.push_back(
            {{relative,
              artifact_type_from_string(
                  metadata.at("artifact_type").get<std::string>()),
              metadata.at("format_version").get<std::string>(),
              metadata.at("aggregate_checksum").get<std::string>()},
             metadata});
      } catch (const std::exception&) {
        // Explicit target validation below remains fail-closed; unrelated
        // malformed catalog entries are not eligible lineage grants.
      }
    }
  }
  if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) {
        return entry.reference.metadata_path == reference.metadata_path;
      })) {
    try {
      const auto path =
          resolve_normal_artifact_metadata(workspace_, reference.metadata_path);
      std::ifstream input(path, std::ios::binary);
      std::ostringstream bytes;
      bytes << input.rdbuf();
      entries.push_back({reference, detail::parse_json_strict(bytes.str())});
    } catch (const std::exception&) {
      return validate(reference);
    }
  }

  std::vector<ValidatedArtifact> grants;
  std::vector<bool> resolved(entries.size(), false);
  std::optional<ArtifactValidationResult> target_failure;
  for (std::size_t pass = 0; pass <= entries.size(); ++pass) {
    bool progress = false;
    for (std::size_t index = 0; index < entries.size(); ++index) {
      if (resolved[index]) {
        continue;
      }
      const auto& source_json = entries[index].metadata.at("source_artifacts");
      if (!source_json.is_array()) {
        resolved[index] = true;
        progress = true;
        continue;
      }
      std::vector<ValidatedArtifact> sources;
      bool ready = true;
      for (const auto& value : source_json) {
        const SourceArtifactIdentity identity{
            artifact_type_from_string(
                value.at("artifact_type").get<std::string>()),
            value.at("format_version").get<std::string>(),
            value.at("aggregate_checksum").get<std::string>()};
        const auto found = std::find_if(
            grants.begin(), grants.end(), [&](const auto& grant) {
              return validation_detail::same_artifact_identity(
                  identity, grant.reference());
            });
        if (found == grants.end()) {
          ready = false;
          break;
        }
        sources.push_back(*found);
      }
      if (!ready) {
        continue;
      }
      auto result = validate(entries[index].reference, sources);
      resolved[index] = true;
      progress = true;
      if (entries[index].reference.metadata_path == reference.metadata_path) {
        if (result.valid()) {
          return result;
        }
        target_failure = result;
      }
      if (result.valid()) {
        grants.push_back(*result.validated_artifact);
      }
    }
    if (!progress) {
      break;
    }
  }
  if (target_failure) {
    return *target_failure;
  }
  return validate(reference);
}

}  // namespace diskann_pipeline
