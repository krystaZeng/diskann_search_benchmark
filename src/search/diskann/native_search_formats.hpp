#pragma once

#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/identifiers.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace diskann_pipeline::search {

inline constexpr const char* kPqPivotsMediaType =
    "application/vnd.diskann.pq-pivots+binary;version=1";
inline constexpr const char* kPqCodesMediaType =
    "application/vnd.diskann.pq-codes+binary;version=1";
inline constexpr const char* kDiskIndexMediaType =
    "application/vnd.diskann.pqflash-disk-index+binary;version=1";
inline constexpr const char* kUint32MatrixMediaType =
    "application/vnd.diskann.uint32-matrix+binary;version=1";
inline constexpr const char* kFloat32MatrixMediaType =
    "application/vnd.diskann.float32-matrix+binary;version=1";

enum class NativeAliasMethod {
  kHardLink,
  kCopy,
};

using NativeHardLinkAttempt = std::function<std::error_code(
    const std::filesystem::path&, const std::filesystem::path&)>;

NativeAliasMethod stage_native_payload_alias(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const NativeHardLinkAttempt& hard_link_attempt = {});

struct SearchAdmissionEvidenceRef {
  std::filesystem::path path;
  std::string sha256;
  ArtifactRef expected_artifact;
};

struct SearchArtifactAdmission {
  ArtifactRef reference;
  DatasetIdentity dataset;
  IdSpace id_space{IdSpace::kNone};
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  std::filesystem::path metadata_file;
};

SearchArtifactAdmission admit_search_artifact_metadata(
    const WorkspaceRef& workspace, const ArtifactRef& reference);

SearchArtifactAdmission admit_prevalidated_search_artifact(
    const WorkspaceRef& workspace, const ArtifactRef& reference,
    const SearchAdmissionEvidenceRef& evidence);

void validate_search_artifact_lineage(
    const SearchArtifactAdmission& dataset,
    const SearchArtifactAdmission& global_graph,
    const SearchArtifactAdmission& pq,
    const SearchArtifactAdmission& disk_layout);

struct SearchRunnerExecutionIdentity {
  std::filesystem::path binary_path;
  std::string binary_sha256;
  std::filesystem::path build_manifest_path;
  std::string build_manifest_sha256;
  SourceIdentity project_source;
  std::string implementation_id;
  std::string implementation_version;
  std::string reproduction_mode;
  std::string diskann_source_commit;
};

void validate_search_runner_execution_identity(
    const WorkspaceRef& workspace,
    const std::filesystem::path& execution_root,
    const std::filesystem::path& actual_executable,
    const SearchRunnerExecutionIdentity& declaration);

void validate_pq_native_payloads(const std::filesystem::path& pivots,
                                 const std::filesystem::path& compressed,
                                 NodeCount expected_points,
                                 std::uint32_t expected_dimension,
                                 std::uint32_t expected_chunks,
                                 bool opq);

void validate_disk_layout_native_payload(
    const std::filesystem::path& disk_index, NodeCount expected_points,
    std::uint32_t expected_dimension, std::uint32_t expected_max_degree,
    bool reorder, std::uint64_t frozen_point_count);

void validate_medoids_native_payload(const std::filesystem::path& medoids,
                                     std::uint32_t expected_count,
                                     NodeCount global_node_count);

void validate_centroids_native_payload(const std::filesystem::path& centroids,
                                       std::uint32_t expected_count,
                                       std::uint32_t dimension);

}  // namespace diskann_pipeline::search
