#pragma once

#include "diskann_pipeline/pipeline.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace diskann_pipeline {

enum class RunnerExitCode : int {
  kSuccess = 0,
  kUsage = 2,
  kRejected = 3,
  kFailed = 4,
  kCancelled = 4,
  kInvalidOutput = 5,
  kValidationFailed = 6,
  kEvaluationFailed = 7,
  kInternal = 10,
};

struct RunnerDispatchResult {
  int exit_code{static_cast<int>(RunnerExitCode::kInternal)};
  std::string result_json;
};

struct RunnerRequestPrelude {
  std::string request_id;
  std::string operation;
  std::filesystem::path workspace_root;
  std::optional<ArtifactRef> reuse_artifact;
};

RunnerRequestPrelude inspect_runner_request(
    const std::filesystem::path& request_path,
    std::string_view expected_operation);

RunnerDispatchResult dispatch_runner_request(
    const std::filesystem::path& request_path,
    ImplementationRegistry& registry, StageContext& context);
RunnerDispatchResult dispatch_runner_request(
    const std::filesystem::path& request_path,
    std::string_view expected_operation,
    ImplementationRegistry& registry, StageContext& context);
RunnerDispatchResult dispatch_registry_request(
    const ImplementationRegistry& registry);
void reject_runner_write_target(const std::filesystem::path& target);
void write_runner_result(const std::filesystem::path& result_path,
                         const std::string& json_text);

}  // namespace diskann_pipeline
