#include "diskann_pipeline/workspace.hpp"

namespace diskann_pipeline {

std::filesystem::path resolve_normal_artifact_metadata(
    const WorkspaceRef& workspace,
    const std::filesystem::path& relative_metadata_path) {
  return resolve_confined_path(workspace.normal_root, relative_metadata_path);
}

}  // namespace diskann_pipeline

