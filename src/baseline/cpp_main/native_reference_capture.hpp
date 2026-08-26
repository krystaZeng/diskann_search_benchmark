#pragma once

#include "baseline/cpp_main/source_build_identity.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace diskann_pipeline::cpp_main {

struct NativeReferenceCaptureRequest {
  std::filesystem::path source_directory;
  std::string source_graph_name;
  std::string source_medoids_name;
  std::filesystem::path captured_graph_path;
  std::filesystem::path captured_medoids_path;
  std::filesystem::path stdout_path;
  std::filesystem::path stderr_path;
  std::vector<std::string> child_argv;
};

struct NativeReferenceCaptureResult {
  int child_exit_code{-1};
  bool exact_graph_event_observed{false};
  bool graph_hard_link_created{false};
  bool observer_armed{false};
  std::string observer_mechanism{
      "prearmed_inotify_same_filesystem_hardlink"};
  std::string observer_ready_at;
  std::string child_launched_at;
  std::vector<std::string> native_child_argv;
  std::string source_graph_name;
  std::filesystem::path captured_graph_path;
  std::string terminal_result{"FAILED"};
  std::string failure_reason;
  std::string started_at;
  std::string ended_at;
};

NativeReferenceCaptureResult capture_native_reference(
    const NativeReferenceCaptureRequest& request);

bool same_source_build_evidence(const SourceBuildEvidence& left,
                                const SourceBuildEvidence& right);

}  // namespace diskann_pipeline::cpp_main
