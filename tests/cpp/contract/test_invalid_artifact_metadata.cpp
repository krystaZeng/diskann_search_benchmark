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
