#pragma once

#include "diskann_pipeline/stage_context.hpp"

#include <filesystem>
#include <string_view>

namespace diskann_pipeline {

WorkspaceRef create_workspace(const std::filesystem::path& root);
TemporaryStorageRef create_temporary_storage(const WorkspaceRef& workspace,
                                             std::string_view stage_identity);
std::filesystem::path resolve_confined_path(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path);
void write_immutable_text(const std::filesystem::path& target,
                          std::string_view content);
std::filesystem::path resolve_normal_artifact_metadata(
    const WorkspaceRef& workspace,
    const std::filesystem::path& relative_metadata_path);

}  // namespace diskann_pipeline
