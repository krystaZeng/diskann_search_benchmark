#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

int main() {
  dp::test::FakeStageEnvironment environment("orchestration");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto context = environment.make_context("executor");
  const auto result = executor.execute(
      dp::test::canonical_execution_request("orchestration-run", dataset),
      context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{2});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{1});
  DP_EXPECT_EQ(result.local_partition_ids,
               std::vector<dp::PartitionId>({0, 1}));
  DP_EXPECT_EQ(result.stage_order,
               std::vector<dp::StageKind>({dp::StageKind::kPartition,
                                           dp::StageKind::kLocalBuild,
                                           dp::StageKind::kLocalBuild,
                                           dp::StageKind::kMerge}));
  DP_EXPECT_TRUE(result.artifact.has_value());
  DP_EXPECT_EQ(result.artifact->aggregate_checksum,
               std::string(dp::test::kGlobalAggregateChecksum));
  DP_EXPECT_TRUE(std::filesystem::exists(
      dp::resolve_normal_artifact_metadata(environment.workspace(),
                                           result.artifact->metadata_path)));
  DP_EXPECT_EQ(result.validation_evidence_paths.size(), std::size_t{4});
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{4});
  return dp::test::finish();
}
