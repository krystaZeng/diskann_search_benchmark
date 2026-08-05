#include "diskann_pipeline/quarantine.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <fstream>
#include <sstream>

namespace diskann_pipeline {
namespace {

std::string read_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "quarantine evidence input is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

std::filesystem::path require_descendant(
    const std::filesystem::path& root, const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, root, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to derive confined quarantine path");
  }
  const auto resolved = resolve_confined_path(root, relative);
  const auto expected = std::filesystem::weakly_canonical(path, error);
  if (error || resolved != expected) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "quarantine input escapes its declared root");
  }
  return relative;
}

}  // namespace

QuarantineManager::QuarantineManager(WorkspaceRef workspace)
    : workspace_(std::move(workspace)) {}

QuarantinePublication QuarantineManager::publish(
    const std::string& quarantine_id,
    const std::filesystem::path& candidate_root,
    const std::filesystem::path& candidate_metadata_path,
    const std::filesystem::path& build_manifest_path,
    const std::filesystem::path& validation_evidence_path,
    const FailureInfo& failure, const std::string& retained_at) const {
  if (quarantine_id.empty() || retained_at.empty() ||
      !std::filesystem::is_directory(candidate_root)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "quarantine publication inputs are incomplete");
  }
  require_descendant(workspace_.temporary_root, candidate_root);
  const auto candidate_metadata_relative =
      require_descendant(candidate_root, candidate_metadata_path);

  const auto entry_root =
      resolve_confined_path(workspace_.quarantine_root, quarantine_id);
  if (std::filesystem::exists(entry_root)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "quarantine identity already exists");
  }
  std::filesystem::create_directories(entry_root);
  const auto isolated_candidate = entry_root / "candidate";
  std::error_code error;
  std::filesystem::rename(candidate_root, isolated_candidate, error);
  if (error) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to isolate invalid candidate: " + error.message());
  }

  write_immutable_text(entry_root / "build-manifest.json",
                       read_bytes(build_manifest_path));
  write_immutable_text(entry_root / "evidence.json",
                       read_bytes(validation_evidence_path));
  QuarantineRecord record{
      quarantine_id,
      std::filesystem::path(quarantine_id) / "candidate" /
          candidate_metadata_relative,
      std::filesystem::path(quarantine_id) / "build-manifest.json",
      std::filesystem::path(quarantine_id) / "evidence.json",
      failure,
      {},
      true,
      retained_at,
      std::nullopt};
  const auto record_path = persist_quarantine_record(workspace_, record);
  return {std::move(record), entry_root, record_path};
}

}  // namespace diskann_pipeline
