#include "baseline/cpp_main/stage_bindings.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/test_assert.hpp"

#include <numeric>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

class GenericAccessor final : public dp::VectorAccessor {
 public:
  std::size_t dimension() const noexcept override { return 4; }
  void read(dp::GlobalNodeId, std::vector<float>& destination) const override {
    destination.assign(4, 0.0F);
  }
};

}  // namespace

int main() {
  fixture::TemporaryDirectory temporary("b0");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace);
  std::vector<dp::GlobalNodeId> ids(64);
  std::iota(ids.begin(), ids.end(), 0U);
  const auto partition =
      fixture::materialize_partition(workspace, dataset, {ids});
  const auto dataset_validation = dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  const auto partition_validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());

  native::CppMainVectorAccessor accessor(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "b0-success");
  environment.context.vector_accessor = &accessor;
  const auto parameters = fixture::write_parameters(
      environment.context.temporary_storage,
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})");
  const auto result = native::make_b0_local_build()->build(
      partition, 0, {parameters}, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  const auto validation = dp::Validator(fixture::candidate_workspace(
                                      environment.context))
                              .validate(*result.output_candidate,
                                        {*partition_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.b0.retrieve"));
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.b0.index_build"));
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.b0.index_save"));
  const auto metadata = fixture::read_json(
      environment.context.temporary_storage.stage_root /
      result.output_candidate->metadata_path);
  DP_EXPECT_EQ(metadata.at("structural").at("partition_id"), 0);
  DP_EXPECT_EQ(metadata.at("structural").at("local_node_count"), ids.size());
  DP_EXPECT_TRUE(metadata.at("structural").at("observed_max_degree") <= 10);
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      environment.context.temporary_storage.stage_root /
      "native/local.index.data"));
  DP_EXPECT_EQ(
      dp::sha256_file(environment.context.temporary_storage.stage_root /
                      "preparation/shard.bin"),
      dp::sha256_file(environment.context.temporary_storage.stage_root /
                      "native/local.index.data"));

  fixture::TestContext invalid_id(workspace, "b0-invalid-id");
  invalid_id.context.vector_accessor = &accessor;
  const auto invalid_parameters = fixture::write_parameters(
      invalid_id.context.temporary_storage,
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})");
  const auto invalid_result = native::make_b0_local_build()->build(
      partition, 1, {invalid_parameters}, invalid_id.context);
  DP_EXPECT_EQ(invalid_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!invalid_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!invalid_id.metrics.saw("cpp_main.b0.retrieve"));

  GenericAccessor generic;
  fixture::TestContext wrong_accessor(workspace, "b0-wrong-accessor");
  wrong_accessor.context.vector_accessor = &generic;
  const auto wrong_parameters = fixture::write_parameters(
      wrong_accessor.context.temporary_storage,
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})");
  const auto wrong_result = native::make_b0_local_build()->build(
      partition, 0, {wrong_parameters}, wrong_accessor.context);
  DP_EXPECT_EQ(wrong_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!wrong_accessor.metrics.saw("cpp_main.b0.retrieve"));

  fixture::TestContext cancelled(workspace, "b0-cancelled");
  cancelled.context.vector_accessor = &accessor;
  cancelled.cancellation.cancelled = true;
  const auto cancelled_parameters = fixture::write_parameters(
      cancelled.context.temporary_storage,
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})");
  const auto cancelled_result = native::make_b0_local_build()->build(
      partition, 0, {cancelled_parameters}, cancelled.context);
  DP_EXPECT_EQ(cancelled_result.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled_result.output_candidate.has_value());
  int scenario = 0;
  for (const auto r : {16U, 64U}) {
    const auto l = r == 16 ? 32U : 100U;
    const auto degree = r == 16 ? 10U : 42U;
    fixture::TestContext configurable(workspace, "b0-v1-1-" + std::to_string(r));
    configurable.context.vector_accessor = &accessor;
    auto values = fixture::read_json(parameters);
    values["R"] = r;
    values["L"] = l;
    const auto path = fixture::write_parameters(configurable.context.temporary_storage, values.dump());
    const auto output = native::make_b0_local_build_v1_1()->build(partition, 0, {path}, configurable.context);
    DP_EXPECT_EQ(output.status, dp::StageStatus::kSucceeded);
    const auto key = "cpp_main.b0.v1_1.partition.0.";
    DP_EXPECT_EQ(configurable.metrics.counters[std::string(key) + "max_degree"], degree);
    DP_EXPECT_EQ(configurable.metrics.counters[std::string(key) + "search_list_size"], l);
    DP_EXPECT_EQ(configurable.metrics.counters[std::string(key) + "filter_list_size"], l);
    DP_EXPECT_EQ(configurable.metrics.counters[std::string(key) + "validation_max_degree"], degree);
    if (output.output_candidate) {
      DP_EXPECT_TRUE(dp::Validator(fixture::candidate_workspace(configurable.context))
          .validate(*output.output_candidate, {*partition_validation.validated_artifact}).valid());
      const auto meta = fixture::read_json(configurable.context.temporary_storage.stage_root /
                                           output.output_candidate->metadata_path);
      DP_EXPECT_EQ(meta.at("structural").at("declared_max_degree"), degree);
      DP_EXPECT_TRUE(meta.at("structural").at("observed_max_degree") <= degree);
    }
    if (r == 64) {
      fixture::TestContext old(workspace, "b0-old-rejects-new");
      old.context.vector_accessor = &accessor;
      const auto old_path = fixture::write_parameters(old.context.temporary_storage, values.dump());
      DP_EXPECT_EQ(native::make_b0_local_build()->build(partition, 0, {old_path}, old.context).status,
                   dp::StageStatus::kFailed);
      DP_EXPECT_TRUE(!old.metrics.saw("cpp_main.b0.retrieve"));
    }
  }
  for (const auto* field : {"R", "L"}) {
    for (const auto& bad : std::vector<nlohmann::json>{0, -1, 64.0, true, "64", 4294967360ULL}) {
      fixture::TestContext rejected(workspace, "b0-v1-1-invalid-" + std::to_string(scenario++));
      rejected.context.vector_accessor = &accessor;
      auto values = fixture::read_json(parameters);
      values[field] = bad;
      const auto path = fixture::write_parameters(rejected.context.temporary_storage, values.dump());
      const auto output = native::make_b0_local_build_v1_1()->build(partition, 0, {path}, rejected.context);
      DP_EXPECT_EQ(output.status, dp::StageStatus::kFailed);
      DP_EXPECT_TRUE(!output.output_candidate);
      DP_EXPECT_TRUE(!rejected.metrics.saw("cpp_main.b0.retrieve"));
    }
  }
  DP_EXPECT_EQ(native::make_b0_local_build_v1_1()->build(
      partition, 0, {wrong_parameters}, wrong_accessor.context).status, dp::StageStatus::kFailed);
  {
    auto values = fixture::read_json(parameters);
    values["alpha"] = 1.2000005;
    // The frozen parser retains its tolerance; only 1.1.0 requires the schema constant.
    const auto legacy = native::parse_b0_parameters(values.dump(), "1.0.0");
    DP_EXPECT_EQ(legacy.r, 16U);
    DP_EXPECT_EQ(legacy.l, 32U);
    DP_EXPECT_EQ(legacy.local_degree, 10U);

    fixture::TestContext near_alpha(workspace, "b0-v1-1-near-alpha");
    near_alpha.context.vector_accessor = &accessor;
    const auto path = fixture::write_parameters(
        near_alpha.context.temporary_storage, values.dump());
    const auto output = native::make_b0_local_build_v1_1()->build(
        partition, 0, {path}, near_alpha.context);
    std::cout << "B0 1.1.0 near-alpha: succeeded="
              << (output.status == dp::StageStatus::kSucceeded)
              << " candidate=" << output.output_candidate.has_value()
              << " retrieve=" << near_alpha.metrics.saw("cpp_main.b0.retrieve")
              << " native_build=" << near_alpha.metrics.saw("cpp_main.b0.index_build") << '\n';
    DP_EXPECT_EQ(output.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!output.output_candidate);
    DP_EXPECT_TRUE(!near_alpha.metrics.saw("cpp_main.b0.retrieve"));
    DP_EXPECT_TRUE(!near_alpha.metrics.saw("cpp_main.b0.index_build"));
  }
  return dp::test::finish();
}
