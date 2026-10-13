#pragma once

#include "baseline/cpp_main/source_build_identity.hpp"
#include "diskann_pipeline/registry.hpp"

namespace diskann_pipeline::diskann_kmeans {

inline constexpr const char* kImplementationId =
    "diskann_78256bbab4685e1774e78d331e081a153be26823_kmeans_configurable_k_v1";
inline constexpr const char* kImplementationVersion = "1.0.0";

struct Parameters {
  std::uint32_t k;
  std::size_t max_k_means_reps{15};
};

ImplementationDescriptor descriptor();
Parameters parse_parameters(const std::string& json, NodeCount dataset_nodes);
double sample_probability(NodeCount dataset_nodes);
void validate_training_sample_count(std::size_t samples, std::uint32_t k);
// Validate the native files before publishing an Artifact. No repair or sorting.
std::vector<NodeCount> validate_native_output(
    const std::filesystem::path& prefix, const DatasetIdentity& dataset,
    std::uint32_t k);
std::unique_ptr<PartitionStrategy> make_partition(cpp_main::SourceBuildEvidence evidence);
BuildManifest build_manifest(const cpp_main::SourceBuildEvidence& evidence,
                             const std::string& run_id);
std::string now_utc();

}  // namespace diskann_pipeline::diskann_kmeans
