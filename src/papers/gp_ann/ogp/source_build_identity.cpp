#include "papers/gp_ann/ogp/source_build_identity.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#ifndef GPANN_OGP_SOURCE_ROOT
#error "GPANN_OGP_SOURCE_ROOT must be supplied by CMake"
#endif
#ifndef GPANN_OGP_BUILD_ROOT
#error "GPANN_OGP_BUILD_ROOT must be supplied by CMake"
#endif
#ifndef GPANN_OGP_PARTITION_EXECUTABLE
#error "GPANN_OGP_PARTITION_EXECUTABLE must be supplied by CMake"
#endif
#ifndef GPANN_OGP_BUILD_IDENTITY_FILE
#error "GPANN_OGP_BUILD_IDENTITY_FILE must be supplied by CMake"
#endif

namespace diskann_pipeline::gp_ann_ogp {
namespace {

std::string shell_quote(const std::filesystem::path& value) {
  std::string quoted = "'";
  for (const char character : value.string()) {
    quoted += character == '\'' ? "'\\''" : std::string(1, character);
  }
  return quoted + "'";
}

std::string trim(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' ||
          value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  return value;
}

std::string command_output(const std::filesystem::path& repository,
                           const std::string& arguments) {
  const auto path = shell_quote(repository);
  const auto command = "git -c safe.directory=" + path + " -C " + path +
                       " " + arguments + " 2>/dev/null";
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "unable to inspect GP-ANN Git identity");
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    output += buffer.data();
  }
  if (pclose(pipe) != 0) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GP-ANN Git identity command failed");
  }
  return trim(std::move(output));
}

std::filesystem::path normalized(const std::filesystem::path& value) {
  std::error_code error;
  const auto result = std::filesystem::weakly_canonical(value, error);
  if (error || result.empty()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to normalize GP-ANN source/build path");
  }
  return result;
}

bool within(const std::filesystem::path& candidate,
            const std::filesystem::path& parent) {
  const auto child = normalized(candidate);
  const auto root = normalized(parent);
  auto child_it = child.begin();
  for (auto root_it = root.begin(); root_it != root.end();
       ++root_it, ++child_it) {
    if (child_it == child.end() || *child_it != *root_it) {
      return false;
    }
  }
  return true;
}

std::map<std::string, std::string> read_identity_file() {
  std::ifstream input(GPANN_OGP_BUILD_IDENTITY_FILE, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "GP-ANN build identity file is missing");
  }
  std::map<std::string, std::string> values;
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("set(", 0) != 0 || line.size() < 8 ||
        line.substr(line.size() - 2) != "\")") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "GP-ANN build identity line is malformed");
    }
    const auto separator = line.find(" \"");
    if (separator == std::string::npos) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "GP-ANN build identity key is malformed");
    }
    const auto key = line.substr(4, separator - 4);
    const auto value = line.substr(separator + 2,
                                   line.size() - separator - 4);
    if (!values.emplace(key, value).second) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "GP-ANN build identity contains a duplicate key");
    }
  }
  return values;
}

const std::string& require(const std::map<std::string, std::string>& values,
                           const std::string& key) {
  const auto found = values.find(key);
  if (found == values.end() || found->second.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "GP-ANN build identity is incomplete: " + key);
  }
  return found->second;
}

std::vector<std::string> split_options(const std::string& value) {
  std::vector<std::string> result;
  std::istringstream input(value);
  std::string item;
  while (std::getline(input, item, '|')) {
    if (!item.empty()) result.push_back(item);
  }
  return result;
}

}  // namespace

GpAnnSourceBuildAdmission configured_source_build_admission() {
  const auto identity = read_identity_file();
  const auto configured_source = normalized(GPANN_OGP_SOURCE_ROOT);
  const auto configured_build = normalized(GPANN_OGP_BUILD_ROOT);
  const auto configured_executable = normalized(GPANN_OGP_PARTITION_EXECUTABLE);
  if (normalized(require(identity, "GPANN_IDENTITY_SOURCE_DIR")) !=
          configured_source ||
      normalized(require(identity, "GPANN_IDENTITY_BINARY_DIR")) !=
          configured_build ||
      normalized(require(identity, "GPANN_IDENTITY_PARTITION_EXECUTABLE")) !=
          configured_executable) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "configured GP-ANN source/build/executable identity differs");
  }
  const std::vector<GpAnnDependencyIdentity> dependencies{
      {"external/parlaylib",
       require(identity, "GPANN_IDENTITY_PARLAYLIB_COMMIT")},
      {"external/KaMinPar",
       require(identity, "GPANN_IDENTITY_KAMINPAR_COMMIT")},
      {"external/hnswlib",
       require(identity, "GPANN_IDENTITY_HNSWLIB_COMMIT")},
      {"external/message-queue",
       require(identity, "GPANN_IDENTITY_MESSAGE_QUEUE_COMMIT")}};
  ToolchainIdentity toolchain{
      "Linux",
      require(identity, "GPANN_IDENTITY_COMPILER"),
      require(identity, "GPANN_IDENTITY_COMPILER_VERSION"),
      require(identity, "GPANN_IDENTITY_CMAKE_VERSION"),
      require(identity, "GPANN_IDENTITY_GENERATOR"),
      require(identity, "GPANN_IDENTITY_BUILD_TYPE"),
      split_options(require(identity, "GPANN_IDENTITY_EFFECTIVE_OPTIONS")),
      {require(identity, "GPANN_IDENTITY_CONFIGURE_COMMAND")},
      {require(identity, "GPANN_IDENTITY_BUILD_COMMAND")}};
  return {configured_source,
          configured_build,
          configured_executable,
          require(identity, "GPANN_IDENTITY_PARTITION_SHA256"),
          require(identity, "GPANN_IDENTITY_REPOSITORY"),
          require(identity, "GPANN_IDENTITY_BRANCH"),
          require(identity, "GPANN_IDENTITY_COMMIT"),
          dependencies,
          std::move(toolchain)};
}

GpAnnSourceBuildEvidence admit_source_and_build(
    const GpAnnSourceBuildAdmission& admission) {
  const auto source = normalized(admission.source_root);
  const auto build = normalized(admission.build_root);
  const auto executable = normalized(admission.partition_executable);
  if (within(build, source) || !within(executable, build) ||
      !std::filesystem::is_regular_file(executable)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "GP-ANN build/executable is not an admitted out-of-tree path");
  }
  const auto repository = command_output(source, "remote get-url origin");
  const auto branch = command_output(source, "branch --show-current");
  const auto commit = command_output(source, "rev-parse HEAD");
  const auto status =
      command_output(source, "status --porcelain=v1 --untracked-files=all");
  if (repository != kRepository || branch != kBranch || commit != kCommit ||
      repository != admission.expected_repository ||
      branch != admission.expected_branch ||
      commit != admission.expected_commit || !status.empty()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GP-ANN repository, branch, commit, or clean state is invalid");
  }
  if (admission.dependencies.size() != 4) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GP-ANN recursive dependency identity is incomplete");
  }
  for (const auto& dependency : admission.dependencies) {
    const auto dependency_root = source / dependency.path;
    if (command_output(dependency_root, "rev-parse HEAD") !=
            dependency.commit ||
        !command_output(dependency_root,
                        "status --porcelain=v1 --untracked-files=all")
             .empty()) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "GP-ANN dependency is mismatched or dirty");
    }
  }
  const auto executable_sha = sha256_file(executable);
  if (admission.partition_executable_sha256.empty() ||
      executable_sha != admission.partition_executable_sha256 ||
      admission.toolchain.compiler_path.empty() ||
      admission.toolchain.compiler_version.empty() ||
      admission.toolchain.cmake_version.empty() ||
      admission.toolchain.generator.empty() ||
      admission.toolchain.build_type.empty() ||
      admission.toolchain.cmake_definitions.empty() ||
      admission.toolchain.compile_commands.empty() ||
      admission.toolchain.link_commands.empty()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "GP-ANN build or executable identity is incomplete or mismatched");
  }
  return {{repository, branch, commit, false, std::nullopt},
          source,
          build,
          executable,
          executable_sha,
          admission.dependencies,
          admission.toolchain};
}

}  // namespace diskann_pipeline::gp_ann_ogp
