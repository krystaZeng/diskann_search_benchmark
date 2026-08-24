#include "search/diskann/fixed_descriptors.hpp"
#include "search/diskann/native_search_formats.hpp"
#include "search/diskann/post_graph_materializer.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>

#if DISKANN_SEARCH_LINUX_NATIVE_TEST
#include <immintrin.h>
#include "linux_aligned_file_reader.h"
#include "pq_flash_index.h"
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dp = diskann_pipeline;
namespace ds = diskann_pipeline::search;
namespace native_fixture = diskann_pipeline::cpp_main::test;
using Json = nlohmann::json;

namespace {

class Token final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return cancelled; }
  bool cancelled{false};
};

struct Inputs {
  native_fixture::TemporaryDirectory temporary{"search-materialization"};
  dp::WorkspaceRef workspace{dp::create_workspace(temporary.path / "workspace")};
  native_fixture::NativeDatasetFixture dataset{workspace, 6, 32};
  dp::ArtifactRef partition{native_fixture::materialize_partition(
      workspace, dataset, {{0, 1, 2}, {3, 4, 5}})};
  dp::ArtifactRef local0{native_fixture::materialize_local(
      workspace, dataset, partition, 0, {0, 1, 2},
      {{1, 2}, {0, 2}, {0, 1}})};
  dp::ArtifactRef local1{native_fixture::materialize_local(
      workspace, dataset, partition, 1, {3, 4, 5},
      {{1, 2}, {0, 2}, {0, 1}})};
  dp::ArtifactRef global{native_fixture::materialize_global(
      workspace, dataset, partition, {local0, local1},
      {{1, 2, 3}, {0, 2, 4}, {0, 1, 5},
       {0, 4, 5}, {1, 3, 5}, {2, 3, 4}},
      {0, 3})};
};

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

ds::MaterializationRequest request(const Inputs& inputs,
                                   const std::string& run_id) {
  ds::MaterializationRequest value;
  value.run_id = run_id;
  value.dataset = inputs.dataset.reference;
  value.global_graph = inputs.global;
  value.source = {"https://example.invalid/project.git",
                  "feature/search-evaluation", std::string(40, 'b'), true,
                  std::string(64, 'c')};
  value.diskann_source = {
                  "https://github.com/microsoft/DiskANN.git", "cpp_main",
                  "78256bbab4685e1774e78d331e081a153be26823", false,
                  std::nullopt};
  value.toolchain = {"test-os", "/usr/bin/c++", "test-compiler", "4.4.2",
                     "test-generator", "Release",
                     {"CMAKE_BUILD_TYPE=Release",
                      "DISKANN_CPP_MAIN_BASELINE=ON",
                      "DISKANN_SEARCH_EVALUATION=ON"},
                     {"/usr/bin/c++ -std=c++17 -O3 -DNDEBUG -mavx2 "
                      "-fopenmp -Wall -Wextra -Wpedantic"},
                     {"diskann_search_evaluation_runner <- diskann_s,OpenMP"}};
  value.resource_budget = {64ULL * 1024 * 1024, 1};
  value.runner_binary_path = "/tmp/diskann_search_evaluation_runner";
  value.runner_binary_sha256 = std::string(64, 'a');
  value.started_at = now_utc();
  return value;
}

void expect_manifest_provenance(const std::filesystem::path& path,
                                const ds::MaterializationRequest& request,
                                const std::string& native_call_time) {
  const auto manifest = native_fixture::read_json(path);
  DP_EXPECT_EQ(manifest.at("source").at("repository").get<std::string>(),
               request.source.repository);
  DP_EXPECT_EQ(manifest.at("source").at("branch").get<std::string>(),
               request.source.branch);
  DP_EXPECT_EQ(manifest.at("source").at("commit").get<std::string>(),
               request.source.commit);
  DP_EXPECT_TRUE(manifest.at("source").at("dirty_worktree").get<bool>());
  const auto& parameters = manifest.at("parameters");
  DP_EXPECT_EQ(parameters.at("diskann_source").at("repository").get<std::string>(),
               request.diskann_source.repository);
  DP_EXPECT_EQ(parameters.at("diskann_source").at("branch").get<std::string>(),
               request.diskann_source.branch);
  DP_EXPECT_EQ(parameters.at("diskann_source").at("commit").get<std::string>(),
               request.diskann_source.commit);
  DP_EXPECT_EQ(parameters.at("runner_binary").at("path").get<std::string>(),
               request.runner_binary_path.string());
  DP_EXPECT_EQ(parameters.at("runner_binary").at("sha256").get<std::string>(),
               request.runner_binary_sha256);
  const auto compile_command =
      manifest.at("toolchain").at("compile_commands").at(0).get<std::string>();
  DP_EXPECT_TRUE(compile_command.find("-O3") != std::string::npos);
  DP_EXPECT_TRUE(compile_command.find("-DNDEBUG") != std::string::npos);
  DP_EXPECT_TRUE(compile_command.find("-mavx2") != std::string::npos);
  DP_EXPECT_TRUE(compile_command.find("-fopenmp") != std::string::npos);
  DP_EXPECT_TRUE(compile_command.find("-Wall") != std::string::npos);
  DP_EXPECT_EQ(manifest.at("started_at").get<std::string>(),
               request.started_at);
  DP_EXPECT_TRUE(manifest.at("started_at").get<std::string>() <=
                 native_call_time);
  DP_EXPECT_TRUE(manifest.at("ended_at").get<std::string>() >=
                 native_call_time);
}

void expect_terminal_times(const std::filesystem::path& path,
                           const ds::MaterializationRequest& request) {
  const auto manifest = native_fixture::read_json(path);
  DP_EXPECT_EQ(manifest.at("started_at").get<std::string>(),
               request.started_at);
  DP_EXPECT_TRUE(manifest.at("ended_at").get<std::string>() >=
                 request.started_at);
}

#if DISKANN_SEARCH_LINUX_NATIVE_TEST
struct PayloadFact {
  bool exists;
  std::uintmax_t byte_length;
  std::string sha256;
};

PayloadFact payload_fact(const std::filesystem::path& path) {
  return {std::filesystem::is_regular_file(path),
          std::filesystem::file_size(path), dp::sha256_file(path)};
}

int run_feature_runner(const std::vector<std::string>& arguments) {
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 2);
  argv.push_back(const_cast<char*>(DISKANN_SEARCH_TEST_RUNNER));
  for (const auto& argument : arguments) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);
  const auto child = fork();
  if (child == 0) {
    execv(DISKANN_SEARCH_TEST_RUNNER, argv.data());
    _exit(127);
  }
  if (child < 0) return -1;
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
  return WEXITSTATUS(status);
}

Json artifact_reference(const dp::ArtifactRef& value) {
  return {{"metadata_path", value.metadata_path.generic_string()},
          {"artifact_type", dp::to_string(value.artifact_type)},
          {"format_version", value.format_version},
          {"aggregate_checksum", value.aggregate_checksum}};
}

dp::ArtifactRef published_reference(const dp::WorkspaceRef& workspace,
                                    const std::filesystem::path& metadata) {
  const auto document = native_fixture::read_json(workspace.normal_root / metadata);
  return {metadata,
          dp::artifact_type_from_string(
              document.at("artifact_type").get<std::string>()),
          document.at("format_version").get<std::string>(),
          document.at("aggregate_checksum").get<std::string>()};
}

void real_post_graph_closure() {
  constexpr std::uint32_t kPoints = 2048;
  constexpr std::uint32_t kDimension = 32;
  constexpr std::uint32_t kDegree = 16;
  std::unique_ptr<native_fixture::TemporaryDirectory> temporary;
  std::filesystem::path retained_root;
  if (const auto* configured = std::getenv("DISKANN_SEARCH_REAL_FIXTURE_ROOT");
      configured != nullptr && configured[0] != '\0') {
    retained_root = std::filesystem::path(configured);
    if (std::filesystem::exists(retained_root)) {
      throw std::runtime_error("retained real-search fixture already exists");
    }
    std::filesystem::create_directories(retained_root);
  } else {
    temporary = std::make_unique<native_fixture::TemporaryDirectory>(
        "search-materialization-real");
    retained_root = temporary->path;
  }
  const auto workspace = dp::create_workspace(retained_root / "workspace");
  native_fixture::NativeDatasetFixture dataset(workspace, kPoints, kDimension);
  std::vector<dp::GlobalNodeId> map(kPoints);
  std::iota(map.begin(), map.end(), 0U);
  const auto partition =
      native_fixture::materialize_partition(workspace, dataset, {map});
  std::vector<std::vector<std::uint32_t>> adjacency(kPoints);
  for (std::uint32_t node = 0; node < kPoints; ++node) {
    for (std::uint32_t edge = 1; edge <= kDegree; ++edge) {
      adjacency[node].push_back((node + edge) % kPoints);
    }
  }
  const auto local = native_fixture::materialize_local(
      workspace, dataset, partition, 0, map, adjacency);
  const auto global = native_fixture::materialize_global(
      workspace, dataset, partition, {local}, adjacency, {0});
  const auto graph_before = dp::sha256_file(
      dp::test::metadata_path(workspace, global));

  const auto request_root = retained_root / "requests";
  std::filesystem::create_directories(request_root);
  const auto pq_request_path = request_root / "pq.json";
  const auto layout_request_path = request_root / "layout.json";
  const Json budget{{"memory_budget_bytes", 256ULL * 1024 * 1024},
                    {"thread_budget", 1}};
  native_fixture::write_text(
      pq_request_path,
      Json{{"workspace", {{"root", workspace.root.string()}}},
           {"run_id", "search-pq-native-real"},
           {"operation", "pq"},
           {"dataset", artifact_reference(dataset.reference)},
           {"parameters", Json::object()},
           {"resource_budget", budget}}
          .dump());
  native_fixture::write_text(
      layout_request_path,
      Json{{"workspace", {{"root", workspace.root.string()}}},
           {"run_id", "search-layout-native-real"},
           {"operation", "disk_layout"},
           {"dataset", artifact_reference(dataset.reference)},
           {"global_graph", artifact_reference(global)},
           {"parameters", {{"centroid_policy", "medoid_vector_fallback"}}},
           {"resource_budget", budget}}
          .dump());
  DP_EXPECT_EQ(run_feature_runner(
                   {"materialize", "--request", pq_request_path.string()}),
               0);
  DP_EXPECT_EQ(run_feature_runner(
                   {"materialize", "--request", layout_request_path.string()}),
               0);
  const auto pq = published_reference(
      workspace,
      "search-pq-native-real/cpp_main_post_graph_pq_v1/artifact.json");
  const auto layout = published_reference(
      workspace,
      "search-layout-native-real/cpp_main_post_graph_disk_layout_v1/artifact.json");
  DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(pq).valid());
  DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(layout).valid());
  DP_EXPECT_EQ(graph_before,
               dp::sha256_file(dp::test::metadata_path(workspace, global)));

  const auto pq_metadata = native_fixture::read_json(
      dp::test::metadata_path(workspace, pq));
  const auto layout_metadata = native_fixture::read_json(
      dp::test::metadata_path(workspace, layout));
  const auto pq_root =
      dp::test::metadata_path(workspace, pq).parent_path();
  const auto layout_root =
      dp::test::metadata_path(workspace, layout).parent_path();
  const auto staged = dp::create_temporary_storage(workspace, "native-load-probe");
  const auto disk = staged.stage_root / "index_disk.index";
  const auto pivots = staged.stage_root / "index_pq_pivots.bin";
  const auto codes = staged.stage_root / "index_pq_compressed.bin";
  DP_EXPECT_EQ(ds::stage_native_payload_alias(layout_root / "disk.index", disk),
               ds::NativeAliasMethod::kHardLink);
  DP_EXPECT_EQ(ds::stage_native_payload_alias(
                   layout_root / "medoids.bin",
                   std::filesystem::path(disk.string() + "_medoids.bin")),
               ds::NativeAliasMethod::kHardLink);
  DP_EXPECT_EQ(ds::stage_native_payload_alias(pq_root / "pivots.bin", pivots),
               ds::NativeAliasMethod::kHardLink);
  DP_EXPECT_EQ(ds::stage_native_payload_alias(pq_root / "compressed.bin", codes),
               ds::NativeAliasMethod::kHardLink);
  std::shared_ptr<AlignedFileReader> reader(new LinuxAlignedFileReader());
  diskann::PQFlashIndex<float> index(reader, diskann::Metric::L2);
  DP_EXPECT_EQ(index.load_from_separate_paths(
                   1, disk.c_str(), pivots.c_str(), codes.c_str()),
               0);
  DP_EXPECT_EQ(index.get_num_points(), static_cast<std::size_t>(kPoints));
  DP_EXPECT_EQ(index.get_data_dim(), static_cast<std::size_t>(kDimension));
  DP_EXPECT_EQ(pq_metadata.at("source_artifacts").size(), std::size_t{1});
  DP_EXPECT_EQ(layout_metadata.at("source_artifacts").size(), std::size_t{2});
  const std::vector<std::filesystem::path> source_payloads{
      layout_root / "disk.index", layout_root / "medoids.bin",
      pq_root / "pivots.bin", pq_root / "compressed.bin"};
  std::vector<PayloadFact> before;
  for (const auto& path : source_payloads) before.push_back(payload_fact(path));
  DP_EXPECT_EQ(
      run_feature_runner(
          {"load-probe", "--workspace", workspace.root.string(),
           "--pq-artifact", pq.metadata_path.string(),
           "--disk-layout-artifact", layout.metadata_path.string()}),
      0);
  for (std::size_t index = 0; index < source_payloads.size(); ++index) {
    const auto after = payload_fact(source_payloads[index]);
    DP_EXPECT_EQ(after.exists, before[index].exists);
    DP_EXPECT_EQ(after.byte_length, before[index].byte_length);
    DP_EXPECT_EQ(after.sha256, before[index].sha256);
  }
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.temporary_root /
      ("load-probe-" + pq.aggregate_checksum.substr(0, 12) + "-" +
       layout.aggregate_checksum.substr(0, 12) + "-native-index")));
  if (std::getenv("DISKANN_SEARCH_REAL_FIXTURE_ROOT") != nullptr) {
    native_fixture::write_text(
        retained_root / "fixture.json",
        Json{{"workspace_root", workspace.root.string()},
             {"dataset", artifact_reference(dataset.reference)},
             {"global_graph", artifact_reference(global)},
             {"pq_artifact", artifact_reference(pq)},
             {"disk_layout_artifact", artifact_reference(layout)}}
            .dump());
  }
}

void runner_provenance_closure(const Inputs& inputs) {
  const auto request_path = inputs.temporary.path / "runner-layout-request.json";
  native_fixture::write_text(
      request_path,
      Json{{"workspace", {{"root", inputs.workspace.root.string()}}},
           {"run_id", "search-layout-runner-provenance"},
           {"operation", "disk_layout"},
           {"dataset", artifact_reference(inputs.dataset.reference)},
           {"global_graph", artifact_reference(inputs.global)},
           {"parameters", {{"centroid_policy", "medoid_vector_fallback"}}},
           {"resource_budget",
            {{"memory_budget_bytes", 64ULL * 1024 * 1024},
             {"thread_budget", 1}}}}
          .dump());
  DP_EXPECT_EQ(run_feature_runner(
                   {"materialize", "--request", request_path.string()}),
               0);
  const auto manifest = native_fixture::read_json(
      inputs.workspace.manifest_root / "search-layout-runner-provenance.json");
  const auto repository =
      manifest.at("source").at("repository").get<std::string>();
  DP_EXPECT_TRUE(repository.rfind("https://", 0) == 0);
  DP_EXPECT_TRUE(repository !=
                 "https://github.com/microsoft/DiskANN.git");
  DP_EXPECT_EQ(manifest.at("source").at("branch").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_PROJECT_BRANCH));
  DP_EXPECT_EQ(manifest.at("source").at("commit").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_PROJECT_COMMIT));
  DP_EXPECT_EQ(manifest.at("source").at("dirty_worktree").get<bool>(),
               DISKANN_SEARCH_TEST_PROJECT_DIRTY != 0);
  DP_EXPECT_EQ(
      manifest.at("parameters").at("diskann_source").at("commit").get<std::string>(),
      std::string("78256bbab4685e1774e78d331e081a153be26823"));
  DP_EXPECT_EQ(
      manifest.at("parameters").at("runner_binary").at("sha256").get<std::string>(),
      dp::sha256_file(DISKANN_SEARCH_TEST_RUNNER));
  const auto& toolchain = manifest.at("toolchain");
  DP_EXPECT_EQ(toolchain.at("os").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_SYSTEM_NAME));
  DP_EXPECT_EQ(toolchain.at("compiler_path").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_CXX_COMPILER));
  DP_EXPECT_EQ(toolchain.at("compiler_version").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_CXX_COMPILER_VERSION));
  DP_EXPECT_EQ(toolchain.at("cmake_version").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_CMAKE_VERSION));
  DP_EXPECT_EQ(toolchain.at("generator").get<std::string>(),
               std::string(DISKANN_SEARCH_TEST_CMAKE_GENERATOR));
  DP_EXPECT_EQ(toolchain.at("build_type").get<std::string>(),
               std::string("Release"));
  const auto definitions = toolchain.at("cmake_definitions").dump();
  DP_EXPECT_TRUE(definitions.find("CMAKE_BUILD_TYPE=Release") !=
                 std::string::npos);
  DP_EXPECT_TRUE(definitions.find("DISKANN_SEARCH_EVALUATION=ON") !=
                 std::string::npos);
  const auto compile_commands = toolchain.at("compile_commands").dump();
  DP_EXPECT_TRUE(compile_commands.find("-O3") != std::string::npos);
  DP_EXPECT_TRUE(compile_commands.find("-DNDEBUG") != std::string::npos);
  DP_EXPECT_TRUE(compile_commands.find("-mavx2") != std::string::npos);
  DP_EXPECT_TRUE(compile_commands.find("-fopenmp") != std::string::npos);
  DP_EXPECT_TRUE(compile_commands.find("-Wall") != std::string::npos);
  DP_EXPECT_TRUE(manifest.at("started_at").get<std::string>() <=
                 manifest.at("ended_at").get<std::string>());
}
#endif

}  // namespace

int main() {
  DP_EXPECT_EQ(ds::fixed_pq_descriptor().implementation_id,
               std::string("cpp_main_post_graph_pq_v1"));
  DP_EXPECT_EQ(ds::fixed_disk_layout_descriptor().implementation_id,
               std::string("cpp_main_post_graph_disk_layout_v1"));
  DP_EXPECT_EQ(ds::fixed_pq_descriptor().provides.artifact_type,
               dp::ArtifactType::kPQ);
  DP_EXPECT_EQ(ds::fixed_disk_layout_descriptor().provides.artifact_type,
               dp::ArtifactType::kDiskLayout);

  Inputs inputs;
  const auto graph_before = dp::sha256_file(
      dp::test::metadata_path(inputs.workspace, inputs.global));
  int pq_calls = 0;
  int layout_calls = 0;
  std::string pq_call_time;
  std::string layout_call_time;
  ds::NativePostGraphCalls calls;
  calls.generate_pq = [&](const auto&, const auto& pivots,
                          const auto& codes, const auto& policy) {
    ++pq_calls;
    pq_call_time = now_utc();
    DP_EXPECT_EQ(policy.pq_chunks, std::uint32_t{32});
    DP_EXPECT_TRUE(!policy.opq);
    dp::test::overwrite_file(pivots,
                             dp::search::test::pq_pivots_bytes(32, 32));
    dp::test::overwrite_file(codes,
                             dp::search::test::pq_codes_bytes(6, 32));
  };
  calls.create_disk_layout = [&](const auto&, const auto&, const auto& output,
                                 const auto& policy) {
    ++layout_calls;
    layout_call_time = now_utc();
    DP_EXPECT_EQ(policy.pq_disk_bytes, std::uint32_t{0});
    DP_EXPECT_TRUE(!policy.reorder);
    dp::test::overwrite_file(
        output, dp::search::test::disk_layout_bytes(6, 32, 3));
  };
  Token token;
  ds::PostGraphMaterializer materializer(inputs.workspace, calls, &token);

  auto pq_request = request(inputs, "search-pq-small");
  pq_request.global_graph.reset();
  const auto pq = materializer.materialize_pq(pq_request);
  DP_EXPECT_EQ(pq.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(pq.artifact.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(pq.build_manifest_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(pq.validation_evidence_path));
  DP_EXPECT_EQ(pq_calls, 1);
  DP_EXPECT_EQ(layout_calls, 0);
  expect_manifest_provenance(pq.build_manifest_path, pq_request,
                             pq_call_time);

  const auto layout_request = request(inputs, "search-layout-small");
  const auto layout = materializer.materialize_disk_layout(layout_request);
  DP_EXPECT_EQ(layout.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(layout.artifact.has_value());
  DP_EXPECT_EQ(pq_calls, 1);
  DP_EXPECT_EQ(layout_calls, 1);
  expect_manifest_provenance(layout.build_manifest_path, layout_request,
                             layout_call_time);
  DP_EXPECT_EQ(graph_before,
               dp::sha256_file(
                   dp::test::metadata_path(inputs.workspace, inputs.global)));

  dp::Validator validator(inputs.workspace);
  const auto pq_valid = validator.validate_with_lineage(*pq.artifact);
  const auto layout_valid = validator.validate_with_lineage(*layout.artifact);
  DP_EXPECT_TRUE(pq_valid.valid());
  DP_EXPECT_TRUE(layout_valid.valid());

  auto declared = request(inputs, "search-layout-declared-centroids");
  declared.centroid_policy = "declared_centroids";
  const auto rejected = materializer.materialize_disk_layout(declared);
  DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(layout_calls, 1);
  DP_EXPECT_TRUE(!rejected.artifact.has_value());
  expect_terminal_times(rejected.build_manifest_path, declared);

  ds::NativePostGraphCalls corrupt_calls = calls;
  corrupt_calls.generate_pq = [](const auto&, const auto& pivots,
                                 const auto& codes, const auto&) {
    dp::test::overwrite_file(pivots, "invalid");
    dp::test::overwrite_file(codes, "invalid");
  };
  ds::PostGraphMaterializer corrupt(inputs.workspace, corrupt_calls, &token);
  auto invalid_request = request(inputs, "search-pq-invalid");
  invalid_request.global_graph.reset();
  const auto invalid = corrupt.materialize_pq(invalid_request);
  DP_EXPECT_EQ(invalid.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(!invalid.artifact.has_value());
  DP_EXPECT_TRUE(invalid.quarantine_id.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(invalid.build_manifest_path));
  expect_terminal_times(invalid.build_manifest_path, invalid_request);

  token.cancelled = true;
  auto cancelled_request = request(inputs, "search-pq-cancelled");
  cancelled_request.global_graph.reset();
  const auto cancelled = materializer.materialize_pq(cancelled_request);
  DP_EXPECT_EQ(cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled.artifact.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      cancelled.build_manifest_path));
  expect_terminal_times(cancelled.build_manifest_path, cancelled_request);

#if DISKANN_SEARCH_LINUX_NATIVE_TEST
  runner_provenance_closure(inputs);
  real_post_graph_closure();
#endif

  return dp::test::finish();
}
