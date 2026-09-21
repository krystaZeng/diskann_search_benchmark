#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#include "papers/hnsw_merger/vamana/runner_distance.hpp"

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
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;

#ifndef HNSW_MERGER_RUNNER_PROJECT_CANONICAL_REPOSITORY_RESOLVED
#define HNSW_MERGER_RUNNER_PROJECT_CANONICAL_REPOSITORY_RESOLVED 0
#endif

namespace {

using Json = nlohmann::json;

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

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                            "comparison request cannot be read");
  }
  return Json::parse(input);
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                            "comparison evidence cannot be read");
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void require_shape(const Json& value, const std::set<std::string>& names,
                   const char* label) {
  if (!value.is_object() || value.size() != names.size()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            std::string(label) + " has an invalid shape");
  }
  for (const auto& name : names) {
    if (!value.contains(name)) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              std::string(label) + " is missing " + name);
    }
  }
}

bool source_matches(const Json& value, const dp::SourceIdentity& expected) {
  if (!value.is_object() || value.value("repository", "") != expected.repository ||
      value.value("branch", "") != expected.branch ||
      value.value("commit", "") != expected.commit ||
      value.value("dirty_worktree", !expected.dirty_worktree) !=
          expected.dirty_worktree) {
    return false;
  }
  if (expected.dirty_worktree) {
    return expected.dirty_source_checksum.has_value() &&
           value.value("dirty_source_checksum", "") ==
               *expected.dirty_source_checksum;
  }
  return !value.contains("dirty_source_checksum");
}

bool canonical_repository_url(const std::string& value) {
  return value.rfind("https://", 0) == 0 || value.rfind("ssh://", 0) == 0 ||
         (value.rfind("git@", 0) == 0 && value.find(':') != std::string::npos);
}

dp::SourceIdentity configured_project_source() {
  dp::SourceIdentity source{HNSW_MERGER_RUNNER_PROJECT_REPOSITORY,
                            HNSW_MERGER_RUNNER_PROJECT_BRANCH,
                            HNSW_MERGER_RUNNER_PROJECT_COMMIT,
                            HNSW_MERGER_RUNNER_PROJECT_DIRTY != 0,
                            std::nullopt};
  if (source.dirty_worktree) {
    source.dirty_source_checksum = HNSW_MERGER_RUNNER_PROJECT_DIRTY_CHECKSUM;
  }
  return source;
}

void require_request(const Json& request,
                     const native::SourceBuildEvidence& diskann,
                     const dp::SourceIdentity& project) {
  require_shape(request,
                {"request_version", "operation", "request_id", "study_class",
                 "reproduction_mode", "replacement_scope", "profile_ref",
                 "profile_checksum", "source_config", "workspace",
                 "source_admission", "shared_inputs", "merges"},
                "comparison request");
  if (request.at("request_version") != "hnsw-merger-comparison-request/v1" ||
      request.at("operation") != "execute" ||
      request.at("study_class") != "single_paper_reproduction" ||
      request.at("reproduction_mode") != "faithful_adaptation" ||
      request.at("replacement_scope") != "merge") {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "comparison governance is invalid");
  }
  require_shape(request.at("source_config"),
                {"yaml_sha256", "normalized_json_sha256"}, "source_config");
  require_shape(request.at("shared_inputs"),
                {"dataset", "partition", "local_graphs"}, "shared_inputs");
  if (!request.at("shared_inputs").at("local_graphs").is_array() ||
      request.at("shared_inputs").at("local_graphs").empty()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "ordered LocalGraph inputs are empty");
  }
  require_shape(request.at("merges"), {"m0", "m1"}, "merges");
  const auto& m0 = request.at("merges").at("m0");
  const auto& paper_merge = request.at("merges").at("m1");
  require_shape(m0, {"implementation_id", "implementation_version", "parameters"},
                "m0 selection");
  require_shape(paper_merge,
                {"implementation_id", "implementation_version", "parameters"},
                "m1 selection");
  if (m0.at("implementation_id") != native::kM0ImplementationId ||
      m0.at("implementation_version") != "1.0.0" ||
      paper_merge.at("implementation_id") != m1::kImplementationId ||
      paper_merge.at("implementation_version") != m1::kImplementationVersion) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "comparison Merge identities are not fixed");
  }
  static_cast<void>(m1::parse_parameters(paper_merge.at("parameters").dump()));

  const auto& admission = request.at("source_admission");
  require_shape(admission, {"project_adaptation", "paper", "diskann", "toolchain"},
                "source_admission");
  if (!source_matches(admission.at("project_adaptation"), project) ||
      !source_matches(admission.at("diskann"), diskann.source)) {
    throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,
                            "comparison source admission does not match build");
  }
  const bool formal_profile =
      request.at("profile_ref") ==
      "experiments/configs/hnsw-merger-diskann-merge-adaptation/"
      "sift1m-structural.yaml";
  if (formal_profile &&
      (HNSW_MERGER_RUNNER_PROJECT_CANONICAL_REPOSITORY_RESOLVED == 0 ||
       project.dirty_worktree || !canonical_repository_url(project.repository))) {
    throw dp::PipelineError(
        dp::ErrorCategory::kSourceMismatch,
        "formal comparison requires the canonical clean project source identity");
  }
  const auto& paper = admission.at("paper");
  if (paper.value("repository", "") != m1::kPaperRepository ||
      paper.value("branch", "") != m1::kPaperBranch ||
      paper.value("commit", "") != m1::kPaperCommit ||
      paper.value("dirty_worktree", true) ||
      paper.value("doi", "") != m1::kPaperDoi ||
      paper.value("pdf_sha256", "") != m1::kPaperPdfSha256 ||
      paper.value("integration_mode", "") != "source_mapping_only" ||
      paper.value("official_binary_linked", true)) {
    throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,
                            "paper/source mapping identity is invalid");
  }
  if (!admission.at("toolchain").is_object() ||
      admission.at("toolchain").empty()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "comparison toolchain admission is empty");
  }
}

std::filesystem::path relative_to(const std::filesystem::path& value,
                                  const std::filesystem::path& root) {
  std::error_code error;
  const auto relative = std::filesystem::relative(value, root, error);
  if (error || relative.empty() || relative.is_absolute()) {
    throw dp::PipelineError(dp::ErrorCategory::kPathEscape,
                            "comparison output escapes workspace");
  }
  static_cast<void>(dp::resolve_confined_path(root, relative));
  return relative;
}

Json artifact_json(const dp::ArtifactRef& value) {
  return Json::parse(dp::serialize_artifact_ref(value));
}

class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string&) override {}
  void increment(const std::string&, std::uint64_t) override {}
};

class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

dp::StageContext context_for(const dp::WorkspaceRef& workspace,
                             const Json& request,
                             native::CppMainVectorAccessor& vectors,
                             dp::DistanceFunction& distance,
                             dp::CheckpointStore& checkpoints,
                             dp::MetricsSink& metrics,
                             dp::CancellationToken& cancellation,
                             const std::string& run_id) {
  return {workspace,
          request.at("workspace").at("memory_budget_bytes").get<std::uint64_t>(),
          request.at("workspace").at("thread_budget").get<std::uint32_t>(),
          dp::create_temporary_storage(workspace, run_id + "-bootstrap"),
          &vectors,
          &distance,
          &checkpoints,
          &metrics,
          &cancellation};
}

Json structural_findings(const dp::PipelineExecutionResult& execution) {
  if (!execution.evaluation_report_path) {
    throw dp::PipelineError(dp::ErrorCategory::kInternal,
                            "successful Merge lacks EvaluationReport");
  }
  const auto report =
      dp::parse_evaluation_report(read_text(*execution.evaluation_report_path));
  return {{"node_count", report.findings.logical_node_count},
          {"invalid_neighbor_count", report.findings.invalid_neighbor_count},
          {"self_edge_count", 0},
          {"observed_max_degree", report.findings.observed_max_degree},
          {"degree_bound_pass",
           report.findings.observed_max_degree <=
               report.findings.declared_max_degree},
          {"lineage_pass", report.findings.passed}};
}

Json merge_record(const dp::PipelineExecutionResult& execution,
                  const std::string& run_id, const std::string& implementation,
                  const dp::WorkspaceRef& workspace) {
  Json value{{"run_id", run_id},
             {"implementation_id", implementation},
             {"implementation_version", "1.0.0"},
             {"status", dp::to_string(execution.status)}};
  if (execution.build_manifest_paths.empty()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "Merge produced no parent BuildManifest");
  }
  value["build_manifest_path"] =
      relative_to(execution.build_manifest_paths.back(), workspace.root)
          .generic_string();
  if (execution.status == dp::StageStatus::kSucceeded) {
    if (!execution.artifact || execution.validation_evidence_paths.empty() ||
        !execution.evaluation_report_path) {
      throw dp::PipelineError(dp::ErrorCategory::kInternal,
                              "successful Merge evidence is incomplete");
    }
    value["global_graph"] = artifact_json(*execution.artifact);
    value["validation_evidence_path"] =
        relative_to(execution.validation_evidence_paths.back(), workspace.root)
            .generic_string();
    value["evaluation_report_path"] =
        relative_to(*execution.evaluation_report_path, workspace.root)
            .generic_string();
    value["structural_findings"] = structural_findings(execution);
  }
  return value;
}

Json comparison_summary(const Json& request,
                        const std::vector<dp::ArtifactRef>& locals,
                        const dp::PipelineExecutionResult& m0,
                        const dp::PipelineExecutionResult& m1_result,
                        const dp::WorkspaceRef& workspace) {
  const auto dataset = dp::parse_artifact_ref(
      request.at("shared_inputs").at("dataset").dump());
  const auto partition = dp::parse_artifact_ref(
      request.at("shared_inputs").at("partition").dump());
  Json owned = Json::array();
  for (std::size_t index = 0; index < locals.size(); ++index) {
    owned.push_back({{"partition_id", index}, {"artifact", artifact_json(locals[index])}});
  }
  const auto evidence = [&](const dp::PipelineExecutionResult& execution) {
    Json paths = Json::array();
    for (const auto& path : execution.validation_evidence_paths) {
      paths.push_back(relative_to(path, workspace.root).generic_string());
    }
    return paths;
  };
  const bool succeeded = m0.status == dp::StageStatus::kSucceeded &&
                         m1_result.status == dp::StageStatus::kSucceeded;
  return {{"schema_version", "same-input-structural-comparison/v1"},
          {"comparison_id", request.at("request_id")},
          {"generated_at", now_utc()},
          {"study_class", "single_paper_reproduction"},
          {"reproduction_mode", "faithful_adaptation"},
          {"replacement_scope", "merge"},
          {"shared_inputs",
           {{"dataset", artifact_json(dataset)},
            {"partition", artifact_json(partition)},
            {"local_graphs", std::move(owned)},
            {"m0_validation_evidence", evidence(m0)},
            {"m1_validation_evidence", evidence(m1_result)}}},
          {"m0", merge_record(m0, request.at("request_id").get<std::string>() + "-m0",
                              native::kM0ImplementationId, workspace)},
          {"m1", merge_record(m1_result,
                              request.at("request_id").get<std::string>() + "-m1",
                              m1::kImplementationId, workspace)},
          {"shared_input_identity", "PASS"},
          {"structural_validation", succeeded ? "PASS" : "FAIL"},
          {"formal_graph_parity", false},
          {"query_evaluation_performed", false}};
}

int exit_for(dp::StageStatus status) {
  if (status == dp::StageStatus::kSucceeded) return 0;
  if (status == dp::StageStatus::kInvalidOutput) return 5;
  if (status == dp::StageStatus::kCancelled) return 4;
  return 4;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || std::string(argv[1]) != "execute" ||
      std::string(argv[2]) != "--request" ||
      std::string(argv[4]) != "--result") {
    std::cerr << "usage: hnsw_merger_experiment_runner execute --request FILE --result FILE\n";
    return 2;
  }
  try {
    const auto request = read_json(argv[3]);
    const auto diskann = native::admit_source_and_build(
        native::configured_source_build_admission());
    const auto project = configured_project_source();
    require_request(request, diskann, project);
    const auto workspace = dp::create_workspace(std::filesystem::absolute(
        request.at("workspace").at("root").get<std::string>()));
    dp::reject_upstream_write_target(workspace.root,
                                     std::filesystem::current_path());
    const auto result_path = dp::resolve_confined_path(
        workspace.root,
        relative_to(std::filesystem::absolute(argv[5]), workspace.root));

    const auto dataset = dp::parse_artifact_ref(
        request.at("shared_inputs").at("dataset").dump());
    const auto partition = dp::parse_artifact_ref(
        request.at("shared_inputs").at("partition").dump());
    std::vector<dp::ArtifactRef> locals;
    for (const auto& value : request.at("shared_inputs").at("local_graphs")) {
      locals.push_back(dp::parse_artifact_ref(value.dump()));
    }

    native::CppMainVectorAccessor vectors(workspace, dataset);
    m1::runner_detail::L2Distance distance;
    Metrics metrics;
    NeverCancelled cancellation;
    dp::UnavailableCheckpointStore checkpoints;
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    registry.register_merge(native::m0_descriptor(), [] {
      return native::make_m0_merge();
    });
    registry.register_merge(m1::descriptor(), [] { return m1::make_m1_merge(); });
    registry.seal();
    dp::PipelineExecutor executor(registry, workspace);

    const auto make_request = [&](const std::string& suffix,
                                  const std::string& implementation,
                                  const Json& parameters,
                                  const dp::SourceIdentity& source) {
      dp::PipelineExecutionRequest value;
      value.run_id = request.at("request_id").get<std::string>() + suffix;
      value.dataset = dataset;
      value.partition_implementation_id = "merge-only-no-partition";
      value.local_implementation_id = "merge-only-no-local-build";
      value.merge_implementation_id = implementation;
      value.merge_parameters_json = parameters.dump();
      value.merge_only_inputs = dp::MergeOnlyInputSet{partition, locals};
      value.source_config_json = request.at("source_config").dump();
      value.source = source;
      value.toolchain = native::make_build_manifest(
                            diskann, value.run_id + "-build", now_utc(), now_utc())
                            .toolchain;
      value.started_at = now_utc();
      value.ended_at = now_utc();
      return value;
    };

    auto m0_context = context_for(workspace, request, vectors, distance, checkpoints,
                                  metrics, cancellation,
                                  request.at("request_id").get<std::string>() + "-m0");
    const auto m0_request = make_request(
        "-m0", native::kM0ImplementationId,
        request.at("merges").at("m0").at("parameters"), diskann.source);
    const auto m0_result = executor.execute(m0_request, m0_context);

    dp::PipelineExecutionResult m1_result;
    if (m0_result.status == dp::StageStatus::kSucceeded) {
      auto paper_context = context_for(
          workspace, request, vectors, distance, checkpoints, metrics,
          cancellation, request.at("request_id").get<std::string>() + "-m1");
      const auto paper_request = make_request(
          "-m1", m1::kImplementationId,
          request.at("merges").at("m1").at("parameters"), project);
      m1_result = executor.execute(paper_request, paper_context);
    } else {
      m1_result.status = dp::StageStatus::kFailed;
      m1_result.failure = dp::FailureInfo{dp::ErrorCategory::kCapabilityUnsatisfied,
                                         "M0 arm did not succeed", std::nullopt};
    }

    const auto summary = comparison_summary(request, locals, m0_result, m1_result,
                                            workspace);
    const auto summary_path = dp::resolve_confined_path(
        workspace.report_root,
        std::filesystem::path("hnsw-merger") /
            request.at("request_id").get<std::string>() /
            "same-input-structural-comparison.json");
    dp::write_immutable_text(summary_path, summary.dump());
    const auto succeeded = m0_result.status == dp::StageStatus::kSucceeded &&
                           m1_result.status == dp::StageStatus::kSucceeded;
    const auto aggregate_merge = m0_result.invocations.merge +
                                 m1_result.invocations.merge;
    Json result{{"result_version", "1.0.0"},
                {"request_id", request.at("request_id")},
                {"operation", "execute"},
                {"outcome", succeeded ? "SUCCEEDED" : "FAILED"},
                {"exit_code", succeeded ? 0 : 4},
                {"stage_invocations",
                 {{"partition", 0}, {"local_build", 0}, {"merge", aggregate_merge}}}};
    if (succeeded) {
      result["artifact"] = artifact_json(*m1_result.artifact);
      result["build_manifest_path"] =
          relative_to(m1_result.build_manifest_paths.back(), workspace.root)
              .generic_string();
      result["validation_evidence_path"] =
          relative_to(m1_result.validation_evidence_paths.back(), workspace.root)
              .generic_string();
      result["evaluation_report_path"] =
          relative_to(*m1_result.evaluation_report_path, workspace.root)
              .generic_string();
    } else {
      const auto& failed = m0_result.status == dp::StageStatus::kSucceeded
                               ? m1_result
                               : m0_result;
      result["failure"] =
          {{"category", failed.failure
                            ? dp::to_string(failed.failure->category)
                            : "execution_blocked"},
           {"reason", failed.failure ? failed.failure->reason
                                     : "Merge comparison failed"}};
    }
    dp::write_immutable_text(result_path, result.dump());
    return succeeded ? 0 : exit_for(m0_result.status == dp::StageStatus::kSucceeded
                                        ? m1_result.status
                                        : m0_result.status);
  } catch (const std::exception& error) {
    std::cerr << "HNSW-Merger experiment runner failed: " << error.what() << '\n';
    return 10;
  }
}
