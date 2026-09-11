#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;

namespace {

using Json = nlohmann::json;

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch()) %
                      std::chrono::seconds(1);
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setw(6) << std::setfill('0') << micros.count() << 'Z';
  return output.str();
}

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                            "cpp_main request cannot be read");
  }
  return Json::parse(input);
}

Json artifact_json(const dp::ArtifactRef& reference) {
  return Json::parse(dp::serialize_artifact_ref(reference));
}

std::filesystem::path relative_to(const std::filesystem::path& value,
                                  const std::filesystem::path& root) {
  std::error_code error;
  const auto relative = std::filesystem::relative(value, root, error);
  if (error || relative.empty() || relative.is_absolute()) {
    throw dp::PipelineError(dp::ErrorCategory::kPathEscape,
                            "cpp_main output reference escapes workspace");
  }
  return relative;
}

class L2Distance final : public dp::DistanceFunction {
 public:
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    if (lhs.size() != rhs.size()) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "distance vector dimensions differ");
    }
    double total = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      const auto difference = lhs[index] - rhs[index];
      total += difference * difference;
    }
    return total;
  }
};

class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string& name) override { events.push_back(name); }
  void increment(const std::string&, std::uint64_t) override {}
  std::vector<std::string> events;
};

class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

void require_request(const Json& request) {
  if (!request.is_object() || request.value("request_version", "") != "1.0.0" ||
      request.value("operation", "") != "execute" ||
      request.value("study_class", "") != "baseline_validation" ||
      request.value("profile_status", "") != "draft" ||
      request.at("source").at("commit") != native::kPinnedCommit ||
      !request.at("admission").at("sharded_path_required").get<bool>()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "cpp_main normalized request governance is invalid");
  }
  const auto& invocations = request.at("stage_invocations");
  if (invocations.at("partition") != 0 || invocations.at("local_build") != 0 ||
      invocations.at("merge") != 0) {
    throw dp::PipelineError(
        dp::ErrorCategory::kSchemaInvalid,
        "cpp_main execute request was not produced by non-running preflight");
  }
  const auto& implementations = request.at("implementations");
  if (implementations.at("partition") != native::kP0ImplementationId ||
      implementations.at("local_build") != native::kB0ImplementationId ||
      implementations.at("merge") != native::kM0ImplementationId) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "cpp_main implementation selection drifted");
  }

  if (!request.contains("stage_parameters") ||
      !request.at("stage_parameters").is_object() ||
      !request.at("stage_parameters").contains("merge") ||
      !request.at("stage_parameters").at("merge").is_object() ||
      !request.at("stage_parameters").at("merge").contains("R")) {
    throw dp::PipelineError(
        dp::ErrorCategory::kSchemaInvalid,
        "cpp_main frozen Baseline merge.R must be integer 16");
  }
  const auto& merge = request.at("stage_parameters").at("merge");
  const auto& degree = merge.at("R");
  const bool frozen_degree =
      (degree.is_number_unsigned() && degree.get<std::uint64_t>() == 16) ||
      (degree.is_number_integer() && !degree.is_number_unsigned() &&
       degree.get<std::int64_t>() == 16);
  if (!frozen_degree) {
    throw dp::PipelineError(
        dp::ErrorCategory::kSchemaInvalid,
        "cpp_main frozen Baseline merge.R must be integer 16");
  }
  if (!merge.contains("use_filters") ||
      !merge.at("use_filters").is_boolean() ||
      merge.at("use_filters").get<bool>()) {
    throw dp::PipelineError(
        dp::ErrorCategory::kSchemaInvalid,
        "cpp_main frozen Baseline merge.use_filters must be false");
  }
}

std::vector<Json> stage_results(const dp::PipelineExecutionResult& execution) {
  std::vector<Json> result;
  std::size_t local_index = 0;
  for (const auto stage : execution.stage_order) {
    Json item{{"stage", dp::to_string(stage)}, {"status", "SUCCEEDED"}};
    if (stage == dp::StageKind::kLocalBuild) {
      item["partition_id"] = execution.local_partition_ids.at(local_index++);
    }
    result.push_back(std::move(item));
  }
  return result;
}

Json terminal_result(const Json& request, const dp::PipelineExecutionResult& execution,
                     const dp::ArtifactRef& dataset,
                     const std::filesystem::path& build_manifest,
                     const dp::WorkspaceRef& workspace,
                     bool clean_before, bool clean_after) {
  Json artifacts{{"dataset", artifact_json(dataset)},
                 {"local_graphs", Json::array()}};
  if (execution.partition_artifact) {
    artifacts["partition"] = artifact_json(*execution.partition_artifact);
  }
  for (const auto& local : execution.local_artifacts) {
    artifacts["local_graphs"].push_back(artifact_json(local));
  }
  if (execution.artifact) {
    artifacts["global_graph"] = artifact_json(*execution.artifact);
  }
  Json manifests = Json::array(
      {relative_to(build_manifest, workspace.root).generic_string()});
  for (const auto& path : execution.build_manifest_paths) {
    manifests.push_back(relative_to(path, workspace.root).generic_string());
  }
  Json evidence = Json::array();
  for (const auto& path : execution.validation_evidence_paths) {
    evidence.push_back(relative_to(path, workspace.root).generic_string());
  }
  Json result{
      {"result_version", "cpp-main-baseline-result/v1"},
      {"request_id", request.at("request_id")},
      {"operation", "execute"},
      {"outcome", execution.status == dp::StageStatus::kSucceeded
                      ? "SUCCEEDED"
                      : dp::to_string(execution.status)},
      {"stage_invocations",
       {{"partition", execution.invocations.partition},
        {"local_build", execution.invocations.local_build},
        {"merge", execution.invocations.merge}}},
      {"local_partition_ids", execution.local_partition_ids},
      {"partition_count", execution.local_partition_ids.size()},
      {"stage_results", stage_results(execution)},
      {"artifacts", std::move(artifacts)},
      {"build_manifest_paths", std::move(manifests)},
      {"validation_evidence_paths", std::move(evidence)},
      {"upstream_clean_before", clean_before},
      {"upstream_clean_after", clean_after}};
  if (execution.evaluation_report_path) {
    result["evaluation_report_path"] =
        relative_to(*execution.evaluation_report_path, workspace.root)
            .generic_string();
  }
  if (execution.failure) {
    result["failure"] =
        {{"category", dp::to_string(execution.failure->category)},
         {"reason", execution.failure->reason}};
  }
  return result;
}

int exit_for(dp::StageStatus status) {
  switch (status) {
    case dp::StageStatus::kSucceeded:
      return 0;
    case dp::StageStatus::kCancelled:
      return 4;
    case dp::StageStatus::kInvalidOutput:
      return 5;
    default:
      return 4;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || std::string(argv[1]) != "execute" ||
      std::string(argv[2]) != "--request" ||
      std::string(argv[4]) != "--result") {
    std::cerr << "usage: cpp_main_baseline_runner execute --request FILE --result FILE\n";
    return 2;
  }
  try {
    const auto request_path = std::filesystem::path(argv[3]);
    const auto result_path = std::filesystem::path(argv[5]);
    const auto request = read_json(request_path);
    require_request(request);
    const auto root = std::filesystem::absolute(
        request.at("workspace").at("root").get<std::string>());
    dp::reject_upstream_write_target(root, std::filesystem::current_path());
    const auto workspace = dp::create_workspace(root);
    const auto result_relative = relative_to(
        std::filesystem::absolute(result_path), workspace.root);

    const auto started_at = now_utc();
    const auto source_before =
        native::admit_source_and_build(native::configured_source_build_admission());
    const bool clean_before = !source_before.source.dirty_worktree;
    const auto dataset = dp::parse_artifact_ref(request.at("dataset").dump());
    native::CppMainVectorAccessor vectors(workspace, dataset);

    auto build_record = native::make_build_manifest(
        source_before, request.at("request_id").get<std::string>() + "-build",
        started_at, now_utc());
    build_record.resource_budget = {
        request.at("workspace").at("memory_budget_bytes").get<std::uint64_t>(),
        request.at("workspace").at("thread_budget").get<std::uint32_t>()};
    build_record.parameters_json =
        Json{{"profile_ref", request.at("profile_ref")},
             {"profile_checksum", request.at("profile_checksum")},
             {"profile_status", "draft"}}
            .dump();
    const auto build_manifest =
        dp::persist_terminal_build_manifest(workspace, build_record);

    L2Distance distance;
    Metrics metrics;
    NeverCancelled cancellation;
    dp::UnavailableCheckpointStore checkpoints;
    auto bootstrap = dp::create_temporary_storage(
        workspace, request.at("request_id").get<std::string>() + "-bootstrap");
    dp::StageContext context{
        workspace,
        request.at("workspace").at("memory_budget_bytes").get<std::uint64_t>(),
        request.at("workspace").at("thread_budget").get<std::uint32_t>(),
        bootstrap,
        &vectors,
        &distance,
        &checkpoints,
        &metrics,
        &cancellation};

    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    native::register_cpp_main_stage_bindings(registry);
    registry.seal();
    const auto& identities = request.at("implementations");
    const auto& parameters = request.at("stage_parameters");
    dp::PipelineExecutionRequest execution_request;
    execution_request.run_id = request.at("request_id").get<std::string>();
    execution_request.dataset = dataset;
    execution_request.partition_implementation_id =
        identities.at("partition").get<std::string>();
    execution_request.local_implementation_id =
        identities.at("local_build").get<std::string>();
    execution_request.merge_implementation_id =
        identities.at("merge").get<std::string>();
    execution_request.partition_parameters_json =
        parameters.at("partition").dump();
    execution_request.local_parameters_json =
        parameters.at("local_build").dump();
    execution_request.merge_parameters_json = parameters.at("merge").dump();
    execution_request.source_config_json =
        request.at("source_config").dump();
    execution_request.source = build_record.source;
    execution_request.toolchain = build_record.toolchain;
    execution_request.started_at = started_at;
    execution_request.ended_at = now_utc();

    const auto execution =
        dp::PipelineExecutor(registry, workspace).execute(execution_request, context);
    const auto source_after =
        native::admit_source_and_build(native::configured_source_build_admission());
    const bool clean_after = !source_after.source.dirty_worktree;
    auto result = terminal_result(request, execution, dataset, build_manifest,
                                  workspace, clean_before, clean_after);
    dp::write_immutable_text(dp::resolve_confined_path(workspace.root,
                                                       result_relative),
                             result.dump());
    return exit_for(execution.status);
  } catch (const std::exception& error) {
    std::cerr << "cpp_main baseline runner failed: " << error.what() << '\n';
    return 10;
  }
}
