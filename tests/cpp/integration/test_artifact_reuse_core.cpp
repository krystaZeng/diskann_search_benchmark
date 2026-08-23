#include "diskann_pipeline/reuse.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "search/search_artifact_test_support.hpp"
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

  {
    dp::test::ContractArtifactFixture global_fixture;
    dp::Validator global_validator(global_fixture.workspace);
    const auto global_dataset = global_validator.validate(global_fixture.dataset);
    const auto global_partition = global_validator.validate(
        global_fixture.partition, {*global_dataset.validated_artifact});
    const auto global_local0 = global_validator.validate(
        global_fixture.local0, {*global_partition.validated_artifact});
    const auto global_local1 = global_validator.validate(
        global_fixture.local1, {*global_partition.validated_artifact});
    DP_EXPECT_TRUE(global_dataset.valid());
    DP_EXPECT_TRUE(global_partition.valid());
    DP_EXPECT_TRUE(global_local0.valid());
    DP_EXPECT_TRUE(global_local1.valid());

    dp::test::mutate_metadata(
        global_fixture.workspace, global_fixture.global, [](auto& metadata) {
          metadata.at("structural")
              .at("source_local_graph_artifacts")
              .at(0)
              .at("format_version") = "local-graph-artifact/v999";
        });
    const dp::ArtifactRequirement global_requirement{
        dp::ArtifactType::kGlobalGraph, {"global-graph-artifact/v1"},
        dp::IdSpace::kGlobal};
    const auto forged_global = dp::ArtifactReuseService(global_validator).resolve(
        global_fixture.global, global_requirement,
        {"graph.global_adjacency/v1"},
        {*global_partition.validated_artifact,
         *global_local0.validated_artifact,
         *global_local1.validated_artifact});
    DP_EXPECT_EQ(forged_global.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!forged_global.validated_artifact.has_value());
    DP_EXPECT_TRUE(forged_global.failure.has_value());
    if (forged_global.failure) {
      DP_EXPECT_EQ(forged_global.failure->category,
                   dp::ErrorCategory::kSourceMismatch);
    }
  }

  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::Validator validator(fixture.base.workspace);
    const auto dataset = validator.validate(fixture.base.dataset);
    const dp::ArtifactRequirement requirement{
        dp::ArtifactType::kPQ, {"pq-artifact/v1"}, dp::IdSpace::kNone};
    const auto reused = dp::ArtifactReuseService(validator).resolve(
        fixture.pq, requirement,
        {"search.pq_in_memory/v1", "search.pqflash_loadable/v1"},
        {*dataset.validated_artifact});
    DP_EXPECT_EQ(reused.status, dp::StageStatus::kReused);
    DP_EXPECT_TRUE(reused.validated_artifact.has_value());

    dp::test::overwrite_file(
        dp::test::metadata_path(fixture.base.workspace, fixture.pq)
                .parent_path() /
            "codes.bin",
        "corrupt");
    const auto rejected = dp::ArtifactReuseService(validator).resolve(
        fixture.pq, requirement,
        {"search.pq_in_memory/v1", "search.pqflash_loadable/v1"},
        {*dataset.validated_artifact});
    DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!rejected.validated_artifact.has_value());
  }

  {
    namespace native_fixture = diskann_pipeline::cpp_main::test;
    native_fixture::TemporaryDirectory temporary("native-global-self-edge-reuse");
    const auto workspace = dp::create_workspace(temporary.path / "workspace");
    native_fixture::NativeDatasetFixture dataset_fixture(workspace, 4, 2);
    const auto partition = native_fixture::materialize_partition(
        workspace, dataset_fixture, {{0, 1}, {2, 3}});
    const auto local0 = native_fixture::materialize_local(
        workspace, dataset_fixture, partition, 0, {0, 1}, {{1}, {0}});
    const auto local1 = native_fixture::materialize_local(
        workspace, dataset_fixture, partition, 1, {2, 3}, {{1}, {0}});
    const auto global = native_fixture::materialize_global(
        workspace, dataset_fixture, partition, {local0, local1},
        {{1}, {1}, {3}, {2}}, {0, 2});
    dp::Validator native_validator(workspace);
    const auto validated_dataset =
        native_validator.validate(dataset_fixture.reference);
    const auto validated_partition = native_validator.validate(
        partition, {*validated_dataset.validated_artifact});
    const auto validated_local0 = native_validator.validate(
        local0, {*validated_partition.validated_artifact});
    const auto validated_local1 = native_validator.validate(
        local1, {*validated_partition.validated_artifact});
    const dp::ArtifactRequirement global_requirement{
        dp::ArtifactType::kGlobalGraph, {"global-graph-artifact/v1"},
        dp::IdSpace::kGlobal};
    const auto rejected = dp::ArtifactReuseService(native_validator).resolve(
        global, global_requirement, {"graph.global_adjacency/v1"},
        {*validated_partition.validated_artifact,
         *validated_local0.validated_artifact,
         *validated_local1.validated_artifact});
    DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!rejected.validated_artifact.has_value());
  }

  return dp::test::finish();
}
