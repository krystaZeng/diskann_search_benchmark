#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/artifacts.hpp"
#include "diskann_pipeline/evaluation.hpp"
#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/registry.hpp"
#include "diskann_pipeline/stage_context.hpp"
#include "diskann_pipeline/stage_interfaces.hpp"

#include "support/test_assert.hpp"

#include <cstdint>
#include <filesystem>
#include <type_traits>
#include <utility>
#include <vector>

namespace dp = diskann_pipeline;

namespace {

class ContractMerger final : public dp::GraphMerger {
 public:
  dp::StageResult merge(
      const dp::ArtifactRef&,
      const std::vector<dp::ArtifactRef>&,
      const std::filesystem::path&,
      dp::StageContext&) override {
    return {dp::StageStatus::kFailed, std::nullopt, "manifests/failure.json"};
  }
};

class ContractLocalBuilder final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef&, dp::PartitionId,
                        const dp::LocalBuildConfig&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kCancelled, std::nullopt,
            "manifests/cancelled.json"};
  }
};

}  // namespace

int main() {
  static_assert(__cplusplus >= 201703L, "public contract requires C++17");
  static_assert(std::is_same_v<dp::GlobalNodeId, std::uint32_t>);
  static_assert(std::is_same_v<dp::LocalNodeId, std::uint32_t>);
  static_assert(std::is_same_v<dp::PartitionId, std::uint32_t>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::PQArtifactMetadata>().pq_chunks),
                std::uint32_t>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::DiskLayoutArtifactMetadata>()
                             .declared_max_degree),
                std::uint32_t>);
  static_assert(std::is_constructible_v<dp::ImplementationRegistry,
                                        dp::RegistryAdmissionPolicy>);
  static_assert(!std::is_default_constructible_v<dp::ImplementationRegistry>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::ContractRequirement>().input_artifacts),
                std::vector<dp::ArtifactRequirement>>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::ContractProvide>().artifact_type),
                dp::ArtifactType>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::StageSourceIdentities>().partition),
                dp::SourceIdentity>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::PipelineExecutionRequest>()
                             .stage_sources),
                std::optional<dp::StageSourceIdentities>>);
  static_assert(std::is_same_v<
                decltype(std::declval<dp::StageResult>().build_manifest_path),
                std::filesystem::path>);

  const dp::ArtifactRef ref{
      "artifacts/example/metadata.json", dp::ArtifactType::kDataset,
      "fake-dataset-json/v1",
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
  const std::vector<dp::ArtifactRef> refs{ref};
  DP_EXPECT_EQ(ref.metadata_path,
               std::filesystem::path("artifacts/example/metadata.json"));
  DP_EXPECT_EQ(refs.front().artifact_type, dp::ArtifactType::kDataset);
  DP_EXPECT_EQ(dp::to_string(dp::ArtifactType::kPQ), std::string_view("pq"));
  DP_EXPECT_EQ(dp::to_string(dp::ArtifactType::kDiskLayout),
               std::string_view("disk_layout"));

  ContractMerger merger;
  ContractLocalBuilder builder;
  static_cast<void>(merger);
  static_cast<void>(builder);
  return dp::test::finish();
}
