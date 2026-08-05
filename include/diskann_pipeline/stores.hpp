#pragma once

#include "diskann_pipeline/evaluation.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <filesystem>

namespace diskann_pipeline {

std::filesystem::path persist_terminal_build_manifest(
    const WorkspaceRef& workspace, const BuildManifest& manifest);
std::filesystem::path persist_evaluation_report(
    const WorkspaceRef& workspace, const EvaluationReport& report);
std::filesystem::path persist_validation_evidence(
    const WorkspaceRef& workspace, const std::string& evidence_id,
    const ValidationEvidence& evidence);
std::filesystem::path persist_quarantine_record(
    const WorkspaceRef& workspace, const QuarantineRecord& record);

}  // namespace diskann_pipeline

