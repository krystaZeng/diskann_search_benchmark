#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <cmath>
#include <fstream>
#include <limits>

namespace diskann_pipeline::cpp_main {

CppMainVectorAccessor::CppMainVectorAccessor(const WorkspaceRef& workspace,
                                             const ArtifactRef& dataset)
    : dataset_reference_(dataset) {
  const auto validation = Validator(workspace).validate(dataset);
  if (!validation.valid()) {
    const auto detail = validation.evidence.checks.empty()
                            ? std::string{"validation evidence is empty"}
                            : validation.evidence.checks.front().message;
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "cpp_main VectorAccessor requires a validated native "
                        "DatasetArtifact: " +
                            detail);
  }
  validated_dataset_ = validation.validated_artifact;
  dataset_identity_ = validated_dataset_->dataset();
  if (dataset.artifact_type != ArtifactType::kDataset ||
      dataset.format_version != "diskann-float32-dataset/v1" ||
      validated_dataset_->id_space() != IdSpace::kNone ||
      !validated_dataset_->has_capability(
          "dataset.diskann_float32_bin/v1") ||
      validated_dataset_->payloads().size() != 1 ||
      validated_dataset_->payloads().front().role != "vectors" ||
      validated_dataset_->payloads().front().media_type !=
          kFloat32DatasetMediaType) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "cpp_main VectorAccessor Dataset binding is incompatible");
  }
  const auto metadata = resolve_normal_artifact_metadata(
      workspace, dataset_reference_.metadata_path);
  native_payload_path_ = resolve_confined_path(
      metadata.parent_path(), validated_dataset_->payloads().front().path);
  validate_float32_dataset(native_payload_path_,
                           dataset_identity_.global_node_count,
                           dataset_identity_.vector_dimension,
                           dataset_identity_.dataset_checksum);
}

std::size_t CppMainVectorAccessor::dimension() const noexcept {
  return dataset_identity_.vector_dimension;
}

void CppMainVectorAccessor::read(
    GlobalNodeId id, std::vector<float>& destination) const {
  if (!is_valid_global_node_id(id, dataset_identity_.global_node_count)) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "cpp_main Dataset row ID is out of range");
  }
  const auto dimension = dataset_identity_.vector_dimension;
  const auto row_bytes = static_cast<std::uint64_t>(dimension) * sizeof(float);
  const auto offset = 8ULL + static_cast<std::uint64_t>(id) * row_bytes;
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max())) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "cpp_main Dataset row offset overflows stream range");
  }
  std::ifstream input(native_payload_path_, std::ios::binary);
  input.seekg(static_cast<std::streamoff>(offset));
  destination.resize(dimension);
  input.read(reinterpret_cast<char*>(destination.data()),
             static_cast<std::streamsize>(row_bytes));
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                        "cpp_main Dataset row is truncated");
  }
  for (const auto value : destination) {
    if (!std::isfinite(value)) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "cpp_main Dataset row contains a non-finite value");
    }
  }
}

const ArtifactRef& CppMainVectorAccessor::dataset_reference() const noexcept {
  return dataset_reference_;
}

const DatasetIdentity& CppMainVectorAccessor::dataset_identity() const noexcept {
  return dataset_identity_;
}

const ValidatedArtifact& CppMainVectorAccessor::validated_dataset() const {
  if (!validated_dataset_) {
    throw PipelineError(ErrorCategory::kInternal,
                        "cpp_main Dataset validation grant is absent");
  }
  return *validated_dataset_;
}

const std::filesystem::path&
CppMainVectorAccessor::native_payload_path() const noexcept {
  return native_payload_path_;
}

}  // namespace diskann_pipeline::cpp_main
