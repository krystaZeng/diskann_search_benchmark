#include "papers/hnsw_merger/vamana/stage_binding.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

constexpr auto kParameters =
    R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})";

std::vector<std::vector<std::uint32_t>> ring(std::size_t count) {
  std::vector<std::vector<std::uint32_t>> result(count);
  for (std::size_t index = 0; index < count; ++index) {
    result[index] = {static_cast<std::uint32_t>((index + 1) % count)};
  }
  return result;
}

}  // namespace

int main() {
  fixture::TemporaryDirectory temporary("hnsw-merger-native-output");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 12, 4);
  const std::vector<std::vector<dp::GlobalNodeId>> maps{
      {0, 1, 2, 3, 4, 5}, {4, 5, 6, 7, 8, 9}, {8, 9, 10, 11}};
  const auto partition =
      fixture::materialize_partition(workspace, dataset, maps);
  std::vector<dp::ArtifactRef> locals;
  for (std::size_t id = 0; id < maps.size(); ++id) {
    locals.push_back(fixture::materialize_local(
        workspace, dataset, partition, id, maps[id], ring(maps[id].size())));
  }

  dp::Validator validator(workspace);
  const auto valid_dataset = validator.validate(dataset.reference);
  const auto valid_partition =
      validator.validate(partition, {*valid_dataset.validated_artifact});
  std::vector<dp::ValidatedArtifact> valid_sources{*valid_partition.validated_artifact};
  for (const auto& local : locals) {
    const auto valid = validator.validate(local, {*valid_partition.validated_artifact});
    DP_EXPECT_TRUE(valid.valid());
    valid_sources.push_back(*valid.validated_artifact);
  }

  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "hnsw-merger-m1");
  environment.context.vector_accessor = &vectors;
  const auto parameter_path = dp::resolve_confined_path(
      workspace.manifest_root, "native-output/parameters/merge.json");
  dp::write_immutable_text(parameter_path, kParameters);
  const auto stage = m1::make_m1_merge()->merge(
      partition, locals, parameter_path, environment.context);
  DP_EXPECT_EQ(stage.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(stage.output_candidate.has_value());
  DP_EXPECT_TRUE(!stage.build_manifest_path.empty());
  if (stage.status != dp::StageStatus::kSucceeded ||
      !stage.output_candidate) {
    return dp::test::finish();
  }
  const auto execution_manifest = dp::parse_build_manifest(
      fixture::read_json(stage.build_manifest_path).dump());
  DP_EXPECT_EQ(execution_manifest.status, dp::StageStatus::kSucceeded);
  const auto provenance = nlohmann::json::parse(
      execution_manifest.parameters_json);
  DP_EXPECT_EQ(provenance.at("paper_source_identity").at("commit"),
               std::string(m1::kPaperCommit));
  DP_EXPECT_EQ(provenance.at("pairwise_merge_decisions").size(),
               std::size_t{2});

  auto candidate_workspace = workspace;
  candidate_workspace.normal_root = environment.context.temporary_storage.stage_root;
  const auto valid_global = dp::Validator(candidate_workspace).validate(
      *stage.output_candidate, valid_sources);
  DP_EXPECT_TRUE(valid_global.valid());
  if (valid_global.valid()) {
    DP_EXPECT_EQ(valid_global.validated_artifact->dataset().global_node_count,
                 dp::NodeCount{12});
    DP_EXPECT_TRUE(valid_global.validated_artifact->has_capability(
        "graph.medoids/v1"));
  }
  return dp::test::finish();
}
