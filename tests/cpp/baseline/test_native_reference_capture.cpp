#include "baseline/cpp_main/native_reference_capture.hpp"
#include "baseline/cpp_main/source_build_identity.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "support/test_assert.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
#if !defined(__linux__)
  static_cast<void>(argc);
  static_cast<void>(argv);
  return 77;
#else
  if (argc >= 4 && std::string(argv[1]) == "--capture-child") {
    const auto source = std::filesystem::path(argv[2]);
    const auto medoids = std::filesystem::path(argv[3]);
    const auto mode = argc == 5 ? std::string(argv[4]) : "success";
    if (mode == "failure") {
      std::cout << "native-child-before-failure\n";
      std::cerr << "native-child-failure\n";
      return 9;
    }
    if (mode == "missing") return 0;
    if (mode == "invalid") {
      fixture::write_bytes(source, {1, 2, 3, 4});
      fixture::write_bytes(medoids, {1, 2, 3});
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      std::filesystem::remove(source);
      return 0;
    }
    if (mode != "empty") {
      std::cout << "Time for partitioning data : 0.100000 seconds\n"
                << "Time for building indices on shards: 0.200000 seconds\n"
                << "Time for merging indices: 0.300000 seconds\n";
      std::cerr << "native-child-stderr\n";
    }
    fixture::write_bytes(source,
                         fixture::native_graph(0, {{1, 2}, {0, 3}, {0, 3}, {1, 2}}));
    fixture::write_bytes(medoids, fixture::id_matrix({0, 2}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::filesystem::remove(source);
    return 0;
  }
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string(argv[index]) != "--index_path_prefix") continue;
    if (const char* counter = std::getenv("NATIVE_CAPTURE_INVOCATION_FILE")) {
      std::ofstream invocation(counter, std::ios::app);
      invocation << "launch\n";
    }
    if (const char* mode = std::getenv("NATIVE_CAPTURE_FIXTURE_MODE")) {
      if (std::string(mode) == "failure") {
        std::cout << "native-child-before-failure\n";
        std::cerr << "native-child-failure\n";
        return 9;
      }
    }
    const auto prefix = std::filesystem::path(argv[index + 1]);
    std::filesystem::path data_path;
    for (int argument = 1; argument + 1 < argc; ++argument) {
      if (std::string(argv[argument]) == "--data_path") {
        data_path = argv[argument + 1];
        break;
      }
    }
    std::ifstream data(data_path, std::ios::binary);
    std::uint32_t rows = 0;
    std::uint32_t dimension = 0;
    data.read(reinterpret_cast<char*>(&rows), sizeof(rows));
    data.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
    if (!data || rows < 4 || dimension == 0) return 3;
    std::vector<std::vector<dp::GlobalNodeId>> adjacency(rows);
    for (std::uint32_t node = 0; node < rows; ++node) {
      adjacency[node] = {(node + rows - 1) % rows, (node + 1) % rows};
    }
    const auto graph = prefix.string() + "_mem.index";
    const auto medoids = prefix.string() + "_disk.index_medoids.bin";
    fixture::write_bytes(graph, fixture::native_graph(0, adjacency));
    fixture::write_bytes(medoids, fixture::id_matrix({0, rows / 2}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::filesystem::remove(graph);
    return 0;
  }

  fixture::TemporaryDirectory temporary("native-reference-capture");
  const auto source_directory = temporary.path / "native-output";
  const auto output_directory = temporary.path / "retained";
  std::filesystem::create_directories(source_directory);
  std::filesystem::create_directories(output_directory);
  const auto source_graph = source_directory / "fixture_mem.index";
  const auto source_medoids = source_directory / "fixture_disk.index_medoids.bin";
  const auto captured_graph = output_directory / "graph.index";
  const auto captured_medoids = output_directory / "medoids.bin";
  const auto captured_stdout = output_directory / "native.stdout";
  const auto captured_stderr = output_directory / "native.stderr";

  native::NativeReferenceCaptureRequest request{
      source_directory,
      source_graph.filename().string(),
      source_medoids.filename().string(),
      captured_graph,
      captured_medoids,
      captured_stdout,
      captured_stderr,
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       source_graph.string(), source_medoids.string()}};
  const auto result = native::capture_native_reference(request);
  DP_EXPECT_EQ(result.child_exit_code, 0);
  DP_EXPECT_TRUE(result.exact_graph_event_observed);
  DP_EXPECT_TRUE(result.graph_hard_link_created);
  DP_EXPECT_TRUE(!std::filesystem::exists(source_graph));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_graph));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_medoids));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_stdout));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_stderr));
  DP_EXPECT_TRUE(read_text(captured_stdout).find(
                     "Time for partitioning data : 0.100000 seconds") !=
                 std::string::npos);
  DP_EXPECT_EQ(read_text(captured_stderr), "native-child-stderr\n");
  DP_EXPECT_TRUE(result.observer_armed);
  DP_EXPECT_TRUE(!result.observer_ready_at.empty());
  DP_EXPECT_TRUE(!result.child_launched_at.empty());
  DP_EXPECT_EQ(result.terminal_result, "SUCCEEDED");
  DP_EXPECT_EQ(
      native::validate_merged_global_graph(captured_graph, 4, 2).entry_point,
      dp::GlobalNodeId{0});
  DP_EXPECT_EQ(native::validate_medoids(captured_medoids, 2, 4).size(),
               std::size_t{2});
  DP_EXPECT_TRUE(result.started_at <= result.ended_at);

  const auto graph_sha = dp::sha256_file(captured_graph);
  const auto medoids_sha = dp::sha256_file(captured_medoids);
  DP_EXPECT_THROW(native::capture_native_reference(request));
  DP_EXPECT_EQ(dp::sha256_file(captured_graph), graph_sha);
  DP_EXPECT_EQ(dp::sha256_file(captured_medoids), medoids_sha);

  const auto empty_directory = temporary.path / "empty-native-output";
  const auto empty_output = temporary.path / "empty-retained";
  std::filesystem::create_directories(empty_directory);
  std::filesystem::create_directories(empty_output);
  native::NativeReferenceCaptureRequest empty_request{
      empty_directory,
      "fixture_mem.index",
      "fixture_disk.index_medoids.bin",
      empty_output / "graph.index",
      empty_output / "medoids.bin",
      empty_output / "native.stdout",
      empty_output / "native.stderr",
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       (empty_directory / "fixture_mem.index").string(),
       (empty_directory / "fixture_disk.index_medoids.bin").string(), "empty"}};
  const auto empty_result = native::capture_native_reference(empty_request);
  DP_EXPECT_EQ(empty_result.child_exit_code, 0);
  DP_EXPECT_EQ(std::filesystem::file_size(empty_request.stdout_path),
               std::uintmax_t{0});
  DP_EXPECT_EQ(std::filesystem::file_size(empty_request.stderr_path),
               std::uintmax_t{0});
  DP_EXPECT_EQ(dp::sha256_file(empty_request.stdout_path),
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  const auto failure_directory = temporary.path / "failure-native-output";
  const auto failure_output = temporary.path / "failure-retained";
  std::filesystem::create_directories(failure_directory);
  std::filesystem::create_directories(failure_output);
  native::NativeReferenceCaptureRequest failure_request{
      failure_directory,
      "fixture_mem.index",
      "fixture_disk.index_medoids.bin",
      failure_output / "graph.index",
      failure_output / "medoids.bin",
      failure_output / "native.stdout",
      failure_output / "native.stderr",
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       (failure_directory / "fixture_mem.index").string(),
       (failure_directory / "fixture_disk.index_medoids.bin").string(), "failure"}};
  const auto failure_result = native::capture_native_reference(failure_request);
  DP_EXPECT_EQ(failure_result.child_exit_code, 9);
  DP_EXPECT_EQ(failure_result.terminal_result, "FAILED");
  DP_EXPECT_TRUE(!failure_result.failure_reason.empty());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(failure_request.stdout_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(failure_request.stderr_path));
  DP_EXPECT_TRUE(!std::filesystem::exists(failure_request.captured_graph_path));
  DP_EXPECT_TRUE(!std::filesystem::exists(failure_request.captured_medoids_path));

  const auto missing_directory = temporary.path / "missing-native-output";
  const auto missing_output = temporary.path / "missing-retained";
  std::filesystem::create_directories(missing_directory);
  std::filesystem::create_directories(missing_output);
  native::NativeReferenceCaptureRequest missing_request{
      missing_directory,
      "fixture_mem.index",
      "fixture_disk.index_medoids.bin",
      missing_output / "graph.index",
      missing_output / "medoids.bin",
      missing_output / "native.stdout",
      missing_output / "native.stderr",
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       (missing_directory / "fixture_mem.index").string(),
       (missing_directory / "fixture_disk.index_medoids.bin").string(), "missing"}};
  const auto missing_result = native::capture_native_reference(missing_request);
  DP_EXPECT_EQ(missing_result.child_exit_code, 0);
  DP_EXPECT_TRUE(!missing_result.exact_graph_event_observed);
  DP_EXPECT_EQ(missing_result.terminal_result, "FAILED");
  DP_EXPECT_TRUE(missing_result.failure_reason.find("not observed") !=
                 std::string::npos);

  const auto invalid_directory = temporary.path / "invalid-native-output";
  const auto invalid_output = temporary.path / "invalid-retained";
  std::filesystem::create_directories(invalid_directory);
  std::filesystem::create_directories(invalid_output);
  native::NativeReferenceCaptureRequest invalid_request{
      invalid_directory,
      "fixture_mem.index",
      "fixture_disk.index_medoids.bin",
      invalid_output / "graph.index",
      invalid_output / "medoids.bin",
      invalid_output / "native.stdout",
      invalid_output / "native.stderr",
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       (invalid_directory / "fixture_mem.index").string(),
       (invalid_directory / "fixture_disk.index_medoids.bin").string(), "invalid"}};
  const auto invalid_result = native::capture_native_reference(invalid_request);
  DP_EXPECT_EQ(invalid_result.terminal_result, "SUCCEEDED");
  DP_EXPECT_THROW(native::validate_merged_global_graph(
      invalid_request.captured_graph_path, 4, 2));
  DP_EXPECT_THROW(native::validate_medoids(
      invalid_request.captured_medoids_path, 2, 4));

  const auto hardlink_directory = temporary.path / "hardlink-native-output";
  const auto hardlink_output = temporary.path / "hardlink-retained";
  const auto hardlink_logs = temporary.path / "hardlink-logs";
  std::filesystem::create_directories(hardlink_directory);
  std::filesystem::create_directories(hardlink_output);
  std::filesystem::create_directories(hardlink_logs);
  std::filesystem::permissions(
      hardlink_output,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
      std::filesystem::perm_options::replace);
  native::NativeReferenceCaptureRequest hardlink_request{
      hardlink_directory,
      "fixture_mem.index",
      "fixture_disk.index_medoids.bin",
      hardlink_output / "graph.index",
      hardlink_output / "medoids.bin",
      hardlink_logs / "native.stdout",
      hardlink_logs / "native.stderr",
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       (hardlink_directory / "fixture_mem.index").string(),
       (hardlink_directory / "fixture_disk.index_medoids.bin").string()}};
  const auto hardlink_result = native::capture_native_reference(hardlink_request);
  std::filesystem::permissions(
      hardlink_output, std::filesystem::perms::owner_all,
      std::filesystem::perm_options::replace);
  DP_EXPECT_EQ(hardlink_result.terminal_result, "FAILED");
  DP_EXPECT_TRUE(!hardlink_result.graph_hard_link_created);
  DP_EXPECT_TRUE(!hardlink_result.failure_reason.empty());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(hardlink_request.stdout_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(hardlink_request.stderr_path));

  const auto admitted =
      native::admit_source_and_build(native::configured_source_build_admission());
  DP_EXPECT_TRUE(native::same_source_build_evidence(admitted, admitted));
  auto mutated = admitted;
  mutated.upstream_library_sha256 = std::string(64, '0');
  DP_EXPECT_TRUE(!native::same_source_build_evidence(admitted, mutated));

  const auto workspace = dp::create_workspace(temporary.path / "runner-workspace");
  fixture::NativeDatasetFixture dataset(workspace, 12, 4);
  const auto manifests = workspace.root / "manifests" / "build";
  const auto evidence_directory = workspace.root / "manifests" / "evidence";
  std::filesystem::create_directories(manifests);
  std::filesystem::create_directories(evidence_directory);
  const fixture::Json toolchain{
      {"os", "linux-x86_64"}, {"compiler_path", "/usr/bin/g++"},
      {"compiler_version", "fixture"}, {"cmake_version", "fixture"},
      {"generator", "fixture"}, {"build_type", "Release"},
      {"cmake_definitions", fixture::Json::array()},
      {"compile_commands",
       fixture::Json::array({"-O3", "-DNDEBUG", "-std=c++17"})},
      {"link_commands", fixture::Json::array()}};
  const auto owner_manifest = manifests / "owner.json";
  const auto native_manifest = manifests / "native.json";
  const auto runner = std::filesystem::path(NATIVE_REFERENCE_CAPTURE_RUNNER);
  const auto executable = std::filesystem::absolute(argv[0]);
  fixture::write_text(
      owner_manifest,
      fixture::Json{
          {"source",
           {{"repository",
             "https://github.com/krystaZeng/diskann_search_benchmark.git"},
            {"branch", "feature/framework-baseline-certification"},
            {"commit", "10f8520d8617fdaf8de3abf8a87842346ae4c96f"},
            {"dirty_worktree", false}}},
          {"toolchain", toolchain},
          {"parameters", {{"binary_sha256", dp::sha256_file(runner)}}}}
          .dump());
  fixture::write_text(
      native_manifest,
      fixture::Json{
          {"source",
           {{"repository", "https://github.com/microsoft/DiskANN.git"},
            {"branch", "cpp_main"},
            {"commit", "78256bbab4685e1774e78d331e081a153be26823"},
            {"dirty_worktree", false}}},
          {"toolchain", toolchain},
          {"parameters", {{"binary_sha256", dp::sha256_file(executable)}}}}
          .dump());
  const auto dataset_evidence = evidence_directory / "dataset.json";
  fixture::write_text(dataset_evidence,
                      fixture::Json{{"result", "VALID"}}.dump());
  const auto document_ref = [&](const std::filesystem::path& value) {
    return fixture::Json{
        {"path", std::filesystem::relative(value, workspace.root).generic_string()},
        {"byte_length", std::filesystem::file_size(value)},
        {"sha256", dp::sha256_file(value)}};
  };
  const auto binary = [&](const char* implementation,
                          const std::filesystem::path& value,
                          const std::filesystem::path& manifest) {
    return fixture::Json{{"implementation_id", implementation},
                         {"path", value.string()},
                         {"byte_length", std::filesystem::file_size(value)},
                         {"sha256", dp::sha256_file(value)},
                         {"build_manifest", document_ref(manifest)}};
  };
  const fixture::Json runner_request{
      {"request_version", "native-reference-build-request/v1"},
      {"run_id", "native-reference-runner-fixture"},
      {"study_class", "baseline_validation"},
      {"reference_role", "cpp_main_native_sharded_reference_v1"},
      {"reproduction_mode", "upstream_faithful"},
      {"execution_authorized", false},
      {"workspace_root", workspace.root.string()},
      {"source",
       {{"repository", "https://github.com/microsoft/DiskANN.git"},
        {"branch", "cpp_main"},
        {"commit", "78256bbab4685e1774e78d331e081a153be26823"},
        {"source_path", "upstream/diskann-cpp-main"},
        {"dirty_worktree", false}}},
      {"project_operation_owner",
       {{"implementation_id", "framework_baseline_native_reference_capture_v1"},
        {"implementation_version", "1.0.0"},
        {"repository",
         "https://github.com/krystaZeng/diskann_search_benchmark.git"},
        {"branch", "feature/framework-baseline-certification"},
        {"commit", "10f8520d8617fdaf8de3abf8a87842346ae4c96f"},
        {"dirty_worktree", false},
        {"binary",
         binary("framework_baseline_native_reference_capture_v1", runner,
                owner_manifest)}}},
      {"native_binary",
       binary("cpp_main_build_disk_index_native_v1", executable,
              native_manifest)},
      {"dataset_artifact",
       {{"metadata_path", dataset.reference.metadata_path.generic_string()},
        {"artifact_type", dp::to_string(dataset.reference.artifact_type)},
        {"format_version", dataset.reference.format_version},
        {"aggregate_checksum", dataset.reference.aggregate_checksum}}},
      {"dataset_validation_evidence", document_ref(dataset_evidence)},
      {"profile",
       {{"path",
         "experiments/configs/cpp-main-upstream-baseline/baseline-run-profile.yaml"},
        {"sha256", std::string(64, '1')}, {"status", "draft"}}},
      {"controls",
       {{"data_type", "float"}, {"distance_metric", "l2"},
        {"branch_mode", "sharded"}, {"R", 16}, {"L", 32}, {"B", 0.03},
        {"build_DRAM_budget_gib", 0.6}, {"num_threads", 6}, {"QD", 0},
        {"disk_PQ_bytes", 0}, {"build_PQ_bytes", 0}, {"opq", false},
        {"labels", false}}},
      {"capture",
       {{"mechanism", "prearmed_inotify_same_filesystem_hardlink"},
        {"source_graph_name", "fixture_mem.index"},
        {"source_medoids_name", "fixture_disk.index_medoids.bin"},
        {"captured_graph_path", "normal/reference/global.index"},
        {"captured_medoids_path", "normal/reference/medoids.bin"},
        {"same_filesystem_required", true},
        {"copy_fallback_allowed", false}, {"exclusive_create", true}}},
      {"resource_budget",
       {{"memory_budget_bytes", 1024}, {"thread_budget", 6},
        {"disk_budget_bytes", 4096}}},
      {"outputs",
       {{"artifact_metadata_path", "normal/reference/artifact.json"},
        {"build_manifest_path", "manifests/reference.json"},
        {"validation_evidence_path", "manifests/evidence/reference.json"},
        {"stdout_path", "reports/reference.stdout"},
        {"stderr_path", "reports/reference.stderr"},
        {"observer_evidence_path", "reports/reference-observer.json"},
        {"structural_projection_path", "reports/reference-structural.json"}}}};
  const auto request_path = workspace.root / "requests" / "reference.json";
  fixture::write_text(request_path, runner_request.dump());
  const pid_t runner_child = ::fork();
  if (runner_child == 0) {
    ::execl(runner.c_str(), runner.c_str(), "--request", request_path.c_str(),
            static_cast<char*>(nullptr));
    _exit(127);
  }
  int runner_status = 0;
  DP_EXPECT_TRUE(runner_child > 0);
  DP_EXPECT_EQ(::waitpid(runner_child, &runner_status, 0), runner_child);
  DP_EXPECT_TRUE(WIFEXITED(runner_status));
  DP_EXPECT_EQ(WEXITSTATUS(runner_status), 0);
  const auto artifact =
      fixture::read_json(workspace.normal_root / "reference/artifact.json");
  const auto manifest =
      fixture::read_json(workspace.root / "manifests/reference.json");
  const auto validation = fixture::read_json(
      workspace.root / "manifests/evidence/reference.json");
  DP_EXPECT_EQ(artifact.at("format_version"), "global-graph-artifact/v2");
  DP_EXPECT_EQ(manifest.at("status"), "SUCCEEDED");
  DP_EXPECT_EQ(validation.at("result"), "VALID");
  DP_EXPECT_TRUE(manifest.at("started_at") <= manifest.at("ended_at"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      workspace.root / "reports/reference.stdout"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      workspace.root / "reports/reference.stderr"));
  DP_EXPECT_TRUE(read_text(workspace.root / "reports/reference.stdout")
                     .find("\"status\"") == std::string::npos);
  const auto observer = fixture::read_json(
      workspace.root / "reports/reference-observer.json");
  DP_EXPECT_EQ(observer.at("terminal_result"), "SUCCEEDED");
  DP_EXPECT_TRUE(manifest.at("parameters").contains("native_child_logs"));
  DP_EXPECT_TRUE(manifest.at("parameters").contains("observer_evidence"));
  DP_EXPECT_TRUE(manifest.at("parameters").at("upstream_admission").at("consistent"));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.root / "reports/reference-structural.json"));
  const dp::ArtifactRef retained_reference{
      "reference/artifact.json", dp::ArtifactType::kGlobalGraph,
      "global-graph-artifact/v2",
      artifact.at("aggregate_checksum").get<std::string>()};
  DP_EXPECT_TRUE(
      dp::Validator(workspace).validate_with_lineage(retained_reference).valid());

  auto failed_request = runner_request;
  failed_request["run_id"] = "native-reference-runner-failure-fixture";
  failed_request["capture"]["captured_graph_path"] =
      "normal/reference-failed/global.index";
  failed_request["capture"]["captured_medoids_path"] =
      "normal/reference-failed/medoids.bin";
  failed_request["outputs"] = {
      {"artifact_metadata_path", "normal/reference-failed/artifact.json"},
      {"build_manifest_path", "manifests/reference-failed.json"},
      {"validation_evidence_path",
       "manifests/evidence/reference-failed.json"},
      {"stdout_path", "reports/reference-failed.stdout"},
      {"stderr_path", "reports/reference-failed.stderr"},
      {"observer_evidence_path", "reports/reference-failed-observer.json"},
      {"structural_projection_path",
       "reports/reference-failed-structural.json"}};
  const auto failed_request_path =
      workspace.root / "requests" / "reference-failed.json";
  fixture::write_text(failed_request_path, failed_request.dump());
  const auto invocation_file = temporary.path / "native-child-invocations.txt";
  ::setenv("NATIVE_CAPTURE_FIXTURE_MODE", "failure", 1);
  ::setenv("NATIVE_CAPTURE_INVOCATION_FILE", invocation_file.c_str(), 1);
  const pid_t failed_runner = ::fork();
  if (failed_runner == 0) {
    ::execl(runner.c_str(), runner.c_str(), "--request",
            failed_request_path.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int failed_status = 0;
  DP_EXPECT_EQ(::waitpid(failed_runner, &failed_status, 0), failed_runner);
  ::unsetenv("NATIVE_CAPTURE_FIXTURE_MODE");
  ::unsetenv("NATIVE_CAPTURE_INVOCATION_FILE");
  DP_EXPECT_TRUE(WIFEXITED(failed_status));
  DP_EXPECT_EQ(WEXITSTATUS(failed_status), 1);
  DP_EXPECT_EQ(read_text(invocation_file), "launch\n");
  const auto failed_manifest =
      fixture::read_json(workspace.root / "manifests/reference-failed.json");
  DP_EXPECT_EQ(failed_manifest.at("status"), "FAILED");
  DP_EXPECT_TRUE(failed_manifest.at("failure").contains("reason"));
  DP_EXPECT_TRUE(!failed_manifest.at("failure").contains("message"));
  DP_EXPECT_EQ(failed_manifest.at("failure").at("exit_code"), 9);
  DP_EXPECT_TRUE(failed_manifest.at("output_artifacts").empty());
  DP_EXPECT_EQ(failed_manifest.at("toolchain"), toolchain);
  DP_EXPECT_TRUE(failed_manifest.at("started_at") <=
                 failed_manifest.at("ended_at"));
  DP_EXPECT_EQ(failed_manifest.at("source").at("commit"),
               "78256bbab4685e1774e78d331e081a153be26823");
  DP_EXPECT_TRUE(
      failed_manifest.at("parameters").at("native_child_logs").contains("stdout"));
  DP_EXPECT_TRUE(
      failed_manifest.at("parameters").contains("observer_evidence"));
  const fixture::Json expected_dataset{
      {"dataset_id", dataset.identity.dataset_id},
      {"dataset_checksum", dataset.identity.dataset_checksum},
      {"global_node_count", dataset.identity.global_node_count},
      {"vector_dimension", dataset.identity.vector_dimension},
      {"scalar_type", dataset.identity.scalar_type},
      {"distance_metric", dataset.identity.distance_metric}};
  DP_EXPECT_EQ(failed_manifest.at("dataset"), expected_dataset);
  DP_EXPECT_EQ(
      failed_manifest.at("resource_budget").at("memory_budget_bytes"), 1024);
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      workspace.root / "reports/reference-failed.stdout"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      workspace.root / "reports/reference-failed.stderr"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      workspace.root / "reports/reference-failed-observer.json"));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.root / "normal/reference-failed/artifact.json"));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.root / "manifests/evidence/reference-failed.json"));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.root / "reports/reference-failed-structural.json"));
  return dp::test::finish();
#endif
}
