#include "baseline/cpp_main/native_reference_capture.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "support/test_assert.hpp"

#include <chrono>
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

int main(int argc, char** argv) {
#if !defined(__linux__)
  static_cast<void>(argc);
  static_cast<void>(argv);
  return 77;
#else
  if (argc == 4 && std::string(argv[1]) == "--capture-child") {
    const auto source = std::filesystem::path(argv[2]);
    const auto medoids = std::filesystem::path(argv[3]);
    fixture::write_bytes(source,
                         fixture::native_graph(0, {{1, 2}, {0, 3}, {0, 3}, {1, 2}}));
    fixture::write_bytes(medoids, fixture::id_matrix({0, 2}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::filesystem::remove(source);
    return 0;
  }
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string(argv[index]) != "--index_path_prefix") continue;
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

  native::NativeReferenceCaptureRequest request{
      source_directory,
      source_graph.filename().string(),
      source_medoids.filename().string(),
      captured_graph,
      captured_medoids,
      {std::filesystem::absolute(argv[0]).string(), "--capture-child",
       source_graph.string(), source_medoids.string()}};
  const auto result = native::capture_native_reference(request);
  DP_EXPECT_EQ(result.child_exit_code, 0);
  DP_EXPECT_TRUE(result.exact_graph_event_observed);
  DP_EXPECT_TRUE(result.graph_hard_link_created);
  DP_EXPECT_TRUE(!std::filesystem::exists(source_graph));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_graph));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(captured_medoids));
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
  const dp::ArtifactRef retained_reference{
      "reference/artifact.json", dp::ArtifactType::kGlobalGraph,
      "global-graph-artifact/v2",
      artifact.at("aggregate_checksum").get<std::string>()};
  DP_EXPECT_TRUE(
      dp::Validator(workspace).validate_with_lineage(retained_reference).valid());
  return dp::test::finish();
#endif
}
