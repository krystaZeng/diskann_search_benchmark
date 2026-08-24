#include "search/diskann/native_search_formats.hpp"

#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "validation/validation_internal.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace diskann_pipeline::search {
namespace {

bool same_artifact_ref(const ArtifactRef& lhs, const ArtifactRef& rhs) {
  return lhs.metadata_path == rhs.metadata_path &&
         lhs.artifact_type == rhs.artifact_type &&
         lhs.format_version == rhs.format_version &&
         lhs.aggregate_checksum == rhs.aggregate_checksum;
}

bool same_dataset(const DatasetIdentity& lhs, const DatasetIdentity& rhs) {
  return lhs.dataset_id == rhs.dataset_id &&
         lhs.dataset_checksum == rhs.dataset_checksum &&
         lhs.global_node_count == rhs.global_node_count &&
         lhs.vector_dimension == rhs.vector_dimension &&
         lhs.scalar_type == rhs.scalar_type &&
         lhs.distance_metric == rhs.distance_metric;
}

bool same_source(const SourceArtifactIdentity& source,
                 const ArtifactRef& artifact) {
  return source.artifact_type == artifact.artifact_type &&
         source.format_version == artifact.format_version &&
         source.aggregate_checksum == artifact.aggregate_checksum;
}

std::string read_text(const std::filesystem::path& path,
                      const char* description) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        std::string(description) + " is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  if (input.bad()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string(description) + " cannot be read");
  }
  return bytes.str();
}

void require_payload_lengths(const SearchArtifactAdmission& artifact) {
  std::set<std::string> observed_roles;
  for (const auto& payload : artifact.payloads) {
    observed_roles.insert(payload.role);
    const auto path = resolve_confined_path(artifact.metadata_file.parent_path(),
                                            payload.path);
    if (!std::filesystem::is_regular_file(path)) {
      throw PipelineError(ErrorCategory::kPayloadMissing,
                          "prevalidated search payload is missing");
    }
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes != payload.byte_length) {
      throw PipelineError(ErrorCategory::kPayloadLengthMismatch,
                          "prevalidated search payload length mismatch");
    }
  }
  const std::set<std::string> required =
      artifact.reference.artifact_type == ArtifactType::kPQ
          ? std::set<std::string>{"pq_pivots", "pq_compressed_vectors"}
          : std::set<std::string>{"disk_index", "medoids"};
  if (!std::includes(observed_roles.begin(), observed_roles.end(),
                     required.begin(), required.end())) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "prevalidated search payload role is missing");
  }
}

std::filesystem::path normalized_executable(
    const std::filesystem::path& base, const std::filesystem::path& value) {
  std::error_code error;
  const auto candidate = value.is_absolute() ? value : base / value;
  const auto result = std::filesystem::weakly_canonical(candidate, error);
  if (error || !std::filesystem::is_regular_file(result)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "search runner executable is missing");
  }
  return result;
}

bool declared_path_matches(const WorkspaceRef& workspace,
                           const std::filesystem::path& execution_root,
                           const std::filesystem::path& declaration,
                           const std::filesystem::path& actual) {
  if (declaration.empty() || declaration.is_absolute()) {
    return false;
  }
  for (const auto& component : declaration) {
    if (component == "..") return false;
  }
  for (const auto& root : {execution_root, workspace.root}) {
    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(root / declaration,
                                                              error);
    if (!error && std::filesystem::is_regular_file(candidate) &&
        candidate == actual) {
      return true;
    }
  }
  return false;
}

void require_project_source(const detail::Json& source,
                            const SourceIdentity& expected) {
  if (source.at("repository") != expected.repository ||
      source.at("branch") != expected.branch ||
      source.at("commit") != expected.commit ||
      source.at("dirty_worktree") != expected.dirty_worktree) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "runner BuildManifest project source mismatch");
  }
}

template <typename T>
T read_scalar(std::ifstream& input, std::uint64_t offset,
              const char* description) {
  T value{};
  input.seekg(static_cast<std::streamoff>(offset));
  input.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (!input) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("truncated native payload: ") + description);
  }
  return value;
}

std::uint64_t native_file_size(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "native payload is not a readable regular file");
  }
  return size;
}

void require_matrix(const std::filesystem::path& path,
                    std::uint32_t expected_rows,
                    std::uint32_t expected_columns,
                    std::uint64_t scalar_bytes) {
  std::ifstream input(path, std::ios::binary);
  const auto rows = read_scalar<std::uint32_t>(input, 0, "matrix rows");
  const auto columns = read_scalar<std::uint32_t>(input, 4, "matrix columns");
  if (rows != expected_rows || columns != expected_columns ||
      native_file_size(path) !=
          8ULL + static_cast<std::uint64_t>(rows) * columns * scalar_bytes) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "native matrix header or byte length is inconsistent");
  }
}

}  // namespace

NativeAliasMethod stage_native_payload_alias(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const NativeHardLinkAttempt& hard_link_attempt) {
  if (!std::filesystem::is_regular_file(source) ||
      std::filesystem::exists(destination)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "native staging source is missing or destination exists");
  }
  std::filesystem::create_directories(destination.parent_path());
  const auto attempt = hard_link_attempt
                           ? hard_link_attempt
                           : NativeHardLinkAttempt{
                                 [](const auto& from, const auto& to) {
                                   std::error_code error;
                                   std::filesystem::create_hard_link(from, to,
                                                                      error);
                                   return error;
                                 }};
  const auto link_error = attempt(source, destination);
  if (!link_error) {
    if (!std::filesystem::is_regular_file(destination)) {
      throw PipelineError(ErrorCategory::kInternal,
                          "hard-link staging did not create a regular file");
    }
    return NativeAliasMethod::kHardLink;
  }
  if (link_error != std::errc::cross_device_link &&
      link_error != std::errc::operation_not_supported &&
      link_error != std::errc::function_not_supported &&
      link_error != std::errc::not_supported) {
    throw PipelineError(ErrorCategory::kInternal,
                        "native hard-link staging failed: " +
                            link_error.message());
  }
  std::error_code copy_error;
  std::filesystem::copy_file(source, destination,
                             std::filesystem::copy_options::none, copy_error);
  if (copy_error || !std::filesystem::is_regular_file(destination)) {
    throw PipelineError(ErrorCategory::kInternal,
                        "native staging copy fallback failed");
  }
  return NativeAliasMethod::kCopy;
}

SearchArtifactAdmission admit_search_artifact_metadata(
    const WorkspaceRef& workspace, const ArtifactRef& reference) {
  const auto admitted = validation_detail::admit_artifact(workspace, reference);
  if (admitted.metadata.at("aggregate_checksum").get<std::string>() !=
      reference.aggregate_checksum) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "Artifact metadata aggregate checksum mismatch");
  }
  return {reference,
          admitted.dataset,
          admitted.id_space,
          admitted.source_artifacts,
          admitted.payloads,
          admitted.metadata_file};
}

SearchArtifactAdmission admit_prevalidated_search_artifact(
    const WorkspaceRef& workspace, const ArtifactRef& reference,
    const SearchAdmissionEvidenceRef& evidence) {
  if (!same_artifact_ref(reference, evidence.expected_artifact)) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "search admission evidence expected ArtifactRef mismatch");
  }
  const auto evidence_path = resolve_confined_path(workspace.root, evidence.path);
  if (!std::filesystem::is_regular_file(evidence_path)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "search admission evidence is missing");
  }
  if (sha256_file(evidence_path) != evidence.sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "search admission evidence checksum mismatch");
  }
  const auto document = parse_validation_evidence(
      read_text(evidence_path, "search admission evidence"));
  const auto& candidate = document.artifact_candidate;
  if (document.result != ValidationResult::kValid ||
      candidate.artifact_type != reference.artifact_type ||
      candidate.format_version != reference.format_version ||
      candidate.metadata_path != reference.metadata_path ||
      !candidate.claimed_aggregate_checksum ||
      *candidate.claimed_aggregate_checksum != reference.aggregate_checksum) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "search admission evidence does not grant ArtifactRef");
  }
  auto admitted = admit_search_artifact_metadata(workspace, reference);
  if (reference.artifact_type != ArtifactType::kPQ &&
      reference.artifact_type != ArtifactType::kDiskLayout) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "search admission evidence type is unsupported");
  }
  require_payload_lengths(admitted);
  return admitted;
}

void validate_search_artifact_lineage(
    const SearchArtifactAdmission& dataset,
    const SearchArtifactAdmission& global_graph,
    const SearchArtifactAdmission& pq,
    const SearchArtifactAdmission& disk_layout) {
  if (dataset.reference.artifact_type != ArtifactType::kDataset ||
      global_graph.reference.artifact_type != ArtifactType::kGlobalGraph ||
      pq.reference.artifact_type != ArtifactType::kPQ ||
      disk_layout.reference.artifact_type != ArtifactType::kDiskLayout ||
      !same_dataset(dataset.dataset, global_graph.dataset) ||
      !same_dataset(dataset.dataset, pq.dataset) ||
      !same_dataset(dataset.dataset, disk_layout.dataset) ||
      pq.source_artifacts.size() != 1 ||
      !same_source(pq.source_artifacts[0], dataset.reference) ||
      disk_layout.source_artifacts.size() != 2 ||
      !same_source(disk_layout.source_artifacts[0], dataset.reference) ||
      !same_source(disk_layout.source_artifacts[1], global_graph.reference)) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "search Artifact dataset or lineage identity mismatch");
  }
}

void validate_search_runner_execution_identity(
    const WorkspaceRef& workspace,
    const std::filesystem::path& execution_root,
    const std::filesystem::path& actual_executable,
    const SearchRunnerExecutionIdentity& declaration) {
  if (declaration.implementation_id !=
          "cpp_main_pqflash_search_instrumented_v1" ||
      declaration.implementation_version != "1.0.0" ||
      declaration.reproduction_mode != "instrumented" ||
      declaration.diskann_source_commit !=
          "78256bbab4685e1774e78d331e081a153be26823") {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "instrumented runner implementation/source mismatch");
  }
  const auto actual = normalized_executable(execution_root, actual_executable);
  if (!declared_path_matches(workspace, execution_root,
                             declaration.binary_path, actual) ||
      sha256_file(actual) != declaration.binary_sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "actual runner executable identity mismatch");
  }
  const auto manifest_path = resolve_confined_path(
      workspace.root, declaration.build_manifest_path);
  if (!std::filesystem::is_regular_file(manifest_path) ||
      sha256_file(manifest_path) != declaration.build_manifest_sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "runner BuildManifest identity mismatch");
  }
  try {
    const auto manifest = detail::parse_json_strict(
        read_text(manifest_path, "runner BuildManifest"));
    if (manifest.at("status") != "SUCCEEDED") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "runner BuildManifest is not successful");
    }
    require_project_source(manifest.at("source"), declaration.project_source);
    const auto& parameters = manifest.at("parameters");
    std::string manifest_binary_sha;
    if (parameters.contains("runner_binary")) {
      const auto& binary = parameters.at("runner_binary");
      manifest_binary_sha = binary.at("sha256").get<std::string>();
      const auto manifest_binary_path =
          std::filesystem::path(binary.at("path").get<std::string>());
      if (manifest_binary_path.is_absolute()) {
        if (std::filesystem::weakly_canonical(manifest_binary_path) != actual) {
          throw PipelineError(ErrorCategory::kSourceMismatch,
                              "runner BuildManifest binary path mismatch");
        }
      } else if (!declared_path_matches(workspace, execution_root,
                                        manifest_binary_path, actual)) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "runner BuildManifest binary path mismatch");
      }
    } else {
      manifest_binary_sha =
          parameters.at("binary_sha256").get<std::string>();
    }
    if (manifest_binary_sha != declaration.binary_sha256 ||
        manifest_binary_sha != sha256_file(actual)) {
      throw PipelineError(ErrorCategory::kChecksumMismatch,
                          "runner BuildManifest binary checksum mismatch");
    }
    const auto& diskann = parameters.at("diskann_source");
    if (diskann.at("repository") !=
            "https://github.com/microsoft/DiskANN.git" ||
        diskann.at("branch") != "cpp_main" ||
        diskann.at("commit") != declaration.diskann_source_commit ||
        diskann.at("dirty_worktree") != false) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "runner BuildManifest DiskANN source mismatch");
    }
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("runner BuildManifest is invalid: ") +
                            error.what());
  }
}

void validate_pq_native_payloads(const std::filesystem::path& pivots,
                                 const std::filesystem::path& compressed,
                                 NodeCount expected_points,
                                 std::uint32_t expected_dimension,
                                 std::uint32_t expected_chunks,
                                 bool opq) {
  if (expected_points > std::numeric_limits<std::uint32_t>::max() ||
      expected_chunks == 0 || expected_chunks > expected_dimension) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ dimensions exceed pinned native header bounds");
  }
  std::ifstream pivot_input(pivots, std::ios::binary);
  const auto offsets_rows =
      read_scalar<std::uint32_t>(pivot_input, 0, "PQ offset rows");
  const auto offsets_columns =
      read_scalar<std::uint32_t>(pivot_input, 4, "PQ offset columns");
  if (offsets_rows != 4 || offsets_columns != 1) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "PQ pivot offset table must contain four offsets");
  }
  std::vector<std::size_t> offsets(offsets_rows);
  pivot_input.seekg(8);
  pivot_input.read(reinterpret_cast<char*>(offsets.data()),
                   static_cast<std::streamsize>(offsets.size() * sizeof(std::size_t)));
  if (!pivot_input || !std::is_sorted(offsets.begin(), offsets.end()) ||
      offsets.front() < 8 + offsets.size() * sizeof(std::size_t) ||
      offsets.back() != native_file_size(pivots)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "PQ pivot offsets are truncated or inconsistent");
  }
  const auto table_rows = read_scalar<std::uint32_t>(
      pivot_input, offsets[0], "PQ centroid rows");
  const auto table_dimension = read_scalar<std::uint32_t>(
      pivot_input, offsets[0] + 4, "PQ centroid dimension");
  const auto mean_dimension = read_scalar<std::uint32_t>(
      pivot_input, offsets[1], "PQ mean dimension");
  const auto mean_columns = read_scalar<std::uint32_t>(
      pivot_input, offsets[1] + 4, "PQ mean columns");
  const auto chunk_rows = read_scalar<std::uint32_t>(
      pivot_input, offsets[2], "PQ chunk rows");
  const auto chunk_columns = read_scalar<std::uint32_t>(
      pivot_input, offsets[2] + 4, "PQ chunk columns");
  if (table_rows != 256 || table_dimension != expected_dimension ||
      mean_dimension != expected_dimension || mean_columns != 1 ||
      chunk_rows != expected_chunks + 1 || chunk_columns != 1) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ pivot contents differ from Artifact metadata");
  }
  std::uint32_t prior = 0;
  for (std::uint32_t index = 0; index < chunk_rows; ++index) {
    const auto boundary = read_scalar<std::uint32_t>(
        pivot_input, offsets[2] + 8 + index * sizeof(std::uint32_t),
        "PQ chunk boundary");
    if ((index == 0 && boundary != 0) || boundary < prior ||
        boundary > expected_dimension) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "PQ chunk boundaries are invalid");
    }
    prior = boundary;
  }
  if (prior != expected_dimension) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ chunks do not cover the declared dimension");
  }

  std::ifstream codes_input(compressed, std::ios::binary);
  const auto points =
      read_scalar<std::uint32_t>(codes_input, 0, "PQ code point count");
  const auto chunks =
      read_scalar<std::uint32_t>(codes_input, 4, "PQ code chunk count");
  if (points != expected_points || chunks != expected_chunks ||
      native_file_size(compressed) !=
          8ULL + static_cast<std::uint64_t>(points) * chunks) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "PQ compressed header or byte length is inconsistent");
  }
  if (!opq &&
      std::filesystem::exists(pivots.string() + "_rotation_matrix.bin")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OPQ rotation payload is forbidden when opq=false");
  }
}

void validate_disk_layout_native_payload(
    const std::filesystem::path& disk_index, NodeCount expected_points,
    std::uint32_t expected_dimension, std::uint32_t expected_max_degree,
    bool reorder, std::uint64_t frozen_point_count) {
  constexpr std::uint64_t kSectorBytes = 4096;
  std::ifstream input(disk_index, std::ios::binary);
  const auto metadata_rows =
      read_scalar<std::uint32_t>(input, 0, "disk metadata rows");
  const auto metadata_columns =
      read_scalar<std::uint32_t>(input, 4, "disk metadata columns");
  if (metadata_rows != 9 || metadata_columns != 1) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "full-precision disk metadata must contain nine values");
  }
  std::vector<std::uint64_t> metadata(metadata_rows);
  input.seekg(8);
  input.read(reinterpret_cast<char*>(metadata.data()),
             static_cast<std::streamsize>(metadata.size() * sizeof(std::uint64_t)));
  const auto actual_bytes = native_file_size(disk_index);
  const auto expected_record_bytes =
      static_cast<std::uint64_t>(expected_dimension) * sizeof(float) +
      (static_cast<std::uint64_t>(expected_max_degree) + 1) *
          sizeof(std::uint32_t);
  if (!input || metadata[0] != expected_points ||
      metadata[1] != expected_dimension || metadata[3] != expected_record_bytes ||
      metadata[4] != kSectorBytes / expected_record_bytes ||
      metadata[5] != frozen_point_count || metadata[7] != (reorder ? 1U : 0U) ||
      metadata[8] != actual_bytes || actual_bytes < 2 * kSectorBytes ||
      actual_bytes % kSectorBytes != 0 || reorder || frozen_point_count != 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "disk layout header violates full-precision MVP semantics");
  }
  if (!is_valid_global_node_id(static_cast<GlobalNodeId>(metadata[2]),
                               expected_points)) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "disk layout medoid is outside GlobalNodeId range");
  }
  const auto nodes_per_sector = metadata[4];
  for (NodeCount node = 0; node < expected_points; ++node) {
    const auto record = kSectorBytes + (node / nodes_per_sector) * kSectorBytes +
                        (node % nodes_per_sector) * expected_record_bytes;
    for (std::uint32_t dimension = 0; dimension < expected_dimension;
         ++dimension) {
      const auto scalar = read_scalar<float>(
          input, record + dimension * sizeof(float), "disk vector scalar");
      if (!std::isfinite(scalar)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "disk vector contains a non-finite value");
      }
    }
    const auto degree = read_scalar<std::uint32_t>(
        input, record + expected_dimension * sizeof(float), "disk degree");
    if (degree == 0 || degree > expected_max_degree) {
      throw PipelineError(ErrorCategory::kDegreeExceeded,
                          "disk graph degree is zero or exceeds the declaration");
    }
    std::set<GlobalNodeId> neighbors;
    for (std::uint32_t edge = 0; edge < degree; ++edge) {
      const auto neighbor = read_scalar<GlobalNodeId>(
          input,
          record + expected_dimension * sizeof(float) + sizeof(std::uint32_t) +
              edge * sizeof(GlobalNodeId),
          "disk neighbor");
      if (!is_valid_global_node_id(neighbor, expected_points) ||
          neighbor == node || !neighbors.insert(neighbor).second) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "disk graph neighbor is invalid");
      }
    }
  }
}

void validate_medoids_native_payload(const std::filesystem::path& medoids,
                                     std::uint32_t expected_count,
                                     NodeCount global_node_count) {
  require_matrix(medoids, expected_count, 1, sizeof(std::uint32_t));
  std::ifstream input(medoids, std::ios::binary);
  std::set<GlobalNodeId> values;
  for (std::uint32_t index = 0; index < expected_count; ++index) {
    const auto value = read_scalar<GlobalNodeId>(
        input, 8 + index * sizeof(GlobalNodeId), "medoid");
    if (!is_valid_global_node_id(value, global_node_count) ||
        !values.insert(value).second) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "medoid is duplicated or outside GlobalNodeId range");
    }
  }
}

void validate_centroids_native_payload(const std::filesystem::path& centroids,
                                       std::uint32_t expected_count,
                                       std::uint32_t dimension) {
  require_matrix(centroids, expected_count, dimension, sizeof(float));
}

}  // namespace diskann_pipeline::search
