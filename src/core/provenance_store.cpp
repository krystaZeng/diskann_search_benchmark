#include "diskann_pipeline/stores.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

namespace diskann_pipeline {

std::filesystem::path persist_terminal_build_manifest(
    const WorkspaceRef& workspace, const BuildManifest& manifest) {
  if (manifest.status == StageStatus::kPlanned ||
      manifest.status == StageStatus::kRunning) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "only terminal BuildManifests may be persisted");
  }
  const auto target = resolve_confined_path(
      workspace.manifest_root, manifest.run_id + ".json");
  write_immutable_text(target, serialize_build_manifest(manifest));
  return target;
}

std::filesystem::path persist_validation_evidence(
    const WorkspaceRef& workspace, const std::string& evidence_id,
    const ValidationEvidence& evidence) {
  const auto target = resolve_confined_path(
      workspace.manifest_root, "evidence/" + evidence_id + ".json");
  write_immutable_text(target, serialize_validation_evidence(evidence));
  return target;
}

std::filesystem::path persist_quarantine_record(
    const WorkspaceRef& workspace, const QuarantineRecord& record) {
  const auto target = resolve_confined_path(
      workspace.quarantine_root, record.quarantine_id + "/record.json");
  write_immutable_text(target, serialize_quarantine_record(record));
  return target;
}

}  // namespace diskann_pipeline

