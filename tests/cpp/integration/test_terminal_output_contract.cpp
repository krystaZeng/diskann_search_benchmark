#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/quarantine.hpp"
#include "support/test_assert.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace dp = diskann_pipeline;

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

dp::PipelineExecutionResult run(const std::string& stage) {
  dp::test::FakeStageEnvironment environment("terminal-output-" + stage);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(
      dp::test::fake_partition_descriptor(),
      stage == "partition" ? dp::PartitionFactory(dp::test::make_empty_fake_partition)
                           : dp::PartitionFactory(dp::test::make_fake_partition));
  registry.register_local_build(
      dp::test::fake_local_graph_descriptor(),
      stage == "local"
          ? dp::LocalBuildFactory(dp::test::make_empty_fake_local_graph)
          : dp::LocalBuildFactory(dp::test::make_fake_local_graph));
  registry.register_merge(
      dp::test::fake_merge_descriptor(),
      stage == "merge" ? dp::MergeFactory(dp::test::make_empty_fake_merge)
                       : dp::MergeFactory(dp::test::make_fake_merge));
  auto context = environment.make_context("terminal-output-context");
  const auto result = dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(
                   "terminal-output-" + stage, dataset),
               context);
  DP_EXPECT_TRUE(!result.build_manifest_paths.empty());
  DP_EXPECT_TRUE(!result.validation_evidence_paths.empty());
  const auto manifest = dp::parse_build_manifest(
      read_text(result.build_manifest_paths.back()));
  const auto evidence = dp::parse_validation_evidence(
      read_text(result.validation_evidence_paths.back()));
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(manifest.failure.has_value());
  DP_EXPECT_TRUE(!manifest.started_at.empty());
  DP_EXPECT_TRUE(!manifest.ended_at.empty());
  DP_EXPECT_EQ(evidence.result, dp::ValidationResult::kInvalid);
  DP_EXPECT_TRUE(result.quarantine_id.has_value());
  const auto record = dp::DiagnosticQuarantineResolver(environment.workspace())
                          .read_record(*result.quarantine_id);
  DP_EXPECT_TRUE(record.isolated_from_normal_resolver);
  const auto normal_identity =
      stage == "local" ? "local-0" : stage;
  DP_EXPECT_TRUE(!std::filesystem::exists(
      environment.workspace().normal_root /
      ("terminal-output-" + stage) / normal_identity));
  return result;
}

void expect_invalid(const dp::PipelineExecutionResult& result,
                    std::size_t partitions, std::size_t locals,
                    std::size_t merges) {
  DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(result.failure.has_value());
  DP_EXPECT_EQ(result.failure->category, dp::ErrorCategory::kSchemaInvalid);
  DP_EXPECT_EQ(result.invocations.partition, partitions);
  DP_EXPECT_EQ(result.invocations.local_build, locals);
  DP_EXPECT_EQ(result.invocations.merge, merges);
  DP_EXPECT_TRUE(result.quarantine_id.has_value());
  DP_EXPECT_TRUE(!result.artifact.has_value());
  DP_EXPECT_TRUE(!result.validation_evidence_paths.empty());
  DP_EXPECT_TRUE(!result.build_manifest_paths.empty());
}

}  // namespace

int main() {
  expect_invalid(run("partition"), 1, 0, 0);
  expect_invalid(run("local"), 1, 1, 0);
  expect_invalid(run("merge"), 1, 2, 1);
  return dp::test::finish();
}
