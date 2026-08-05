#include "diskann_pipeline/validation.hpp"

#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

namespace {

dp::ArtifactValidationResult validate_partition(dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  return validator.validate(fixture.partition, {*dataset.validated_artifact});
}

dp::ArtifactValidationResult validate_local(dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  const auto partition =
      validator.validate(fixture.partition, {*dataset.validated_artifact});
  return validator.validate(fixture.local0, {*partition.validated_artifact});
}

dp::ArtifactValidationResult validate_global(dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  const auto partition =
      validator.validate(fixture.partition, {*dataset.validated_artifact});
  const auto local0 =
      validator.validate(fixture.local0, {*partition.validated_artifact});
  const auto local1 =
      validator.validate(fixture.local1, {*partition.validated_artifact});
  return validator.validate(fixture.global,
                            {*partition.validated_artifact,
                             *local0.validated_artifact,
                             *local1.validated_artifact});
}

}  // namespace

int main() {
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.local0,
        [](auto& json) { json["structural"].erase("partition_id"); });
    DP_EXPECT_EQ(validate_local(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kSchemaInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.local0,
        [](auto& json) { json["structural"]["partition_id"] = 2; });
    DP_EXPECT_EQ(validate_local(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdRangeInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.local0,
        [](auto& json) { json["structural"]["partition_id"] = 1; });
    DP_EXPECT_EQ(validate_local(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kMappingIncomplete);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(fixture.workspace, fixture.partition,
                              "partition/0/local_to_global_map", "{}");
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kMappingIncomplete);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(
        fixture.workspace, fixture.partition,
        "partition/0/local_to_global_map", "{\"local_to_global\":[0,1,2]}");
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kMappingIncomplete);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(
        fixture.workspace, fixture.partition,
        "partition/0/local_to_global_map", "{\"local_to_global\":[0,1,2,6]}");
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdRangeInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(
        fixture.workspace, fixture.local0, "local_graph",
        "{\"adjacency\":[[4],[0],[1],[]]}");
    DP_EXPECT_EQ(validate_local(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdRangeInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(
        fixture.workspace, fixture.global, "global_graph",
        "{\"adjacency\":[[\"replica:0\"],[0],[1],[2],[3],[]]}");
    DP_EXPECT_EQ(validate_global(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdRangeInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(fixture.workspace, fixture.global, "global_graph",
                              "{\"adjacency\":[[1],[0],[1],[2],[3]]}");
    DP_EXPECT_EQ(validate_global(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kNodeCountMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::replace_payload(
        fixture.workspace, fixture.local0, "local_graph",
        "{\"adjacency\":[[1,2,3],[0],[1],[]]}");
    DP_EXPECT_EQ(validate_local(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kDegreeExceeded);
  }

  return dp::test::finish();
}
