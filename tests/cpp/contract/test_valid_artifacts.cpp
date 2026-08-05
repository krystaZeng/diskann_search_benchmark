#include "diskann_pipeline/validation.hpp"

#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <type_traits>

namespace dp = diskann_pipeline;

int main() {
  static_assert(!std::is_default_constructible_v<dp::ValidatedArtifact>);
  dp::test::ContractArtifactFixture fixture;
  dp::Validator validator(fixture.workspace);

  const auto dataset = validator.validate(fixture.dataset);
  DP_EXPECT_TRUE(dataset.valid());
  DP_EXPECT_EQ(dataset.validated_artifact->id_space(), dp::IdSpace::kNone);

  const auto partition =
      validator.validate(fixture.partition, {*dataset.validated_artifact});
  DP_EXPECT_TRUE(partition.valid());
  DP_EXPECT_TRUE(partition.validated_artifact->has_capability(
      "partition.complete_local_to_global_map/v1"));

  const auto local0 =
      validator.validate(fixture.local0, {*partition.validated_artifact});
  const auto local1 =
      validator.validate(fixture.local1, {*partition.validated_artifact});
  DP_EXPECT_TRUE(local0.valid());
  DP_EXPECT_TRUE(local1.valid());
  DP_EXPECT_EQ(local0.validated_artifact->partition_id().value(),
               dp::PartitionId{0});
  DP_EXPECT_EQ(local1.validated_artifact->partition_id().value(),
               dp::PartitionId{1});

  const auto global = validator.validate(
      fixture.global,
      {*partition.validated_artifact, *local0.validated_artifact,
       *local1.validated_artifact});
  DP_EXPECT_TRUE(global.valid());
  DP_EXPECT_EQ(global.validated_artifact->dataset().global_node_count,
               dp::NodeCount{6});
  DP_EXPECT_TRUE(global.validated_artifact->has_capability(
      "graph.global_adjacency/v1"));
  DP_EXPECT_EQ(global.evidence.result, dp::ValidationResult::kValid);

  return dp::test::finish();
}
