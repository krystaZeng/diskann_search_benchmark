#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

int main() {
  fixture::TemporaryDirectory temporary("p0");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace);
  const auto dataset_validation = dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());

  fixture::TestContext environment(workspace, "p0-success");
  const auto parameters = fixture::write_parameters(
      environment.context.temporary_storage,
      R"({"R":16,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":1.0})");
  const auto result = native::make_p0_partition()->run(
      dataset.reference, parameters, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  const auto validation = dp::Validator(fixture::candidate_workspace(
                                      environment.context))
                              .validate(*result.output_candidate,
                                        {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());
  const auto metadata = fixture::read_json(
      environment.context.temporary_storage.stage_root /
      result.output_candidate->metadata_path);
  DP_EXPECT_TRUE(metadata.at("structural").at("partition_count") >= 2);
  DP_EXPECT_TRUE(std::all_of(
      metadata.at("structural").at("partitions").begin(),
      metadata.at("structural").at("partitions").end(),
      [](const auto& record) { return !record.contains("shard_vectors_role"); }));
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.p0.partition_with_ram_budget"));

  fixture::TestContext cancelled(workspace, "p0-cancelled");
  cancelled.cancellation.cancelled = true;
  const auto cancelled_parameters = fixture::write_parameters(
      cancelled.context.temporary_storage,
      R"({"R":16,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":1.0})");
  const auto cancelled_result = native::make_p0_partition()->run(
      dataset.reference, cancelled_parameters, cancelled.context);
  DP_EXPECT_EQ(cancelled_result.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled_result.output_candidate.has_value());

  fixture::TestContext invalid(workspace, "p0-invalid");
  const auto invalid_parameters = fixture::write_parameters(
      invalid.context.temporary_storage,
      R"({"R":15,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":1.0})");
  const auto invalid_result = native::make_p0_partition()->run(
      dataset.reference, invalid_parameters, invalid.context);
  DP_EXPECT_EQ(invalid_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!invalid_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!invalid.metrics.saw("cpp_main.p0.partition_with_ram_budget"));
  DP_EXPECT_TRUE(!invalid.metrics.saw("cpp_main.b0.retrieve"));
  // The version-bound factory must use the same parser as entry preflight.
  for (const auto r : {16U, 64U}) {
    fixture::TestContext configurable(workspace, "p0-v1-1-" + std::to_string(r));
    const auto path = fixture::write_parameters(configurable.context.temporary_storage,
        nlohmann::json{{"R", r}, {"build_DRAM_budget_gib", 0.6},
                       {"replication_factor", 2}, {"sample_probability", 1.0}}.dump());
    const auto output = native::make_p0_partition_v1_1()->run(dataset.reference, path, configurable.context);
    DP_EXPECT_EQ(output.status, dp::StageStatus::kSucceeded);
    DP_EXPECT_EQ(configurable.metrics.counters["cpp_main.p0.v1_1.graph_degree"], r == 16 ? 10U : 42U);
    if (output.output_candidate) {
      DP_EXPECT_TRUE(dp::Validator(fixture::candidate_workspace(configurable.context))
          .validate(*output.output_candidate, {*dataset_validation.validated_artifact}).valid());
    }
  }
  int scenario = 0;
  for (const auto& bad : std::vector<nlohmann::json>{0, 1, -1, 64.0, true, "64", 4294967360ULL}) {
    fixture::TestContext rejected(workspace, "p0-v1-1-invalid-" + std::to_string(scenario++));
    const auto path = fixture::write_parameters(rejected.context.temporary_storage,
        nlohmann::json{{"R", bad}, {"build_DRAM_budget_gib", 0.6},
                       {"replication_factor", 2}, {"sample_probability", 1.0}}.dump());
    const auto output = native::make_p0_partition_v1_1()->run(dataset.reference, path, rejected.context);
    DP_EXPECT_EQ(output.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!output.output_candidate);
    DP_EXPECT_TRUE(!rejected.metrics.saw("cpp_main.p0.partition_with_ram_budget"));
  }
  {
    auto values = fixture::read_json(parameters);
    values["build_DRAM_budget_gib"] = 0.6000000000005;
    // The frozen parser retains its tolerance; only 1.1.0 requires the schema constant.
    const auto legacy = native::parse_p0_parameters(
        values.dump(), dataset.identity.global_node_count, "1.0.0");
    DP_EXPECT_EQ(legacy.build_dram_budget_gib, 0.6000000000005);
    DP_EXPECT_EQ(legacy.local_degree, 10U);

    fixture::TestContext near_budget(workspace, "p0-v1-1-near-budget");
    const auto path = fixture::write_parameters(
        near_budget.context.temporary_storage, values.dump());
    const auto output = native::make_p0_partition_v1_1()->run(
        dataset.reference, path, near_budget.context);
    std::cout << "P0 1.1.0 near-budget: succeeded="
              << (output.status == dp::StageStatus::kSucceeded)
              << " candidate=" << output.output_candidate.has_value()
              << " native_partition="
              << near_budget.metrics.saw("cpp_main.p0.partition_with_ram_budget") << '\n';
    DP_EXPECT_EQ(output.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!output.output_candidate);
    DP_EXPECT_TRUE(!near_budget.metrics.saw("cpp_main.p0.partition_with_ram_budget"));
  }
  return dp::test::finish();
}
