#include "diskann_pipeline/pipeline.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace diskann_pipeline {
namespace {

using Json = nlohmann::json;
constexpr const char* kFakeGlobalGraphMediaType =
    "application/json";
constexpr const char* kCppMainGlobalGraphMediaType =
    "application/vnd.diskann.cpp-main.global-graph;version=1";

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "evaluation input is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  try {
    return Json::parse(bytes.str());
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("invalid evaluation JSON: ") + error.what());
  }
}

}  // namespace

EvaluationReport evaluate_validated_global_graph(
    const WorkspaceRef& workspace, const ValidatedArtifact& artifact,
    const std::filesystem::path& validation_evidence_path,
    const std::string& report_id, const std::string& generated_at) {
  if (artifact.reference().artifact_type != ArtifactType::kGlobalGraph ||
      artifact.id_space() != IdSpace::kGlobal ||
      !artifact.has_capability("graph.global_adjacency/v1")) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "evaluator requires a validated global adjacency graph");
  }

  const auto structural = Json::parse(artifact.structural_json());
  const auto logical_node_count =
      structural.at("global_node_count").get<NodeCount>();
  const auto declared_max_degree =
      structural.at("declared_max_degree").get<std::uint32_t>();

  const auto graph_payload = std::find_if(
      artifact.payloads().begin(), artifact.payloads().end(),
      [](const PayloadRef& payload) { return payload.role == "global_graph"; });
  if (graph_payload == artifact.payloads().end()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "global graph payload role is missing");
  }

  if (graph_payload->media_type == kCppMainGlobalGraphMediaType) {
    if (!artifact.has_capability("graph.medoids/v1")) {
      throw PipelineError(
          ErrorCategory::kCapabilityUnsatisfied,
          "cpp_main native evaluator requires the medoids capability");
    }
    const auto evidence_path = resolve_confined_path(
        workspace.root, validation_evidence_path);
    const auto evidence = read_json(evidence_path);
    const auto& candidate = evidence.at("artifact_candidate");
    if (evidence.at("result") != "VALID" ||
        candidate.at("artifact_type") != "global_graph" ||
        candidate.at("format_version") !=
            artifact.reference().format_version ||
        !candidate.contains("claimed_aggregate_checksum") ||
        candidate.at("claimed_aggregate_checksum") !=
            artifact.reference().aggregate_checksum) {
      throw PipelineError(
          ErrorCategory::kSchemaInvalid,
          "cpp_main native evaluator requires matching VALID evidence");
    }
    const auto observed_max_degree =
        structural.at("observed_max_degree").get<std::uint32_t>();
    const bool degree_within_bound =
        observed_max_degree <= declared_max_degree;
    const bool passed = degree_within_bound;
    return {report_id,
            artifact.reference(),
            validation_evidence_path,
            {"cpp_main_native_graph_integrity_evaluator", "1.0.0"},
            generated_at,
            {passed ? "VALID" : "INVALID", logical_node_count, 0,
             declared_max_degree, observed_max_degree, passed},
            "{}"};
  }

  if (graph_payload->media_type != kFakeGlobalGraphMediaType) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "global graph media type is not evaluator-reviewed");
  }

  const auto metadata_path = resolve_normal_artifact_metadata(
      workspace, artifact.reference().metadata_path);
  const auto metadata = read_json(metadata_path);
  std::filesystem::path graph_path;
  for (const auto& payload : metadata.at("payloads")) {
    if (payload.at("role") == "global_graph") {
      graph_path = metadata_path.parent_path() /
                   payload.at("path").get<std::string>();
      break;
    }
  }
  if (graph_path.empty()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "global graph payload role is missing");
  }

  const auto adjacency = read_json(graph_path).at("adjacency");
  NodeCount invalid_neighbors = 0;
  std::uint32_t observed_max_degree = 0;
  bool node_count_matches = adjacency.is_array() &&
                            adjacency.size() == logical_node_count;
  if (adjacency.is_array()) {
    for (const auto& neighbors : adjacency) {
      if (!neighbors.is_array()) {
        ++invalid_neighbors;
        continue;
      }
      observed_max_degree = std::max(
          observed_max_degree,
          static_cast<std::uint32_t>(neighbors.size()));
      for (const auto& neighbor : neighbors) {
        if ((!neighbor.is_number_unsigned() &&
             !neighbor.is_number_integer()) ||
            (neighbor.is_number_integer() &&
             neighbor.get<std::int64_t>() < 0) ||
            (neighbor.is_number() &&
             neighbor.get<NodeCount>() >= logical_node_count)) {
          ++invalid_neighbors;
        }
      }
    }
  }
  const bool degree_within_bound = observed_max_degree <= declared_max_degree;
  const bool passed = node_count_matches && invalid_neighbors == 0 &&
                      degree_within_bound;
  const std::string integrity = passed ? "VALID" : "INVALID";
  return {report_id,
          artifact.reference(),
          validation_evidence_path,
          {"fake_graph_integrity_evaluator", "1.0.0"},
          generated_at,
          {integrity, logical_node_count, invalid_neighbors,
           declared_max_degree, observed_max_degree, passed},
          "{}"};
}

std::string deterministic_evaluation_projection(
    const EvaluationReport& report) {
  return Json{{"degree_bound",
               report.findings.observed_max_degree <=
                   report.findings.declared_max_degree},
              {"declared_max_degree",
               report.findings.declared_max_degree},
              {"graph_integrity", report.findings.graph_integrity},
              {"invalid_neighbor_count",
               report.findings.invalid_neighbor_count},
              {"logical_node_count", report.findings.logical_node_count},
              {"observed_max_degree",
               report.findings.observed_max_degree},
              {"passed", report.findings.passed}}
      .dump();
}

}  // namespace diskann_pipeline
