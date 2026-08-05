#pragma once

#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/identifiers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline {

struct ImplementationIdentity {
  std::string implementation_id;
  std::string implementation_version;
};

struct SourceIdentity {
  std::string repository;
  std::string branch;
  std::string commit;
  bool dirty_worktree{false};
  std::optional<std::string> dirty_source_checksum;
};

struct ToolchainIdentity {
  std::string os;
  std::string compiler_path;
  std::string compiler_version;
  std::string cmake_version;
  std::string generator;
  std::string build_type;
  std::vector<std::string> cmake_definitions;
  std::vector<std::string> compile_commands;
  std::vector<std::string> link_commands;
};

struct ResourceBudget {
  std::uint64_t memory_budget_bytes{0};
  std::uint32_t thread_budget{0};
};

struct FailureInfo {
  ErrorCategory category{ErrorCategory::kInternal};
  std::string reason;
  std::optional<int> exit_code;
};

struct ReuseInfo {
  bool reused{false};
  std::string reason;
  std::optional<ArtifactRef> source_artifact;
};

struct BuildManifest {
  std::string run_id;
  std::string stage;
  ImplementationIdentity implementation;
  SourceIdentity source;
  ToolchainIdentity toolchain;
  DatasetIdentity dataset;
  std::string parameters_json{"{}"};
  ResourceBudget resource_budget;
  std::vector<ArtifactRef> input_artifacts;
  std::vector<ArtifactRef> output_artifacts;
  StageStatus status{StageStatus::kPlanned};
  std::string started_at;
  std::string ended_at;
  std::optional<FailureInfo> failure;
  std::optional<ReuseInfo> reuse;
  std::optional<std::filesystem::path> validation_evidence_path;
  std::string metrics_json{"{}"};
};

enum class ValidationResult {
  kValid,
  kInvalid,
};

struct ArtifactCandidateIdentity {
  ArtifactType artifact_type{ArtifactType::kDataset};
  std::string format_version;
  std::filesystem::path metadata_path;
  std::optional<std::string> claimed_aggregate_checksum;
};

struct ValidationCheck {
  std::string check_id;
  bool passed{false};
  std::string message;
  std::optional<std::string> expected_json;
  std::optional<std::string> observed_json;
};

struct ValidationEvidence {
  std::string validator_version;
  ArtifactCandidateIdentity artifact_candidate;
  std::vector<ValidationCheck> checks;
  ValidationResult result{ValidationResult::kInvalid};
  std::optional<ErrorCategory> failure_category;
};

struct QuarantineRecord {
  std::string quarantine_id;
  std::filesystem::path candidate_metadata_path;
  std::filesystem::path build_manifest_path;
  std::filesystem::path validation_evidence_path;
  FailureInfo failure;
  std::vector<std::string> capabilities;
  bool isolated_from_normal_resolver{true};
  std::string retained_at;
  std::optional<std::string> diagnostic_parent_quarantine_id;
};

}  // namespace diskann_pipeline

