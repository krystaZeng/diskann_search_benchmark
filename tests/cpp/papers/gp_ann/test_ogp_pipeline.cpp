#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "fakes/fake_pipeline.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;
namespace ogp = diskann_pipeline::gp_ann_ogp;
using Json = nlohmann::json;

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

dp::PipelineExecutionRequest request(
    const std::string& run_id, const dp::ArtifactRef& dataset,
    const ogp::GpAnnSourceBuildEvidence& gpann,
    const native::SourceBuildEvidence& diskann) {
  const auto build = native::make_build_manifest(
      diskann, run_id + "-build", "2026-08-10T00:00:00Z",
      "2026-08-10T00:00:01Z");
  dp::PipelineExecutionRequest value;
  value.run_id = run_id;
  value.dataset = dataset;
  value.partition_implementation_id = ogp::kImplementationId;
  value.local_implementation_id = native::kB0ImplementationId;
  value.merge_implementation_id = native::kM0ImplementationId;
  value.partition_parameters_json =
      R"({"requested_num_clusters":4,"requested_overlap":0.2,"partitioning_mode":"default"})";
  value.local_parameters_json =
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})";
  value.merge_parameters_json = R"({"R":16,"use_filters":false})";
  value.source_config_json =
      R"({"yaml_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","normalized_json_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"})";
  value.source = diskann.source;
  value.stage_sources =
      dp::StageSourceIdentities{gpann.source, diskann.source, diskann.source};
  value.toolchain = build.toolchain;
  value.started_at = "2026-08-10T00:00:00Z";
  value.ended_at = "2026-08-10T00:00:01Z";
  return value;
}

dp::ImplementationRegistry composed_registry(
    const ogp::GpAnnSourceBuildEvidence& gpann,
    dp::PartitionFactory partition_factory = {}) {
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  native::register_cpp_main_stage_bindings(registry);
  if (!partition_factory) {
    partition_factory = [gpann] { return ogp::make_ogp_partition(gpann); };
  }
  registry.register_partition(ogp::descriptor(), std::move(partition_factory));
  registry.seal();
  return registry;
}

class CorruptPartition final : public dp::PartitionStrategy {
 public:
  dp::StageResult run(const dp::ArtifactRef& dataset,
                      const std::filesystem::path&,
                      dp::StageContext& context) override {
    native::CppMainVectorAccessor vectors(context.workspace, dataset);
    std::vector<std::vector<dp::GlobalNodeId>> clusters{{}, {}};
    for (dp::GlobalNodeId id = 0;
         id < vectors.dataset_identity().global_node_count; ++id) {
      clusters[id % 2].push_back(id);
    }
    auto candidate = ogp::materialize_partition_candidate(
        context.temporary_storage.stage_root, dataset,
        vectors.dataset_identity(), clusters);
    const auto map = context.temporary_storage.stage_root / "candidate" /
                     "partition-0-map.bin";
    std::fstream output(map, std::ios::binary | std::ios::in | std::ios::out);
    output.seekp(-1, std::ios::end);
    const char byte = '\x5a';
    output.write(&byte, 1);
    return {dp::StageStatus::kSucceeded, candidate, {}};
  }
};

void test_registry_coexistence_and_baseline_isolation(
    const ogp::GpAnnSourceBuildEvidence& gpann) {
  auto feature = composed_registry(gpann);
  const auto descriptors = feature.enumerate();
  DP_EXPECT_TRUE(std::any_of(descriptors.begin(), descriptors.end(),
                             [](const auto& item) {
                               return item.implementation_id ==
                                      native::kP0ImplementationId;
                             }));
  DP_EXPECT_TRUE(std::any_of(descriptors.begin(), descriptors.end(),
                             [](const auto& item) {
                               return item.implementation_id ==
                                      ogp::kImplementationId;
                             }));

  dp::ImplementationRegistry baseline(dp::RegistryAdmissionPolicy::kProduction);
  native::register_cpp_main_stage_bindings(baseline);
  baseline.seal();
  DP_EXPECT_THROW(baseline.resolve_partition_factory(
      ogp::kImplementationId, ogp::kImplementationVersion));
  DP_EXPECT_EQ(baseline.size(), std::size_t{3});
}

void test_invalid_partition_stops_b0_and_m0(
    const ogp::GpAnnSourceBuildEvidence& gpann,
    const native::SourceBuildEvidence& diskann) {
  fixture::TemporaryDirectory temporary("ogp-composed-invalid");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 32, 4);
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "invalid");
  environment.context.vector_accessor = &vectors;
  auto registry = composed_registry(
      gpann, [] { return std::make_unique<CorruptPartition>(); });
  const auto result = dp::PipelineExecutor(registry, workspace).execute(
      request("ogp-composed-invalid", dataset.reference, gpann, diskann),
      environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!result.partition_artifact.has_value());
}

void test_legacy_source_fallback() {
  dp::test::FakeStageEnvironment environment("ogp-legacy-fallback");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  auto legacy = dp::test::canonical_execution_request(
      "ogp-legacy-fallback", dataset);
  legacy.stage_sources.reset();
  auto context = environment.make_context("executor");
  const auto result = dp::PipelineExecutor(registry, environment.workspace())
                          .execute(legacy, context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  for (const auto& path : result.build_manifest_paths) {
    const auto manifest = dp::parse_build_manifest(read_text(path));
    DP_EXPECT_EQ(manifest.source.repository, legacy.source.repository);
    DP_EXPECT_EQ(manifest.source.commit, legacy.source.commit);
  }
}

void test_small_real_composed_pipeline(
    const ogp::GpAnnSourceBuildEvidence& gpann,
    const native::SourceBuildEvidence& diskann) {
  fixture::TemporaryDirectory temporary("ogp-composed-real");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 96, 4);
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::TestContext environment(workspace, "composed");
  environment.context.vector_accessor = &vectors;
  auto registry = composed_registry(gpann);
  const auto result = dp::PipelineExecutor(registry, workspace).execute(
      request("ogp-composed-real", dataset.reference, gpann, diskann),
      environment.context);
  if (result.failure) {
    std::cerr << "OGP composed failure: " << result.failure->reason << '\n';
  }
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_TRUE(result.invocations.local_build >= std::size_t{2});
  DP_EXPECT_EQ(result.invocations.local_build,
               result.local_partition_ids.size());
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(result.partition_artifact.has_value());
  DP_EXPECT_EQ(result.local_artifacts.size(),
               result.local_partition_ids.size());
  DP_EXPECT_TRUE(result.artifact.has_value());
  DP_EXPECT_TRUE(result.evaluation_report_path.has_value());
  if (result.status != dp::StageStatus::kSucceeded) return;

  std::size_t partition_manifests = 0;
  std::size_t local_manifests = 0;
  std::size_t merge_manifests = 0;
  for (const auto& path : result.build_manifest_paths) {
    const auto manifest = dp::parse_build_manifest(read_text(path));
    const auto parameters = Json::parse(manifest.parameters_json);
    DP_EXPECT_EQ(parameters.at("source_config"),
                 Json::parse(request("projection", dataset.reference, gpann,
                                     diskann)
                                 .source_config_json));
    if (manifest.stage == "partition") {
      ++partition_manifests;
      DP_EXPECT_EQ(manifest.source.repository, std::string(ogp::kRepository));
      DP_EXPECT_EQ(manifest.source.commit, std::string(ogp::kCommit));
      DP_EXPECT_TRUE(parameters.contains("stage_execution_manifest"));
      const auto reference = parameters.at("stage_execution_manifest");
      const auto subordinate_path = dp::resolve_confined_path(
          workspace.root, reference.at("path").get<std::string>());
      DP_EXPECT_EQ(dp::sha256_file(subordinate_path),
                   reference.at("sha256").get<std::string>());
      const auto subordinate =
          dp::parse_build_manifest(read_text(subordinate_path));
      DP_EXPECT_EQ(subordinate.source.repository,
                   std::string(ogp::kRepository));
      DP_EXPECT_EQ(subordinate.source.commit, std::string(ogp::kCommit));
      DP_EXPECT_EQ(subordinate.toolchain.compiler_version,
                   gpann.toolchain.compiler_version);
      const auto evidence = Json::parse(subordinate.parameters_json);
      DP_EXPECT_EQ(evidence.at("admitted_executable").at("sha256"),
                   gpann.partition_executable_sha256);
      DP_EXPECT_TRUE(evidence.contains("argv"));
      DP_EXPECT_TRUE(evidence.contains("stdout"));
      DP_EXPECT_TRUE(evidence.contains("stderr"));
      DP_EXPECT_TRUE(evidence.contains("exit_status"));
      DP_EXPECT_TRUE(evidence.contains("expected_output"));
    } else if (manifest.stage == "local_build") {
      ++local_manifests;
      DP_EXPECT_EQ(manifest.source.repository,
                   std::string(native::kPinnedRepository));
      DP_EXPECT_TRUE(!parameters.contains("stage_execution_manifest"));
    } else if (manifest.stage == "merge") {
      ++merge_manifests;
      DP_EXPECT_EQ(manifest.source.repository,
                   std::string(native::kPinnedRepository));
      DP_EXPECT_TRUE(!parameters.contains("stage_execution_manifest"));
    }
  }
  DP_EXPECT_EQ(partition_manifests, std::size_t{1});
  DP_EXPECT_EQ(local_manifests, result.local_artifacts.size());
  DP_EXPECT_EQ(merge_manifests, std::size_t{1});

  std::ifstream report_input(*result.evaluation_report_path);
  const auto report = dp::parse_evaluation_report(
      std::string(std::istreambuf_iterator<char>(report_input),
                  std::istreambuf_iterator<char>()));
  DP_EXPECT_EQ(report.evaluator.implementation_id,
               "cpp_main_native_graph_integrity_evaluator");
  DP_EXPECT_EQ(report.evaluator.version, "1.0.0");
  DP_EXPECT_TRUE(report.findings.passed);
}

}  // namespace

int main() {
  const auto gpann =
      ogp::admit_source_and_build(ogp::configured_source_build_admission());
  const auto diskann = native::admit_source_and_build(
      native::configured_source_build_admission());
  test_registry_coexistence_and_baseline_isolation(gpann);
  test_invalid_partition_stops_b0_and_m0(gpann, diskann);
  test_legacy_source_fallback();
  test_small_real_composed_pipeline(gpann, diskann);
  return dp::test::finish();
}
