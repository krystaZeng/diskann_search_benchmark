#include "papers/hnsw_merger/vamana/stage_binding.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/registry.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

constexpr auto kM1Parameters =
    R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})";

std::vector<std::vector<std::uint32_t>> ring(std::size_t count) {
  std::vector<std::vector<std::uint32_t>> result(count);
  for (std::size_t index = 0; index < count; ++index) {
    result[index] = {static_cast<std::uint32_t>((index + 1) % count)};
  }
  return result;
}

dp::PipelineExecutionRequest request(
    const std::string& run_id, const dp::ArtifactRef& dataset,
    const dp::ArtifactRef& partition,
    const std::vector<dp::ArtifactRef>& locals, const std::string& merge_id,
    const std::string& merge_parameters) {
  dp::PipelineExecutionRequest value;
  value.run_id = run_id;
  value.dataset = dataset;
  value.partition_implementation_id = "must-not-resolve-partition";
  value.local_implementation_id = "must-not-resolve-local";
  value.merge_implementation_id = merge_id;
  value.merge_parameters_json = merge_parameters;
  value.merge_only_inputs = dp::MergeOnlyInputSet{partition, locals};
  value.source_config_json =
      R"({"yaml_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","normalized_json_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
  value.source = {"https://example.invalid/project.git", "feature/merge-adaptation",
                  "1111111111111111111111111111111111111111", false,
                  std::nullopt};
  value.toolchain = {"Linux", "/usr/bin/c++", "11.4.0", "3.22.1",
                     "Unix Makefiles", "Release", {"FEATURE=ON"}, {}, {}};
  value.started_at = "2026-08-13T00:00:00Z";
  value.ended_at = "2026-08-13T00:00:01Z";
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  fixture::TemporaryDirectory temporary("hnsw-merger-merge-only");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 12, 4);
  const std::vector<std::vector<dp::GlobalNodeId>> maps{
      {0, 1, 2, 3, 4, 5}, {4, 5, 6, 7, 8, 9}, {8, 9, 10, 11}};
  const auto partition = fixture::materialize_partition(workspace, dataset, maps);
  std::vector<dp::ArtifactRef> locals;
  for (std::size_t id = 0; id < maps.size(); ++id) {
    locals.push_back(fixture::materialize_local(
        workspace, dataset, partition, id, maps[id], ring(maps[id].size())));
  }

  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "merge-only-bootstrap");
  environment.context.vector_accessor = &vectors;
  std::size_t m0_factories = 0;
  std::size_t m1_factories = 0;
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_merge(native::m0_descriptor(), [&] {
    ++m0_factories;
    return native::make_m0_merge();
  });
  registry.register_merge(m1::descriptor(), [&] {
    ++m1_factories;
    return m1::make_m1_merge();
  });
  registry.seal();
  dp::PipelineExecutor executor(registry, workspace);

  auto m0_request = request("same-input-m0", dataset.reference, partition,
                            locals, native::kM0ImplementationId,
                            R"({"R":16,"use_filters":false})");
  auto m0_context = environment.context;
  m0_context.temporary_storage = dp::create_temporary_storage(
      workspace, "same-input-m0-bootstrap");
  const auto m0 = executor.execute(m0_request, m0_context);
  DP_EXPECT_EQ(m0.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(m0.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(m0.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(m0.invocations.merge, std::size_t{1});
  DP_EXPECT_EQ(m0.stage_order, std::vector<dp::StageKind>({dp::StageKind::kMerge}));

  auto m1_request = request("same-input-m1", dataset.reference, partition,
                            locals, m1::kImplementationId, kM1Parameters);
  auto m1_context = environment.context;
  m1_context.temporary_storage = dp::create_temporary_storage(
      workspace, "same-input-m1-bootstrap");
  const auto m1_result = executor.execute(m1_request, m1_context);
  DP_EXPECT_EQ(m1_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(m1_result.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(m1_result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(m1_result.invocations.merge, std::size_t{1});
  DP_EXPECT_EQ(m0_factories, std::size_t{1});
  DP_EXPECT_EQ(m1_factories, std::size_t{1});
  DP_EXPECT_TRUE(m0.artifact.has_value());
  DP_EXPECT_TRUE(m1_result.artifact.has_value());
  DP_EXPECT_TRUE(m0.evaluation_report_path.has_value());
  DP_EXPECT_TRUE(m1_result.evaluation_report_path.has_value());

  const auto m1_manifest = dp::parse_build_manifest(
      fixture::read_json(m1_result.build_manifest_paths.back()).dump());
  const auto m0_manifest = dp::parse_build_manifest(
      fixture::read_json(m0.build_manifest_paths.back()).dump());
  const auto m0_parameters =
      nlohmann::json::parse(m0_manifest.parameters_json);
  DP_EXPECT_TRUE(!m0_parameters.contains("stage_execution_manifest"));
  const auto m1_parameters =
      nlohmann::json::parse(m1_manifest.parameters_json);
  DP_EXPECT_TRUE(m1_parameters.contains("stage_execution_manifest"));
  const auto execution_reference =
      m1_parameters.at("stage_execution_manifest");
  const auto execution_path = dp::resolve_confined_path(
      workspace.root,
      execution_reference.at("path").get<std::string>());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(execution_path));
  DP_EXPECT_EQ(execution_reference.at("sha256").get<std::string>(),
               dp::sha256_file(execution_path));
  const auto subordinate = dp::parse_build_manifest(
      fixture::read_json(execution_path).dump());
  const auto execution_provenance =
      nlohmann::json::parse(subordinate.parameters_json);
  DP_EXPECT_EQ(execution_provenance.at("paper_source_identity").at("commit"),
               std::string(m1::kPaperCommit));
  DP_EXPECT_EQ(execution_provenance.at("pairwise_merge_decisions").size(),
               locals.size() - 1);
  DP_EXPECT_TRUE(!m1_parameters.contains("pairwise_merge_decisions"));
  DP_EXPECT_EQ(m1_manifest.input_artifacts.size(), locals.size() + 2);
  DP_EXPECT_EQ(m1_manifest.input_artifacts.at(0).aggregate_checksum,
               dataset.reference.aggregate_checksum);
  DP_EXPECT_EQ(m1_manifest.input_artifacts.at(1).aggregate_checksum,
               partition.aggregate_checksum);
  for (std::size_t index = 0; index < locals.size(); ++index) {
    DP_EXPECT_EQ(m1_manifest.input_artifacts.at(index + 2).aggregate_checksum,
                 locals.at(index).aggregate_checksum);
  }

  auto malformed = request("same-input-malformed", dataset.reference,
                           partition, locals, m1::kImplementationId,
                           kM1Parameters);
  std::swap(malformed.merge_only_inputs->local_graphs[0],
            malformed.merge_only_inputs->local_graphs[1]);
  auto bad_context = environment.context;
  bad_context.temporary_storage = dp::create_temporary_storage(
      workspace, "same-input-malformed-bootstrap");
  const auto rejected = executor.execute(malformed, bad_context);
  DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(rejected.invocations.merge, std::size_t{0});
  DP_EXPECT_EQ(m1_factories, std::size_t{1});

  auto conflicting = request("same-input-conflicting", dataset.reference,
                             partition, locals, m1::kImplementationId,
                             kM1Parameters);
  conflicting.reused_partition = partition;
  auto conflict_context = environment.context;
  conflict_context.temporary_storage = dp::create_temporary_storage(
      workspace, "same-input-conflicting-bootstrap");
  const auto conflict = executor.execute(conflicting, conflict_context);
  DP_EXPECT_EQ(conflict.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(conflict.invocations.merge, std::size_t{0});

  if (argc == 3 && std::string(argv[1]) == "--emit-request") {
    const auto target = std::filesystem::absolute(argv[2]);
    if (std::filesystem::exists(target)) {
      throw std::runtime_error("small runner fixture target already exists");
    }
    std::filesystem::create_directories(target.parent_path());
    std::filesystem::copy(
        workspace.root, target,
        std::filesystem::copy_options::recursive);
    nlohmann::json project{{"repository", HNSW_MERGER_TEST_PROJECT_REPOSITORY},
                           {"branch", HNSW_MERGER_TEST_PROJECT_BRANCH},
                           {"commit", HNSW_MERGER_TEST_PROJECT_COMMIT},
                           {"dirty_worktree", HNSW_MERGER_TEST_PROJECT_DIRTY != 0}};
    if (HNSW_MERGER_TEST_PROJECT_DIRTY != 0) {
      project["dirty_source_checksum"] =
          HNSW_MERGER_TEST_PROJECT_DIRTY_CHECKSUM;
    }
    const nlohmann::json paper{
        {"repository", m1::kPaperRepository},
        {"branch", m1::kPaperBranch},
        {"commit", m1::kPaperCommit},
        {"dirty_worktree", false},
        {"doi", m1::kPaperDoi},
        {"pdf_sha256", m1::kPaperPdfSha256},
        {"integration_mode", "source_mapping_only"},
        {"official_binary_linked", false}};
    const nlohmann::json diskann{
        {"repository", native::kPinnedRepository},
        {"branch", native::kPinnedBranch},
        {"commit", native::kPinnedCommit},
        {"dirty_worktree", false}};
    nlohmann::json local_values = nlohmann::json::array();
    for (const auto& local : locals) {
      local_values.push_back(
          nlohmann::json::parse(dp::serialize_artifact_ref(local)));
    }
    const nlohmann::json normalized{
        {"request_version", "hnsw-merger-comparison-request/v1"},
        {"operation", "execute"},
        {"request_id", "hnsw-merger-small-runner"},
        {"study_class", "single_paper_reproduction"},
        {"reproduction_mode", "faithful_adaptation"},
        {"replacement_scope", "merge"},
        {"profile_ref", "specs/004-hnsw-merger-diskann-merge-adaptation/quickstart.md"},
        {"profile_checksum", std::string(64, 'a')},
        {"source_config",
         {{"yaml_sha256", std::string(64, 'a')},
          {"normalized_json_sha256", std::string(64, 'b')}}},
        {"workspace",
         {{"root", target.generic_string()},
          {"memory_budget_bytes", 64U * 1024U * 1024U},
          {"thread_budget", 1}}},
        {"source_admission",
         {{"project_adaptation", project},
          {"paper", paper},
          {"diskann", diskann},
          {"toolchain",
           {{"os", "Linux"},
            {"compiler_path", "/usr/bin/c++"},
            {"compiler_version", "11.4.0"},
            {"cmake_version", "3.22.1"},
            {"generator", "Unix Makefiles"},
            {"build_type", "Release"},
            {"effective_cmake_definitions", nlohmann::json::array({"DISKANN_HNSW_MERGER=ON"})},
            {"compile_commands", nlohmann::json::array({"fixture"})},
            {"link_commands", nlohmann::json::array({"diskann_s"})}}}}},
        {"shared_inputs",
         {{"dataset", nlohmann::json::parse(
                          dp::serialize_artifact_ref(dataset.reference))},
          {"partition", nlohmann::json::parse(
                            dp::serialize_artifact_ref(partition))},
          {"local_graphs", local_values}}},
        {"merges",
         {{"m0",
           {{"implementation_id", native::kM0ImplementationId},
            {"implementation_version", "1.0.0"},
            {"parameters", {{"R", 16}, {"use_filters", false}}}}},
          {"m1",
           {{"implementation_id", m1::kImplementationId},
            {"implementation_version", m1::kImplementationVersion},
            {"parameters", nlohmann::json::parse(kM1Parameters)}}}}}};
    std::ofstream(target / "comparison-request.json", std::ios::binary)
        << normalized.dump();
  } else if (argc != 1) {
    throw std::runtime_error("usage: test [--emit-request WORKSPACE]");
  }
  return dp::test::finish();
}
