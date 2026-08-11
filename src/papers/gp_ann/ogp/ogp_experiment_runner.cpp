#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace ogp = diskann_pipeline::gp_ann_ogp;

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
                            "OGP request cannot be read");
  }
  return Json::parse(input);
}

std::filesystem::path relative_to(const std::filesystem::path& value,
                                  const std::filesystem::path& root) {
  std::error_code error;
  const auto relative = std::filesystem::relative(value, root, error);
  if (error || relative.empty() || relative.is_absolute()) {
    throw dp::PipelineError(dp::ErrorCategory::kPathEscape,
                            "OGP result reference escapes workspace");
  }
  static_cast<void>(dp::resolve_confined_path(root, relative));
  return relative;
}

bool source_matches(const Json& value, const dp::SourceIdentity& expected) {
  return value.is_object() && value.value("repository", "") == expected.repository &&
         value.value("branch", "") == expected.branch &&
         value.value("commit", "") == expected.commit &&
         !value.value("dirty_worktree", true);
}

void require_request(const Json& request,
                     const ogp::GpAnnSourceBuildEvidence& gpann,
                     const native::SourceBuildEvidence& diskann) {
  if (!request.is_object() || request.value("request_version", "") != "1.0.0" ||
      request.value("operation", "") != "execute" ||
      request.value("study_class", "") != "single_paper_reproduction" ||
      request.value("reproduction_mode", "") != "faithful_adaptation" ||
      request.value("replacement_scope", "") != "partition") {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "OGP normalized request governance is invalid");
  }
  const auto& identities = request.at("implementations");
  if (identities.at("partition") != ogp::kImplementationId ||
      identities.at("local_build") != native::kB0ImplementationId ||
      identities.at("merge") != native::kM0ImplementationId) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "OGP runner implementation selection drifted");
  }
  const auto& sources = request.at("stage_sources");
  if (!source_matches(sources.at("partition"), gpann.source) ||
      !source_matches(sources.at("local_build"), diskann.source) ||
      !source_matches(sources.at("merge"), diskann.source)) {
    throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,
                            "OGP stage source identity differs from admission");
  }
  const auto& source_config = request.at("source_config");
  if (!source_config.is_object() || source_config.size() != 2 ||
      !source_config.contains("yaml_sha256") ||
      !source_config.contains("normalized_json_sha256")) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "OGP source_config is not the shared two-digest identity");
  }
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
      const auto delta = lhs[index] - rhs[index];
      total += delta * delta;
    }
    return total;
  }
};

class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string& value) override { events.push_back(value); }
  void increment(const std::string&, std::uint64_t) override {}
  std::vector<std::string> events;
};

class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

int exit_for(dp::StageStatus status) {
  if (status == dp::StageStatus::kSucceeded) return 0;
  if (status == dp::StageStatus::kInvalidOutput) return 5;
  return 4;
}

Json terminal_result(const Json& request,
                     const dp::PipelineExecutionResult& execution,
                     const dp::WorkspaceRef& workspace) {
  const auto exit_code = exit_for(execution.status);
  Json result{{"result_version", "1.0.0"},
              {"request_id", request.at("request_id")},
              {"operation", "execute"},
              {"outcome", dp::to_string(execution.status)},
              {"exit_code", exit_code},
              {"stage_invocations",
               {{"partition", execution.invocations.partition},
                {"local_build", execution.invocations.local_build},
                {"merge", execution.invocations.merge}}}};
  if (execution.artifact) {
    result["artifact"] = Json::parse(dp::serialize_artifact_ref(*execution.artifact));
  }
  if (!execution.build_manifest_paths.empty()) {
    result["build_manifest_path"] =
        relative_to(execution.build_manifest_paths.back(), workspace.root)
            .generic_string();
  }
  if (!execution.validation_evidence_paths.empty()) {
    result["validation_evidence_path"] =
        relative_to(execution.validation_evidence_paths.back(), workspace.root)
            .generic_string();
  }
  if (execution.evaluation_report_path) {
    result["evaluation_report_path"] =
        relative_to(*execution.evaluation_report_path, workspace.root)
            .generic_string();
  }
  if (execution.quarantine_id) result["quarantine_id"] = *execution.quarantine_id;
  if (execution.failure) {
    result["failure"] = {{"category", dp::to_string(execution.failure->category)},
                         {"reason", execution.failure->reason}};
    if (execution.failure->exit_code) {
      result["failure"]["exit_code"] = *execution.failure->exit_code;
    }
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || std::string(argv[1]) != "execute" ||
      std::string(argv[2]) != "--request" ||
      std::string(argv[4]) != "--result") {
    std::cerr << "usage: ogp_experiment_runner execute --request FILE --result FILE\n";
    return 2;
  }
  try {
    const auto request = read_json(argv[3]);
    const auto gpann =
        ogp::admit_source_and_build(ogp::configured_source_build_admission());
    const auto diskann = native::admit_source_and_build(
        native::configured_source_build_admission());
    require_request(request, gpann, diskann);

    const auto root = std::filesystem::absolute(
        request.at("workspace").at("root").get<std::string>());
    dp::reject_upstream_write_target(root, std::filesystem::current_path());
    const auto workspace = dp::create_workspace(root);
    const auto result_relative = relative_to(
        std::filesystem::absolute(argv[5]), workspace.root);
    const auto dataset = dp::parse_artifact_ref(request.at("dataset").dump());
    native::CppMainVectorAccessor vectors(workspace, dataset);
    const auto build = native::make_build_manifest(
        diskann, request.at("request_id").get<std::string>() + "-build",
        now_utc(), now_utc());

    L2Distance distance;
    Metrics metrics;
    NeverCancelled cancellation;
    dp::UnavailableCheckpointStore checkpoints;
    const auto bootstrap = dp::create_temporary_storage(
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
    registry.register_partition(ogp::descriptor(),
                                [gpann] { return ogp::make_ogp_partition(gpann); });
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
    execution_request.partition_parameters_json = parameters.at("partition").dump();
    execution_request.local_parameters_json = parameters.at("local_build").dump();
    execution_request.merge_parameters_json = parameters.at("merge").dump();
    execution_request.source_config_json = request.at("source_config").dump();
    execution_request.source = diskann.source;
    execution_request.stage_sources =
        dp::StageSourceIdentities{gpann.source, diskann.source, diskann.source};
    execution_request.toolchain = build.toolchain;
    execution_request.started_at = now_utc();
    execution_request.ended_at = now_utc();

    const auto execution =
        dp::PipelineExecutor(registry, workspace).execute(execution_request, context);
    static_cast<void>(ogp::admit_source_and_build(
        ogp::configured_source_build_admission()));
    static_cast<void>(native::admit_source_and_build(
        native::configured_source_build_admission()));
    const auto result = terminal_result(request, execution, workspace);
    dp::write_immutable_text(
        dp::resolve_confined_path(workspace.root, result_relative), result.dump());
    return exit_for(execution.status);
  } catch (const std::exception& error) {
    std::cerr << "OGP experiment runner failed: " << error.what() << '\n';
    return 10;
  }
}
