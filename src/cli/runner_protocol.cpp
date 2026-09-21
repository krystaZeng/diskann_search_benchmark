#include "diskann_pipeline/runner_protocol.hpp"

#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "core/json_codec.hpp"
#include "validation/parameter_validator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifndef DISKANN_PIPELINE_SOURCE_REPOSITORY
#define DISKANN_PIPELINE_SOURCE_REPOSITORY "unknown"
#endif
#ifndef DISKANN_PIPELINE_SOURCE_BRANCH
#define DISKANN_PIPELINE_SOURCE_BRANCH "unknown"
#endif
#ifndef DISKANN_PIPELINE_SOURCE_COMMIT
#define DISKANN_PIPELINE_SOURCE_COMMIT "unknown"
#endif
#ifndef DISKANN_PIPELINE_SOURCE_DIRTY
#define DISKANN_PIPELINE_SOURCE_DIRTY 1
#endif
#ifndef DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM
#define DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM \
  "0000000000000000000000000000000000000000000000000000000000000000"
#endif
#ifndef DISKANN_PIPELINE_SYSTEM_NAME
#define DISKANN_PIPELINE_SYSTEM_NAME "unknown"
#endif
#ifndef DISKANN_PIPELINE_CXX_COMPILER
#define DISKANN_PIPELINE_CXX_COMPILER "unknown"
#endif
#ifndef DISKANN_PIPELINE_CXX_COMPILER_VERSION
#define DISKANN_PIPELINE_CXX_COMPILER_VERSION "unknown"
#endif
#ifndef DISKANN_PIPELINE_CMAKE_VERSION
#define DISKANN_PIPELINE_CMAKE_VERSION "unknown"
#endif
#ifndef DISKANN_PIPELINE_CMAKE_GENERATOR
#define DISKANN_PIPELINE_CMAKE_GENERATOR "unknown"
#endif
#ifndef DISKANN_PIPELINE_BUILD_TYPE
#define DISKANN_PIPELINE_BUILD_TYPE "unknown"
#endif
#ifndef DISKANN_PIPELINE_BINARY_DIR
#define DISKANN_PIPELINE_BINARY_DIR ""
#endif
#ifndef DISKANN_PIPELINE_PROJECT_ROOT
#define DISKANN_PIPELINE_PROJECT_ROOT ""
#endif

namespace diskann_pipeline {
namespace {

using Json = nlohmann::json;

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner request cannot be read");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return detail::parse_json_strict(bytes.str());
}

void validate_workspace_shape(const Json& workspace) {
  detail::require_object_shape(
      workspace,
      {"root", "normal_root", "temporary_root", "manifest_root",
       "report_root", "quarantine_root", "memory_budget_bytes",
       "thread_budget"});
  for (const auto* field : {"root", "normal_root", "temporary_root",
                            "manifest_root", "report_root",
                            "quarantine_root"}) {
    if (!workspace.at(field).is_string() ||
        workspace.at(field).get_ref<const std::string&>().empty()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "runner workspace path is not a string");
    }
  }
  if ((!workspace.at("memory_budget_bytes").is_number_unsigned() &&
       !workspace.at("memory_budget_bytes").is_number_integer()) ||
      (!workspace.at("thread_budget").is_number_unsigned() &&
       !workspace.at("thread_budget").is_number_integer())) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner workspace budgets are not integers");
  }
  if ((workspace.at("memory_budget_bytes").is_number_integer() &&
       workspace.at("memory_budget_bytes").get<std::int64_t>() <= 0) ||
      (workspace.at("thread_budget").is_number_integer() &&
       workspace.at("thread_budget").get<std::int64_t>() <= 0)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner workspace budgets must be positive");
  }
  const auto root = std::filesystem::path(workspace.at("root").get<std::string>())
                        .lexically_normal();
  if (root.empty() || !root.is_absolute() ||
      workspace.at("memory_budget_bytes").get<std::uint64_t>() == 0 ||
      workspace.at("thread_budget").get<std::uint32_t>() == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner workspace is not normalized");
  }
  for (const auto& item : {
           std::pair<const char*, const char*>{"normal_root", "normal"},
           {"temporary_root", "temporary"}, {"manifest_root", "manifests"},
           {"report_root", "reports"}, {"quarantine_root", "quarantine"}}) {
    const auto observed =
        std::filesystem::path(workspace.at(item.first).get<std::string>())
            .lexically_normal();
    if (observed != (root / item.second).lexically_normal()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "runner workspace lifecycle roots are inconsistent");
    }
  }
}

void validate_request_shape(const Json& request) {
  detail::require_object_shape(
      request,
      {"request_version", "request_id", "operation", "study_class",
       "workspace"},
      {"dataset", "implementations", "stage_parameters",
       "repetition_policy", "reuse_artifact", "target_artifact",
       "expected_contract", "evaluator", "source_config"});
  if (!request.at("request_id").is_string() ||
      request.at("request_id").get_ref<const std::string&>().empty() ||
      !request.at("operation").is_string() ||
      !request.at("request_version").is_string() ||
      !request.at("study_class").is_string() ||
      request.at("request_version") != "1.0.0" ||
      request.at("study_class") != "infrastructure_validation") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner request governance fields are invalid");
  }
  const auto operation = request.at("operation").get<std::string>();
  if (operation != "preflight" && operation != "execute" &&
      operation != "validate" && operation != "evaluate") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unknown runner operation");
  }
  validate_workspace_shape(request.at("workspace"));
  if (operation == "preflight" || operation == "execute") {
    for (const auto* field : {"dataset", "implementations",
                              "stage_parameters", "repetition_policy"}) {
      if (!request.contains(field)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            std::string("runner request is missing ") + field);
      }
    }
    detail::require_object_shape(
        request.at("implementations"), {"partition", "local_build", "merge"});
    detail::require_object_shape(
        request.at("stage_parameters"), {"partition", "local_build", "merge"});
    for (const auto* stage : {"partition", "local_build", "merge"}) {
      if (!request.at("stage_parameters").at(stage).is_object()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "stage parameters must be JSON objects");
      }
    }
    detail::require_object_shape(request.at("repetition_policy"),
                                 {"count", "aggregation"});
    if (request.at("repetition_policy").at("count") != 2 ||
        request.at("repetition_policy").at("aggregation") != "retain_all") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "runner requires two retained runs");
    }
    const auto dataset = parse_artifact_ref(request.at("dataset").dump());
    if (dataset.artifact_type != ArtifactType::kDataset ||
        dataset.format_version != "fake-dataset-json/v1" ||
        dataset.aggregate_checksum !=
            "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "Feature 001 runner accepts only the canonical Fake dataset");
    }
    if (request.contains("reuse_artifact")) {
      static_cast<void>(
          parse_artifact_ref(request.at("reuse_artifact").dump()));
    }
  } else if (operation == "validate") {
    if (!request.contains("target_artifact") ||
        !request.contains("expected_contract")) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "validate request is incomplete");
    }
    static_cast<void>(parse_artifact_ref(request.at("target_artifact").dump()));
    detail::require_object_shape(
        request.at("expected_contract"),
        {"artifact_type", "accepted_format_versions", "id_space",
         "capabilities"});
  } else if (!request.contains("target_artifact") ||
             !request.contains("evaluator")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "evaluate request is incomplete");
  } else {
    static_cast<void>(parse_artifact_ref(request.at("target_artifact").dump()));
    detail::require_object_shape(request.at("evaluator"),
                                 {"implementation_id", "parameters"});
  }
  if (request.contains("source_config")) {
    detail::require_object_shape(
        request.at("source_config"),
        {"yaml_sha256", "normalized_json_sha256"});
  }
}

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

std::filesystem::path relative_to(const std::filesystem::path& path,
                                  const std::filesystem::path& root) {
  std::error_code error;
  const auto result = std::filesystem::relative(path, root, error);
  if (error || result.empty() || result.is_absolute()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "runner output path is outside workspace");
  }
  return result;
}

Json artifact_ref_json(const ArtifactRef& reference) {
  return Json::parse(serialize_artifact_ref(reference));
}

Json failure_json(const FailureInfo& failure) {
  Json result{{"category", to_string(failure.category)},
              {"reason", failure.reason}};
  if (failure.exit_code) {
    result["exit_code"] = *failure.exit_code;
  }
  return result;
}

Json base_result(const std::string& request_id, const std::string& operation) {
  return {{"result_version", "1.0.0"},
          {"request_id", request_id},
          {"operation", operation},
          {"outcome", "FAILED"},
          {"exit_code", static_cast<int>(RunnerExitCode::kInternal)},
          {"stage_invocations",
           {{"partition", 0}, {"local_build", 0}, {"merge", 0}}}};
}

RunnerDispatchResult encoded(Json result, RunnerExitCode code) {
  result["exit_code"] = static_cast<int>(code);
  return {static_cast<int>(code), result.dump()};
}

SourceIdentity configured_source() {
  SourceIdentity result{DISKANN_PIPELINE_SOURCE_REPOSITORY,
                        DISKANN_PIPELINE_SOURCE_BRANCH,
                        DISKANN_PIPELINE_SOURCE_COMMIT,
                        DISKANN_PIPELINE_SOURCE_DIRTY != 0,
                        std::nullopt};
  if (result.dirty_worktree) {
    result.dirty_source_checksum = DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM;
  }
  return result;
}

ToolchainIdentity configured_toolchain() {
  const std::filesystem::path binary_root = DISKANN_PIPELINE_BINARY_DIR;
  const auto harness_manifest = binary_root / "build-manifest.json";
  if (std::filesystem::is_regular_file(harness_manifest)) {
    try {
      const auto toolchain = read_json(harness_manifest).at("toolchain");
      return {toolchain.at("os").get<std::string>(),
              toolchain.at("compiler_path").get<std::string>(),
              toolchain.at("compiler_version").get<std::string>(),
              toolchain.at("cmake_version").get<std::string>(),
              toolchain.at("generator").get<std::string>(),
              toolchain.at("build_type").get<std::string>(),
              toolchain.at("cmake_definitions")
                  .get<std::vector<std::string>>(),
              toolchain.at("compile_commands")
                  .get<std::vector<std::string>>(),
              toolchain.at("link_commands").get<std::vector<std::string>>()};
    } catch (const std::exception&) {
      // Fall through to configured facts when no Harness manifest is usable.
    }
  }
  return {DISKANN_PIPELINE_SYSTEM_NAME,
          DISKANN_PIPELINE_CXX_COMPILER,
          DISKANN_PIPELINE_CXX_COMPILER_VERSION,
          DISKANN_PIPELINE_CMAKE_VERSION,
          DISKANN_PIPELINE_CMAKE_GENERATOR,
          DISKANN_PIPELINE_BUILD_TYPE,
          {"CMAKE_CXX_STANDARD=17", "CMAKE_CXX_STANDARD_REQUIRED=ON",
           "CMAKE_CXX_EXTENSIONS=OFF",
           std::string("CMAKE_BUILD_TYPE=") + DISKANN_PIPELINE_BUILD_TYPE,
           "PROJECT_COMPILE_OPTIONS=-Wall;-Wextra;-Wpedantic"},
          {}, {}};
}

CompatibilityResult request_preflight(const Json& request,
                                      ImplementationRegistry& registry,
                                      const ValidatedArtifact& dataset,
                                      const StageContext& context) {
  const auto& ids = request.at("implementations");
  const auto& partition = registry.resolve_descriptor(
      ids.at("partition").get<std::string>(), "1.0.0",
      StageKind::kPartition);
  const auto& local = registry.resolve_descriptor(
      ids.at("local_build").get<std::string>(), "1.0.0",
      StageKind::kLocalBuild);
  const auto& merge = registry.resolve_descriptor(
      ids.at("merge").get<std::string>(), "1.0.0", StageKind::kMerge);
  for (const auto* consumer : {&partition, &local, &merge}) {
    // A reused Partition does not execute its selected producer's services.
    if (consumer == &partition && request.contains("reuse_artifact")) continue;
    const auto services = check_runtime_services(
        consumer->requires, context, dataset.dataset());
    if (!services.compatible) return services;
  }
  auto partition_contract = partition;
  if (request.contains("reuse_artifact")) {
    partition_contract.requires.required_services.clear();
  }
  return CompatibilityChecker{}.check_chain(
      {partition_contract, local, merge},
      {{dataset.reference().artifact_type, dataset.reference().format_version,
        dataset.id_space(), dataset.capabilities()}},
      runtime_service_capabilities(context, dataset.dataset()));
}

void validate_request_parameters(const Json& request,
                                 ImplementationRegistry& registry) {
  const auto& ids = request.at("implementations");
  const auto& parameters = request.at("stage_parameters");
  for (const auto& selection : {
           std::pair<const char*, StageKind>{"partition", StageKind::kPartition},
           {"local_build", StageKind::kLocalBuild},
           {"merge", StageKind::kMerge}}) {
    const auto& descriptor = registry.resolve_descriptor(
        ids.at(selection.first).get<std::string>(), "1.0.0",
        selection.second);
    detail::validate_implementation_parameters(
        descriptor, parameters.at(selection.first).dump(),
        DISKANN_PIPELINE_PROJECT_ROOT);
  }
}

std::vector<ArtifactRef> catalog_artifacts(const WorkspaceRef& workspace) {
  std::vector<ArtifactRef> result;
  std::error_code error;
  if (!std::filesystem::exists(workspace.normal_root)) return result;
  for (std::filesystem::recursive_directory_iterator iterator(
           workspace.normal_root, error),
       end;
       iterator != end && !error; iterator.increment(error)) {
    if (!iterator->is_regular_file() || iterator->path().filename() !=
                                                "artifact.json") {
      continue;
    }
    try {
      const auto metadata = read_json(iterator->path());
      const auto relative = relative_to(iterator->path(), workspace.normal_root);
      result.push_back(
          {relative,
           artifact_type_from_string(
               metadata.at("artifact_type").get<std::string>()),
           metadata.at("format_version").get<std::string>(),
           metadata.at("aggregate_checksum").get<std::string>()});
    } catch (const std::exception&) {
      // Invalid documents remain discoverable only through their explicit ref.
    }
  }
  return result;
}

bool same_identity(const SourceArtifactIdentity& source,
                   const ValidatedArtifact& artifact) {
  return source.artifact_type == artifact.reference().artifact_type &&
         source.format_version == artifact.reference().format_version &&
         source.aggregate_checksum == artifact.reference().aggregate_checksum;
}

std::optional<ValidatedArtifact> validate_from_catalog(
    const WorkspaceRef& workspace, const ArtifactRef& target,
    ValidationEvidence* final_evidence) {
  auto references = catalog_artifacts(workspace);
  if (std::none_of(references.begin(), references.end(), [&](const auto& value) {
        return value.metadata_path == target.metadata_path;
      })) {
    references.push_back(target);
  }
  std::vector<ValidatedArtifact> grants;
  std::vector<bool> resolved(references.size(), false);
  const Validator validator(workspace);
  for (std::size_t pass = 0; pass <= references.size(); ++pass) {
    bool progress = false;
    for (std::size_t index = 0; index < references.size(); ++index) {
      if (resolved[index]) continue;
      const auto metadata_path = resolve_normal_artifact_metadata(
          workspace, references[index].metadata_path);
      Json metadata;
      try {
        metadata = read_json(metadata_path);
      } catch (const PipelineError&) {
        continue;
      }
      std::vector<ValidatedArtifact> sources;
      bool ready = true;
      for (const auto& source_json : metadata.at("source_artifacts")) {
        const SourceArtifactIdentity source{
            artifact_type_from_string(
                source_json.at("artifact_type").get<std::string>()),
            source_json.at("format_version").get<std::string>(),
            source_json.at("aggregate_checksum").get<std::string>()};
        const auto found = std::find_if(
            grants.begin(), grants.end(),
            [&](const auto& grant) { return same_identity(source, grant); });
        if (found == grants.end()) {
          ready = false;
          break;
        }
        sources.push_back(*found);
      }
      if (!ready) continue;
      const bool is_target =
          references[index].metadata_path == target.metadata_path;
      // Keep the caller's target identity authoritative, even when the
      // catalog already contains valid metadata at the same path.
      auto validation = validator.validate(
          is_target ? target : references[index], sources);
      if (!validation.valid()) {
        if (is_target) {
          *final_evidence = validation.evidence;
        }
        resolved[index] = true;
        progress = true;
        continue;
      }
      grants.push_back(*validation.validated_artifact);
      resolved[index] = true;
      progress = true;
      if (is_target) {
        *final_evidence = validation.evidence;
        return validation.validated_artifact;
      }
    }
    if (!progress) break;
  }
  const auto validation = validator.validate(target);
  *final_evidence = validation.evidence;
  return validation.validated_artifact;
}

RunnerDispatchResult dispatch_validation(const Json& request,
                                         const WorkspaceRef& workspace,
                                         Json result) {
  const auto target = parse_artifact_ref(request.at("target_artifact").dump());
  ValidationEvidence evidence;
  auto validated = validate_from_catalog(workspace, target, &evidence);
  const auto evidence_path = persist_validation_evidence(
      workspace, request.at("request_id").get<std::string>() + "-validate",
      evidence);
  result["validation_evidence_path"] =
      relative_to(evidence_path, workspace.root).generic_string();
  if (!validated) {
    const FailureInfo failure{
        evidence.failure_category.value_or(ErrorCategory::kSchemaInvalid),
        evidence.checks.empty() ? "Artifact validation failed"
                                : evidence.checks.back().message,
        std::nullopt};
    result["outcome"] = "FAILED";
    result["failure"] = failure_json(failure);
    return encoded(std::move(result), RunnerExitCode::kValidationFailed);
  }
  if (request.contains("expected_contract")) {
    const auto& expected = request.at("expected_contract");
    const ImplementationDescriptor consumer{
        "runner_validation_consumer", "1.0.0", StageKind::kMerge,
        {{{artifact_type_from_string(
               expected.at("artifact_type").get<std::string>()),
           expected.at("accepted_format_versions")
               .get<std::vector<std::string>>(),
           id_space_from_string(expected.at("id_space").get<std::string>())}},
         expected.at("capabilities").get<std::vector<std::string>>(), {}},
        {ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
         IdSpace::kGlobal, {}},
        "contracts/v1/schemas/fake-parameters.schema.json", false};
    const auto compatibility = CompatibilityChecker{}.check(
        consumer,
        {{validated->reference().artifact_type,
          validated->reference().format_version, validated->id_space(),
          validated->capabilities()}},
        {{}, false});
    if (!compatibility.compatible) {
      result["outcome"] = "FAILED";
      result["failure"] = failure_json(*compatibility.failure);
      return encoded(std::move(result), RunnerExitCode::kValidationFailed);
    }
  }
  result["outcome"] = "SUCCEEDED";
  result["artifact"] = artifact_ref_json(target);
  return encoded(std::move(result), RunnerExitCode::kSuccess);
}

RunnerDispatchResult dispatch_evaluation(const Json& request,
                                         const WorkspaceRef& workspace,
                                         Json result) {
  const auto target = parse_artifact_ref(request.at("target_artifact").dump());
  ValidationEvidence evidence;
  auto validated = validate_from_catalog(workspace, target, &evidence);
  if (!validated) {
    const FailureInfo failure{
        evidence.failure_category.value_or(ErrorCategory::kSchemaInvalid),
        "evaluation input failed fresh validation", std::nullopt};
    result["outcome"] = "FAILED";
    result["failure"] = failure_json(failure);
    return encoded(std::move(result), RunnerExitCode::kEvaluationFailed);
  }
  const auto evidence_path = persist_validation_evidence(
      workspace, request.at("request_id").get<std::string>() + "-evaluate",
      evidence);
  const auto report = evaluate_validated_global_graph(
      workspace, *validated, relative_to(evidence_path, workspace.root),
      request.at("request_id").get<std::string>() + "-evaluation", now_utc());
  const auto report_path = persist_evaluation_report(workspace, report);
  result["outcome"] = "SUCCEEDED";
  result["artifact"] = artifact_ref_json(target);
  result["validation_evidence_path"] =
      relative_to(evidence_path, workspace.root).generic_string();
  result["evaluation_report_path"] =
      relative_to(report_path, workspace.root).generic_string();
  return encoded(std::move(result), RunnerExitCode::kSuccess);
}

}  // namespace

RunnerRequestPrelude inspect_runner_request(
    const std::filesystem::path& request_path,
    std::string_view expected_operation) {
  const auto request = read_json(request_path);
  validate_request_shape(request);
  const auto operation = request.at("operation").get<std::string>();
  if (!expected_operation.empty() && expected_operation != operation) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "runner command differs from request operation");
  }
  const auto root =
      request.at("workspace").at("root").get<std::string>();
  reject_runner_write_target(root);
  RunnerRequestPrelude prelude{
      request.at("request_id").get<std::string>(), operation, root,
      std::nullopt};
  if (request.contains("reuse_artifact")) {
    prelude.reuse_artifact =
        parse_artifact_ref(request.at("reuse_artifact").dump());
  }
  return prelude;
}

RunnerDispatchResult dispatch_runner_request(
    const std::filesystem::path& request_path,
    ImplementationRegistry& registry, StageContext& context) {
  return dispatch_runner_request(request_path, {}, registry, context);
}

RunnerDispatchResult dispatch_runner_request(
    const std::filesystem::path& request_path,
    std::string_view expected_operation,
    ImplementationRegistry& registry, StageContext& context) {
  Json request;
  Json result = base_result("invalid-request", "execute");
  try {
    request = read_json(request_path);
    validate_request_shape(request);
    const auto request_id = request.at("request_id").get<std::string>();
    const auto operation = request.at("operation").get<std::string>();
    if (!expected_operation.empty() && expected_operation != operation) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "runner command differs from request operation");
    }
    result = base_result(request_id, operation);
    const auto& workspace_json = request.at("workspace");
    reject_runner_write_target(
        workspace_json.at("root").get<std::string>());
    const auto workspace = create_workspace(
        workspace_json.at("root").get<std::string>());
    context.workspace = workspace;
    context.memory_budget_bytes =
        workspace_json.at("memory_budget_bytes").get<std::uint64_t>();
    context.thread_budget =
        workspace_json.at("thread_budget").get<std::uint32_t>();
    context.temporary_storage =
        create_temporary_storage(workspace, request_id + "-runner");
    validate_stage_context(context);

    if (operation == "validate") {
      return dispatch_validation(request, workspace, std::move(result));
    }
    if (operation == "evaluate") {
      return dispatch_evaluation(request, workspace, std::move(result));
    }

    const auto dataset = parse_artifact_ref(request.at("dataset").dump());
    const auto dataset_validation = Validator(workspace).validate(dataset);
    if (!dataset_validation.valid()) {
      const FailureInfo failure{
          dataset_validation.evidence.failure_category.value_or(
              ErrorCategory::kSchemaInvalid),
          "dataset failed runner validation", std::nullopt};
      result["outcome"] = "REJECTED";
      result["failure"] = failure_json(failure);
      return encoded(std::move(result), RunnerExitCode::kRejected);
    }
    const auto compatibility = request_preflight(
        request, registry, *dataset_validation.validated_artifact, context);
    if (!compatibility.compatible) {
      result["outcome"] = "REJECTED";
      result["failure"] = failure_json(*compatibility.failure);
      return encoded(std::move(result), RunnerExitCode::kRejected);
    }
    validate_request_parameters(request, registry);
    if (operation == "preflight") {
      result["outcome"] = "SUCCEEDED";
      return encoded(std::move(result), RunnerExitCode::kSuccess);
    }

    const auto& identities = request.at("implementations");
    const auto& parameters = request.at("stage_parameters");
    PipelineExecutionRequest execution;
    execution.run_id = request_id;
    execution.dataset = dataset;
    execution.partition_implementation_id =
        identities.at("partition").get<std::string>();
    execution.local_implementation_id =
        identities.at("local_build").get<std::string>();
    execution.merge_implementation_id =
        identities.at("merge").get<std::string>();
    execution.partition_parameters_json = parameters.at("partition").dump();
    execution.local_parameters_json = parameters.at("local_build").dump();
    execution.merge_parameters_json = parameters.at("merge").dump();
    execution.source_config_json =
        request.contains("source_config") ? request.at("source_config").dump()
                                          : "{}";
    if (request.contains("reuse_artifact")) {
      execution.reused_partition =
          parse_artifact_ref(request.at("reuse_artifact").dump());
    }
    execution.source = configured_source();
    execution.toolchain = configured_toolchain();
    execution.started_at = now_utc();
    execution.ended_at = now_utc();
    const auto executed =
        PipelineExecutor(registry, workspace).execute(execution, context);
    result["stage_invocations"] =
        {{"partition", executed.invocations.partition},
         {"local_build", executed.invocations.local_build},
         {"merge", executed.invocations.merge}};
    if (executed.artifact) result["artifact"] = artifact_ref_json(*executed.artifact);
    if (!executed.build_manifest_paths.empty()) {
      result["build_manifest_path"] =
          relative_to(executed.build_manifest_paths.back(), workspace.root)
              .generic_string();
    }
    if (!executed.validation_evidence_paths.empty()) {
      result["validation_evidence_path"] =
          relative_to(executed.validation_evidence_paths.back(), workspace.root)
              .generic_string();
    }
    if (executed.evaluation_report_path) {
      result["evaluation_report_path"] =
          relative_to(*executed.evaluation_report_path, workspace.root)
              .generic_string();
    }
    if (executed.quarantine_id) result["quarantine_id"] = *executed.quarantine_id;
    if (executed.failure) result["failure"] = failure_json(*executed.failure);

    if (executed.status == StageStatus::kSucceeded) {
      result["outcome"] = "SUCCEEDED";
      return encoded(std::move(result), RunnerExitCode::kSuccess);
    }
    if (executed.status == StageStatus::kCancelled) {
      result["outcome"] = "CANCELLED";
      return encoded(std::move(result), RunnerExitCode::kCancelled);
    }
    if (executed.status == StageStatus::kInvalidOutput) {
      result["outcome"] = "INVALID_OUTPUT";
      return encoded(std::move(result), RunnerExitCode::kInvalidOutput);
    }
    result["outcome"] = "FAILED";
    return encoded(std::move(result), RunnerExitCode::kFailed);
  } catch (const PipelineError& error) {
    result["outcome"] = "FAILED";
    result["failure"] = failure_json(
        {error.category(), error.what(), std::nullopt});
    const auto code =
        error.category() == ErrorCategory::kImplementationNotFound ||
                error.category() == ErrorCategory::kCapabilityUnsatisfied ||
                error.category() == ErrorCategory::kIdSpaceMismatch ||
                error.category() == ErrorCategory::kUnsupportedVersion
            ? RunnerExitCode::kRejected
            : RunnerExitCode::kUsage;
    if (code == RunnerExitCode::kRejected) result["outcome"] = "REJECTED";
    return encoded(std::move(result), code);
  } catch (const std::exception& error) {
    result["outcome"] = "FAILED";
    result["failure"] = failure_json(
        {ErrorCategory::kInternal, error.what(), std::nullopt});
    return encoded(std::move(result), RunnerExitCode::kInternal);
  }
}

RunnerDispatchResult dispatch_registry_request(
    const ImplementationRegistry& registry) {
  Json result = base_result("registry", "registry");
  Json descriptors = Json::array();
  for (const auto& descriptor : registry.enumerate()) {
    descriptors.push_back(
        Json::parse(serialize_implementation_descriptor(descriptor)));
  }
  result["outcome"] = "SUCCEEDED";
  result["registered_implementations"] = std::move(descriptors);
  return encoded(std::move(result), RunnerExitCode::kSuccess);
}

void reject_runner_write_target(const std::filesystem::path& target) {
  reject_upstream_write_target(target, DISKANN_PIPELINE_PROJECT_ROOT);
}

void write_runner_result(const std::filesystem::path& result_path,
                         const std::string& json_text) {
  reject_runner_write_target(result_path);
  write_immutable_text(result_path, json_text);
}

}  // namespace diskann_pipeline
