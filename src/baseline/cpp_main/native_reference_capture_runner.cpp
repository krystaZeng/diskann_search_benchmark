#include "baseline/cpp_main/native_reference_capture.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
using Json = nlohmann::json;

namespace {

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read JSON: " + path.string());
  return Json::parse(input);
}

std::filesystem::path confined(const std::filesystem::path& root,
                               const std::string& relative) {
  const auto value = std::filesystem::path(relative);
  if (value.empty() || value.is_absolute()) {
    throw std::runtime_error("request path is not relative");
  }
  for (const auto& part : value) {
    if (part == "..") throw std::runtime_error("request path escapes workspace");
  }
  const auto resolved = std::filesystem::weakly_canonical(root / value);
  const auto canonical_root = std::filesystem::weakly_canonical(root);
  if (resolved.string().rfind(canonical_root.string() + "/", 0) != 0) {
    throw std::runtime_error("request path escapes workspace");
  }
  return resolved;
}

void write_exclusive(const std::filesystem::path& path, const Json& value) {
  std::filesystem::create_directories(path.parent_path());
  const auto payload = value.dump();
  const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL,
                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (descriptor < 0) throw std::runtime_error("immutable output already exists");
  const auto written = ::write(descriptor, payload.data(), payload.size());
  const bool ok = written == static_cast<ssize_t>(payload.size()) &&
                  ::fsync(descriptor) == 0;
  ::close(descriptor);
  if (!ok) throw std::runtime_error("cannot publish immutable JSON");
}

Json artifact_ref(const dp::ArtifactRef& value) {
  return {{"metadata_path", value.metadata_path.generic_string()},
          {"artifact_type", dp::to_string(value.artifact_type)},
          {"format_version", value.format_version},
          {"aggregate_checksum", value.aggregate_checksum}};
}

dp::ArtifactRef parse_artifact_ref(const Json& value) {
  return {value.at("metadata_path").get<std::string>(),
          dp::artifact_type_from_string(
              value.at("artifact_type").get<std::string>()),
          value.at("format_version").get<std::string>(),
          value.at("aggregate_checksum").get<std::string>()};
}

Json verify_binary_identity(const std::filesystem::path& workspace_root,
                            const Json& identity, const Json& expected_source) {
  const auto binary = std::filesystem::path(identity.at("path").get<std::string>());
  if (!binary.is_absolute() || !std::filesystem::is_regular_file(binary) ||
      std::filesystem::file_size(binary) !=
          identity.at("byte_length").get<std::uint64_t>() ||
      dp::sha256_file(binary) != identity.at("sha256").get<std::string>()) {
    throw std::runtime_error("native Reference binary identity mismatch");
  }
  const auto& reference = identity.at("build_manifest");
  const auto manifest_path =
      confined(workspace_root, reference.at("path").get<std::string>());
  if (!std::filesystem::is_regular_file(manifest_path) ||
      std::filesystem::file_size(manifest_path) !=
          reference.at("byte_length").get<std::uint64_t>() ||
      dp::sha256_file(manifest_path) != reference.at("sha256").get<std::string>()) {
    throw std::runtime_error("native Reference BuildManifest identity mismatch");
  }
  const auto manifest = read_json(manifest_path);
  if (!manifest.contains("source") || manifest.at("source") != expected_source ||
      !manifest.contains("toolchain") || !manifest.contains("parameters")) {
    throw std::runtime_error("native Reference BuildManifest source/toolchain mismatch");
  }
  const auto& parameters = manifest.at("parameters");
  std::string recorded_sha;
  if (parameters.contains("binary_sha256")) {
    recorded_sha = parameters.at("binary_sha256").get<std::string>();
  } else if (parameters.contains("binary") &&
             parameters.at("binary").contains("sha256")) {
    recorded_sha = parameters.at("binary").at("sha256").get<std::string>();
  } else if (parameters.contains("runner_binary") &&
             parameters.at("runner_binary").contains("sha256")) {
    recorded_sha =
        parameters.at("runner_binary").at("sha256").get<std::string>();
  } else if (parameters.contains("build_closure") &&
             parameters.at("build_closure").contains("binary") &&
             parameters.at("build_closure").at("binary").contains("sha256")) {
    recorded_sha = parameters.at("build_closure")
                       .at("binary")
                       .at("sha256")
                       .get<std::string>();
  }
  if (recorded_sha != identity.at("sha256").get<std::string>()) {
    throw std::runtime_error("native Reference BuildManifest binary mismatch");
  }
  return manifest;
}

Json payload(const std::string& role, const std::filesystem::path& relative,
             const char* media, const std::filesystem::path& absolute) {
  return {{"role", role},
          {"path", relative.generic_string()},
          {"media_type", media},
          {"byte_length", std::filesystem::file_size(absolute)},
          {"sha256", dp::sha256_file(absolute)}};
}

std::string aggregate_for(const Json& metadata) {
  dp::AggregateIdentityProjection projection;
  projection.artifact_type = dp::ArtifactType::kGlobalGraph;
  projection.format_version = "global-graph-artifact/v2";
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space = dp::IdSpace::kGlobal;
  projection.capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  for (const auto& item : metadata.at("payloads")) {
    projection.payloads.push_back(
        {item.at("role").get<std::string>(),
         item.at("byte_length").get<std::uint64_t>(),
         item.at("sha256").get<std::string>()});
  }
  for (const auto& item : metadata.at("source_artifacts")) {
    projection.source_artifacts.push_back(
        {dp::artifact_type_from_string(
             item.at("artifact_type").get<std::string>()),
         item.at("format_version").get<std::string>(),
         item.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return dp::aggregate_checksum(projection);
}

Json source_json(const dp::SourceIdentity& source) {
  Json value{{"repository", source.repository},
             {"branch", source.branch},
             {"commit", source.commit},
             {"dirty_worktree", source.dirty_worktree}};
  if (source.dirty_source_checksum.has_value()) {
    value["dirty_source_checksum"] = *source.dirty_source_checksum;
  }
  return value;
}

Json build_json(const native::BuildConfiguration& build) {
  return {{"os", build.os},
          {"compiler_path", build.compiler_path},
          {"compiler_version", build.compiler_version},
          {"cmake_version", build.cmake_version},
          {"generator", build.generator},
          {"build_type", build.build_type},
          {"cmake_definitions", build.effective_cmake_definitions},
          {"compile_commands", build.project_compile_options},
          {"link_commands", build.linked_dependencies},
          {"configure_command", build.configure_command},
          {"build_command", build.build_command}};
}

Json admission_json(const native::SourceBuildEvidence& evidence) {
  return {{"source", source_json(evidence.source)},
          {"gitlink_commit", evidence.gitlink_commit},
          {"upstream_source_dir", evidence.upstream_source_dir.string()},
          {"upstream_binary_dir", evidence.upstream_binary_dir.string()},
          {"upstream_library",
           {{"path", evidence.upstream_library_file.string()},
            {"sha256", evidence.upstream_library_sha256}}},
          {"build", build_json(evidence.build)}};
}

Json document_identity(const std::filesystem::path& workspace_root,
                       const std::filesystem::path& path,
                       const std::string& stream_role = "") {
  Json value{{"path", std::filesystem::relative(path, workspace_root).generic_string()},
             {"byte_length", std::filesystem::file_size(path)},
             {"sha256", dp::sha256_file(path)}};
  if (!stream_role.empty()) value["stream_role"] = stream_role;
  return value;
}

Json observer_json(const Json& request,
                   const native::NativeReferenceCaptureResult& result) {
  Json value{{"run_id", request.at("run_id")},
             {"observer_mechanism", result.observer_mechanism},
             {"observer_armed", result.observer_armed},
             {"observer_ready_at", result.observer_ready_at},
             {"child_launched_at", result.child_launched_at},
             {"native_child_argv", result.native_child_argv},
             {"source_graph_name", result.source_graph_name},
             {"captured_graph_path",
              request.at("capture").at("captured_graph_path")},
             {"child_exit_code", result.child_exit_code},
             {"exact_graph_event_observed", result.exact_graph_event_observed},
             {"graph_hard_link_created", result.graph_hard_link_created},
             {"capture_started_at", result.started_at},
             {"capture_ended_at", result.ended_at},
             {"terminal_result", result.terminal_result}};
  if (!result.failure_reason.empty()) {
    value["failure_reason"] = result.failure_reason;
  }
  return value;
}

Json request_identity(const std::filesystem::path& path) {
  return {{"path", std::filesystem::absolute(path).string()},
          {"byte_length", std::filesystem::file_size(path)},
          {"sha256", dp::sha256_file(path)}};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3 || std::string(argv[1]) != "--request") {
    std::cerr << "usage: native_reference_capture_runner --request <path>\n";
    return 2;
  }
  Json request;
  Json native_manifest;
  Json dataset_metadata;
  Json upstream_source;
  Json parameters = Json::object();
  std::filesystem::path workspace_root;
  std::filesystem::path manifest_path;
  std::filesystem::path evidence_path;
  std::filesystem::path artifact_path;
  std::filesystem::path captured_graph;
  std::filesystem::path captured_medoids;
  std::filesystem::path stdout_path;
  std::filesystem::path stderr_path;
  std::filesystem::path observer_path;
  std::filesystem::path structural_projection_path;
  std::filesystem::path source_directory;
  std::string started_at;
  bool lifecycle_established = false;
  int native_child_exit_code = -1;
  try {
    request = read_json(argv[2]);
    if (request.at("request_version") != "native-reference-build-request/v1" ||
        request.at("execution_authorized") != false ||
        request.at("reference_role") !=
            "cpp_main_native_sharded_reference_v1" ||
        request.at("source").at("commit") !=
            "78256bbab4685e1774e78d331e081a153be26823") {
      throw std::runtime_error("native Reference request identity is invalid");
    }
    workspace_root = request.at("workspace_root").get<std::string>();
    const auto workspace = dp::create_workspace(workspace_root);
    upstream_source = Json{
        {"repository", request.at("source").at("repository")},
        {"branch", request.at("source").at("branch")},
        {"commit", request.at("source").at("commit")},
        {"dirty_worktree", request.at("source").at("dirty_worktree")}};
    const auto& owner = request.at("project_operation_owner");
    const Json owner_source{{"repository", owner.at("repository")},
                            {"branch", owner.at("branch")},
                            {"commit", owner.at("commit")},
                            {"dirty_worktree", owner.at("dirty_worktree")}};
    const auto owner_manifest = verify_binary_identity(
        workspace_root, owner.at("binary"), owner_source);
    static_cast<void>(owner_manifest);
    native_manifest = verify_binary_identity(
        workspace_root, request.at("native_binary"), upstream_source);
    if (std::filesystem::weakly_canonical(argv[0]) !=
        std::filesystem::weakly_canonical(
            owner.at("binary").at("path").get<std::string>())) {
      throw std::runtime_error("executed capture owner path identity mismatch");
    }
    manifest_path = confined(
        workspace_root,
        request.at("outputs").at("build_manifest_path").get<std::string>());
    evidence_path = confined(
        workspace_root,
        request.at("outputs").at("validation_evidence_path").get<std::string>());
    artifact_path = confined(
        workspace_root,
        request.at("outputs").at("artifact_metadata_path").get<std::string>());
    captured_graph = confined(
        workspace_root,
        request.at("capture").at("captured_graph_path").get<std::string>());
    captured_medoids = confined(
        workspace_root,
        request.at("capture").at("captured_medoids_path").get<std::string>());
    stdout_path = confined(
        workspace_root, request.at("outputs").at("stdout_path").get<std::string>());
    stderr_path = confined(
        workspace_root, request.at("outputs").at("stderr_path").get<std::string>());
    observer_path = confined(
        workspace_root,
        request.at("outputs").at("observer_evidence_path").get<std::string>());
    structural_projection_path = confined(
        workspace_root,
        request.at("outputs").at("structural_projection_path").get<std::string>());
    for (const auto& path : {manifest_path, evidence_path, artifact_path,
                            captured_graph, captured_medoids, stdout_path,
                            stderr_path, observer_path,
                            structural_projection_path}) {
      if (std::filesystem::exists(path)) {
        throw std::runtime_error("native Reference output already exists");
      }
    }

    const auto dataset_reference =
        parse_artifact_ref(request.at("dataset_artifact"));
    dp::Validator validator(workspace);
    const auto dataset_validation =
        validator.validate_with_lineage(dataset_reference);
    if (!dataset_validation.valid()) {
      throw std::runtime_error("DatasetArtifact fresh validation failed");
    }
    dataset_metadata = read_json(
        workspace.normal_root / dataset_reference.metadata_path);
    const auto vectors_record = dataset_metadata.at("payloads").at(0);
    const auto vectors =
        workspace.normal_root / dataset_reference.metadata_path.parent_path() /
        vectors_record.at("path").get<std::string>();

    source_directory =
        workspace.temporary_root /
        (request.at("run_id").get<std::string>() + "-native-output");
    if (std::filesystem::exists(source_directory)) {
      throw std::runtime_error("native output directory already exists");
    }
    std::filesystem::create_directories(source_directory);
    const auto graph_name =
        request.at("capture").at("source_graph_name").get<std::string>();
    const std::string suffix = "_mem.index";
    if (graph_name.size() <= suffix.size() ||
        graph_name.substr(graph_name.size() - suffix.size()) != suffix) {
      throw std::runtime_error("native graph filename is not exact");
    }
    const auto prefix =
        source_directory / graph_name.substr(0, graph_name.size() - suffix.size());
    const auto& controls = request.at("controls");
    std::vector<std::string> child{
        request.at("native_binary").at("path").get<std::string>(),
        "--data_type", controls.at("data_type").get<std::string>(),
        "--dist_fn", controls.at("distance_metric").get<std::string>(),
        "--index_path_prefix", prefix.string(),
        "--data_path", vectors.string(),
        "--search_DRAM_budget", std::to_string(controls.at("B").get<double>()),
        "--build_DRAM_budget",
        std::to_string(controls.at("build_DRAM_budget_gib").get<double>()),
        "--num_threads", std::to_string(controls.at("num_threads").get<int>()),
        "--max_degree", std::to_string(controls.at("R").get<int>()),
        "--Lbuild", std::to_string(controls.at("L").get<int>()),
        "--QD", std::to_string(controls.at("QD").get<int>()),
        "--PQ_disk_bytes", std::to_string(controls.at("disk_PQ_bytes").get<int>()),
        "--build_PQ_bytes", std::to_string(controls.at("build_PQ_bytes").get<int>())};
    started_at = now_utc();
    lifecycle_established = true;
    parameters = {{"reference_role", request.at("reference_role")},
                  {"request", request_identity(argv[2])},
                  {"project_operation_owner",
                   request.at("project_operation_owner")},
                  {"native_binary", request.at("native_binary")},
                  {"profile", request.at("profile")},
                  {"controls", request.at("controls")},
                  {"capture", request.at("capture")},
                  {"native_child_argv", child}};

    const auto source_before = native::admit_source_and_build(
        native::configured_source_build_admission());
    if (source_json(source_before.source) != upstream_source) {
      throw std::runtime_error("pre-run upstream source admission mismatch");
    }
    parameters["upstream_admission"] = {{"pre", admission_json(source_before)}};

    native::NativeReferenceCaptureRequest capture{
        source_directory,
        graph_name,
        request.at("capture").at("source_medoids_name").get<std::string>(),
        captured_graph,
        captured_medoids,
        stdout_path,
        stderr_path,
        child};
    const auto capture_result = native::capture_native_reference(capture);
    native_child_exit_code = capture_result.child_exit_code;
    write_exclusive(observer_path, observer_json(request, capture_result));
    if (std::filesystem::is_regular_file(stdout_path) &&
        std::filesystem::is_regular_file(stderr_path)) {
      parameters["native_child_logs"] =
          {{"stdout", document_identity(workspace_root, stdout_path, "stdout")},
           {"stderr", document_identity(workspace_root, stderr_path, "stderr")}};
    }
    parameters["observer_evidence"] =
        document_identity(workspace_root, observer_path);

    const auto source_after = native::admit_source_and_build(
        native::configured_source_build_admission());
    const bool consistent =
        native::same_source_build_evidence(source_before, source_after);
    parameters["upstream_admission"]["post"] = admission_json(source_after);
    parameters["upstream_admission"]["consistent"] = consistent;
    if (!consistent || source_json(source_after.source) != upstream_source) {
      throw std::runtime_error("post-run upstream/build admission mismatch");
    }
    if (capture_result.terminal_result != "SUCCEEDED") {
      throw std::runtime_error(capture_result.failure_reason.empty()
                                   ? "native Reference capture failed"
                                   : capture_result.failure_reason);
    }

    const auto node_count =
        dataset_metadata.at("dataset").at("global_node_count").get<dp::NodeCount>();
    const auto declared = controls.at("R").get<std::uint32_t>();
    const auto graph_facts = native::validate_merged_global_graph(
        captured_graph, node_count, declared);
    std::ifstream medoid_input(captured_medoids, std::ios::binary);
    std::uint32_t medoid_count = 0;
    medoid_input.read(reinterpret_cast<char*>(&medoid_count), sizeof(medoid_count));
    const auto medoids =
        native::validate_medoids(captured_medoids, medoid_count, node_count);
    if (medoid_count < 2 ||
        std::find(medoids.begin(), medoids.end(), graph_facts.entry_point) ==
            medoids.end()) {
      throw std::runtime_error("native graph entry/medoids are invalid");
    }

    const auto metadata_parent = artifact_path.parent_path();
    Json dataset_source = request.at("dataset_artifact");
    dataset_source.erase("metadata_path");
    Json metadata{
        {"schema_version", "1.0.0"},
        {"canonicalization", "diskann-artifact-c14n-v1"},
        {"artifact_type", "global_graph"},
        {"format_version", "global-graph-artifact/v2"},
        {"dataset", dataset_metadata.at("dataset")},
        {"id_space", "global"},
        {"structural",
         {{"lineage_mode", "direct_native_final"},
          {"global_node_count", node_count},
          {"declared_max_degree", declared},
          {"observed_max_degree", graph_facts.observed_max_degree},
          {"partition_count", medoid_count},
          {"medoid_count", medoid_count}}},
        {"capabilities",
         Json::array({"graph.global_adjacency/v1", "graph.medoids/v1"})},
        {"source_artifacts", Json::array({dataset_source})},
        {"payloads",
         Json::array({payload("global_graph",
                              std::filesystem::relative(captured_graph,
                                                        metadata_parent),
                              native::kGlobalGraphMediaType, captured_graph),
                      payload("medoids",
                              std::filesystem::relative(captured_medoids,
                                                        metadata_parent),
                              native::kMedoidsMediaType, captured_medoids)})}};
    metadata["aggregate_checksum"] = aggregate_for(metadata);
    write_exclusive(artifact_path, metadata);
    const auto artifact_relative =
        std::filesystem::relative(artifact_path, workspace.normal_root);
    dp::ArtifactRef reference{artifact_relative, dp::ArtifactType::kGlobalGraph,
                              "global-graph-artifact/v2",
                              metadata.at("aggregate_checksum").get<std::string>()};
    const auto validation = validator.validate(
        reference, {*dataset_validation.validated_artifact});
    if (!validation.valid()) {
      const auto reason =
          validation.evidence.checks.empty()
              ? std::string("no validation check was retained")
              : validation.evidence.checks.back().message;
      throw std::runtime_error("published GlobalGraph/v2 validation failed: " +
                               reason);
    }
    Json evidence{
        {"evidence_version", "1.0.0"},
        {"validator_version", "artifact-contract-validator/1.1.0"},
        {"artifact_candidate",
         {{"artifact_type", "global_graph"},
          {"format_version", "global-graph-artifact/v2"},
          {"metadata_path", artifact_relative.generic_string()},
          {"claimed_aggregate_checksum", reference.aggregate_checksum}}},
        {"checks",
         Json::array({{{"check_id", "artifact.contract/v2"},
                       {"passed", true},
                       {"message", "direct native final graph and medoids are valid"}}})},
        {"result", "VALID"}};
    write_exclusive(evidence_path, evidence);

    const auto ended_at = now_utc();

    Json manifest{
        {"manifest_version", "1.0.0"},
        {"run_id", request.at("run_id")},
        {"stage", "build"},
        {"implementation",
         {{"implementation_id", "framework_baseline_native_reference_capture_v1"},
          {"implementation_version", "1.0.0"}}},
        {"source", upstream_source},
        {"toolchain", native_manifest.at("toolchain")},
        {"dataset", dataset_metadata.at("dataset")},
        {"parameters", parameters},
        {"resource_budget",
         {{"memory_budget_bytes",
           request.at("resource_budget").at("memory_budget_bytes")},
          {"thread_budget",
           request.at("resource_budget").at("thread_budget")}}},
        {"input_artifacts", Json::array({request.at("dataset_artifact")})},
        {"output_artifacts", Json::array({artifact_ref(reference)})},
        {"status", "SUCCEEDED"},
        {"started_at", started_at},
        {"ended_at", ended_at},
        {"validation_evidence_path",
         std::filesystem::relative(evidence_path, workspace_root).generic_string()}};
    write_exclusive(manifest_path, manifest);
    std::cout << Json{{"status", "SUCCEEDED"},
                      {"artifact", artifact_ref(reference)},
                      {"manifest_path", manifest_path.string()},
                      {"validation_evidence_path", evidence_path.string()}}
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception& error) {
    const auto ended_at = now_utc();
    if (!artifact_path.empty()) {
      std::error_code ignored;
      std::filesystem::remove(artifact_path, ignored);
      std::filesystem::remove(evidence_path, ignored);
      std::filesystem::remove(captured_graph, ignored);
      std::filesystem::remove(captured_medoids, ignored);
    }
    if (lifecycle_established && !manifest_path.empty() &&
        !std::filesystem::exists(manifest_path)) {
      try {
        Json failure_logs = Json::object();
        if (!stdout_path.empty() && std::filesystem::is_regular_file(stdout_path)) {
          failure_logs["stdout"] =
              document_identity(workspace_root, stdout_path, "stdout");
        }
        if (!stderr_path.empty() && std::filesystem::is_regular_file(stderr_path)) {
          failure_logs["stderr"] =
              document_identity(workspace_root, stderr_path, "stderr");
        }
        if (!failure_logs.empty()) {
          parameters["native_child_logs"] = std::move(failure_logs);
        }
        if (!observer_path.empty() &&
            std::filesystem::is_regular_file(observer_path)) {
          parameters["observer_evidence"] =
              document_identity(workspace_root, observer_path);
        }
        Json failure{
            {"manifest_version", "1.0.0"},
            {"run_id", request.value("run_id", "native-reference-failed")},
            {"stage", "build"},
            {"implementation",
             {{"implementation_id", "framework_baseline_native_reference_capture_v1"},
              {"implementation_version", "1.0.0"}}},
            {"source", upstream_source},
            {"toolchain", native_manifest.at("toolchain")},
            {"dataset", dataset_metadata.at("dataset")},
            {"parameters", parameters},
            {"resource_budget",
             {{"memory_budget_bytes",
               request.at("resource_budget").at("memory_budget_bytes")},
              {"thread_budget",
               request.at("resource_budget").at("thread_budget")}}},
            {"input_artifacts", Json::array({request.at("dataset_artifact")})},
            {"output_artifacts", Json::array()},
            {"status", "FAILED"},
            {"started_at", started_at},
            {"ended_at", ended_at},
            {"failure",
             {{"category", "native_reference_capture_failed"},
              {"reason", error.what()}}}};
        if (native_child_exit_code >= 0) {
          failure["failure"]["exit_code"] = native_child_exit_code;
        }
        write_exclusive(manifest_path, failure);
      } catch (const std::exception&) {
      }
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
}
