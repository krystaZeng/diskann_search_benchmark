#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

namespace dp = diskann_pipeline;

namespace {

dp::PipelineExecutionResult cancelled_at(std::size_t observation,
                                         const std::string& identity) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  environment.cancellation().cancel_on_observation(observation);
  auto context = environment.make_context(identity + "-context");
  return dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(identity, dataset), context);
}

}  // namespace

int main() {
  std::ifstream inventory(std::filesystem::path(DISKANN_PIPELINE_TEST_SOURCE_DIR) /
                          "fixtures/fake_pipeline/negative/cases.json");
  const auto matrix = nlohmann::json::parse(inventory);
  DP_EXPECT_EQ(matrix.at("cases").size(), std::size_t{29});
  std::set<std::string> identities;
  for (const auto& item : matrix.at("cases")) {
    identities.insert(item.at("id").get<std::string>());
    DP_EXPECT_TRUE(!item.at("category").get<std::string>().empty());
    DP_EXPECT_TRUE(!item.at("gate").get<std::string>().empty());
  }
  DP_EXPECT_EQ(identities.size(), std::size_t{29});

  const std::vector<std::size_t> cancellation_checkpoints{1, 2, 3, 4, 5,
                                                          6, 10, 11, 12};
  for (const auto checkpoint : cancellation_checkpoints) {
    const auto result =
        cancelled_at(checkpoint, "matrix-cancel-" + std::to_string(checkpoint));
    DP_EXPECT_EQ(result.status, dp::StageStatus::kCancelled);
    DP_EXPECT_TRUE(!result.artifact.has_value());
    DP_EXPECT_TRUE(!result.quarantine_id.has_value());
    if (checkpoint <= 3) {
      DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
      DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
    } else if (checkpoint <= 6) {
      DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
    }
  }

  dp::test::FakeStageEnvironment invalid_environment("matrix-invalid");
  const auto dataset =
      dp::test::materialize_fake_dataset(invalid_environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  auto invalid_request =
      dp::test::canonical_execution_request("matrix-invalid", dataset);
  invalid_request.merge_parameters_json = "{\"mode\":\"invalid_output\"}";
  auto context = invalid_environment.make_context("matrix-invalid-context");
  const auto invalid =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(invalid_request, context);
  DP_EXPECT_EQ(invalid.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(invalid.quarantine_id.has_value());
  DP_EXPECT_TRUE(!invalid.artifact.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(
      invalid_environment.workspace().normal_root / "matrix-invalid/merge"));

  auto incompatible =
      dp::test::canonical_execution_request("matrix-incompatible", dataset);
  incompatible.local_implementation_id = "not_registered";
  const auto rejected =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(incompatible, context);
  DP_EXPECT_EQ(rejected.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(rejected.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(rejected.invocations.merge, std::size_t{0});
  return dp::test::finish();
}
