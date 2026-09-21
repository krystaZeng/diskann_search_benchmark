#include "diskann_pipeline/validation.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <algorithm>

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

void check_ascending_claim(bool native_map, bool ascending, bool declared) {
  namespace fixture = dp::cpp_main::test;
  fixture::TemporaryDirectory temporary("map-ascending-claim");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace, 3, 2);
  const std::vector<dp::GlobalNodeId> map = ascending
      ? std::vector<dp::GlobalNodeId>{0, 1, 2}
      : std::vector<dp::GlobalNodeId>{1, 2, 0};
  // Keep the first map ascending to catch checks that inspect only one shard.
  const std::vector<std::vector<dp::GlobalNodeId>> maps{{0, 1, 2}, map};
  auto partition = fixture::materialize_partition(workspace, dataset, maps);
  dp::test::mutate_metadata(workspace, partition, [&](auto& metadata) {
    if (!declared) {
      auto& caps = metadata.at("capabilities");
      caps.erase(std::remove(caps.begin(), caps.end(),
                              "partition.local_to_global_ascending/v1"),
                 caps.end());
    }
    if (!native_map) {
      for (std::size_t index = 0; index < maps.size(); ++index) {
        const auto name = "map-" + std::to_string(index) + ".json";
        const auto path = workspace.normal_root / "partition" / name;
        fixture::write_text(
            path, fixture::Json{{"local_to_global", maps[index]}}.dump());
        metadata["payloads"][index] = fixture::payload(
            "partition/" + std::to_string(index) + "/local_to_global_map",
            name, "application/json", path);
      }
    }
  });
  const auto dataset_result = dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_result.valid());
  if (!dataset_result.valid()) return;
  const auto result = dp::Validator(workspace).validate(
      partition, {*dataset_result.validated_artifact});
  std::cout << "ascending-claim native=" << native_map
            << " ascending=" << ascending << " declared=" << declared
            << " valid=" << result.valid() << '\n';
  if (declared && !ascending) {
    DP_EXPECT_TRUE(!result.validated_artifact.has_value());
    DP_EXPECT_EQ(result.evidence.failure_category,
                 std::optional<dp::ErrorCategory>{dp::ErrorCategory::kMappingIncomplete});
  } else {
    // Generic Partitions do not acquire M0's ascending requirement.
    DP_EXPECT_TRUE(result.valid());
  }
}

}  // namespace

int main() {
  for (bool native_map : {false, true}) {
    for (bool ascending : {false, true}) {
      for (bool declared : {false, true}) {
        check_ascending_claim(native_map, ascending, declared);
      }
    }
  }
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
