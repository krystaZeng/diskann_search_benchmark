#pragma once

#include "diskann_pipeline/artifact_ref.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace diskann_pipeline {

struct EvaluatorIdentity {
  std::string implementation_id;
  std::string version;
};

struct EvaluationFindings {
  std::string graph_integrity;
  NodeCount logical_node_count{0};
  NodeCount invalid_neighbor_count{0};
  std::uint32_t declared_max_degree{0};
  std::uint32_t observed_max_degree{0};
  bool passed{false};
};

struct EvaluationReport {
  std::string report_id;
  ArtifactRef artifact;
  std::filesystem::path validation_evidence_path;
  EvaluatorIdentity evaluator;
  std::string generated_at;
  EvaluationFindings findings;
  std::string metrics_json{"{}"};
};

}  // namespace diskann_pipeline

