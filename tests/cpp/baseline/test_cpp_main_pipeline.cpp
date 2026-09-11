#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/source_build_identity.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifndef CPP_MAIN_BASELINE_RUNNER
#error "CPP_MAIN_BASELINE_RUNNER must name the real Baseline runner"
#endif

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;
using Json = nlohmann::json;

namespace {

const std::string kRunNonce = std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count());

std::string shell_quote(const std::filesystem::path& value) {
  std::string result = "'";
  for (const char character : value.string()) {
    result += character == '\'' ? "'\\''" : std::string(1, character);
  }
  result += "'";
  return result;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

dp::WorkspaceRef workspace_layout(const std::filesystem::path& root) {
  const auto absolute = std::filesystem::absolute(root);
  return {absolute, absolute / "normal", absolute / "temporary",
          absolute / "manifests", absolute / "reports",
          absolute / "quarantine"};
}

class MissingOutputPartition final : public dp::PartitionStrategy {
 public:
  dp::StageResult run(const dp::ArtifactRef&, const std::filesystem::path&,
                      dp::StageContext&) override {
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class MissingOutputLocal final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef&, dp::PartitionId,
                        const dp::LocalBuildConfig&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class MissingOutputMerge final : public dp::GraphMerger {
 public:
  dp::StageResult merge(const dp::ArtifactRef&,
                        const std::vector<dp::ArtifactRef>&,
                        const std::filesystem::path&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }
};

std::unique_ptr<dp::PartitionStrategy> missing_partition() {
  return std::make_unique<MissingOutputPartition>();
}
std::unique_ptr<dp::LocalGraphBuilder> missing_local() {
  return std::make_unique<MissingOutputLocal>();
}
std::unique_ptr<dp::GraphMerger> missing_merge() {
  return std::make_unique<MissingOutputMerge>();
}

dp::PipelineExecutionRequest request(const std::string& run_id,
                                     const dp::ArtifactRef& dataset) {
  dp::PipelineExecutionRequest value;
  value.run_id = run_id;
  value.dataset = dataset;
  value.partition_implementation_id = native::kP0ImplementationId;
  value.local_implementation_id = native::kB0ImplementationId;
  value.merge_implementation_id = native::kM0ImplementationId;
  value.partition_parameters_json =
      R"({"R":16,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":1.0})";
  value.local_parameters_json =
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})";
  value.merge_parameters_json = R"({"R":16,"use_filters":false})";
  value.source_config_json =
      R"({"yaml_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","normalized_json_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
  value.source = {native::kPinnedRepository, native::kPinnedBranch,
                  native::kPinnedCommit, false, std::nullopt};
  value.toolchain = {"Linux", "/usr/bin/c++", "test", "test", "test",
                     "Release", {"DISKANN_CPP_MAIN_BASELINE=ON"},
                     {"cmake configure"}, {"cmake build"}};
  value.started_at = "2026-08-07T00:00:00Z";
  value.ended_at = "2026-08-07T00:00:01Z";
  return value;
}

Json baseline_runner_request(const dp::WorkspaceRef& workspace,
                             const dp::ArtifactRef& dataset,
                             const std::string& request_id,
                             std::int64_t merge_r, bool use_filters) {
  return {
      {"request_version", "1.0.0"},
      {"request_id", request_id},
      {"operation", "execute"},
      {"study_class", "baseline_validation"},
      {"profile_ref",
       "experiments/configs/cpp-main-upstream-baseline/"
       "baseline-run-profile.yaml"},
      {"profile_checksum", std::string(64, 'c')},
      {"profile_status", "draft"},
      {"source", {{"commit", native::kPinnedCommit}}},
      {"dataset_identity", {{"checksum", dataset.aggregate_checksum}}},
      {"dataset", Json::parse(dp::serialize_artifact_ref(dataset))},
      {"workspace",
       {{"root", workspace.root.string()},
        {"memory_budget_bytes", 1024ULL * 1024ULL * 1024ULL},
        {"thread_budget", 2}}},
      {"implementations",
       {{"partition", native::kP0ImplementationId},
        {"local_build", native::kB0ImplementationId},
        {"merge", native::kM0ImplementationId}}},
      {"stage_parameters",
       {{"partition",
         {{"R", 16},
          {"build_DRAM_budget_gib", 0.6},
          {"replication_factor", 2},
          {"sample_probability", 1.0}}},
        {"local_build", Json::parse(
             R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})")},
        {"merge", {{"R", merge_r}, {"use_filters", use_filters}}}}},
      {"stage_invocations",
       {{"partition", 0}, {"local_build", 0}, {"merge", 0}}},
      {"admission", {{"sharded_path_required", true}}},
      {"source_config",
       {{"yaml_sha256", std::string(64, 'a')},
        {"normalized_json_sha256", std::string(64, 'b')}}}};
}

struct PipelineFixture {
  explicit PipelineFixture(const std::string& label)
      : temporary(label + "-" + kRunNonce),
        workspace(dp::create_workspace(temporary.path / "workspace")),
        dataset(workspace, 96, 4),
        vectors(workspace, dataset.reference),
        environment(workspace, "bootstrap") {
    environment.context.vector_accessor = &vectors;
  }

  fixture::TemporaryDirectory temporary;
  dp::WorkspaceRef workspace;
  fixture::NativeDatasetFixture dataset;
  native::CppMainVectorAccessor vectors;
  fixture::TestContext environment;
};

dp::ImplementationRegistry registry_with(
    dp::PartitionFactory partition = native::make_p0_partition,
    dp::LocalBuildFactory local = native::make_b0_local_build,
    dp::MergeFactory merge = native::make_m0_merge) {
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_partition(native::p0_descriptor(), std::move(partition));
  registry.register_local_build(native::b0_descriptor(), std::move(local));
  registry.register_merge(native::m0_descriptor(), std::move(merge));
  registry.seal();
  return registry;
}

dp::ValidatedArtifact validate_terminal(
    const PipelineFixture& fixture_value,
    const dp::PipelineExecutionResult& result) {
  const auto dataset = dp::Validator(fixture_value.workspace)
                           .validate(fixture_value.dataset.reference);
  DP_EXPECT_TRUE(dataset.valid());
  const auto partition = dp::Validator(fixture_value.workspace)
                             .validate(*result.partition_artifact,
                                       {*dataset.validated_artifact});
  DP_EXPECT_TRUE(partition.valid());
  std::vector<dp::ValidatedArtifact> sources{*partition.validated_artifact};
  for (const auto& local : result.local_artifacts) {
    const auto validated = dp::Validator(fixture_value.workspace)
                               .validate(local, {*partition.validated_artifact});
    DP_EXPECT_TRUE(validated.valid());
    sources.push_back(*validated.validated_artifact);
  }
  const auto global =
      dp::Validator(fixture_value.workspace).validate(*result.artifact, sources);
  DP_EXPECT_TRUE(global.valid());
  return *global.validated_artifact;
}

int invoke_baseline_runner(const Json& document,
                           const std::filesystem::path& request_path,
                           const std::filesystem::path& result_path,
                           const std::filesystem::path& stdout_path,
                           const std::filesystem::path& stderr_path) {
  fixture::write_text(request_path, document.dump());
  const auto command =
      shell_quote(CPP_MAIN_BASELINE_RUNNER) + " execute --request " +
      shell_quote(request_path) + " --result " + shell_quote(result_path) +
      " >" + shell_quote(stdout_path) + " 2>" + shell_quote(stderr_path);
  return std::system(command.c_str());
}

void test_frozen_baseline_runner_parameters() {
  {
    fixture::TemporaryDirectory temporary("runner-r7-" + kRunNonce);
    const auto workspace = workspace_layout(temporary.path / "workspace");
    fixture::NativeDatasetFixture dataset(workspace, 32, 4);
    const auto request_path = temporary.path / "request.json";
    const auto result_path = workspace.root / "result.json";
    const auto stdout_path = temporary.path / "stdout.txt";
    const auto stderr_path = temporary.path / "stderr.txt";
    const auto status = invoke_baseline_runner(
        baseline_runner_request(workspace, dataset.reference,
                                "cpp-main-runner-r7", 7, false),
        request_path, result_path, stdout_path, stderr_path);
    DP_EXPECT_TRUE(status != 0);
    DP_EXPECT_TRUE(!std::filesystem::exists(result_path));
    DP_EXPECT_TRUE(!std::filesystem::exists(workspace.temporary_root));
    DP_EXPECT_TRUE(!std::filesystem::exists(workspace.manifest_root));
    DP_EXPECT_TRUE(read_text(stderr_path).find("merge.R") !=
                   std::string::npos);
  }

  {
    fixture::TemporaryDirectory temporary("runner-filters-" + kRunNonce);
    const auto workspace = workspace_layout(temporary.path / "workspace");
    fixture::NativeDatasetFixture dataset(workspace, 32, 4);
    const auto request_path = temporary.path / "request.json";
    const auto result_path = workspace.root / "result.json";
    const auto stdout_path = temporary.path / "stdout.txt";
    const auto stderr_path = temporary.path / "stderr.txt";
    const auto status = invoke_baseline_runner(
        baseline_runner_request(workspace, dataset.reference,
                                "cpp-main-runner-filters", 16, true),
        request_path, result_path, stdout_path, stderr_path);
    DP_EXPECT_TRUE(status != 0);
    DP_EXPECT_TRUE(!std::filesystem::exists(result_path));
    DP_EXPECT_TRUE(!std::filesystem::exists(workspace.temporary_root));
    DP_EXPECT_TRUE(!std::filesystem::exists(workspace.manifest_root));
    DP_EXPECT_TRUE(read_text(stderr_path).find("use_filters") !=
                   std::string::npos);
  }

  {
    fixture::TemporaryDirectory temporary("runner-r16-" + kRunNonce);
    const auto workspace = workspace_layout(temporary.path / "workspace");
    fixture::NativeDatasetFixture dataset(workspace, 32, 4);
    const auto request_path = temporary.path / "request.json";
    const auto result_path = workspace.root / "result.json";
    const auto stdout_path = temporary.path / "stdout.txt";
    const auto stderr_path = temporary.path / "stderr.txt";
    const auto status = invoke_baseline_runner(
        baseline_runner_request(workspace, dataset.reference,
                                "cpp-main-runner-r16", 16, false),
        request_path, result_path, stdout_path, stderr_path);
    DP_EXPECT_EQ(status, 0);
    DP_EXPECT_TRUE(std::filesystem::is_regular_file(result_path));
    if (std::filesystem::is_regular_file(result_path)) {
      const auto result = Json::parse(read_text(result_path));
      DP_EXPECT_EQ(result.at("outcome"), "SUCCEEDED");
      DP_EXPECT_EQ(result.at("stage_invocations").at("partition"), 1);
      DP_EXPECT_TRUE(
          result.at("stage_invocations").at("local_build").get<std::size_t>() >=
          std::size_t{2});
      DP_EXPECT_EQ(result.at("stage_invocations").at("merge"), 1);
    }
  }
}

}  // namespace

int main() {
  {
    PipelineFixture data("pipeline-success");
    auto registry = registry_with();
    const auto result = dp::PipelineExecutor(registry, data.workspace)
                            .execute(request("cpp-main-small-pipeline",
                                             data.dataset.reference),
                                     data.environment.context);
    if (result.failure) {
      std::cerr << "pipeline failure: " << result.failure->reason << '\n';
    }
    DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_TRUE(result.invocations.local_build >= std::size_t{2});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{1});
    DP_EXPECT_EQ(result.stage_order.front(), dp::StageKind::kPartition);
    DP_EXPECT_EQ(result.stage_order.back(), dp::StageKind::kMerge);
    DP_EXPECT_TRUE(std::is_sorted(result.local_partition_ids.begin(),
                                  result.local_partition_ids.end()));
    DP_EXPECT_EQ(result.local_partition_ids.size(), result.local_artifacts.size());
    DP_EXPECT_TRUE(result.artifact.has_value());
    DP_EXPECT_TRUE(result.evaluation_report_path.has_value());
    if (!result.artifact || !result.partition_artifact ||
        result.local_artifacts.empty() || !result.evaluation_report_path) {
      return dp::test::finish();
    }
    const auto validated = validate_terminal(data, result);
    DP_EXPECT_TRUE(validated.has_capability("graph.medoids/v1"));
    std::ifstream report_input(*result.evaluation_report_path);
    const auto report = dp::parse_evaluation_report(
        std::string(std::istreambuf_iterator<char>(report_input),
                    std::istreambuf_iterator<char>()));
    DP_EXPECT_EQ(report.evaluator.implementation_id,
                 "cpp_main_native_graph_integrity_evaluator");
    DP_EXPECT_EQ(report.evaluator.version, "1.0.0");
    DP_EXPECT_EQ(report.findings.logical_node_count,
                 data.dataset.identity.global_node_count);
    DP_EXPECT_EQ(report.findings.invalid_neighbor_count, dp::NodeCount{0});
    DP_EXPECT_TRUE(report.findings.passed);

    bool missing_evidence_rejected = false;
    try {
      static_cast<void>(dp::evaluate_validated_global_graph(
          data.workspace, validated, "manifests/evidence/missing.json",
          "missing-evidence", "2026-08-07T00:00:02Z"));
    } catch (const dp::PipelineError&) {
      missing_evidence_rejected = true;
    }
    DP_EXPECT_TRUE(missing_evidence_rejected);
  }

  {
    PipelineFixture data("pipeline-invalid-p0");
    auto registry = registry_with(missing_partition);
    const auto result = dp::PipelineExecutor(registry, data.workspace)
                            .execute(request("invalid-p0", data.dataset.reference),
                                     data.environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  }
  {
    PipelineFixture data("pipeline-invalid-b0");
    auto registry = registry_with(native::make_p0_partition, missing_local);
    const auto result = dp::PipelineExecutor(registry, data.workspace)
                            .execute(request("invalid-b0", data.dataset.reference),
                                     data.environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.local_build, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  }
  {
    PipelineFixture data("pipeline-invalid-m0");
    auto registry = registry_with(native::make_p0_partition,
                                  native::make_b0_local_build, missing_merge);
    const auto result = dp::PipelineExecutor(registry, data.workspace)
                            .execute(request("invalid-m0", data.dataset.reference),
                                     data.environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_TRUE(result.invocations.local_build >= std::size_t{2});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{1});
    DP_EXPECT_TRUE(!result.evaluation_report_path.has_value());
  }
  {
    PipelineFixture data("pipeline-cancelled");
    data.environment.cancellation.cancelled = true;
    auto registry = registry_with();
    const auto result = dp::PipelineExecutor(registry, data.workspace)
                            .execute(request("cancelled", data.dataset.reference),
                                     data.environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kCancelled);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  }

  test_frozen_baseline_runner_parameters();

  return dp::test::finish();
}
