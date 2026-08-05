#pragma once

#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <filesystem>
#include <string>

namespace diskann_pipeline {

struct QuarantinePublication {
  QuarantineRecord record;
  std::filesystem::path entry_root;
  std::filesystem::path record_path;
};

class QuarantineManager {
 public:
  explicit QuarantineManager(WorkspaceRef workspace);

  QuarantinePublication publish(
      const std::string& quarantine_id,
      const std::filesystem::path& candidate_root,
      const std::filesystem::path& candidate_metadata_path,
      const std::filesystem::path& build_manifest_path,
      const std::filesystem::path& validation_evidence_path,
      const FailureInfo& failure, const std::string& retained_at) const;

 private:
  WorkspaceRef workspace_;
};

class DiagnosticQuarantineResolver {
 public:
  explicit DiagnosticQuarantineResolver(WorkspaceRef workspace);

  std::filesystem::path resolve(const std::string& quarantine_id,
                                const std::filesystem::path& relative) const;
  QuarantineRecord read_record(const std::string& quarantine_id) const;
  std::string read_text(const std::string& quarantine_id,
                        const std::filesystem::path& relative) const;
  void remove(const std::string& quarantine_id) const;

 private:
  WorkspaceRef workspace_;
};

}  // namespace diskann_pipeline
