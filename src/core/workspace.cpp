#include "diskann_pipeline/workspace.hpp"

#include "diskann_pipeline/errors.hpp"

#include <chrono>
#include <fstream>
#include <system_error>

namespace diskann_pipeline {
namespace {

bool is_path_prefix(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) {
  auto root_iterator = root.begin();
  auto candidate_iterator = candidate.begin();
  for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
    if (candidate_iterator == candidate.end() ||
        *root_iterator != *candidate_iterator) {
      return false;
    }
  }
  return true;
}

std::filesystem::path normalized_absolute(const std::filesystem::path& path) {
  std::error_code error;
  auto result = std::filesystem::weakly_canonical(path, error);
  if (error) {
    error.clear();
    result = std::filesystem::absolute(path, error).lexically_normal();
  }
  if (error || result.empty()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to normalize workspace path");
  }
  return result;
}

}  // namespace

std::filesystem::path resolve_confined_path(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path) {
  if (relative_path.empty() || relative_path.is_absolute()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "workspace-relative path must be non-empty and relative");
  }
  for (const auto& component : relative_path) {
    if (component == "..") {
      throw PipelineError(ErrorCategory::kPathEscape,
                          "workspace-relative path contains '..'");
    }
  }
  const auto normalized_root = normalized_absolute(root);
  const auto candidate = normalized_absolute(normalized_root / relative_path);
  if (!is_path_prefix(normalized_root, candidate) || candidate == normalized_root) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "path escapes its declared workspace root");
  }
  return candidate;
}

WorkspaceRef create_workspace(const std::filesystem::path& root) {
  const auto normalized_root = normalized_absolute(root);
  WorkspaceRef workspace{
      normalized_root,
      normalized_root / "normal",
      normalized_root / "temporary",
      normalized_root / "manifests",
      normalized_root / "reports",
      normalized_root / "quarantine"};
  std::filesystem::create_directories(workspace.normal_root);
  std::filesystem::create_directories(workspace.temporary_root);
  std::filesystem::create_directories(workspace.manifest_root);
  std::filesystem::create_directories(workspace.report_root);
  std::filesystem::create_directories(workspace.quarantine_root);
  return workspace;
}

void reject_upstream_write_target(
    const std::filesystem::path& target,
    const std::filesystem::path& repository_root) {
  const auto normalized_target = normalized_absolute(target);
  const auto upstream = normalized_absolute(
      repository_root / "upstream" / "diskann-cpp-main");
  if (is_path_prefix(upstream, normalized_target)) {
    throw PipelineError(
        ErrorCategory::kPathEscape,
        "writable target must remain outside upstream/diskann-cpp-main");
  }
}

TemporaryStorageRef create_temporary_storage(const WorkspaceRef& workspace,
                                             std::string_view stage_identity) {
  if (stage_identity.empty()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "temporary stage identity is empty");
  }
  const auto stage_root = resolve_confined_path(
      workspace.temporary_root, std::filesystem::path(stage_identity));
  if (std::filesystem::exists(stage_root)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "temporary stage directory already exists");
  }
  std::filesystem::create_directories(stage_root);
  return {stage_root};
}

void write_immutable_text(const std::filesystem::path& target,
                          std::string_view content) {
  if (target.empty() || std::filesystem::exists(target)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "immutable target already exists or is empty");
  }
  std::filesystem::create_directories(target.parent_path());
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto temporary = target.parent_path() /
                         (target.filename().string() + ".tmp-" + suffix);
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw PipelineError(ErrorCategory::kInternal,
                          "unable to create immutable temporary file");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
      std::filesystem::remove(temporary);
      throw PipelineError(ErrorCategory::kInternal,
                          "unable to write immutable temporary file");
    }
  }
  std::error_code error;
  std::filesystem::create_hard_link(temporary, target, error);
  std::filesystem::remove(temporary);
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to publish immutable file without overwrite: " +
                            error.message());
  }
}

void validate_stage_context(const StageContext& context) {
  if (context.memory_budget_bytes == 0 || context.thread_budget == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "StageContext resource budgets must be positive");
  }
  if (context.workspace.root.empty() || context.temporary_storage.stage_root.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "StageContext workspace services must be configured");
  }
  if (context.vector_accessor == nullptr || context.distance_function == nullptr ||
      context.checkpoint_store == nullptr || context.metrics_sink == nullptr ||
      context.cancellation_token == nullptr) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "StageContext service pointers must be non-null");
  }
  static_cast<void>(resolve_confined_path(
      context.workspace.root,
      std::filesystem::relative(context.temporary_storage.stage_root,
                                context.workspace.root)));
}

}  // namespace diskann_pipeline
