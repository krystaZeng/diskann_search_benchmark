#pragma once

// Private to the two Merge wrappers. No Artifact or runtime-service extension.
#include "baseline/cpp_main/source_build_identity.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace diskann_pipeline::merge_record {

class CallTiming {
 public:
  using Clock = std::chrono::steady_clock;
  explicit CallTiming(const char* scope) : scope_(scope) {}
  void start() { start_ = Clock::now(); }
  // The caller supplies the end captured immediately on return/exception.
  // Once frozen, later cancellation, publication or audit work cannot retime it.
  void finish(Clock::time_point end, const char* status) {
    if (start_ && !elapsed_) {
      elapsed_ = std::chrono::duration<double, std::micro>(end - *start_).count();
      status_ = status;
    }
  }
  void mark_failed_return() { status_ = "FAILED"; }
  nlohmann::json metrics() const {
    if (!elapsed_) return nlohmann::json::object();
    return {{"global_graph_merge_time", {{"value", *elapsed_}, {"unit", "us"},
             {"clock", "steady_clock"}, {"scope", scope_}, {"call_status", status_}}}};
  }
 private:
  const char* scope_;
  const char* status_{"FAILED"};
  std::optional<Clock::time_point> start_;
  std::optional<double> elapsed_;
};

inline std::string now_utc() {
  const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

inline std::string read_build_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw PipelineError(ErrorCategory::kPayloadMissing,
                                 "Merge build provenance missing: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

// Called outside the measured region; use actual build files and executable,
// not the frozen Baseline's illustrative configure/link command strings.
inline nlohmann::json build_provenance(
    BuildManifest& manifest, const cpp_main::SourceBuildEvidence& native,
    const SourceIdentity& wrapper_source, const std::filesystem::path& build,
    const std::filesystem::path& library,
    const std::vector<std::pair<std::filesystem::path, std::string>>& sources) {
  using Json = nlohmann::json;
  Json hashes = Json::object();
  for (const auto& entry : sources) {
    if (sha256_file(entry.first) != entry.second)
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "Merge source changed since CMake configuration: " + entry.first.string());
    hashes[entry.first.generic_string()] = entry.second;
  }
  manifest.toolchain.cmake_definitions.clear();
  std::istringstream cache(read_build_file(build / "CMakeCache.txt"));
  std::string line;
  while (std::getline(cache, line)) {
    if (line.rfind("DISKANN_", 0) == 0 || line.rfind("CMAKE_CXX_FLAGS", 0) == 0)
      manifest.toolchain.cmake_definitions.push_back(line);
  }
  manifest.toolchain.compile_commands.clear();
  const auto executable = std::filesystem::read_symlink("/proc/self/exe");
  const auto target_link = std::filesystem::path("CMakeFiles") /
      (executable.filename().string() + ".dir") / "link.txt";
  std::optional<std::filesystem::path> link_record;
  // RUNTIME_OUTPUT_DIRECTORY can differ from the target's CMake directory
  // (notably cpp_main_baseline_runner). Resolve through actual build metadata.
  for (const auto& unit : Json::parse(read_build_file(build / "compile_commands.json"))) {
    if (unit.at("file").get<std::string>().find("/tests/") == std::string::npos)
      manifest.toolchain.compile_commands.push_back(unit.at("command").get<std::string>());
    const auto candidate = std::filesystem::path(unit.at("directory").get<std::string>()) / target_link;
    if (std::filesystem::is_regular_file(candidate)) link_record = candidate;
  }
  if (!link_record)
    throw PipelineError(ErrorCategory::kPayloadMissing, "Merge executable link command is missing: " + executable.string());
  manifest.toolchain.link_commands = {read_build_file(*link_record)};
  Json wrapper{{"repository", wrapper_source.repository}, {"branch", wrapper_source.branch},
               {"commit", wrapper_source.commit}, {"dirty_worktree", wrapper_source.dirty_worktree}};
  if (wrapper_source.dirty_source_checksum) wrapper["dirty_source_checksum"] = *wrapper_source.dirty_source_checksum;
  return {{"wrapper_source", wrapper}, {"source_files_sha256", hashes},
          {"runner_binary", {{"path", executable.generic_string()}, {"sha256", sha256_file(executable)}}},
          {"wrapper_library", {{"path", library.generic_string()}, {"sha256", sha256_file(library)}}},
          {"native_library", {{"path", native.upstream_library_file.generic_string()}, {"sha256", native.upstream_library_sha256}}},
          {"diskann_source", {{"repository", native.source.repository}, {"branch", native.source.branch},
                               {"commit", native.source.commit}, {"dirty_worktree", native.source.dirty_worktree}}}};
}

}  // namespace diskann_pipeline::merge_record
