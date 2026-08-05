#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace dp = diskann_pipeline;

int main() {
  const auto partition = dp::test::fake_partition_descriptor();
  const auto local = dp::test::fake_local_graph_descriptor();
  const auto merge = dp::test::fake_merge_descriptor();
  DP_EXPECT_EQ(partition.implementation_id, std::string("fake_partition_v1"));
  DP_EXPECT_EQ(local.implementation_id, std::string("fake_local_graph_v1"));
  DP_EXPECT_EQ(merge.implementation_id, std::string("fake_merge_v1"));
  DP_EXPECT_TRUE(partition.test_only && local.test_only && merge.test_only);
  DP_EXPECT_EQ(local.provides.output_id_space, dp::IdSpace::kLocal);
  DP_EXPECT_EQ(merge.provides.output_id_space, dp::IdSpace::kGlobal);
  DP_EXPECT_TRUE(!partition.parameter_schema_path.is_absolute());
  DP_EXPECT_TRUE(!local.parameter_schema_path.is_absolute());
  DP_EXPECT_TRUE(!merge.parameter_schema_path.is_absolute());
  DP_EXPECT_EQ(merge.requires.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_TRUE(std::any_of(
      merge.requires.input_artifacts.begin(), merge.requires.input_artifacts.end(),
      [](const auto& input) {
        return input.artifact_type == dp::ArtifactType::kPartition;
      }));
  DP_EXPECT_TRUE(std::any_of(
      merge.requires.input_artifacts.begin(), merge.requires.input_artifacts.end(),
      [](const auto& input) {
        return input.artifact_type == dp::ArtifactType::kLocalGraph;
      }));

  dp::test::FakeStageEnvironment environment("stage-contracts");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto request = dp::test::canonical_execution_request("contract-run", dataset);
  auto context = environment.make_context("executor");
  const auto result = executor.execute(request, context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.local_partition_ids,
               std::vector<dp::PartitionId>({0, 1}));
  DP_EXPECT_EQ(result.local_artifacts.size(), std::size_t{2});
  DP_EXPECT_EQ(result.local_artifacts[0].artifact_type,
               dp::ArtifactType::kLocalGraph);
  for (std::size_t index = 0; index < result.local_artifacts.size(); ++index) {
    std::ifstream metadata(dp::resolve_normal_artifact_metadata(
        environment.workspace(), result.local_artifacts[index].metadata_path));
    const auto document = nlohmann::json::parse(metadata);
    DP_EXPECT_EQ(document.at("structural").at("partition_id")
                     .get<dp::PartitionId>(),
                 result.local_partition_ids[index]);
    DP_EXPECT_EQ(document.at("structural")
                     .at("source_partition_checksum")
                     .get<std::string>(),
                 std::string(dp::test::kPartitionAggregateChecksum));
    DP_EXPECT_EQ(document.at("payloads").at(0).at("role"),
                 nlohmann::json("local_to_global_map"));
  }

  return dp::test::finish();
}
