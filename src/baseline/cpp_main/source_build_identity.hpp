#pragma once

#include "diskann_pipeline/provenance.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace diskann_pipeline::cpp_main {

inline constexpr const char* kPinnedRepository =
    "https://github.com/microsoft/DiskANN.git";
inline constexpr const char* kPinnedBranch = "cpp_main";
inline constexpr const char* kPinnedCommit =
    "78256bbab4685e1774e78d331e081a153be26823";

struct BuildConfiguration {
  std::string os;
  std::string compiler_path;
  std::string compiler_version;
  std::string cmake_version;
  std::string generator;
  std::string build_type;
  std::vector<std::string> effective_cmake_definitions;
  std::vector<std::string> project_compile_options;
  std::string configure_command;
  std::string build_command;
  std::filesystem::path upstream_library_file;
  std::vector<std::string> linked_dependencies;
};

struct SourceBuildAdmission {
  std::filesystem::path repository_root;
  std::filesystem::path upstream_source_dir;
  std::filesystem::path upstream_binary_dir;
  std::string expected_repository{kPinnedRepository};
  std::string expected_branch{kPinnedBranch};
  std::string expected_commit{kPinnedCommit};
  BuildConfiguration build;
};

struct SourceBuildEvidence {
  SourceIdentity source;
  std::string gitlink_commit;
  std::filesystem::path upstream_source_dir;
  std::filesystem::path upstream_binary_dir;
  std::filesystem::path upstream_library_file;
  std::string upstream_library_sha256;
  BuildConfiguration build;
};

SourceBuildAdmission configured_source_build_admission();
SourceBuildEvidence admit_source_and_build(
    const SourceBuildAdmission& admission);
BuildManifest make_build_manifest(const SourceBuildEvidence& evidence,
                                  std::string run_id,
                                  std::string started_at,
                                  std::string ended_at);

}  // namespace diskann_pipeline::cpp_main
