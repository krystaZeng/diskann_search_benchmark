#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace diskann_pipeline {

enum class ErrorCategory {
  kSchemaInvalid,
  kUnsupportedVersion,
  kPathEscape,
  kPayloadMissing,
  kPayloadLengthMismatch,
  kChecksumMismatch,
  kIdSpaceMismatch,
  kIdRangeInvalid,
  kMappingIncomplete,
  kDegreeExceeded,
  kNodeCountMismatch,
  kCapabilityUnsatisfied,
  kDuplicateImplementation,
  kReuseMismatch,
  kCancelled,
  kCheckpointUnavailable,
  kInternal,
};

std::string_view to_string(ErrorCategory value) noexcept;
ErrorCategory error_category_from_string(std::string_view value);

class PipelineError : public std::runtime_error {
 public:
  PipelineError(ErrorCategory category, std::string message)
      : std::runtime_error(std::move(message)), category_(category) {}

  ErrorCategory category() const noexcept { return category_; }

 private:
  ErrorCategory category_;
};

}  // namespace diskann_pipeline

