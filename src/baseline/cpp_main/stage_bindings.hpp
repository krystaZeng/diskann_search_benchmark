#pragma once

#include "diskann_pipeline/registry.hpp"
#include "diskann_pipeline/validation.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace diskann_pipeline::cpp_main {

inline constexpr const char* kP0ImplementationId =
    "cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_callable_v1";
inline constexpr const char* kB0ImplementationId =
    "cpp_main_78256bbab4685e1774e78d331e081a153be26823_b0_callable_v1";
inline constexpr const char* kM0ImplementationId =
    "cpp_main_78256bbab4685e1774e78d331e081a153be26823_m0_callable_v1";

// Shared preflight uses the same frozen rules as the native stage.
void validate_p0_parameters(const std::string& json, NodeCount global_node_count);
void validate_b0_parameters(const std::string& json);
void validate_m0_parameters(const std::string& json);

// Version selection is explicit. These parsers are shared by entry and factory.
struct P0Parameters {
  std::uint32_t r;
  std::uint32_t local_degree;
  double build_dram_budget_gib;
  std::uint32_t replication_factor;
  double sample_probability;
};
struct B0Parameters {
  std::uint32_t r;
  std::uint32_t l;
  std::uint32_t local_degree;
};
P0Parameters parse_p0_parameters(const std::string& json, NodeCount nodes,
                                 const std::string& version);
B0Parameters parse_b0_parameters(const std::string& json, const std::string& version);

ImplementationDescriptor p0_descriptor();
ImplementationDescriptor b0_descriptor();
ImplementationDescriptor m0_descriptor();
ImplementationDescriptor p0_descriptor_v1_1();
ImplementationDescriptor b0_descriptor_v1_1();
CompatibilityResult b0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context);
CompatibilityResult m0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context);

std::unique_ptr<PartitionStrategy> make_p0_partition();
std::unique_ptr<LocalGraphBuilder> make_b0_local_build();
std::unique_ptr<GraphMerger> make_m0_merge();
std::unique_ptr<PartitionStrategy> make_p0_partition_v1_1();
std::unique_ptr<LocalGraphBuilder> make_b0_local_build_v1_1();
void register_cpp_main_stage_bindings(ImplementationRegistry& registry);

class CppMainVectorAccessor final : public VectorAccessor {
 public:
  CppMainVectorAccessor(const WorkspaceRef& workspace,
                        const ArtifactRef& dataset);

  std::size_t dimension() const noexcept override;
  void read(GlobalNodeId id, std::vector<float>& destination) const override;
  const ValidatedArtifact* bound_dataset() const noexcept override {
    return validated_dataset_ ? &*validated_dataset_ : nullptr;
  }

  const ArtifactRef& dataset_reference() const noexcept;
  const DatasetIdentity& dataset_identity() const noexcept;
  const ValidatedArtifact& validated_dataset() const;
  const std::filesystem::path& native_payload_path() const noexcept;

 private:
  ArtifactRef dataset_reference_;
  DatasetIdentity dataset_identity_;
  std::optional<ValidatedArtifact> validated_dataset_;
  std::filesystem::path native_payload_path_;
};

}  // namespace diskann_pipeline::cpp_main
