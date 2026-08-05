#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace dp = diskann_pipeline;

namespace {

struct RunEvidence {
  dp::PipelineExecutionResult result;
  dp::EvaluationReport report;
  std::string graph;
};

RunEvidence run(dp::test::FakeStageEnvironment& environment,
                const std::string& run_id, const std::string& ended_at) {
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  registry.seal();
  auto request = dp::test::canonical_execution_request(run_id, dataset);
  request.ended_at = ended_at;
  auto context = environment.make_context(run_id + "-context");
  auto result = dp::PipelineExecutor(registry, environment.workspace())
                    .execute(request, context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.artifact.has_value());
  DP_EXPECT_TRUE(result.evaluation_report_path.has_value());
  const auto report = dp::parse_evaluation_report(
      [&] {
        std::ifstream input(*result.evaluation_report_path);
        std::ostringstream bytes;
        bytes << input.rdbuf();
        return bytes.str();
      }());
  const auto metadata = dp::resolve_normal_artifact_metadata(
      environment.workspace(), result.artifact->metadata_path);
  std::ifstream graph_input(metadata.parent_path() / "graph.json");
  std::ostringstream graph;
  graph << graph_input.rdbuf();
  return {std::move(result), report, graph.str()};
}

}  // namespace

int main() {
  dp::test::FakeStageEnvironment first_environment("e2e-first");
  dp::test::FakeStageEnvironment second_environment("e2e-second");
  const auto first = run(first_environment, "retained-run-1",
                         "2026-08-05T00:00:01.000001Z");
  const auto second = run(second_environment, "retained-run-2",
                          "2026-08-05T00:00:02.000002Z");

  const std::string expected_graph =
      "{\"adjacency\":[[1,2],[0,2],[1,3,4],[2,4],[3,5],[]]}";
  DP_EXPECT_EQ(first.graph, expected_graph);
  DP_EXPECT_EQ(second.graph, expected_graph);
  DP_EXPECT_EQ(first.result.artifact->aggregate_checksum,
               std::string(dp::test::kGlobalAggregateChecksum));
  DP_EXPECT_EQ(first.result.artifact->aggregate_checksum,
               second.result.artifact->aggregate_checksum);
  DP_EXPECT_EQ(first.result.stage_order,
               std::vector<dp::StageKind>({dp::StageKind::kPartition,
                                           dp::StageKind::kLocalBuild,
                                           dp::StageKind::kLocalBuild,
                                           dp::StageKind::kMerge}));
  DP_EXPECT_EQ(first.result.validation_evidence_paths.size(), std::size_t{4});
  DP_EXPECT_EQ(first.result.build_manifest_paths.size(), std::size_t{4});
  DP_EXPECT_EQ(first.report.findings.logical_node_count, dp::NodeCount{6});
  DP_EXPECT_EQ(first.report.findings.invalid_neighbor_count, dp::NodeCount{0});
  DP_EXPECT_TRUE(first.report.findings.observed_max_degree <=
                 first.report.findings.declared_max_degree);
  DP_EXPECT_TRUE(first.report.findings.passed);
  const auto projection = nlohmann::json::parse(
      dp::deterministic_evaluation_projection(first.report));
  DP_EXPECT_EQ(projection.at("declared_max_degree").get<std::uint32_t>(),
               first.report.findings.declared_max_degree);
  DP_EXPECT_EQ(projection.at("observed_max_degree").get<std::uint32_t>(),
               first.report.findings.observed_max_degree);
  for (const auto partition_id : {0U, 1U}) {
    const auto local_root = first_environment.workspace().normal_root /
                            "retained-run-1" /
                            ("local-" + std::to_string(partition_id));
    std::ifstream map_input(local_root / "map.json");
    std::ifstream graph_input(local_root / "graph.json");
    const auto map = nlohmann::json::parse(map_input)
                         .at("local_to_global")
                         .get<std::vector<dp::GlobalNodeId>>();
    const auto local_graph = nlohmann::json::parse(graph_input).at("adjacency");
    DP_EXPECT_EQ(map.size(), std::size_t{4});
    DP_EXPECT_EQ(local_graph.size(), map.size());
    for (const auto global_id : map) {
      DP_EXPECT_TRUE(global_id < 6);
    }
    for (const auto& neighbors : local_graph) {
      DP_EXPECT_TRUE(neighbors.size() <= 2);
      for (const auto& neighbor : neighbors) {
        DP_EXPECT_TRUE(neighbor.get<dp::LocalNodeId>() < map.size());
      }
    }
  }
  const auto global_graph = nlohmann::json::parse(first.graph).at("adjacency");
  DP_EXPECT_EQ(global_graph.size(), std::size_t{6});
  for (const auto& neighbors : global_graph) {
    DP_EXPECT_TRUE(neighbors.size() <= 3);
    for (const auto& neighbor : neighbors) {
      DP_EXPECT_TRUE(neighbor.get<dp::GlobalNodeId>() < 6);
    }
  }
  DP_EXPECT_EQ(dp::deterministic_evaluation_projection(first.report),
               dp::deterministic_evaluation_projection(second.report));
  auto identity_only_change = first.report;
  identity_only_change.report_id = "different-report-id";
  identity_only_change.generated_at = "2030-01-01T00:00:00Z";
  DP_EXPECT_EQ(dp::deterministic_evaluation_projection(first.report),
               dp::deterministic_evaluation_projection(identity_only_change));
  auto deterministic_mismatch = first.report;
  ++deterministic_mismatch.findings.observed_max_degree;
  DP_EXPECT_TRUE(dp::deterministic_evaluation_projection(first.report) !=
                 dp::deterministic_evaluation_projection(
                     deterministic_mismatch));
  DP_EXPECT_TRUE(first.report.report_id != second.report.report_id);
  DP_EXPECT_TRUE(first.report.generated_at != second.report.generated_at);
  DP_EXPECT_TRUE(first.result.evaluation_report_path !=
                 second.result.evaluation_report_path);
  return dp::test::finish();
}
