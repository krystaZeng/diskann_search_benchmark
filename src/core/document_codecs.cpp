#include "diskann_pipeline/document_codec.hpp"

#include "core/json_codec.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace diskann_pipeline {
namespace {

using detail::Json;

Json artifact_ref_json(const ArtifactRef& value) {
  return {{"artifact_type", to_string(value.artifact_type)},
          {"format_version", value.format_version},
          {"aggregate_checksum", value.aggregate_checksum},
          {"metadata_path", value.metadata_path.generic_string()}};
}

ArtifactRef artifact_ref_from_json(const Json& value) {
  detail::require_object_shape(
      value,
      {"artifact_type", "format_version", "aggregate_checksum", "metadata_path"});
  return {value.at("metadata_path").get<std::string>(),
          artifact_type_from_string(value.at("artifact_type").get<std::string>()),
          value.at("format_version").get<std::string>(),
          value.at("aggregate_checksum").get<std::string>()};
}

Json dataset_json(const DatasetIdentity& value) {
  return {{"dataset_id", value.dataset_id},
          {"dataset_checksum", value.dataset_checksum},
          {"global_node_count", value.global_node_count},
          {"vector_dimension", value.vector_dimension},
          {"scalar_type", value.scalar_type},
          {"distance_metric", value.distance_metric}};
}

DatasetIdentity dataset_from_json(const Json& value) {
  detail::require_object_shape(
      value, {"dataset_id", "dataset_checksum", "global_node_count",
              "vector_dimension", "scalar_type", "distance_metric"});
  return {value.at("dataset_id").get<std::string>(),
          value.at("dataset_checksum").get<std::string>(),
          value.at("global_node_count").get<NodeCount>(),
          value.at("vector_dimension").get<std::uint32_t>(),
          value.at("scalar_type").get<std::string>(),
          value.at("distance_metric").get<std::string>()};
}

Json failure_json(const FailureInfo& value) {
  Json result{{"category", to_string(value.category)}, {"reason", value.reason}};
  if (value.exit_code) {
    result["exit_code"] = *value.exit_code;
  }
  return result;
}

FailureInfo failure_from_json(const Json& value) {
  detail::require_object_shape(value, {"category", "reason"}, {"exit_code"});
  FailureInfo result{error_category_from_string(
                         value.at("category").get<std::string>()),
                     value.at("reason").get<std::string>(), std::nullopt};
  if (value.contains("exit_code")) {
    result.exit_code = value.at("exit_code").get<int>();
  }
  return result;
}

Json artifact_array_json(const std::vector<ArtifactRef>& values) {
  Json result = Json::array();
  for (const auto& value : values) {
    result.push_back(artifact_ref_json(value));
  }
  return result;
}

std::vector<ArtifactRef> artifact_array_from_json(const Json& value) {
  if (!value.is_array()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "ArtifactRef collection must be an array");
  }
  std::vector<ArtifactRef> result;
  result.reserve(value.size());
  for (const auto& item : value) {
    result.push_back(artifact_ref_from_json(item));
  }
  return result;
}

Json parse_fragment(const std::string& value, const char* label) {
  try {
    return detail::parse_json_strict(value);
  } catch (const PipelineError& error) {
    throw PipelineError(error.category(),
                        std::string("invalid ") + label + ": " + error.what());
  }
}

Json descriptor_json(const ImplementationDescriptor& value) {
  Json inputs = Json::array();
  for (const auto& input : value.requires.input_artifacts) {
    inputs.push_back({{"artifact_type", to_string(input.artifact_type)},
                      {"accepted_format_versions",
                       input.accepted_format_versions},
                      {"input_id_space", to_string(input.input_id_space)}});
  }
  return {
      {"descriptor_version", "1.0.0"},
      {"implementation_id", value.implementation_id},
      {"implementation_version", value.implementation_version},
      {"stage", to_string(value.stage)},
      {"requires",
       {{"input_artifacts", std::move(inputs)},
        {"capabilities", value.requires.required_capabilities},
        {"runtime_services", value.requires.required_services}}},
      {"provides",
       {{"artifact_type", to_string(value.provides.artifact_type)},
        {"format_version", value.provides.format_version},
        {"output_id_space", to_string(value.provides.output_id_space)},
        {"capabilities", value.provides.capabilities}}},
      {"parameter_schema_path", value.parameter_schema_path.generic_string()},
      {"test_only", value.test_only}};
}

}  // namespace

std::string serialize_artifact_ref(const ArtifactRef& value) {
  return detail::dump_json(artifact_ref_json(value));
}

ArtifactRef parse_artifact_ref(std::string_view json_text) {
  return artifact_ref_from_json(detail::parse_json_strict(json_text));
}

std::string serialize_build_manifest(const BuildManifest& value) {
  Json json{
      {"manifest_version", "1.0.0"},
      {"run_id", value.run_id},
      {"stage", value.stage},
      {"implementation",
       {{"implementation_id", value.implementation.implementation_id},
        {"implementation_version",
         value.implementation.implementation_version}}},
      {"source",
       {{"repository", value.source.repository},
        {"branch", value.source.branch},
        {"commit", value.source.commit},
        {"dirty_worktree", value.source.dirty_worktree}}},
      {"toolchain",
       {{"os", value.toolchain.os},
        {"compiler_path", value.toolchain.compiler_path},
        {"compiler_version", value.toolchain.compiler_version},
        {"cmake_version", value.toolchain.cmake_version},
        {"generator", value.toolchain.generator},
        {"build_type", value.toolchain.build_type},
        {"cmake_definitions", value.toolchain.cmake_definitions},
        {"compile_commands", value.toolchain.compile_commands},
        {"link_commands", value.toolchain.link_commands}}},
      {"dataset", dataset_json(value.dataset)},
      {"parameters", parse_fragment(value.parameters_json, "parameters JSON")},
      {"resource_budget",
       {{"memory_budget_bytes", value.resource_budget.memory_budget_bytes},
        {"thread_budget", value.resource_budget.thread_budget}}},
      {"input_artifacts", artifact_array_json(value.input_artifacts)},
      {"output_artifacts", artifact_array_json(value.output_artifacts)},
      {"status", to_string(value.status)},
      {"started_at", value.started_at},
      {"ended_at", value.ended_at}};
  if (value.source.dirty_source_checksum) {
    json["source"]["dirty_source_checksum"] =
        *value.source.dirty_source_checksum;
  }
  if (value.failure) {
    json["failure"] = failure_json(*value.failure);
  }
  if (value.reuse) {
    Json reuse{{"reused", value.reuse->reused},
               {"reason", value.reuse->reason}};
    if (value.reuse->source_artifact) {
      reuse["source_artifact"] = artifact_ref_json(*value.reuse->source_artifact);
    }
    json["reuse"] = std::move(reuse);
  }
  if (value.validation_evidence_path) {
    json["validation_evidence_path"] =
        value.validation_evidence_path->generic_string();
  }
  json["metrics"] = parse_fragment(value.metrics_json, "metrics JSON");
  return detail::dump_json(json);
}

BuildManifest parse_build_manifest(std::string_view json_text) {
  const auto json = detail::parse_json_strict(json_text);
  detail::require_object_shape(
      json,
      {"manifest_version", "run_id", "stage", "implementation", "source",
       "toolchain", "dataset", "parameters", "resource_budget",
       "input_artifacts", "output_artifacts", "status", "started_at",
       "ended_at"},
      {"failure", "reuse", "validation_evidence_path", "metrics"});
  if (json.at("manifest_version") != "1.0.0") {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "unsupported BuildManifest version");
  }

  BuildManifest result;
  result.run_id = json.at("run_id").get<std::string>();
  result.stage = json.at("stage").get<std::string>();
  const auto& implementation = json.at("implementation");
  detail::require_object_shape(
      implementation, {"implementation_id", "implementation_version"});
  result.implementation = {
      implementation.at("implementation_id").get<std::string>(),
      implementation.at("implementation_version").get<std::string>()};

  const auto& source = json.at("source");
  detail::require_object_shape(source,
                               {"repository", "branch", "commit",
                                "dirty_worktree"},
                               {"dirty_source_checksum"});
  result.source.repository = source.at("repository").get<std::string>();
  result.source.branch = source.at("branch").get<std::string>();
  result.source.commit = source.at("commit").get<std::string>();
  result.source.dirty_worktree = source.at("dirty_worktree").get<bool>();
  if (source.contains("dirty_source_checksum")) {
    result.source.dirty_source_checksum =
        source.at("dirty_source_checksum").get<std::string>();
  }

  const auto& toolchain = json.at("toolchain");
  detail::require_object_shape(
      toolchain,
      {"os", "compiler_path", "compiler_version", "cmake_version",
       "generator", "build_type", "cmake_definitions", "compile_commands",
       "link_commands"});
  result.toolchain = {
      toolchain.at("os").get<std::string>(),
      toolchain.at("compiler_path").get<std::string>(),
      toolchain.at("compiler_version").get<std::string>(),
      toolchain.at("cmake_version").get<std::string>(),
      toolchain.at("generator").get<std::string>(),
      toolchain.at("build_type").get<std::string>(),
      toolchain.at("cmake_definitions").get<std::vector<std::string>>(),
      toolchain.at("compile_commands").get<std::vector<std::string>>(),
      toolchain.at("link_commands").get<std::vector<std::string>>()};
  result.dataset = dataset_from_json(json.at("dataset"));
  result.parameters_json = detail::dump_json(json.at("parameters"));
  const auto& budget = json.at("resource_budget");
  detail::require_object_shape(budget,
                               {"memory_budget_bytes", "thread_budget"});
  result.resource_budget = {
      budget.at("memory_budget_bytes").get<std::uint64_t>(),
      budget.at("thread_budget").get<std::uint32_t>()};
  result.input_artifacts = artifact_array_from_json(json.at("input_artifacts"));
  result.output_artifacts = artifact_array_from_json(json.at("output_artifacts"));
  result.status = stage_status_from_string(json.at("status").get<std::string>());
  result.started_at = json.at("started_at").get<std::string>();
  result.ended_at = json.at("ended_at").get<std::string>();
  if (json.contains("failure")) {
    result.failure = failure_from_json(json.at("failure"));
  }
  if (json.contains("reuse")) {
    const auto& reuse = json.at("reuse");
    detail::require_object_shape(reuse, {"reused", "reason"},
                                 {"source_artifact"});
    ReuseInfo info{reuse.at("reused").get<bool>(),
                   reuse.at("reason").get<std::string>(), std::nullopt};
    if (reuse.contains("source_artifact")) {
      info.source_artifact = artifact_ref_from_json(reuse.at("source_artifact"));
    }
    result.reuse = std::move(info);
  }
  if (json.contains("validation_evidence_path")) {
    result.validation_evidence_path =
        json.at("validation_evidence_path").get<std::string>();
  }
  result.metrics_json =
      json.contains("metrics") ? detail::dump_json(json.at("metrics")) : "{}";
  return result;
}

std::string serialize_evaluation_report(const EvaluationReport& value) {
  Json json{
      {"report_version", "1.0.0"},
      {"report_id", value.report_id},
      {"artifact", artifact_ref_json(value.artifact)},
      {"validation_evidence_path",
       value.validation_evidence_path.generic_string()},
      {"evaluator",
       {{"implementation_id", value.evaluator.implementation_id},
        {"version", value.evaluator.version}}},
      {"generated_at", value.generated_at},
      {"findings",
       {{"graph_integrity", value.findings.graph_integrity},
        {"logical_node_count", value.findings.logical_node_count},
        {"invalid_neighbor_count", value.findings.invalid_neighbor_count},
        {"declared_max_degree", value.findings.declared_max_degree},
        {"observed_max_degree", value.findings.observed_max_degree},
        {"passed", value.findings.passed}}},
      {"metrics", parse_fragment(value.metrics_json, "metrics JSON")}};
  return detail::dump_json(json);
}

EvaluationReport parse_evaluation_report(std::string_view json_text) {
  const auto json = detail::parse_json_strict(json_text);
  detail::require_object_shape(
      json,
      {"report_version", "report_id", "artifact",
       "validation_evidence_path", "evaluator", "generated_at", "findings"},
      {"metrics"});
  if (json.at("report_version") != "1.0.0") {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "unsupported EvaluationReport version");
  }
  EvaluationReport result;
  result.report_id = json.at("report_id").get<std::string>();
  result.artifact = artifact_ref_from_json(json.at("artifact"));
  result.validation_evidence_path =
      json.at("validation_evidence_path").get<std::string>();
  const auto& evaluator = json.at("evaluator");
  detail::require_object_shape(evaluator, {"implementation_id", "version"});
  result.evaluator = {evaluator.at("implementation_id").get<std::string>(),
                      evaluator.at("version").get<std::string>()};
  result.generated_at = json.at("generated_at").get<std::string>();
  const auto& findings = json.at("findings");
  detail::require_object_shape(
      findings,
      {"graph_integrity", "logical_node_count", "invalid_neighbor_count",
       "declared_max_degree", "observed_max_degree", "passed"});
  result.findings = {
      findings.at("graph_integrity").get<std::string>(),
      findings.at("logical_node_count").get<NodeCount>(),
      findings.at("invalid_neighbor_count").get<NodeCount>(),
      findings.at("declared_max_degree").get<std::uint32_t>(),
      findings.at("observed_max_degree").get<std::uint32_t>(),
      findings.at("passed").get<bool>()};
  result.metrics_json =
      json.contains("metrics") ? detail::dump_json(json.at("metrics")) : "{}";
  return result;
}

std::string serialize_validation_evidence(const ValidationEvidence& value) {
  Json candidate{{"artifact_type", to_string(value.artifact_candidate.artifact_type)},
                 {"format_version", value.artifact_candidate.format_version},
                 {"metadata_path",
                  value.artifact_candidate.metadata_path.generic_string()}};
  if (value.artifact_candidate.claimed_aggregate_checksum) {
    candidate["claimed_aggregate_checksum"] =
        *value.artifact_candidate.claimed_aggregate_checksum;
  }
  Json checks = Json::array();
  for (const auto& check : value.checks) {
    Json item{{"check_id", check.check_id},
              {"passed", check.passed},
              {"message", check.message}};
    if (check.expected_json) {
      item["expected"] = parse_fragment(*check.expected_json, "expected JSON");
    }
    if (check.observed_json) {
      item["observed"] = parse_fragment(*check.observed_json, "observed JSON");
    }
    checks.push_back(std::move(item));
  }
  Json json{{"evidence_version", "1.0.0"},
            {"validator_version", value.validator_version},
            {"artifact_candidate", std::move(candidate)},
            {"checks", std::move(checks)},
            {"result", value.result == ValidationResult::kValid ? "VALID" : "INVALID"}};
  if (value.failure_category) {
    json["failure_category"] = to_string(*value.failure_category);
  }
  return detail::dump_json(json);
}

ValidationEvidence parse_validation_evidence(std::string_view json_text) {
  const auto json = detail::parse_json_strict(json_text);
  detail::require_object_shape(
      json,
      {"evidence_version", "validator_version", "artifact_candidate", "checks",
       "result"},
      {"failure_category"});
  if (json.at("evidence_version") != "1.0.0") {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "unsupported ValidationEvidence version");
  }
  ValidationEvidence result;
  result.validator_version = json.at("validator_version").get<std::string>();
  const auto& candidate = json.at("artifact_candidate");
  detail::require_object_shape(
      candidate, {"artifact_type", "format_version", "metadata_path"},
      {"claimed_aggregate_checksum"});
  result.artifact_candidate = {
      artifact_type_from_string(candidate.at("artifact_type").get<std::string>()),
      candidate.at("format_version").get<std::string>(),
      candidate.at("metadata_path").get<std::string>(), std::nullopt};
  if (candidate.contains("claimed_aggregate_checksum")) {
    result.artifact_candidate.claimed_aggregate_checksum =
        candidate.at("claimed_aggregate_checksum").get<std::string>();
  }
  if (!json.at("checks").is_array()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "ValidationEvidence checks must be an array");
  }
  for (const auto& item : json.at("checks")) {
    detail::require_object_shape(item, {"check_id", "passed", "message"},
                                 {"expected", "observed"});
    ValidationCheck check{item.at("check_id").get<std::string>(),
                          item.at("passed").get<bool>(),
                          item.at("message").get<std::string>(), std::nullopt,
                          std::nullopt};
    if (item.contains("expected")) {
      check.expected_json = detail::dump_json(item.at("expected"));
    }
    if (item.contains("observed")) {
      check.observed_json = detail::dump_json(item.at("observed"));
    }
    result.checks.push_back(std::move(check));
  }
  const auto result_text = json.at("result").get<std::string>();
  if (result_text == "VALID") {
    result.result = ValidationResult::kValid;
  } else if (result_text == "INVALID") {
    result.result = ValidationResult::kInvalid;
  } else {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unknown ValidationEvidence result");
  }
  if (json.contains("failure_category")) {
    result.failure_category = error_category_from_string(
        json.at("failure_category").get<std::string>());
  }
  return result;
}

std::string serialize_quarantine_record(const QuarantineRecord& value) {
  Json json{{"record_version", "1.0.0"},
            {"quarantine_id", value.quarantine_id},
            {"status", "INVALID_OUTPUT"},
            {"candidate_metadata_path",
             value.candidate_metadata_path.generic_string()},
            {"build_manifest_path", value.build_manifest_path.generic_string()},
            {"validation_evidence_path",
             value.validation_evidence_path.generic_string()},
            {"failure", failure_json(value.failure)},
            {"capabilities", value.capabilities},
            {"isolated_from_normal_resolver",
             value.isolated_from_normal_resolver},
            {"retained_at", value.retained_at}};
  if (value.diagnostic_parent_quarantine_id) {
    json["diagnostic_parent_quarantine_id"] =
        *value.diagnostic_parent_quarantine_id;
  }
  return detail::dump_json(json);
}

QuarantineRecord parse_quarantine_record(std::string_view json_text) {
  const auto json = detail::parse_json_strict(json_text);
  detail::require_object_shape(
      json,
      {"record_version", "quarantine_id", "status",
       "candidate_metadata_path", "build_manifest_path",
       "validation_evidence_path", "failure", "capabilities",
       "isolated_from_normal_resolver", "retained_at"},
      {"diagnostic_parent_quarantine_id"});
  if (json.at("record_version") != "1.0.0") {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "unsupported QuarantineRecord version");
  }
  if (json.at("status") != "INVALID_OUTPUT") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "quarantine status must be INVALID_OUTPUT");
  }
  QuarantineRecord result;
  result.quarantine_id = json.at("quarantine_id").get<std::string>();
  result.candidate_metadata_path =
      json.at("candidate_metadata_path").get<std::string>();
  result.build_manifest_path =
      json.at("build_manifest_path").get<std::string>();
  result.validation_evidence_path =
      json.at("validation_evidence_path").get<std::string>();
  result.failure = failure_from_json(json.at("failure"));
  result.capabilities = json.at("capabilities").get<std::vector<std::string>>();
  result.isolated_from_normal_resolver =
      json.at("isolated_from_normal_resolver").get<bool>();
  result.retained_at = json.at("retained_at").get<std::string>();
  if (json.contains("diagnostic_parent_quarantine_id")) {
    result.diagnostic_parent_quarantine_id =
        json.at("diagnostic_parent_quarantine_id").get<std::string>();
  }
  return result;
}

std::string serialize_implementation_descriptor(
    const ImplementationDescriptor& value) {
  return detail::dump_json(descriptor_json(value));
}

ImplementationDescriptor parse_implementation_descriptor(
    std::string_view json_text) {
  const auto json = detail::parse_json_strict(json_text);
  detail::require_object_shape(
      json,
      {"descriptor_version", "implementation_id", "implementation_version",
       "stage", "requires", "provides", "parameter_schema_path",
       "test_only"});
  if (json.at("descriptor_version") != "1.0.0") {
    throw PipelineError(ErrorCategory::kUnsupportedVersion,
                        "unsupported ImplementationDescriptor version");
  }
  const auto& requires = json.at("requires");
  detail::require_object_shape(requires,
                               {"input_artifacts", "capabilities",
                                "runtime_services"});
  if (!requires.at("input_artifacts").is_array()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "input_artifacts must be an array");
  }
  ContractRequirement requirement;
  for (const auto& item : requires.at("input_artifacts")) {
    detail::require_object_shape(
        item, {"artifact_type", "accepted_format_versions", "input_id_space"});
    requirement.input_artifacts.push_back(
        {artifact_type_from_string(item.at("artifact_type").get<std::string>()),
         item.at("accepted_format_versions").get<std::vector<std::string>>(),
         id_space_from_string(item.at("input_id_space").get<std::string>())});
  }
  requirement.required_capabilities =
      requires.at("capabilities").get<std::vector<std::string>>();
  requirement.required_services =
      requires.at("runtime_services").get<std::vector<std::string>>();

  const auto& provides = json.at("provides");
  detail::require_object_shape(
      provides,
      {"artifact_type", "format_version", "output_id_space", "capabilities"});
  ContractProvide provide{
      artifact_type_from_string(provides.at("artifact_type").get<std::string>()),
      provides.at("format_version").get<std::string>(),
      id_space_from_string(provides.at("output_id_space").get<std::string>()),
      provides.at("capabilities").get<std::vector<std::string>>()};
  return {json.at("implementation_id").get<std::string>(),
          json.at("implementation_version").get<std::string>(),
          stage_kind_from_string(json.at("stage").get<std::string>()),
          std::move(requirement), std::move(provide),
          json.at("parameter_schema_path").get<std::string>(),
          json.at("test_only").get<bool>()};
}

}  // namespace diskann_pipeline

