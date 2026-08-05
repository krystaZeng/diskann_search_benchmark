#pragma once

#include "diskann_pipeline/pipeline.hpp"

#include <filesystem>
#include <string>

namespace diskann_pipeline {

enum class RunnerExitCode : int {
  kSuccess = 0,
  kUsage = 2,
  kRejected = 3,
  kFailed = 4,
  kInvalidOutput = 5,
  kCancelled = 6,
  kValidationFailed = 7,
  kInternal = 10,
};

struct RunnerDispatchResult {
  int exit_code{static_cast<int>(RunnerExitCode::kInternal)};
  std::string result_json;
};

RunnerDispatchResult dispatch_runner_request(
    const std::filesystem::path& request_path,
    ImplementationRegistry& registry, StageContext& context);
void write_runner_result(const std::filesystem::path& result_path,
                         const std::string& json_text);

}  // namespace diskann_pipeline
