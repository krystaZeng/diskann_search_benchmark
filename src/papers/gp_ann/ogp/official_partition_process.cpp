#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace diskann_pipeline::gp_ann_ogp {
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

std::filesystem::path require_confined_existing(
    const std::filesystem::path& root, const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = path.is_absolute()
                            ? std::filesystem::relative(path, root, error)
                            : path;
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to confine OGP input path");
  }
  const auto confined = resolve_confined_path(root, relative);
  if (!std::filesystem::is_regular_file(confined)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "OGP input is not a regular file");
  }
  return confined;
}

std::filesystem::path relative_to_workspace(
    const WorkspaceRef& workspace, const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, workspace.root, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to form OGP workspace-relative evidence path");
  }
  static_cast<void>(resolve_confined_path(workspace.root, relative));
  return relative;
}

std::string overlap_token(double value) {
  if (!std::isfinite(value) || value < 0.0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP overlap is not finite and non-negative");
  }
  return Json(value).dump();
}

void stage_input(const std::filesystem::path& source,
                 const std::filesystem::path& target,
                 std::uint64_t expected_length,
                 const std::string& expected_sha256) {
  if (std::filesystem::file_size(source) != expected_length ||
      sha256_file(source) != expected_sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "OGP source Dataset bytes differ from admitted identity");
  }
  std::filesystem::create_directories(target.parent_path());
  std::error_code error;
  std::filesystem::create_hard_link(source, target, error);
  if (error) {
    error.clear();
    if (!std::filesystem::copy_file(
            source, target, std::filesystem::copy_options::none, error) ||
        error) {
      throw PipelineError(ErrorCategory::kInternal,
                          "unable to stage checksum-identical OGP .fbin input");
    }
  }
  if (std::filesystem::file_size(target) != expected_length ||
      sha256_file(target) != expected_sha256) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "staged OGP .fbin bytes changed");
  }
}

int wait_for_process(pid_t process) {
  int status = 0;
  while (waitpid(process, &status, 0) < 0) {
    if (errno != EINTR) {
      throw PipelineError(ErrorCategory::kInternal,
                          "waitpid failed for official GP-ANN Partition");
    }
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 255;
}

Json evidence_file(const WorkspaceRef& workspace,
                   const std::filesystem::path& path) {
  return {{"path", relative_to_workspace(workspace, path).generic_string()},
          {"sha256", sha256_file(path)}};
}

}  // namespace

OfficialPartitionInvocationResult run_official_partition(
    const GpAnnSourceBuildEvidence& evidence,
    const DatasetIdentity& dataset,
    const std::filesystem::path& dataset_payload,
    std::uint64_t dataset_payload_length,
    const std::string& dataset_payload_sha256,
    const OgpParameters& parameters,
    const std::string& run_id,
    StageContext& context) {
  validate_stage_context(context);
  if (run_id.empty() || context.cancellation_token->is_cancelled()) {
    throw PipelineError(run_id.empty() ? ErrorCategory::kSchemaInvalid
                                       : ErrorCategory::kCancelled,
                        run_id.empty() ? "OGP subprocess run_id is empty"
                                       : "OGP subprocess cancelled before launch");
  }
  if (evidence.source.repository != kRepository ||
      evidence.source.branch != kBranch ||
      evidence.source.commit != kCommit || evidence.source.dirty_worktree ||
      !std::filesystem::is_regular_file(evidence.partition_executable) ||
      sha256_file(evidence.partition_executable) !=
          evidence.partition_executable_sha256) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "official GP-ANN executable identity is no longer admitted");
  }
  if (parameters.requested_num_clusters == 0 ||
      parameters.requested_num_clusters > dataset.global_node_count ||
      (parameters.partitioning_mode != "default" &&
       parameters.partitioning_mode != "strong")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP subprocess parameters are invalid");
  }
  const auto source_payload =
      require_confined_existing(context.workspace.normal_root, dataset_payload);
  const auto staged_input = resolve_confined_path(
      context.temporary_storage.stage_root, "official-input.fbin");
  stage_input(source_payload, staged_input, dataset_payload_length,
              dataset_payload_sha256);

  const auto output_directory = resolve_confined_path(
      context.temporary_storage.stage_root, "official-output");
  std::filesystem::create_directories(output_directory);
  const auto output_prefix = output_directory / "partition";
  const auto requested_clusters =
      std::to_string(parameters.requested_num_clusters);
  const auto requested_overlap = overlap_token(parameters.requested_overlap);
  const auto expected_output = std::filesystem::path(
      output_prefix.string() + ".k=" + requested_clusters + ".OGP.o=" +
      requested_overlap);
  static_cast<void>(resolve_confined_path(
      context.temporary_storage.stage_root,
      std::filesystem::relative(expected_output,
                                context.temporary_storage.stage_root)));

  const auto evidence_root = resolve_confined_path(
      context.workspace.report_root,
      std::filesystem::path("gp-ann-ogp") / run_id / "partition-subprocess");
  if (std::filesystem::exists(evidence_root)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "OGP subprocess evidence directory already exists");
  }
  std::filesystem::create_directories(evidence_root);
  const auto stdout_path = evidence_root / "stdout.log";
  const auto stderr_path = evidence_root / "stderr.log";
  const int stdout_fd =
      open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  const int stderr_fd =
      open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (stdout_fd < 0 || stderr_fd < 0) {
    if (stdout_fd >= 0) close(stdout_fd);
    if (stderr_fd >= 0) close(stderr_fd);
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to create OGP stdout/stderr evidence");
  }

  OfficialPartitionInvocationResult result;
  result.argv = {evidence.partition_executable.string(), staged_input.string(),
                 output_prefix.string(), requested_clusters, "OGP",
                 parameters.partitioning_mode, requested_overlap};
  std::vector<char*> child_argv;
  child_argv.reserve(result.argv.size() + 1);
  for (auto& token : result.argv) child_argv.push_back(token.data());
  child_argv.push_back(nullptr);

  const auto started_at = now_utc();
  const pid_t process = fork();
  if (process == 0) {
    if (dup2(stdout_fd, STDOUT_FILENO) < 0 ||
        dup2(stderr_fd, STDERR_FILENO) < 0) {
      _exit(126);
    }
    close(stdout_fd);
    close(stderr_fd);
    const auto threads = std::to_string(context.thread_budget);
    if (setenv("PARLAY_NUM_THREADS", threads.c_str(), 1) != 0) {
      _exit(126);
    }
    execv(child_argv.front(), child_argv.data());
    _exit(127);
  }
  close(stdout_fd);
  close(stderr_fd);
  if (process < 0) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to launch official GP-ANN Partition");
  }
  result.exit_status = wait_for_process(process);
  const auto ended_at = now_utc();
  result.process_status = result.exit_status == 0 ? StageStatus::kSucceeded
                                                   : StageStatus::kFailed;
  result.staged_input_path = staged_input;
  result.stdout_path = stdout_path;
  result.stderr_path = stderr_path;
  result.expected_output_path = expected_output;
  if (std::filesystem::is_regular_file(expected_output)) {
    result.expected_output_sha256 = sha256_file(expected_output);
  }

  Json dependencies = Json::object();
  for (const auto& dependency : evidence.dependencies) {
    dependencies[dependency.path.generic_string()] = dependency.commit;
  }
  Json manifest_parameters{
      {"argv", result.argv},
      {"effective_epsilon", 0.05},
      {"effective_epsilon_source", "upstream_cli_constant"},
      {"requested_thread_budget", context.thread_budget},
      {"configured_environment",
       {{"PARLAY_NUM_THREADS", std::to_string(context.thread_budget)}}},
      {"kaminpar_threads_passed",
       std::min<std::uint32_t>(32, context.thread_budget)},
      {"kaminpar_threads_passed_evidence_class",
       "source_derived_from_pinned_parlay_contract"},
      {"staged_input",
       {{"path", relative_to_workspace(context.workspace, staged_input)
                     .generic_string()},
        {"byte_length", dataset_payload_length},
        {"sha256", sha256_file(staged_input)},
        {"source_sha256", dataset_payload_sha256}}},
      {"stdout", evidence_file(context.workspace, stdout_path)},
      {"stderr", evidence_file(context.workspace, stderr_path)},
      {"exit_status", result.exit_status},
      {"admitted_executable",
       {{"path", evidence.partition_executable.generic_string()},
        {"sha256", evidence.partition_executable_sha256}}},
      {"dependency_commits", std::move(dependencies)}};
  Json output_evidence{{"path", relative_to_workspace(context.workspace,
                                                       expected_output)
                                    .generic_string()}};
  if (result.expected_output_sha256) {
    output_evidence["sha256"] = *result.expected_output_sha256;
  }
  manifest_parameters["expected_output"] = std::move(output_evidence);

  BuildManifest manifest;
  manifest.run_id = run_id;
  manifest.stage = "partition";
  manifest.implementation = {"gp_ann_official_partition_subprocess_v1",
                             "1.0.0"};
  manifest.source = evidence.source;
  manifest.toolchain = evidence.toolchain;
  manifest.dataset = dataset;
  manifest.parameters_json = manifest_parameters.dump();
  manifest.resource_budget = {context.memory_budget_bytes,
                              context.thread_budget};
  manifest.status = result.process_status;
  manifest.started_at = started_at;
  manifest.ended_at = ended_at;
  if (result.process_status == StageStatus::kFailed) {
    manifest.failure = FailureInfo{ErrorCategory::kInternal,
                                   "official GP-ANN Partition exited nonzero",
                                   result.exit_status};
  }
  const auto manifest_path = evidence_root / "build-manifest.json";
  write_immutable_text(manifest_path, serialize_build_manifest(manifest));
  result.build_manifest_path = manifest_path;
  return result;
}

}  // namespace diskann_pipeline::gp_ann_ogp
