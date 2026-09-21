#include "papers/xn_graph/xn_descent/stage_binding.hpp"

#ifdef XN_DESCENT_EXPERIMENT_RUNNER_MAIN

int main(int argc, char** argv) {
  return diskann_pipeline::xn_descent::xn_descent_experiment_runner_main(
      argc, argv);
}

#else

#include "papers/xn_graph/xn_descent/parameters.hpp"

#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "validation/parameter_validator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace diskann_pipeline::xn_descent {
namespace {

using Json = nlohmann::json;

struct NormalizedRequest {
  Json document;
  Json source_config;
  ArtifactRef dataset;
  std::filesystem::path workspace_root;
  std::uint64_t memory_budget_bytes{0};
  std::uint32_t thread_budget{0};
};

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch()) %
                      std::chrono::seconds(1);
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setw(6) << std::setfill('0') << micros.count() << 'Z';
  return output.str();
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "XN-Descent request cannot be read");
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void require_exact_string(const Json& object, const char* key,
                          const char* expected) {
  if (!object.at(key).is_string() ||
      object.at(key).get<std::string>() != expected) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("XN-Descent request field ") + key +
                            " must equal " + expected);
  }
}

bool is_sha256(const Json& value) {
  if (!value.is_string()) return false;
  const auto text = value.get<std::string>();
  return text.size() == 64 &&
         std::all_of(text.begin(), text.end(), [](unsigned char character) {
           return std::isxdigit(character) != 0;
         });
}

bool safe_run_id(const std::string& value) {
  if (value.empty() || value == "." || value == "..") return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' ||
           character == '_' || character == '.';
  });
}

std::uint32_t require_merge_degree(const Json& merge) {
  if (!merge.is_object() || !merge.contains("R")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent merge.R is required");
  }
  const auto& value = merge.at("R");
  std::uint64_t parsed = 0;
  if (value.is_number_unsigned()) {
    parsed = value.get<std::uint64_t>();
  } else if (value.is_number_integer()) {
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value <= 0) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "XN-Descent merge.R must be an integer in 1..UINT32_MAX");
    }
    parsed = static_cast<std::uint64_t>(signed_value);
  } else {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "XN-Descent merge.R must be an integer in 1..UINT32_MAX");
  }
  if (parsed == 0 ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "XN-Descent merge.R must be an integer in 1..UINT32_MAX");
  }
  return static_cast<std::uint32_t>(parsed);
}

NormalizedRequest preflight(const std::string& request_json) {
  auto request = detail::parse_json_strict(request_json);
  if (!request.is_object() || !request.contains("request_version") ||
      !request.at("request_version").is_string()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent request_version must be a string");
  }
  const auto request_version =
      request.at("request_version").get<std::string>();
  if (request_version == "1.0.0") {
    detail::require_object_shape(
        request,
        {"request_version", "operation", "request_id", "study_class",
         "reproduction_mode", "replacement_scope", "profile", "workspace",
         "dataset", "implementations", "stage_parameters", "source_config"});
  } else if (request_version == "1.1.0") {
    detail::require_object_shape(
        request,
        {"request_version", "operation", "request_id", "study_class",
         "reproduction_mode", "replacement_scope", "profile", "workspace",
         "dataset", "implementations", "stage_parameters"});
  } else {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "XN-Descent request_version is unsupported");
  }
  require_exact_string(request, "operation", "execute");
  require_exact_string(request, "study_class",
                       "single_paper_reproduction");
  require_exact_string(request, "reproduction_mode", "faithful_adaptation");
  require_exact_string(request, "replacement_scope", "local_build_only");

  if (!request.at("request_id").is_string() ||
      !safe_run_id(request.at("request_id").get<std::string>())) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent request_id is not a safe run identity");
  }

  detail::require_object_shape(
      request.at("workspace"),
      {"root", "memory_budget_bytes", "thread_budget"});
  const auto& workspace = request.at("workspace");
  if (!workspace.at("root").is_string() ||
      workspace.at("root").get<std::string>().empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent workspace root must be non-empty");
  }
  const auto workspace_root =
      std::filesystem::path(workspace.at("root").get<std::string>());
  if (!workspace_root.is_absolute()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent workspace root must be absolute");
  }
  if (!workspace.at("memory_budget_bytes").is_number_unsigned() ||
      workspace.at("memory_budget_bytes").get<std::uint64_t>() == 0 ||
      !workspace.at("thread_budget").is_number_unsigned()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent workspace budgets must be positive");
  }
  const auto thread_budget_value =
      workspace.at("thread_budget").get<std::uint64_t>();
  if (thread_budget_value == 0 ||
      thread_budget_value >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::uint32_t>::max())) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent thread budget must be a positive uint32");
  }

  detail::require_object_shape(
      request.at("implementations"),
      {"partition", "local_build", "merge"});
  const auto& implementations = request.at("implementations");
  if (!implementations.at("partition").is_string() ||
      !implementations.at("local_build").is_string() ||
      !implementations.at("merge").is_string() ||
      implementations.at("partition").get<std::string>() !=
          cpp_main::kP0ImplementationId ||
      implementations.at("local_build").get<std::string>() !=
          kImplementationId ||
      implementations.at("merge").get<std::string>() !=
          cpp_main::kM0ImplementationId) {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "XN-Descent runner requires the fixed P0/B1/M0 composition");
  }

  detail::require_object_shape(
      request.at("stage_parameters"),
      {"partition", "local_build", "merge"});
  const auto& stage_parameters = request.at("stage_parameters");
  if (!stage_parameters.at("partition").is_object() ||
      !stage_parameters.at("local_build").is_object() ||
      !stage_parameters.at("merge").is_object()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent stage parameters must be objects");
  }
  static_cast<void>(require_merge_degree(stage_parameters.at("merge")));
  detail::validate_implementation_parameters(
      cpp_main::p0_descriptor(), stage_parameters.at("partition").dump(),
      XN_DESCENT_PROJECT_ROOT);
  detail::validate_implementation_parameters(
      descriptor(), stage_parameters.at("local_build").dump(),
      XN_DESCENT_PROJECT_ROOT);
  detail::validate_implementation_parameters(
      cpp_main::m0_descriptor(), stage_parameters.at("merge").dump(),
      XN_DESCENT_PROJECT_ROOT);
  const auto xn_parameters =
      parse_parameters(stage_parameters.at("local_build").dump());

  if (!request.at("profile").is_string()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent profile must be a string");
  }
  const auto profile = request.at("profile").get<std::string>();
  if (profile != to_string(xn_parameters.validation_profile)) {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "XN-Descent request profile differs from local parameters");
  }
  if (profile != "pipeline-compatible" && profile != "paper-style") {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "XN-Descent partitioned runner requires pipeline-compatible or "
        "paper-style profile");
  }

  Json source_config;
  if (request_version == "1.0.0") {
    detail::require_object_shape(
        request.at("source_config"),
        {"yaml_sha256", "normalized_json_sha256"});
    if (!is_sha256(request.at("source_config").at("yaml_sha256")) ||
        !is_sha256(
            request.at("source_config").at("normalized_json_sha256"))) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "XN-Descent source_config must contain two SHA-256 identities");
    }
    source_config = request.at("source_config");
  } else {
    source_config = {
        {"source_format", "json"},
        {"raw_request_sha256", sha256_bytes(request_json)},
        {"normalized_json_sha256", sha256_bytes(request.dump())},
        {"normalization_policy", "xn-json-compact-v1"}};
  }

  const auto dataset = parse_artifact_ref(request.at("dataset").dump());
  if (dataset.artifact_type != ArtifactType::kDataset ||
      dataset.format_version != "diskann-float32-dataset/v1") {
    throw PipelineError(
        ErrorCategory::kUnsupportedVersion,
        "XN-Descent runner requires diskann-float32-dataset/v1");
  }

  const auto memory_budget_bytes =
      workspace.at("memory_budget_bytes").get<std::uint64_t>();
  return {std::move(request), std::move(source_config), dataset,
          workspace_root, memory_budget_bytes,
          static_cast<std::uint32_t>(thread_budget_value)};
}

SourceIdentity project_source() {
  SourceIdentity source{XN_DESCENT_PROJECT_REPOSITORY,
                        XN_DESCENT_PROJECT_BRANCH,
                        XN_DESCENT_PROJECT_COMMIT,
                        XN_DESCENT_PROJECT_DIRTY != 0,
                        std::nullopt};
  if (source.dirty_worktree) {
    source.dirty_source_checksum = XN_DESCENT_PROJECT_DIRTY_CHECKSUM;
  }
  return source;
}

class SquaredL2Distance final : public DistanceFunction {
 public:
  DistanceSemantics semantics() const noexcept override {
    return DistanceSemantics::kSquaredL2;
  }
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    if (lhs.size() != rhs.size()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent distance dimensions differ");
    }
    double total = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      const auto delta = static_cast<double>(lhs[index]) - rhs[index];
      total += delta * delta;
    }
    return total;
  }
};

class Metrics final : public MetricsSink {
 public:
  void event(const std::string& name) override { events.push_back(name); }
  void increment(const std::string&, std::uint64_t) override {}
  std::vector<std::string> events;
};

class NeverCancelled final : public CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

int exit_for(StageStatus status) {
  if (status == StageStatus::kSucceeded) return 0;
  if (status == StageStatus::kInvalidOutput) return 5;
  return 4;
}

Json artifact_json(const ArtifactRef& value) {
  return Json::parse(serialize_artifact_ref(value));
}

Json result_document(const std::string& request_id,
                     const PipelineExecutionResult& execution) {
  Json local_graphs = Json::array();
  for (const auto& local : execution.local_artifacts) {
    local_graphs.push_back(artifact_json(local));
  }
  Json manifests = Json::array();
  for (const auto& path : execution.build_manifest_paths) {
    manifests.push_back(path.generic_string());
  }
  Json evidence = Json::array();
  for (const auto& path : execution.validation_evidence_paths) {
    evidence.push_back(path.generic_string());
  }
  Json result{
      {"result_version", "xn-descent-pipeline-result/v1"},
      {"request_id", request_id},
      {"operation", "execute"},
      {"outcome", to_string(execution.status)},
      {"study_class", "single_paper_reproduction"},
      {"reproduction_mode", "faithful_adaptation"},
      {"replacement_scope", "local_build_only"},
      {"claim_scope", "component-level_faithful_adaptation"},
      {"full_xn_graph_system_reproduction", false},
      {"excluded_components",
       Json::array({"boundary-adaptive-balanced-partition", "secondary-index",
                    "imf-search", "ssd-page-layout"})},
      {"stage_invocations",
       {{"partition", execution.invocations.partition},
        {"local_build", execution.invocations.local_build},
        {"merge", execution.invocations.merge}}},
      {"local_partition_ids", execution.local_partition_ids},
      {"local_graphs", std::move(local_graphs)},
      {"build_manifest_paths", std::move(manifests)},
      {"validation_evidence_paths", std::move(evidence)}};
  if (execution.partition_artifact) {
    result["partition_artifact"] = artifact_json(*execution.partition_artifact);
  }
  if (execution.artifact) {
    result["global_graph_artifact"] = artifact_json(*execution.artifact);
  }
  if (execution.evaluation_report_path) {
    result["evaluation_report_path"] =
        execution.evaluation_report_path->generic_string();
  }
  if (execution.quarantine_id) {
    result["quarantine_id"] = *execution.quarantine_id;
  }
  if (execution.failure) {
    result["failure"] =
        {{"category", to_string(execution.failure->category)},
         {"reason", execution.failure->reason}};
    if (execution.failure->exit_code) {
      result["failure"]["exit_code"] = *execution.failure->exit_code;
    }
  }
  return result;
}

ExperimentRunnerResult failure_result(std::string request_id,
                                      ErrorCategory category,
                                      const std::string& reason) {
  PipelineExecutionResult execution;
  execution.status = StageStatus::kFailed;
  execution.failure = FailureInfo{category, reason, std::nullopt};
  auto document = result_document(std::move(request_id), execution);
  return {4, std::move(execution), document.dump()};
}

ExperimentStageFactories default_factories() {
  return {cpp_main::make_p0_partition, make_xn_descent_local_build,
          cpp_main::make_m0_merge};
}

}  // namespace

ExperimentRunnerResult execute_pipeline_experiment_request(
    const std::string& request_json,
    const ExperimentStageFactories* stage_factories) {
  std::string request_id = "unavailable";
  std::optional<NormalizedRequest> request;
  try {
    const auto parsed = detail::parse_json_strict(request_json);
    if (parsed.is_object() && parsed.contains("request_id") &&
        parsed.at("request_id").is_string()) {
      request_id = parsed.at("request_id").get<std::string>();
    }
    request = preflight(request_json);
    request_id = request->document.at("request_id").get<std::string>();
  } catch (const PipelineError& error) {
    return failure_result(request_id, error.category(), error.what());
  } catch (const std::exception& error) {
    return failure_result(request_id, ErrorCategory::kSchemaInvalid,
                          error.what());
  }

  try {
    const auto diskann = cpp_main::admit_source_and_build(
        cpp_main::configured_source_build_admission());
    reject_upstream_write_target(request->workspace_root,
                                 XN_DESCENT_PROJECT_ROOT);
    const auto workspace = create_workspace(request->workspace_root);
    cpp_main::CppMainVectorAccessor vectors(workspace, request->dataset);
    SquaredL2Distance distance;
    Metrics metrics;
    NeverCancelled cancellation;
    UnavailableCheckpointStore checkpoints;
    const auto bootstrap = create_temporary_storage(
        workspace, request_id + "-bootstrap");
    StageContext context{workspace,
                         request->memory_budget_bytes,
                         request->thread_budget,
                         bootstrap,
                         &vectors,
                         &distance,
                         &checkpoints,
                         &metrics,
                         &cancellation};

    auto factories =
        stage_factories == nullptr ? default_factories() : *stage_factories;
    if (!factories.partition || !factories.local_build || !factories.merge) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent stage factory set is incomplete");
    }
    ImplementationRegistry registry(RegistryAdmissionPolicy::kProduction);
    registry.register_partition(cpp_main::p0_descriptor(),
                                std::move(factories.partition));
    registry.register_local_build(descriptor(),
                                  std::move(factories.local_build));
    registry.register_merge(cpp_main::m0_descriptor(),
                            std::move(factories.merge));
    registry.seal();

    const auto& document = request->document;
    const auto& implementations = document.at("implementations");
    const auto& parameters = document.at("stage_parameters");
    const auto build = cpp_main::make_build_manifest(
        diskann, request_id + "-build", now_utc(), now_utc());
    PipelineExecutionRequest execution_request;
    execution_request.run_id = request_id;
    execution_request.dataset = request->dataset;
    execution_request.partition_implementation_id =
        implementations.at("partition").get<std::string>();
    execution_request.local_implementation_id =
        implementations.at("local_build").get<std::string>();
    execution_request.merge_implementation_id =
        implementations.at("merge").get<std::string>();
    execution_request.partition_parameters_json =
        parameters.at("partition").dump();
    execution_request.local_parameters_json =
        parameters.at("local_build").dump();
    execution_request.merge_parameters_json = parameters.at("merge").dump();
    execution_request.source_config_json = request->source_config.dump();
    execution_request.source = project_source();
    execution_request.stage_sources = StageSourceIdentities{
        diskann.source, project_source(), diskann.source};
    execution_request.toolchain = build.toolchain;
    execution_request.started_at = now_utc();
    execution_request.ended_at = now_utc();

    auto execution =
        PipelineExecutor(registry, workspace).execute(execution_request, context);
    const auto exit_code = exit_for(execution.status);
    auto result = result_document(request_id, execution);
    return {exit_code, std::move(execution), result.dump()};
  } catch (const PipelineError& error) {
    return failure_result(request_id, error.category(), error.what());
  } catch (const std::exception& error) {
    return failure_result(request_id, ErrorCategory::kInternal, error.what());
  }
}

int xn_descent_experiment_runner_main(int argc, char** argv) {
  if ((argc != 3 && argc != 5) || std::string(argv[1]) != "--request" ||
      (argc == 5 && std::string(argv[3]) != "--result")) {
    std::cerr << "usage: xn_descent_experiment_runner --request FILE "
                 "[--result FILE]\n";
    return 2;
  }
  try {
    const auto result = execute_pipeline_experiment_request(read_text(argv[2]));
    if (argc == 5) {
      const auto output = std::filesystem::absolute(argv[4]);
      reject_upstream_write_target(output, XN_DESCENT_PROJECT_ROOT);
      write_immutable_text(output, result.result_json);
    } else {
      std::cout << result.result_json << '\n';
    }
    return result.exit_code;
  } catch (const std::exception& error) {
    std::cerr << "XN-Descent experiment runner failed: " << error.what()
              << '\n';
    return 10;
  }
}

}  // namespace diskann_pipeline::xn_descent

#endif
