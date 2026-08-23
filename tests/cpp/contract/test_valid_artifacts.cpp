#include "diskann_pipeline/validation.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "search/search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

#include <functional>
#include <type_traits>

namespace dp = diskann_pipeline;

namespace {

dp::ArtifactValidationResult validate_global(
    dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  DP_EXPECT_TRUE(dataset.valid());
  const auto partition =
      validator.validate(fixture.partition, {*dataset.validated_artifact});
  DP_EXPECT_TRUE(partition.valid());
  const auto local0 =
      validator.validate(fixture.local0, {*partition.validated_artifact});
  const auto local1 =
      validator.validate(fixture.local1, {*partition.validated_artifact});
  DP_EXPECT_TRUE(local0.valid());
  DP_EXPECT_TRUE(local1.valid());
  return validator.validate(
      fixture.global,
      {*partition.validated_artifact, *local0.validated_artifact,
       *local1.validated_artifact});
}

void expect_global_source_mismatch(
    const std::function<void(dp::test::Json&)>& mutation) {
  dp::test::ContractArtifactFixture fixture;
  dp::test::mutate_metadata(fixture.workspace, fixture.global, mutation);
  const auto result = validate_global(fixture);
  DP_EXPECT_TRUE(!result.valid());
  DP_EXPECT_TRUE(!result.validated_artifact.has_value());
  DP_EXPECT_TRUE(result.evidence.failure_category.has_value());
  if (result.evidence.failure_category) {
    DP_EXPECT_EQ(*result.evidence.failure_category,
                 dp::ErrorCategory::kSourceMismatch);
  }
}

}  // namespace

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

  {
    dp::search::test::SearchArtifactFixture search_fixture;
    dp::Validator search_validator(search_fixture.base.workspace);
    const auto search_dataset =
        search_validator.validate(search_fixture.base.dataset);
    const auto pq = search_validator.validate(
        search_fixture.pq, {*search_dataset.validated_artifact});
    DP_EXPECT_TRUE(pq.valid());
    DP_EXPECT_EQ(pq.validated_artifact->id_space(), dp::IdSpace::kNone);

    const auto search_partition = search_validator.validate(
        search_fixture.base.partition, {*search_dataset.validated_artifact});
    const auto search_local0 = search_validator.validate(
        search_fixture.base.local0, {*search_partition.validated_artifact});
    const auto search_local1 = search_validator.validate(
        search_fixture.base.local1, {*search_partition.validated_artifact});
    const auto search_global = search_validator.validate(
        search_fixture.base.global,
        {*search_partition.validated_artifact,
         *search_local0.validated_artifact, *search_local1.validated_artifact});
    const auto layout = search_validator.validate(
        search_fixture.layout,
        {*search_dataset.validated_artifact,
         *search_global.validated_artifact});
    DP_EXPECT_TRUE(layout.valid());
    DP_EXPECT_EQ(layout.validated_artifact->id_space(), dp::IdSpace::kGlobal);
  }

  // GlobalGraph structural lineage is independently checksum-bound as part of
  // the aggregate identity, but it must still match the complete identity of
  // the already validated sources. Recomputing the target aggregate checksum
  // must not turn a forged source type or version into valid lineage.
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("structural")
        .at("source_partition_artifact")
        .at("artifact_type") = "dataset";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("structural")
        .at("source_partition_artifact")
        .at("format_version") = "partition-artifact/v999";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("structural")
        .at("source_local_graph_artifacts")
        .at(0)
        .at("artifact_type") = "dataset";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("structural")
        .at("source_local_graph_artifacts")
        .at(0)
        .at("format_version") = "local-graph-artifact/v999";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("structural")
        .at("source_local_graph_artifacts")
        .at(0)
        .at("aggregate_checksum") = std::string(64, '0');
  });

  // The canonical top-level lineage remains governed by the same complete
  // identity tuple and must fail before a ValidatedArtifact grant is issued.
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("source_artifacts").at(1).at("artifact_type") = "dataset";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("source_artifacts").at(1).at("format_version") =
        "local-graph-artifact/v999";
  });
  expect_global_source_mismatch([](auto& metadata) {
    metadata.at("source_artifacts").at(1).at("aggregate_checksum") =
        std::string(64, '0');
  });

  // Native GlobalGraph payloads must fail before a ValidatedArtifact grant
  // even when the enclosing Artifact identity correctly binds the corrupt
  // payload bytes.
  {
    diskann_pipeline::cpp_main::test::TemporaryDirectory temporary(
        "native-global-self-edge-contract");
    const auto workspace = dp::create_workspace(temporary.path / "workspace");
    diskann_pipeline::cpp_main::test::NativeDatasetFixture native_dataset(
        workspace, 4, 2);
    const auto native_partition =
        diskann_pipeline::cpp_main::test::materialize_partition(
            workspace, native_dataset, {{0, 1}, {2, 3}});
    const auto native_local0 =
        diskann_pipeline::cpp_main::test::materialize_local(
            workspace, native_dataset, native_partition, 0, {0, 1},
            {{1}, {0}});
    const auto native_local1 =
        diskann_pipeline::cpp_main::test::materialize_local(
            workspace, native_dataset, native_partition, 1, {2, 3},
            {{1}, {0}});
    const auto self_edged =
        diskann_pipeline::cpp_main::test::materialize_global(
            workspace, native_dataset, native_partition,
            {native_local0, native_local1}, {{0, 1}, {0}, {3}, {2}},
            {0, 2});
    dp::Validator native_validator(workspace);
    const auto validated_dataset = native_validator.validate(
        native_dataset.reference);
    const auto validated_partition = native_validator.validate(
        native_partition, {*validated_dataset.validated_artifact});
    const auto validated_local0 = native_validator.validate(
        native_local0, {*validated_partition.validated_artifact});
    const auto validated_local1 = native_validator.validate(
        native_local1, {*validated_partition.validated_artifact});
    const auto rejected = native_validator.validate(
        self_edged, {*validated_partition.validated_artifact,
                     *validated_local0.validated_artifact,
                     *validated_local1.validated_artifact});
    DP_EXPECT_TRUE(!rejected.valid());
    DP_EXPECT_TRUE(!rejected.validated_artifact.has_value());
  }

  return dp::test::finish();
}
