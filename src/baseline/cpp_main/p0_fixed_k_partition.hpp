#pragma once

#include "baseline/cpp_main/source_build_identity.hpp"
#include "diskann_pipeline/registry.hpp"

namespace diskann_pipeline::cpp_main::fixed_k {

inline constexpr const char* kImplementationId =
    "cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_fixed_k_v1";
inline constexpr const char* kImplementationVersion = "1.0.0";

struct Parameters { std::uint32_t k; };
ImplementationDescriptor descriptor();
Parameters parse_parameters(const std::string& json, NodeCount dataset_nodes,
                            const std::string& version = kImplementationVersion);
double sample_probability(NodeCount dataset_nodes);
void validate_native_shape(NodeCount dataset_nodes, std::uint32_t dimension, std::uint32_t k);
void validate_training_sample_count(std::size_t samples, std::uint32_t k);
// Pre-publication checks specific to this implementation; never repair output.
std::vector<NodeCount> validate_native_output(const std::filesystem::path& prefix,
                                             const DatasetIdentity& dataset, std::uint32_t k);
std::unique_ptr<PartitionStrategy> make_partition(SourceBuildEvidence evidence,
                                                 const std::string& version = kImplementationVersion);

}  // namespace diskann_pipeline::cpp_main::fixed_k
