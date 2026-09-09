#include "xn_descent_test_support.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/validation.hpp"

#include <algorithm>
#include <numeric>
#include <set>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::xn_descent::test;

int main() {
  fixture::baseline_fixture::TemporaryDirectory temporary("xn-artifact");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture dataset(workspace, 3,
                                                                 1);
  const std::vector<dp::GlobalNodeId> map{2, 1, 0};
  const auto partition = fixture::baseline_fixture::materialize_partition(
      workspace, dataset, {map});
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::baseline_fixture::TestContext environment(workspace,
                                                      "xn-direct-us1");
  environment.context.vector_accessor = &vectors;
  const auto parameter_path = fixture::baseline_fixture::write_parameters(
      environment.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
  const auto result = xn::make_xn_descent_local_build()->build(
      partition, 0, {parameter_path}, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.build_manifest_path));

  const auto dataset_validation = dp::Validator(workspace).validate(
      dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  const auto partition_validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());
  const auto candidate_workspace =
      fixture::baseline_fixture::candidate_workspace(environment.context);
  const auto validation = dp::Validator(candidate_workspace).validate(
      *result.output_candidate, {*partition_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());

  const auto candidate = environment.context.temporary_storage.stage_root /
                         "candidate";
  const auto metadata =
      fixture::baseline_fixture::read_json(candidate / "artifact.json");
  DP_EXPECT_EQ(metadata.at("id_space"), "local");
  DP_EXPECT_EQ(metadata.at("structural").at("local_node_count"), map.size());
  DP_EXPECT_EQ(metadata.at("structural").at("declared_max_degree"), 3);
  DP_EXPECT_EQ(metadata.at("structural").at("source_partition_checksum"),
               partition.aggregate_checksum);

  const auto partition_map = workspace.normal_root / "partition/map-0.bin";
  DP_EXPECT_EQ(dp::sha256_file(partition_map),
               dp::sha256_file(candidate / "map.bin"));
  DP_EXPECT_EQ(fixture::read_bytes(partition_map),
               fixture::read_bytes(candidate / "map.bin"));

  const auto graph_path = candidate / "graph.index";
  const auto graph_bytes = fixture::read_bytes(graph_path);
  const auto actual = fixture::read_native_adjacency(graph_path, map.size());
  const auto native_header_degree = fixture::read_u32(graph_bytes, 8);
  const auto native_entry = fixture::read_u32(graph_bytes, 12);
  DP_EXPECT_EQ(native_entry, dp::LocalNodeId{1});
  std::uint32_t observed = 0;
  for (std::size_t owner = 0; owner < actual.size(); ++owner) {
    observed = std::max(observed,
                        static_cast<std::uint32_t>(actual[owner].size()));
    const std::set<dp::LocalNodeId> unique(actual[owner].begin(),
                                           actual[owner].end());
    DP_EXPECT_EQ(unique.size(), actual[owner].size());
    DP_EXPECT_TRUE(unique.count(static_cast<dp::LocalNodeId>(owner)) == 0);
    DP_EXPECT_TRUE(std::all_of(actual[owner].begin(), actual[owner].end(),
                               [&](auto id) { return id < map.size(); }));
  }
  DP_EXPECT_EQ(native_header_degree, observed);
  DP_EXPECT_EQ(metadata.at("structural").at("observed_max_degree"), observed);
  DP_EXPECT_TRUE(observed <= 3);

  const auto manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(result.build_manifest_path).dump());
  DP_EXPECT_EQ(manifest.implementation.implementation_id,
               xn::kImplementationId);
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(manifest.resource_budget.thread_budget,
               environment.context.thread_budget);
  DP_EXPECT_EQ(manifest.resource_budget.memory_budget_bytes,
               environment.context.memory_budget_bytes);
  DP_EXPECT_EQ(manifest.output_artifacts.size(), std::size_t{1});
  if (!manifest.output_artifacts.empty()) {
    DP_EXPECT_EQ(manifest.output_artifacts.front().aggregate_checksum,
                 result.output_candidate->aggregate_checksum);
  }
  const auto manifest_parameters =
      fixture::Json::parse(manifest.parameters_json);
  DP_EXPECT_EQ(manifest_parameters.at("termination"), "converged");
  DP_EXPECT_TRUE(manifest_parameters.at("completed_rounds").get<int>() > 0);
  DP_EXPECT_TRUE(
      manifest_parameters.at("convergence").at("direct_equality_checked"));
  DP_EXPECT_TRUE(manifest_parameters.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(manifest_parameters.at("convergence")
                   .at("convergence_condition"),
               "full_state_fixed_point");
  DP_EXPECT_EQ(
      manifest_parameters.at("diskann_native_layout_source").at("commit"),
      "78256bbab4685e1774e78d331e081a153be26823");
  DP_EXPECT_EQ(manifest.metrics_json, "{}");
  DP_EXPECT_TRUE(!metadata.contains("recall"));
  DP_EXPECT_TRUE(!metadata.contains("qps"));
  DP_EXPECT_TRUE(!metadata.contains("latency"));

  fixture::baseline_fixture::TestContext repeated(workspace,
                                                   "xn-direct-us1-repeat");
  repeated.context.vector_accessor = &vectors;
  const auto repeated_parameters = fixture::baseline_fixture::write_parameters(
      repeated.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
  const auto repeated_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {repeated_parameters}, repeated.context);
  DP_EXPECT_EQ(repeated_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(
      fixture::read_bytes(graph_path),
      fixture::read_bytes(repeated.context.temporary_storage.stage_root /
                          "candidate/graph.index"));

  fixture::baseline_fixture::TestContext undersized(
      workspace, "xn-direct-us1-undersized-partition");
  undersized.context.vector_accessor = &vectors;
  undersized.context.memory_budget_bytes = 4096;
  const auto undersized_parameters =
      fixture::baseline_fixture::write_parameters(
          undersized.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1000, 3, 1.2,
                                             16));
  const auto undersized_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {undersized_parameters}, undersized.context);
  DP_EXPECT_EQ(undersized_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(undersized_result.output_candidate.has_value());

  fixture::baseline_fixture::TestContext capped(workspace,
                                                 "xn-direct-us1-cap");
  capped.context.vector_accessor = &vectors;
  const auto capped_parameters = fixture::baseline_fixture::write_parameters(
      capped.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 1));
  const auto capped_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {capped_parameters}, capped.context);
  DP_EXPECT_EQ(capped_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!capped_result.output_candidate.has_value());
  DP_EXPECT_TRUE(
      std::filesystem::is_regular_file(capped_result.build_manifest_path));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      capped.context.temporary_storage.stage_root / "candidate/artifact.json"));
  const auto capped_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(capped_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(capped_manifest.failure.has_value());
  DP_EXPECT_TRUE(capped_manifest.failure->reason.find(
                     "iteration_cap_nonconverged") != std::string::npos);
  DP_EXPECT_TRUE(capped_manifest.output_artifacts.empty());
  const auto capped_evidence =
      fixture::Json::parse(capped_manifest.parameters_json);
  DP_EXPECT_EQ(capped_evidence.at("termination"),
               "iteration_cap_nonconverged");
  DP_EXPECT_EQ(capped_evidence.at("completed_rounds"), 1);
  DP_EXPECT_TRUE(
      capped_evidence.at("convergence").at("direct_equality_checked"));
  DP_EXPECT_TRUE(!capped_evidence.at("convergence").at("fixed_point"));

  fixture::baseline_fixture::TestContext constrained(
      workspace, "xn-direct-us1-budget");
  constrained.context.vector_accessor = &vectors;
  constrained.context.memory_budget_bytes = 1;
  const auto constrained_parameters =
      fixture::baseline_fixture::write_parameters(
          constrained.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 5, 3, 1.2,
                                             16));
  const auto constrained_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {constrained_parameters}, constrained.context);
  DP_EXPECT_EQ(constrained_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!constrained_result.output_candidate.has_value());
  const auto constrained_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          constrained_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(constrained_manifest.failure.has_value());
  DP_EXPECT_TRUE(constrained_manifest.failure->reason.find("memory budget") !=
                 std::string::npos);
  DP_EXPECT_TRUE(constrained_manifest.output_artifacts.empty());
  const auto constrained_evidence =
      fixture::Json::parse(constrained_manifest.parameters_json);
  DP_EXPECT_EQ(constrained_evidence.at("termination"),
               "memory_budget_preflight_exceeded");
  DP_EXPECT_EQ(constrained_evidence.at("completed_rounds"), 0);
  DP_EXPECT_TRUE(
      !constrained_evidence.at("convergence").at("fixed_point"));

  fixture::baseline_fixture::TestContext unsupported(
      workspace, "xn-direct-us1-unsupported-accessor");
  fixture::MatrixVectorAccessor generic_vectors(fixture::line_vectors({0.0F}));
  unsupported.context.vector_accessor = &generic_vectors;
  const auto unsupported_parameters =
      fixture::baseline_fixture::write_parameters(
          unsupported.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.0,
                                             4));
  const auto unsupported_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {unsupported_parameters}, unsupported.context);
  DP_EXPECT_EQ(unsupported_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!unsupported_result.output_candidate.has_value());
  DP_EXPECT_TRUE(unsupported_result.build_manifest_path.empty());

  fixture::baseline_fixture::TemporaryDirectory cycle_temporary(
      "xn-artifact-r-cycle");
  const auto cycle_workspace =
      dp::create_workspace(cycle_temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture cycle_dataset(
      cycle_workspace, 16, 1);
  const auto cycle_partition =
      fixture::baseline_fixture::materialize_partition(
          cycle_workspace, cycle_dataset, {{0, 1, 2, 15}});
  native::CppMainVectorAccessor cycle_vectors(cycle_workspace,
                                               cycle_dataset.reference);

  fixture::baseline_fixture::TestContext cycle_success(
      cycle_workspace, "xn-direct-us1-r-cycle-success");
  cycle_success.context.vector_accessor = &cycle_vectors;
  const auto cycle_success_parameters =
      fixture::baseline_fixture::write_parameters(
          cycle_success.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.2,
                                             8, 20));
  const auto cycle_success_result = xn::make_xn_descent_local_build()->build(
      cycle_partition, 0, {cycle_success_parameters}, cycle_success.context);
  DP_EXPECT_EQ(cycle_success_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(cycle_success_result.output_candidate.has_value());
  const auto cycle_success_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          cycle_success_result.build_manifest_path)
          .dump());
  const auto cycle_success_evidence =
      fixture::Json::parse(cycle_success_manifest.parameters_json);
  DP_EXPECT_EQ(cycle_success_evidence.at("termination"), "converged");
  DP_EXPECT_EQ(cycle_success_evidence.at("completed_rounds"), 8);
  DP_EXPECT_TRUE(
      !cycle_success_evidence.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(cycle_success_evidence.at("convergence")
                   .at("stable_c_g_rounds"),
               3);
  DP_EXPECT_EQ(cycle_success_evidence.at("convergence")
                   .at("convergence_condition"),
               "three_round_c_g_stability");

  fixture::baseline_fixture::TestContext cycle_capped(
      cycle_workspace, "xn-direct-us1-r-cycle-cap");
  cycle_capped.context.vector_accessor = &cycle_vectors;
  const auto cycle_capped_parameters =
      fixture::baseline_fixture::write_parameters(
          cycle_capped.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.2,
                                             7, 20));
  const auto cycle_capped_result = xn::make_xn_descent_local_build()->build(
      cycle_partition, 0, {cycle_capped_parameters}, cycle_capped.context);
  DP_EXPECT_EQ(cycle_capped_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!cycle_capped_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(
      cycle_capped.context.temporary_storage.stage_root /
      "candidate/artifact.json"));
  const auto cycle_capped_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          cycle_capped_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(cycle_capped_manifest.output_artifacts.empty());
  const auto cycle_capped_evidence =
      fixture::Json::parse(cycle_capped_manifest.parameters_json);
  DP_EXPECT_EQ(cycle_capped_evidence.at("termination"),
               "iteration_cap_nonconverged");
  DP_EXPECT_EQ(cycle_capped_evidence.at("completed_rounds"), 7);
  DP_EXPECT_TRUE(
      !cycle_capped_evidence.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(cycle_capped_evidence.at("convergence")
                   .at("convergence_condition"),
               "none");
  return dp::test::finish();
}
