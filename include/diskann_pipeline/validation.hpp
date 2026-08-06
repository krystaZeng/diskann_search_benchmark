#pragma once

#include "diskann_pipeline/artifacts.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace diskann_pipeline {

class ValidatedArtifact {
 public:
  bool has_capability(std::string_view capability) const;
  const ArtifactRef& reference() const noexcept { return reference_; }
  const DatasetIdentity& dataset() const noexcept { return dataset_; }
  IdSpace id_space() const noexcept { return id_space_; }
  const std::vector<std::string>& capabilities() const noexcept {
    return capabilities_;
  }
  const std::vector<SourceArtifactIdentity>& source_artifacts() const noexcept {
    return source_artifacts_;
  }
  const std::vector<PayloadRef>& payloads() const noexcept { return payloads_; }
  const std::string& structural_json() const noexcept { return structural_json_; }
  const std::optional<PartitionId>& partition_id() const noexcept {
    return partition_id_;
  }

 private:
  friend class Validator;
  ValidatedArtifact(ArtifactRef reference, DatasetIdentity dataset,
                    IdSpace id_space, std::vector<std::string> capabilities,
                    std::vector<SourceArtifactIdentity> source_artifacts,
                    std::vector<PayloadRef> payloads,
                    std::string structural_json,
                    std::optional<PartitionId> partition_id);

  ArtifactRef reference_;
  DatasetIdentity dataset_;
  IdSpace id_space_{IdSpace::kNone};
  std::vector<std::string> capabilities_;
  std::vector<SourceArtifactIdentity> source_artifacts_;
  std::vector<PayloadRef> payloads_;
  std::string structural_json_;
  std::optional<PartitionId> partition_id_;
};

struct ArtifactValidationResult {
  ValidationEvidence evidence;
  std::optional<ValidatedArtifact> validated_artifact;

  bool valid() const noexcept { return validated_artifact.has_value(); }
};

enum class PartitionVectorSource {
  kArtifactPayload,
  kVectorAccessor,
};

PartitionVectorSource resolve_partition_vector_source(
    const ValidatedArtifact& partition_artifact, PartitionId partition_id);

void require_partition_vector_source(
    const ValidatedArtifact& partition_artifact, PartitionId partition_id,
    const VectorAccessor* vector_accessor);

class Validator {
 public:
  explicit Validator(WorkspaceRef workspace);

  ArtifactValidationResult validate(
      const ArtifactRef& reference,
      const std::vector<ValidatedArtifact>& validated_sources = {}) const;

 private:
  WorkspaceRef workspace_;
};

}  // namespace diskann_pipeline
