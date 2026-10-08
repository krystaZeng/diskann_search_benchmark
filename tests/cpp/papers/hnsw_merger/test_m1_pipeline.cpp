#include "papers/hnsw_merger/vamana/stage_binding.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "support/test_assert.hpp"

#include <iostream>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

constexpr auto kM1Parameters =
    R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})";

dp::PipelineExecutionRequest base_request(const std::string& run_id,
                                          const dp::ArtifactRef& dataset,
                                          const std::string& merge,
                                          const std::string& parameters) {
  dp::PipelineExecutionRequest request;
  request.run_id = run_id;
  request.dataset = dataset;
  request.partition_implementation_id = native::kP0ImplementationId;
  request.local_implementation_id = native::kB0ImplementationId;
  request.merge_implementation_id = merge;
  request.partition_parameters_json =
      R"({"R":16,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":1.0})";
  request.local_parameters_json =
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})";
  request.merge_parameters_json = parameters;
  request.source_config_json =
      R"({"yaml_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","normalized_json_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
  request.source = {"https://example.invalid/project.git", "feature/merge-adaptation",
                    "1111111111111111111111111111111111111111", false,
                    std::nullopt};
  request.toolchain = {"Linux", "/usr/bin/c++", "11.4.0", "3.22.1",
                       "Unix Makefiles", "Release",
                       {"DISKANN_CPP_MAIN_BASELINE=ON", "DISKANN_HNSW_MERGER=ON"},
                       {"cmake configure"}, {"cmake build"}};
  request.started_at = "2026-08-13T00:00:00Z";
  request.ended_at = "2026-08-13T00:00:01Z";
  return request;
}

dp::StageContext fresh_context(fixture::TestContext& environment,
                               const dp::WorkspaceRef& workspace,
                               const std::string& run_id) {
  auto result = environment.context;
  result.temporary_storage =
      dp::create_temporary_storage(workspace, run_id + "-bootstrap");
  return result;
}

}  // namespace

int main() {
  fixture::TemporaryDirectory temporary("hnsw-merger-real-pipeline");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 96, 4);
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "pipeline-bootstrap");
  environment.context.vector_accessor = &vectors;

  std::size_t m1_factories = 0;
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_partition(native::p0_descriptor(), native::make_p0_partition);
  registry.register_local_build(native::b0_descriptor(), native::make_b0_local_build, native::b0_input_admission);
  registry.register_merge(native::m0_descriptor(), native::make_m0_merge, native::m0_input_admission);
  registry.register_merge(m1::descriptor(), [&] {
    ++m1_factories;
    return m1::make_m1_merge();
  }, m1::input_admission);
  registry.seal();
  dp::PipelineExecutor executor(registry, workspace);

  auto composed_request = base_request("small-p0-b0-m1", dataset.reference,
                                       m1::kImplementationId, kM1Parameters);
  auto composed_context = fresh_context(environment, workspace,
                                        "small-p0-b0-m1");
  const auto composed = executor.execute(composed_request, composed_context);
  if (composed.failure) {
    std::cerr << "composed failure: " << composed.failure->reason << '\n';
  }
  DP_EXPECT_EQ(composed.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(composed.invocations.partition, std::size_t{1});
  DP_EXPECT_TRUE(composed.invocations.local_build >= std::size_t{2});
  DP_EXPECT_EQ(composed.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(composed.partition_artifact.has_value());
  DP_EXPECT_TRUE(!composed.local_artifacts.empty());
  DP_EXPECT_TRUE(composed.artifact.has_value());
  DP_EXPECT_TRUE(composed.evaluation_report_path.has_value());
  if (!composed.partition_artifact || composed.local_artifacts.empty()) {
    return dp::test::finish();
  }

  auto merge_only = [&](const std::string& run_id, const std::string& id,
                        const std::string& parameters) {
    auto request = base_request(run_id, dataset.reference, id, parameters);
    request.partition_implementation_id = "must-not-resolve-partition";
    request.local_implementation_id = "must-not-resolve-local-build";
    request.merge_only_inputs =
        dp::MergeOnlyInputSet{*composed.partition_artifact,
                              composed.local_artifacts};
    auto context = fresh_context(environment, workspace, run_id);
    return executor.execute(request, context);
  };
  const auto m0_result = merge_only(
      "small-same-input-m0", native::kM0ImplementationId,
      R"({"R":16,"use_filters":false})");
  const auto m1_result = merge_only("small-same-input-m1", m1::kImplementationId,
                                    kM1Parameters);
  DP_EXPECT_EQ(m0_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(m1_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(m0_result.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(m0_result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(m0_result.invocations.merge, std::size_t{1});
  DP_EXPECT_EQ(m1_result.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(m1_result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(m1_result.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(m0_result.artifact.has_value());
  DP_EXPECT_TRUE(m1_result.artifact.has_value());
  DP_EXPECT_TRUE(m0_result.evaluation_report_path.has_value());
  DP_EXPECT_TRUE(m1_result.evaluation_report_path.has_value());

  auto invalid = base_request("invalid-upstream", dataset.reference,
                              m1::kImplementationId, kM1Parameters);
  invalid.merge_only_inputs =
      dp::MergeOnlyInputSet{*composed.partition_artifact,
                            composed.local_artifacts};
  invalid.dataset.aggregate_checksum = std::string(64, 'f');
  const auto before = m1_factories;
  auto invalid_context = fresh_context(environment, workspace, "invalid-upstream");
  const auto rejected = executor.execute(invalid, invalid_context);
  DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(rejected.invocations.merge, std::size_t{0});
  DP_EXPECT_EQ(m1_factories, before);
  return dp::test::finish();
}
