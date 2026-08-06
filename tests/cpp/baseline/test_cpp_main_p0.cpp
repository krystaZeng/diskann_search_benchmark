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
  return dp::test::finish();
}
