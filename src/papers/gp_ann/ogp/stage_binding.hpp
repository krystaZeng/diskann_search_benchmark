#pragma once

#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline::gp_ann_ogp {

inline constexpr const char* kImplementationId =
    "gp_ann_ogp_partition_v1";
inline constexpr const char* kImplementationVersion = "1.0.0";
inline constexpr const char* kRepository =
    "https://github.com/larsgottesbueren/gp-ann.git";
inline constexpr const char* kBranch = "main";
inline constexpr const char* kCommit =
    "df5efb4a6b303011c795666ed267f29d994b558c";

struct OgpParameters {
  std::uint32_t requested_num_clusters{0};
  double requested_overlap{0.0};
  std::string partitioning_mode;
};

struct GpAnnDependencyIdentity {
  std::filesystem::path path;
  std::string commit;
};

struct GpAnnSourceBuildAdmission {
  std::filesystem::path source_root;
  std::filesystem::path build_root;
  std::filesystem::path partition_executable;
  std::string partition_executable_sha256;
  std::string expected_repository{kRepository};
  std::string expected_branch{kBranch};
  std::string expected_commit{kCommit};
  std::vector<GpAnnDependencyIdentity> dependencies;
  ToolchainIdentity toolchain;
};

struct GpAnnSourceBuildEvidence {
  SourceIdentity source;
  std::filesystem::path source_root;
  std::filesystem::path build_root;
  std::filesystem::path partition_executable;
  std::string partition_executable_sha256;
  std::vector<GpAnnDependencyIdentity> dependencies;
  ToolchainIdentity toolchain;
};

struct OfficialPartitionInvocationResult {
  StageStatus process_status{StageStatus::kFailed};
  int exit_status{-1};
  std::vector<std::string> argv;
  std::filesystem::path staged_input_path;
  std::filesystem::path stdout_path;
  std::filesystem::path stderr_path;
  std::filesystem::path expected_output_path;
  std::optional<std::string> expected_output_sha256;
  std::filesystem::path build_manifest_path;
};

ImplementationDescriptor descriptor();
OgpParameters parse_parameters(const std::string& json,
                               NodeCount global_node_count);
GpAnnSourceBuildAdmission configured_source_build_admission();
GpAnnSourceBuildEvidence admit_source_and_build(
    const GpAnnSourceBuildAdmission& admission);
OfficialPartitionInvocationResult run_official_partition(
    const GpAnnSourceBuildEvidence& evidence,
    const DatasetIdentity& dataset,
    const std::filesystem::path& dataset_payload,
    std::uint64_t dataset_payload_length,
    const std::string& dataset_payload_sha256,
    const OgpParameters& parameters,
    const std::string& run_id,
    StageContext& context);

std::vector<std::vector<GlobalNodeId>> parse_and_canonicalize_clusters(
    const std::filesystem::path& cluster_output,
    NodeCount global_node_count);
ArtifactRef materialize_partition_candidate(
    const std::filesystem::path& stage_root,
    const ArtifactRef& dataset,
    const DatasetIdentity& dataset_identity,
    const std::vector<std::vector<GlobalNodeId>>& clusters);
std::unique_ptr<PartitionStrategy> make_ogp_partition(
    GpAnnSourceBuildEvidence evidence);

}  // namespace diskann_pipeline::gp_ann_ogp
