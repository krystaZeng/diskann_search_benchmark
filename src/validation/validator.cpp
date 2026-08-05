#include "diskann_pipeline/validation.hpp"

#include "validation/validation_internal.hpp"

#include <algorithm>

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

Validator::Validator(WorkspaceRef workspace) : workspace_(std::move(workspace)) {}

ArtifactValidationResult Validator::validate(
    const ArtifactRef& reference,
    const std::vector<ValidatedArtifact>& validated_sources) const {
  ValidationEvidence evidence;
  evidence.validator_version = "artifact-contract-validator/1.0.0";
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

}  // namespace diskann_pipeline
