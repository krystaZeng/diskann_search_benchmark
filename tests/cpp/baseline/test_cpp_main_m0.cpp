#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/test_assert.hpp"

#include <array>
#include <numeric>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

int main() {
  fixture::TemporaryDirectory temporary("m0");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace, 20, 4);
  std::vector<dp::GlobalNodeId> map0(20);
  std::iota(map0.begin(), map0.end(), 0U);
  const auto map1 = map0;
  std::vector<std::vector<std::uint32_t>> adjacency0(20);
  std::vector<std::vector<std::uint32_t>> adjacency1(20);
  for (std::uint32_t node = 0; node < 20; ++node) {
    for (std::uint32_t offset = 1; offset <= 10; ++offset) {
      adjacency0[node].push_back((node + offset) % 20);
    }
    for (std::uint32_t offset = 10; offset <= 19; ++offset) {
      adjacency1[node].push_back((node + offset) % 20);
    }
  }
  const auto partition =
      fixture::materialize_partition(workspace, dataset, {map0, map1});
  const auto local0 = fixture::materialize_local(
      workspace, dataset, partition, 0, map0, adjacency0);
  const auto local1 = fixture::materialize_local(
      workspace, dataset, partition, 1, map1, adjacency1);
  const auto dataset_validation = dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  const auto partition_validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());
  const auto local0_validation = dp::Validator(workspace).validate(
      local0, {*partition_validation.validated_artifact});
  const auto local1_validation = dp::Validator(workspace).validate(
      local1, {*partition_validation.validated_artifact});
  DP_EXPECT_TRUE(local0_validation.valid() && local1_validation.valid());

  fixture::TestContext environment(workspace, "m0-success");
  const auto parameters = fixture::write_parameters(
      environment.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto result = native::make_m0_merge()->merge(
      partition, {local0, local1}, parameters, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  const auto validation = dp::Validator(fixture::candidate_workspace(
                                      environment.context))
                              .validate(*result.output_candidate,
                                        {*partition_validation.validated_artifact,
                                         *local0_validation.validated_artifact,
                                         *local1_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.m0.merge_shards"));
  const auto metadata = fixture::read_json(
      environment.context.temporary_storage.stage_root /
      result.output_candidate->metadata_path);
  DP_EXPECT_EQ(metadata.at("payloads").size(), std::size_t{2});
  DP_EXPECT_EQ(metadata.at("payloads").at(0).at("role"), "global_graph");
  DP_EXPECT_EQ(metadata.at("payloads").at(1).at("role"), "medoids");
  DP_EXPECT_EQ(metadata.at("structural").at("global_node_count"), 20);

  fixture::TestContext configurable_r(workspace, "m0-configurable-r");
  const auto configurable_r_parameters = fixture::write_parameters(
      configurable_r.context.temporary_storage,
      R"({"R":7,"use_filters":false})");
  const auto configurable_r_result = native::make_m0_merge()->merge(
      partition, {local0, local1}, configurable_r_parameters,
      configurable_r.context);
  DP_EXPECT_EQ(configurable_r_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(configurable_r_result.output_candidate.has_value());
  DP_EXPECT_TRUE(configurable_r.metrics.saw("cpp_main.m0.merge_shards"));
  if (configurable_r_result.output_candidate) {
    const auto configurable_r_validation =
        dp::Validator(fixture::candidate_workspace(configurable_r.context))
            .validate(*configurable_r_result.output_candidate,
                      {*partition_validation.validated_artifact,
                       *local0_validation.validated_artifact,
                       *local1_validation.validated_artifact});
    DP_EXPECT_TRUE(configurable_r_validation.valid());
    const auto configurable_r_metadata = fixture::read_json(
        configurable_r.context.temporary_storage.stage_root /
        configurable_r_result.output_candidate->metadata_path);
    DP_EXPECT_EQ(configurable_r_metadata.at("structural")
                     .at("declared_max_degree"),
                 7);
    DP_EXPECT_TRUE(configurable_r_metadata.at("structural")
                       .at("observed_max_degree")
                       .get<std::uint32_t>() <= 7U);
  }

  {
    fixture::TemporaryDirectory degree_eleven_temporary("m0-degree-eleven");
    const auto degree_eleven_workspace = dp::create_workspace(
        degree_eleven_temporary.path / "workspace");
    const fixture::NativeDatasetFixture degree_eleven_dataset(
        degree_eleven_workspace, 20, 4);
    std::vector<dp::GlobalNodeId> degree_eleven_map(20);
    std::iota(degree_eleven_map.begin(), degree_eleven_map.end(), 0U);
    std::vector<std::vector<std::uint32_t>> degree_eleven_adjacency(20);
    for (std::uint32_t node = 0; node < 20; ++node) {
      for (std::uint32_t offset = 1; offset <= 11; ++offset) {
        degree_eleven_adjacency[node].push_back((node + offset) % 20);
      }
    }
    const auto degree_eleven_partition = fixture::materialize_partition(
        degree_eleven_workspace, degree_eleven_dataset,
        {degree_eleven_map, degree_eleven_map});
    const auto degree_eleven_local0 = fixture::materialize_local(
        degree_eleven_workspace, degree_eleven_dataset,
        degree_eleven_partition, 0, degree_eleven_map,
        degree_eleven_adjacency);
    const auto degree_eleven_local1 = fixture::materialize_local(
        degree_eleven_workspace, degree_eleven_dataset,
        degree_eleven_partition, 1, degree_eleven_map,
        degree_eleven_adjacency);

    const auto degree_eleven_dataset_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_dataset.reference);
    DP_EXPECT_TRUE(degree_eleven_dataset_validation.valid());
    const auto degree_eleven_partition_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_partition,
                      {*degree_eleven_dataset_validation.validated_artifact});
    DP_EXPECT_TRUE(degree_eleven_partition_validation.valid());
    const auto degree_eleven_local0_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_local0,
                      {*degree_eleven_partition_validation.validated_artifact});
    const auto degree_eleven_local1_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_local1,
                      {*degree_eleven_partition_validation.validated_artifact});
    DP_EXPECT_TRUE(degree_eleven_local0_validation.valid());
    DP_EXPECT_TRUE(degree_eleven_local1_validation.valid());

    fixture::TestContext degree_eleven(degree_eleven_workspace,
                                       "m0-degree-eleven");
    const auto degree_eleven_parameters = fixture::write_parameters(
        degree_eleven.context.temporary_storage,
        R"({"R":16,"use_filters":false})");
    const auto degree_eleven_result = native::make_m0_merge()->merge(
        degree_eleven_partition,
        {degree_eleven_local0, degree_eleven_local1},
        degree_eleven_parameters, degree_eleven.context);
    DP_EXPECT_EQ(degree_eleven_result.status, dp::StageStatus::kSucceeded);
    DP_EXPECT_TRUE(degree_eleven_result.output_candidate.has_value());
    DP_EXPECT_TRUE(degree_eleven.metrics.saw("cpp_main.m0.merge_shards"));
    if (degree_eleven_result.output_candidate) {
      const auto degree_eleven_output_validation =
          dp::Validator(fixture::candidate_workspace(degree_eleven.context))
              .validate(
                  *degree_eleven_result.output_candidate,
                  {*degree_eleven_partition_validation.validated_artifact,
                   *degree_eleven_local0_validation.validated_artifact,
                   *degree_eleven_local1_validation.validated_artifact});
      DP_EXPECT_TRUE(degree_eleven_output_validation.valid());
      const auto degree_eleven_metadata = fixture::read_json(
          degree_eleven.context.temporary_storage.stage_root /
          degree_eleven_result.output_candidate->metadata_path);
      DP_EXPECT_EQ(degree_eleven_metadata.at("structural")
                       .at("declared_max_degree"),
                   16);
      DP_EXPECT_TRUE(degree_eleven_metadata.at("structural")
                         .at("observed_max_degree")
                         .get<std::uint32_t>() <= 16U);
    }
  }

  const std::array<std::pair<const char*, const char*>, 5>
      invalid_r_parameters{{
          {"m0-r-missing", R"({"use_filters":false})"},
          {"m0-r-zero", R"({"R":0,"use_filters":false})"},
          {"m0-r-negative", R"({"R":-1,"use_filters":false})"},
          {"m0-r-non-integer", R"({"R":7.5,"use_filters":false})"},
          {"m0-r-too-large",
           R"({"R":4294967296,"use_filters":false})"},
      }};
  for (const auto& invalid_r : invalid_r_parameters) {
    fixture::TestContext invalid(workspace, invalid_r.first);
    const auto invalid_parameters = fixture::write_parameters(
        invalid.context.temporary_storage, invalid_r.second);
    const auto invalid_result = native::make_m0_merge()->merge(
        partition, {local0, local1}, invalid_parameters, invalid.context);
    DP_EXPECT_EQ(invalid_result.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!invalid_result.output_candidate.has_value());
    DP_EXPECT_TRUE(!invalid.metrics.saw("cpp_main.m0.merge_shards"));
  }

  fixture::TestContext missing(workspace, "m0-missing");
  const auto missing_parameters = fixture::write_parameters(
      missing.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto missing_result = native::make_m0_merge()->merge(
      partition, {local0}, missing_parameters, missing.context);
  DP_EXPECT_EQ(missing_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!missing_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!missing.metrics.saw("cpp_main.m0.merge_shards"));

  fixture::TestContext duplicate(workspace, "m0-duplicate");
  const auto duplicate_parameters = fixture::write_parameters(
      duplicate.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto duplicate_result = native::make_m0_merge()->merge(
      partition, {local0, local0}, duplicate_parameters, duplicate.context);
  DP_EXPECT_EQ(duplicate_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!duplicate.metrics.saw("cpp_main.m0.merge_shards"));

  fixture::TestContext cancelled(workspace, "m0-cancelled");
  cancelled.cancellation.cancelled = true;
  const auto cancelled_parameters = fixture::write_parameters(
      cancelled.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto cancelled_result = native::make_m0_merge()->merge(
      partition, {local0, local1}, cancelled_parameters, cancelled.context);
  DP_EXPECT_EQ(cancelled_result.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled_result.output_candidate.has_value());
  return dp::test::finish();
}
