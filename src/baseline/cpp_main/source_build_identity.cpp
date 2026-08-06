#include "baseline/cpp_main/source_build_identity.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <array>
#include <cstdio>
#include <sstream>
#include <string>

#ifndef CPP_MAIN_REPOSITORY_ROOT
#error "CPP_MAIN_REPOSITORY_ROOT must be supplied by CMake"
#endif
#ifndef CPP_MAIN_UPSTREAM_SOURCE_DIR
#error "CPP_MAIN_UPSTREAM_SOURCE_DIR must be supplied by CMake"
#endif
#ifndef CPP_MAIN_UPSTREAM_BINARY_DIR
#error "CPP_MAIN_UPSTREAM_BINARY_DIR must be supplied by CMake"
#endif
#ifndef CPP_MAIN_UPSTREAM_LIBRARY_FILE
#error "CPP_MAIN_UPSTREAM_LIBRARY_FILE must be supplied by CMake"
#endif

namespace diskann_pipeline::cpp_main {
namespace {

std::string shell_quote(const std::filesystem::path& value) {
  std::string quoted = "'";
  for (const char character : value.string()) {
    quoted += character == '\'' ? "'\\''" : std::string(1, character);
  }
  quoted += "'";
  return quoted;
}

std::string trim(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' ||
          value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  return value;
}

std::string command_output(const std::string& command) {
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
  if (pipe == nullptr) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "unable to execute source identity command");
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    output += buffer.data();
  }
  const int status = pclose(pipe);
  if (status != 0) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "source identity command failed");
  }
  return trim(std::move(output));
}

std::filesystem::path normalized_absolute(const std::filesystem::path& value) {
  std::error_code error;
  auto absolute = std::filesystem::absolute(value, error);
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "cannot resolve source/build path");
  }
  return absolute.lexically_normal();
}

bool within(const std::filesystem::path& candidate,
            const std::filesystem::path& parent) {
  const auto normalized_candidate = normalized_absolute(candidate);
  const auto normalized_parent = normalized_absolute(parent);
  auto candidate_it = normalized_candidate.begin();
  for (auto parent_it = normalized_parent.begin();
       parent_it != normalized_parent.end(); ++parent_it, ++candidate_it) {
    if (candidate_it == normalized_candidate.end() ||
        *candidate_it != *parent_it) {
      return false;
    }
  }
  return true;
}

void require_build_identity(const BuildConfiguration& build) {
  if (build.os.empty() || build.compiler_path.empty() ||
      build.compiler_version.empty() || build.cmake_version.empty() ||
      build.generator.empty() || build.build_type.empty() ||
      build.effective_cmake_definitions.empty() ||
      build.project_compile_options.empty() || build.configure_command.empty() ||
      build.build_command.empty() || build.upstream_library_file.empty() ||
      build.linked_dependencies.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "cpp_main Build Identity is incomplete");
  }
  if (!std::filesystem::is_regular_file(build.upstream_library_file)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "linked upstream library does not exist");
  }
}

}  // namespace

SourceBuildAdmission configured_source_build_admission() {
  BuildConfiguration build{
      CPP_MAIN_SYSTEM_NAME,
      CPP_MAIN_CXX_COMPILER,
      CPP_MAIN_CXX_COMPILER_VERSION,
      CPP_MAIN_CMAKE_VERSION,
      CPP_MAIN_CMAKE_GENERATOR,
      CPP_MAIN_BUILD_TYPE,
      {CPP_MAIN_EFFECTIVE_DEFINITIONS},
      {CPP_MAIN_PROJECT_COMPILE_OPTIONS},
      CPP_MAIN_CONFIGURE_COMMAND,
      CPP_MAIN_BUILD_COMMAND,
      CPP_MAIN_UPSTREAM_LIBRARY_FILE,
      {"diskann_s", CPP_MAIN_UPSTREAM_LIBRARY_FILE}};
  return {CPP_MAIN_REPOSITORY_ROOT,
          CPP_MAIN_UPSTREAM_SOURCE_DIR,
          CPP_MAIN_UPSTREAM_BINARY_DIR,
          kPinnedRepository,
          kPinnedBranch,
          kPinnedCommit,
          std::move(build)};
}

SourceBuildEvidence admit_source_and_build(
    const SourceBuildAdmission& admission) {
  const auto repository_root = normalized_absolute(admission.repository_root);
  const auto source_dir = normalized_absolute(admission.upstream_source_dir);
  const auto expected_source =
      (repository_root / "upstream" / "diskann-cpp-main").lexically_normal();
  const auto binary_dir = normalized_absolute(admission.upstream_binary_dir);
  if (source_dir != expected_source || within(binary_dir, source_dir)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "cpp_main source or out-of-tree build path is invalid");
  }

  const auto root = shell_quote(repository_root);
  const auto source = shell_quote(source_dir);
  const auto root_git = "git -c safe.directory=" + root + " -C " + root;
  const auto source_git =
      "git -c safe.directory=" + source + " -C " + source;
  const auto gitlink_line = command_output(
      root_git +
      " ls-files -s -- upstream/diskann-cpp-main");
  std::istringstream gitlink_input(gitlink_line);
  std::string mode;
  std::string commit;
  std::string stage;
  std::string path;
  gitlink_input >> mode >> commit >> stage >> path;
  if (mode != "160000" || stage != "0" ||
      path != "upstream/diskann-cpp-main" ||
      commit != admission.expected_commit) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "parent repository gitlink is not the pinned commit");
  }

  const auto head = command_output(source_git + " rev-parse HEAD");
  const auto branch =
      command_output(source_git + " branch --show-current");
  const auto remote =
      command_output(source_git + " remote get-url origin");
  const auto status = command_output(
      source_git +
      " status --porcelain=v1 --untracked-files=all");
  if (head != admission.expected_commit || branch != admission.expected_branch ||
      remote != admission.expected_repository || !status.empty()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "cpp_main HEAD, branch, remote, or clean state is invalid");
  }

  require_build_identity(admission.build);
  if (within(admission.build.upstream_library_file, source_dir)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "linked upstream library was written under source");
  }

  SourceIdentity source_identity{remote, branch, head, false, std::nullopt};
  return {std::move(source_identity),
          commit,
          source_dir,
          binary_dir,
          admission.build.upstream_library_file,
          sha256_file(admission.build.upstream_library_file),
          admission.build};
}

BuildManifest make_build_manifest(const SourceBuildEvidence& evidence,
                                  std::string run_id,
                                  std::string started_at,
                                  std::string ended_at) {
  BuildManifest manifest;
  manifest.run_id = std::move(run_id);
  manifest.stage = "build";
  manifest.implementation = {"cpp_main_fixed_build_v1", "1.0.0"};
  manifest.source = evidence.source;
  manifest.toolchain = {
      evidence.build.os,
      evidence.build.compiler_path,
      evidence.build.compiler_version,
      evidence.build.cmake_version,
      evidence.build.generator,
      evidence.build.build_type,
      evidence.build.effective_cmake_definitions,
      {evidence.build.configure_command,
       "project_compile_options=" +
           evidence.build.project_compile_options.front()},
      {evidence.build.build_command,
       "diskann_s=" + evidence.upstream_library_file.generic_string() +
           ";sha256=" + evidence.upstream_library_sha256}};
  manifest.dataset = {
      "sift1m-base-float32-v1",
      "8c7b3d999ba3133f865af72df078f77c2d248fdb80571d7ea1f1bb8e1750658e",
      1000000,
      128,
      "float32",
      "l2"};
  manifest.parameters_json = "{}";
  manifest.resource_budget = {1, 1};
  manifest.status = StageStatus::kSucceeded;
  manifest.started_at = std::move(started_at);
  manifest.ended_at = std::move(ended_at);
  manifest.metrics_json = "{}";
  return manifest;
}

}  // namespace diskann_pipeline::cpp_main
