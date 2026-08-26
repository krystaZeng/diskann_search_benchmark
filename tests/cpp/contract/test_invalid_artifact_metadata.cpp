#include "diskann_pipeline/validation.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "search/search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

#include <functional>

namespace dp = diskann_pipeline;

namespace {

dp::ArtifactValidationResult validate_partition(dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  return validator.validate(fixture.partition, {*dataset.validated_artifact});
}

dp::ArtifactValidationResult validate_direct_v2(
    const std::function<void(dp::test::Json&)>& mutation,
    bool recompute = true) {
  diskann_pipeline::cpp_main::test::TemporaryDirectory temporary(
      "direct-native-v2-invalid");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  diskann_pipeline::cpp_main::test::NativeDatasetFixture dataset(workspace, 4,
                                                                  2);
  auto graph =
      diskann_pipeline::cpp_main::test::materialize_direct_native_global_v2(
          workspace, dataset, {{1, 2}, {0, 3}, {0, 3}, {1, 2}}, {0, 2});
  dp::test::mutate_metadata(workspace, graph, mutation, recompute);
  dp::Validator validator(workspace);
  const auto validated_dataset = validator.validate(dataset.reference);
  return validator.validate(graph, {*validated_dataset.validated_artifact});
}

}  // namespace

int main() {
  {
    dp::test::ContractArtifactFixture fixture;
    std::filesystem::remove(dp::test::metadata_path(fixture.workspace,
                                                    fixture.partition)
                                .parent_path() /
                            "p0-map.json");
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kPayloadMissing);
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["format_version"] = "global-graph-artifact/v1"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["lineage_mode"] = "other"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["source_artifacts"] = dp::test::Json::array(); });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2([](auto& json) {
      json["dataset"]["dataset_checksum"] = std::string(64, '0');
    });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["observed_max_degree"] = 1; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["medoid_count"] = 3; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["payloads"][1]["role"] = "centroids"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.base.workspace, fixture.pq,
        [](auto& json) { json["payloads"][0]["path"] = "../escape.bin"; });
    dp::Validator validator(fixture.base.workspace);
    const auto dataset = validator.validate(fixture.base.dataset);
    const auto rejected =
        validator.validate(fixture.pq, {*dataset.validated_artifact});
    DP_EXPECT_EQ(rejected.evidence.failure_category.value(),
                 dp::ErrorCategory::kPathEscape);
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    fixture.layout.format_version = "disk-layout-artifact/v2";
    dp::Validator validator(fixture.base.workspace);
    const auto dataset = validator.validate(fixture.base.dataset);
    const auto rejected =
        validator.validate(fixture.layout, {*dataset.validated_artifact});
    DP_EXPECT_EQ(rejected.evidence.failure_category.value(),
                 dp::ErrorCategory::kUnsupportedVersion);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["payloads"][0]["byte_length"] = 38; });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kPayloadLengthMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["payloads"][0]["sha256"] = std::string(64, '0'); });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kChecksumMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.aggregate_checksum = std::string(64, '0');
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kChecksumMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.format_version = "partition-artifact/v2";
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kUnsupportedVersion);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.artifact_type = dp::ArtifactType::kLocalGraph;
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kSchemaInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["id_space"] = "global"; });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdSpaceMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) {
          json["source_artifacts"][0]["aggregate_checksum"] =
              std::string(64, '0');
        });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kSourceMismatch);
  }

  return dp::test::finish();
}
