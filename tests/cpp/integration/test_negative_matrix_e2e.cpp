#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

namespace dp = diskann_pipeline;

int main() {
  std::ifstream inventory(std::filesystem::path(DISKANN_PIPELINE_TEST_SOURCE_DIR) /
                          "fixtures/fake_pipeline/negative/cases.json");
  const auto matrix = nlohmann::json::parse(inventory);
  DP_EXPECT_EQ(matrix.at("cases").size(), std::size_t{29});
  std::set<std::string> identities;
  for (const auto& item : matrix.at("cases")) {
    const auto id = item.at("id").get<std::string>();
    const auto expected_gate = item.at("gate").get<std::string>();
    const auto expected_category = dp::error_category_from_string(
        item.at("category").get<std::string>());
    identities.insert(id);
    const auto observed = dp::test::execute_negative_case(id);
    DP_EXPECT_EQ(observed.category, expected_category);
    DP_EXPECT_EQ(observed.gate, expected_gate);
    DP_EXPECT_TRUE(!observed.normal_artifact_published);
    DP_EXPECT_EQ(observed.evaluation_invocations, std::size_t{0});
    DP_EXPECT_EQ(observed.reuse_invocations, std::size_t{0});
    DP_EXPECT_EQ(observed.aggregation_invocations, std::size_t{0});

    const bool artifact_gate = expected_gate == "artifact_validation" ||
                               expected_gate == "local_input";
    DP_EXPECT_EQ(observed.quarantined, artifact_gate);
    if (expected_gate == "preflight" || expected_gate == "registry" ||
        expected_gate == "reuse" || artifact_gate) {
      DP_EXPECT_EQ(observed.invocations.partition, std::size_t{0});
      DP_EXPECT_EQ(observed.invocations.local_build, std::size_t{0});
      DP_EXPECT_EQ(observed.invocations.merge, std::size_t{0});
    }
    if (expected_category == dp::ErrorCategory::kCancelled) {
      DP_EXPECT_EQ(observed.status, dp::StageStatus::kCancelled);
      DP_EXPECT_TRUE(!observed.quarantined);
      if (expected_gate == "stage_start") {
        DP_EXPECT_EQ(observed.invocations.partition, std::size_t{0});
      } else {
        DP_EXPECT_EQ(observed.invocations.partition, std::size_t{1});
      }
      if (expected_gate == "next_stage_start")
        DP_EXPECT_EQ(observed.invocations.local_build, std::size_t{0});
      if (expected_gate.find("local.") == 0)
        DP_EXPECT_EQ(observed.invocations.local_build, std::size_t{1});
      if (expected_gate.find("merge.") == 0) {
        DP_EXPECT_EQ(observed.invocations.local_build, std::size_t{2});
        DP_EXPECT_EQ(observed.invocations.merge, std::size_t{1});
      }
      if (expected_gate.find("partition") != std::string::npos ||
          expected_gate == "stage_start") {
        DP_EXPECT_EQ(observed.invocations.local_build, std::size_t{0});
        DP_EXPECT_EQ(observed.invocations.merge, std::size_t{0});
      } else if (expected_gate.find("local") != std::string::npos ||
                 expected_gate == "next_stage_start") {
        DP_EXPECT_EQ(observed.invocations.merge, std::size_t{0});
      }
    }
    if (expected_gate == "reuse") {
      DP_EXPECT_TRUE(observed.immutable_reuse_preserved);
    }
  }
  DP_EXPECT_EQ(identities.size(), std::size_t{29});
  return dp::test::finish();
}
