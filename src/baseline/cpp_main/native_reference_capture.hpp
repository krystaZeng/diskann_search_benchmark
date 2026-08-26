#pragma once

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
  std::vector<std::string> child_argv;
};

struct NativeReferenceCaptureResult {
  int child_exit_code{-1};
  bool exact_graph_event_observed{false};
  bool graph_hard_link_created{false};
  std::string started_at;
  std::string ended_at;
};

NativeReferenceCaptureResult capture_native_reference(
    const NativeReferenceCaptureRequest& request);

}  // namespace diskann_pipeline::cpp_main
