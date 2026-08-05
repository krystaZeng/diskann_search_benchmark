#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

namespace {

dp::PipelineExecutionResult run_with_cancel(std::size_t observation,
                                            const std::string& identity) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  environment.cancellation().cancel_on_observation(observation);
  auto context = environment.make_context("executor");
  return dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(identity, dataset), context);
}

}  // namespace

int main() {
  const auto partition_cancelled = run_with_cancel(1, "cancel-partition");
  DP_EXPECT_EQ(partition_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(partition_cancelled.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(partition_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partition_cancelled.artifact.has_value());

  const auto local_cancelled = run_with_cancel(4, "cancel-local");
  DP_EXPECT_EQ(local_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(local_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!local_cancelled.artifact.has_value());

  const auto merge_cancelled = run_with_cancel(10, "cancel-merge");
  DP_EXPECT_EQ(merge_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(merge_cancelled.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(!merge_cancelled.artifact.has_value());

  dp::test::FakeStageEnvironment invalid_environment("invalid-local");
  const auto dataset =
      dp::test::materialize_fake_dataset(invalid_environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  auto invalid_request =
      dp::test::canonical_execution_request("invalid-local", dataset);
  invalid_request.local_parameters_json = "{\"mode\":\"invalid_output\"}";
  auto context = invalid_environment.make_context("executor");
  const auto invalid =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(invalid_request, context);
  DP_EXPECT_EQ(invalid.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(invalid.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(invalid.quarantine_id.has_value());
  DP_EXPECT_TRUE(!invalid.artifact.has_value());

  auto incompatible_request =
      dp::test::canonical_execution_request("incompatible", dataset);
  incompatible_request.merge_implementation_id = "missing_merge";
  const auto incompatible =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(incompatible_request, context);
  DP_EXPECT_EQ(incompatible.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(incompatible.invocations.partition, std::size_t{0});

  return dp::test::finish();
}
