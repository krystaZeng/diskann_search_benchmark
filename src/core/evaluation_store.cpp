#include "diskann_pipeline/stores.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/workspace.hpp"

namespace diskann_pipeline {

std::filesystem::path persist_evaluation_report(
    const WorkspaceRef& workspace, const EvaluationReport& report) {
  const auto target = resolve_confined_path(
      workspace.report_root, report.report_id + ".json");
  write_immutable_text(target, serialize_evaluation_report(report));
  return target;
}

}  // namespace diskann_pipeline

