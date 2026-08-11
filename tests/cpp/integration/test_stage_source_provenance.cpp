#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace dp = diskann_pipeline;
using Json = nlohmann::json;

namespace {

const dp::SourceIdentity kLegacySource{
    "https://example.invalid/legacy.git", "master",
    "1111111111111111111111111111111111111111", false, std::nullopt};
const dp::SourceIdentity kGpAnnSource{
    "https://github.com/larsgottesbueren/gp-ann.git", "main",
    "df5efb4a6b303011c795666ed267f29d994b558c", false, std::nullopt};
const dp::SourceIdentity kDiskAnnSource{
    "https://github.com/microsoft/DiskANN.git", "cpp_main",
    "78256bbab4685e1774e78d331e081a153be26823", false, std::nullopt};

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  return bytes.str();
}

bool same_source(const dp::SourceIdentity& lhs,
                 const dp::SourceIdentity& rhs) {
  return lhs.repository == rhs.repository && lhs.branch == rhs.branch &&
         lhs.commit == rhs.commit &&
         lhs.dirty_worktree == rhs.dirty_worktree &&
         lhs.dirty_source_checksum == rhs.dirty_source_checksum;
}

dp::BuildManifest read_manifest(const std::filesystem::path& path) {
  return dp::parse_build_manifest(read_text(path));
}

Json parameters(const dp::BuildManifest& manifest) {
  return Json::parse(manifest.parameters_json);
}

dp::PipelineExecutionRequest request_for(const std::string& run_id,
                                         const dp::ArtifactRef& dataset) {
  auto request = dp::test::canonical_execution_request(run_id, dataset);
  request.source = kLegacySource;
  request.source_config_json =
      R"({"yaml_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","normalized_json_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
  return request;
}

void expect_shared_source_config(const dp::BuildManifest& manifest) {
  const auto config = parameters(manifest).at("source_config");
  DP_EXPECT_EQ(config.size(), std::size_t{2});
  DP_EXPECT_EQ(config.at("yaml_sha256"),
               std::string(64, 'a'));
  DP_EXPECT_EQ(config.at("normalized_json_sha256"),
               std::string(64, 'b'));
}

enum class SubordinateMode {
  kValid,
  kMalformed,
  kMismatchedRun,
  kRunning,
  kPathEscape,
  kOuterFailed,
};

class ManifestPartition final : public dp::PartitionStrategy {
 public:
  ManifestPartition(std::string run_id, SubordinateMode mode)
      : run_id_(std::move(run_id)), mode_(mode) {}

  dp::StageResult run(const dp::ArtifactRef& dataset,
                      const std::filesystem::path& parameter_path,
                      dp::StageContext& context) override {
    auto result = dp::test::make_fake_partition()->run(
        dataset, parameter_path, context);
    if (mode_ == SubordinateMode::kPathEscape) {
      result.build_manifest_path = "../../outside-build-manifest.json";
      return result;
    }

    if (mode_ == SubordinateMode::kOuterFailed) {
      result.status = dp::StageStatus::kFailed;
      result.output_candidate.reset();
    }

    const auto path = dp::resolve_confined_path(
        context.workspace.report_root,
        std::filesystem::path("stage-owned") / run_id_ / "partition.json");
    if (mode_ == SubordinateMode::kMalformed) {
      dp::write_immutable_text(path, R"({"not":"a manifest"})");
      result.build_manifest_path = path;
      return result;
    }

    const dp::DatasetIdentity dataset_identity{
        "fake-six-node-v1",
        "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b",
        6,
        2,
        "float32",
        "l2"};
    dp::BuildManifest manifest{
        mode_ == SubordinateMode::kMismatchedRun ? run_id_ + "-wrong" : run_id_,
        "partition",
        {"subordinate_partition_process", "1.0.0"},
        kGpAnnSource,
        {"linux", "/usr/bin/c++", "11.4.0", "3.22.1",
         "Unix Makefiles", "Release", {"GPANN=ON"}, {}, {}},
        dataset_identity,
        R"({"argv":["Partition"],"exit_status":0})",
        {context.memory_budget_bytes, context.thread_budget},
        {dataset},
        result.output_candidate ? std::vector<dp::ArtifactRef>{*result.output_candidate}
                                : std::vector<dp::ArtifactRef>{},
        mode_ == SubordinateMode::kRunning
            ? dp::StageStatus::kRunning
            : (mode_ == SubordinateMode::kOuterFailed
                   ? dp::StageStatus::kFailed
                   : dp::StageStatus::kSucceeded),
        "2026-08-10T00:00:00Z",
        "2026-08-10T00:00:01Z",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "{}"};
    if (mode_ == SubordinateMode::kOuterFailed) {
      manifest.failure = dp::FailureInfo{dp::ErrorCategory::kInternal,
                                         "subprocess failed", 17};
    }
    dp::write_immutable_text(path, dp::serialize_build_manifest(manifest));
    result.build_manifest_path = path;
    return result;
  }

 private:
  std::string run_id_;
  SubordinateMode mode_;
};

dp::PipelineExecutionResult execute_with_partition(
    dp::test::FakeStageEnvironment& environment, const dp::ArtifactRef& dataset,
    const std::string& run_id, SubordinateMode mode) {
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(
      dp::test::fake_partition_descriptor(),
      [run_id, mode]() {
        return std::make_unique<ManifestPartition>(run_id, mode);
      });
  registry.register_local_build(dp::test::fake_local_graph_descriptor(),
                                dp::test::make_fake_local_graph);
  registry.register_merge(dp::test::fake_merge_descriptor(),
                          dp::test::make_fake_merge);
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto context = environment.make_context(run_id + "-executor");
  auto request = request_for(run_id, dataset);
  request.stage_sources =
      dp::StageSourceIdentities{kGpAnnSource, kDiskAnnSource, kDiskAnnSource};
  return executor.execute(request, context);
}

void test_legacy_fallback_and_empty_transport() {
  dp::test::FakeStageEnvironment environment("stage-source-legacy");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto context = environment.make_context("legacy-executor");
  const auto result = executor.execute(request_for("legacy-run", dataset), context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  for (const auto& path : result.build_manifest_paths) {
    const auto manifest = read_manifest(path);
    DP_EXPECT_TRUE(same_source(manifest.source, kLegacySource));
    DP_EXPECT_TRUE(!parameters(manifest).contains("stage_execution_manifest"));
    expect_shared_source_config(manifest);
  }
}

void test_present_stage_sources_for_new_and_reused_partition() {
  dp::test::FakeStageEnvironment environment("stage-source-split");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  dp::PipelineExecutor executor(registry, environment.workspace());

  auto request = request_for("split-run", dataset);
  request.stage_sources =
      dp::StageSourceIdentities{kGpAnnSource, kDiskAnnSource, kDiskAnnSource};
  auto context = environment.make_context("split-executor");
  const auto result = executor.execute(request, context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.partition_artifact.has_value());
  for (const auto& path : result.build_manifest_paths) {
    const auto manifest = read_manifest(path);
    if (manifest.stage == "partition") {
      DP_EXPECT_TRUE(same_source(manifest.source, kGpAnnSource));
    } else {
      DP_EXPECT_TRUE(same_source(manifest.source, kDiskAnnSource));
    }
    expect_shared_source_config(manifest);
  }

  auto reused_request = request_for("split-reused-run", dataset);
  reused_request.stage_sources = request.stage_sources;
  reused_request.reused_partition = result.partition_artifact;
  auto reused_context = environment.make_context("split-reused-executor");
  const auto reused = executor.execute(reused_request, reused_context);
  DP_EXPECT_EQ(reused.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(reused.invocations.partition, std::size_t{0});
  for (const auto& path : reused.build_manifest_paths) {
    const auto manifest = read_manifest(path);
    if (manifest.stage == "partition") {
      DP_EXPECT_TRUE(same_source(manifest.source, kGpAnnSource));
    } else {
      DP_EXPECT_TRUE(same_source(manifest.source, kDiskAnnSource));
    }
  }
}

void test_valid_subordinate_manifest_is_confined_and_stage_local() {
  dp::test::FakeStageEnvironment environment("stage-source-subordinate");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  const auto result = execute_with_partition(
      environment, dataset, "subordinate-run", SubordinateMode::kValid);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{4});

  std::size_t references = 0;
  for (const auto& path : result.build_manifest_paths) {
    const auto manifest = read_manifest(path);
    const auto value = parameters(manifest);
    expect_shared_source_config(manifest);
    if (manifest.stage == "partition") {
      DP_EXPECT_TRUE(value.contains("stage_execution_manifest"));
      const auto reference = value.at("stage_execution_manifest");
      const auto relative =
          std::filesystem::path(reference.at("path").get<std::string>());
      const auto persisted = dp::resolve_confined_path(
          environment.workspace().root, relative);
      DP_EXPECT_TRUE(std::filesystem::exists(persisted));
      DP_EXPECT_EQ(dp::sha256_file(persisted),
                   reference.at("sha256").get<std::string>());
      const auto subordinate = read_manifest(persisted);
      DP_EXPECT_EQ(subordinate.run_id, std::string("subordinate-run"));
      DP_EXPECT_EQ(subordinate.stage, std::string("partition"));
      DP_EXPECT_TRUE(same_source(subordinate.source, kGpAnnSource));
      ++references;
    } else {
      DP_EXPECT_TRUE(!value.contains("stage_execution_manifest"));
    }
  }
  DP_EXPECT_EQ(references, std::size_t{1});
}

void test_invalid_subordinate_manifests_fail_closed() {
  const std::vector<SubordinateMode> modes{
      SubordinateMode::kMalformed, SubordinateMode::kMismatchedRun,
      SubordinateMode::kRunning, SubordinateMode::kPathEscape};
  std::size_t index = 0;
  for (const auto mode : modes) {
    const auto identity = "invalid-subordinate-" + std::to_string(index++);
    dp::test::FakeStageEnvironment environment(identity);
    const auto dataset =
        dp::test::materialize_fake_dataset(environment.workspace());
    const auto result = execute_with_partition(environment, dataset, identity, mode);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  }
}

void test_failed_outer_stage_retains_valid_subordinate_reference() {
  dp::test::FakeStageEnvironment environment("failed-outer-subordinate");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  const auto result = execute_with_partition(
      environment, dataset, "failed-outer-run", SubordinateMode::kOuterFailed);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{1});
  const auto parent = read_manifest(result.build_manifest_paths.front());
  const auto reference = parameters(parent).at("stage_execution_manifest");
  const auto subordinate_path = dp::resolve_confined_path(
      environment.workspace().root,
      reference.at("path").get<std::string>());
  DP_EXPECT_EQ(dp::sha256_file(subordinate_path),
               reference.at("sha256").get<std::string>());
  const auto subordinate = read_manifest(subordinate_path);
  DP_EXPECT_EQ(subordinate.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(subordinate.failure.has_value());
  DP_EXPECT_EQ(subordinate.failure->exit_code, std::optional<int>{17});
}

}  // namespace

int main() {
  test_legacy_fallback_and_empty_transport();
  test_present_stage_sources_for_new_and_reused_partition();
  test_valid_subordinate_manifest_is_confined_and_stage_local();
  test_invalid_subordinate_manifests_fail_closed();
  test_failed_outer_stage_retains_valid_subordinate_reference();
  return dp::test::finish();
}
