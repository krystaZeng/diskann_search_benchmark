#include "diskann_pipeline/reuse.hpp"

#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

int main() {
  dp::test::ContractArtifactFixture fixture;
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  DP_EXPECT_TRUE(dataset.valid());

  const dp::ArtifactRequirement requirement{
      dp::ArtifactType::kPartition, {"partition-artifact/v1"},
      dp::IdSpace::kLocal};
  dp::ArtifactReuseService reuse(validator);
  int producer_invocations = 0;
  const auto accepted = reuse.resolve(
      fixture.partition, requirement,
      {"partition.complete_local_to_global_map/v1"},
      {*dataset.validated_artifact});
  DP_EXPECT_EQ(producer_invocations, 0);
  DP_EXPECT_EQ(accepted.status, dp::StageStatus::kReused);
  DP_EXPECT_TRUE(accepted.validated_artifact.has_value());
  DP_EXPECT_TRUE(accepted.provenance.reused);
  DP_EXPECT_EQ(accepted.provenance.source_artifact->aggregate_checksum,
               fixture.partition.aggregate_checksum);

  const auto before = dp::sha256_file(
      dp::test::metadata_path(fixture.workspace, fixture.partition));
  auto wrong_requirement = requirement;
  wrong_requirement.accepted_format_versions = {"partition-artifact/v2"};
  const auto mismatch = reuse.resolve(
      fixture.partition, wrong_requirement,
      {"partition.complete_local_to_global_map/v1"},
      {*dataset.validated_artifact});
  DP_EXPECT_EQ(mismatch.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(mismatch.failure->category, dp::ErrorCategory::kReuseMismatch);
  DP_EXPECT_EQ(before, dp::sha256_file(
                           dp::test::metadata_path(fixture.workspace,
                                                   fixture.partition)));

  const auto partition_path =
      dp::test::metadata_path(fixture.workspace, fixture.partition);
  const auto payload_path = partition_path.parent_path() / "p0-vectors.json";
  dp::test::overwrite_file(payload_path, std::string(37, 'x'));
  const auto stale = reuse.resolve(
      fixture.partition, requirement,
      {"partition.complete_local_to_global_map/v1"},
      {*dataset.validated_artifact});
  DP_EXPECT_EQ(stale.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!stale.validated_artifact.has_value());
  DP_EXPECT_EQ(stale.failure->category, dp::ErrorCategory::kChecksumMismatch);
  DP_EXPECT_EQ(producer_invocations, 0);

  return dp::test::finish();
}
