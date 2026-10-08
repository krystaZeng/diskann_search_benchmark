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

ImplementationDescriptor p0_descriptor();
ImplementationDescriptor b0_descriptor();
ImplementationDescriptor m0_descriptor();
CompatibilityResult b0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context);
CompatibilityResult m0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context);

std::unique_ptr<PartitionStrategy> make_p0_partition();
std::unique_ptr<LocalGraphBuilder> make_b0_local_build();
std::unique_ptr<GraphMerger> make_m0_merge();
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
