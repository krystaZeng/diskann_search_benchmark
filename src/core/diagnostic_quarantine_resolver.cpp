#include "diskann_pipeline/quarantine.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <fstream>
#include <sstream>

namespace diskann_pipeline {

DiagnosticQuarantineResolver::DiagnosticQuarantineResolver(WorkspaceRef workspace)
    : workspace_(std::move(workspace)) {}

std::filesystem::path DiagnosticQuarantineResolver::resolve(
    const std::string& quarantine_id,
    const std::filesystem::path& relative) const {
  const auto entry =
      resolve_confined_path(workspace_.quarantine_root, quarantine_id);
  if (!std::filesystem::is_directory(entry) ||
      std::filesystem::is_symlink(entry)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "quarantine entry does not exist");
  }
  const auto result = resolve_confined_path(entry, relative);
  if (!std::filesystem::exists(result)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "quarantine diagnostic file does not exist");
  }
  return result;
}

std::string DiagnosticQuarantineResolver::read_text(
    const std::string& quarantine_id,
    const std::filesystem::path& relative) const {
  std::ifstream input(resolve(quarantine_id, relative), std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

QuarantineRecord DiagnosticQuarantineResolver::read_record(
    const std::string& quarantine_id) const {
  return parse_quarantine_record(read_text(quarantine_id, "record.json"));
}

void DiagnosticQuarantineResolver::remove(
    const std::string& quarantine_id) const {
  const auto entry =
      resolve_confined_path(workspace_.quarantine_root, quarantine_id);
  if (!std::filesystem::is_directory(entry) ||
      std::filesystem::is_symlink(entry)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "quarantine entry cannot be deleted because it is absent or linked");
  }
  std::error_code error;
  std::filesystem::remove_all(entry, error);
  if (error || std::filesystem::exists(entry)) {
    throw PipelineError(ErrorCategory::kInternal,
                        "explicit quarantine deletion failed");
  }
}

}  // namespace diskann_pipeline
