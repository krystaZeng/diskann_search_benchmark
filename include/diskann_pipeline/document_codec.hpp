#pragma once

#include "diskann_pipeline/evaluation.hpp"
#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/provenance.hpp"

#include <string>
#include <string_view>

namespace diskann_pipeline {

std::string serialize_artifact_ref(const ArtifactRef& value);
ArtifactRef parse_artifact_ref(std::string_view json_text);

std::string serialize_build_manifest(const BuildManifest& value);
BuildManifest parse_build_manifest(std::string_view json_text);

std::string serialize_evaluation_report(const EvaluationReport& value);
EvaluationReport parse_evaluation_report(std::string_view json_text);

std::string serialize_validation_evidence(const ValidationEvidence& value);
ValidationEvidence parse_validation_evidence(std::string_view json_text);

std::string serialize_quarantine_record(const QuarantineRecord& value);
QuarantineRecord parse_quarantine_record(std::string_view json_text);

std::string serialize_implementation_descriptor(
    const ImplementationDescriptor& value);
ImplementationDescriptor parse_implementation_descriptor(
    std::string_view json_text);

}  // namespace diskann_pipeline

