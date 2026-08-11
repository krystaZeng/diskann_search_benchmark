#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "fakes/fake_pipeline.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;
namespace ogp = diskann_pipeline::gp_ann_ogp;
using Json = nlohmann::json;

namespace {

void write_small_fbin(const std::filesystem::path& path,
                      std::uint32_t count, std::uint32_t dimension) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&dimension), sizeof(dimension));
  for (std::uint32_t point = 0; point < count; ++point) {
    for (std::uint32_t coordinate = 0; coordinate < dimension; ++coordinate) {
      const float value = static_cast<float>(
          (point % 8U) * (coordinate + 1U) + (point / 8U));
      output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
  }
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

class CorruptOgpPartition final : public dp::PartitionStrategy {
 public:
  dp::StageResult run(const dp::ArtifactRef& dataset,
                      const std::filesystem::path&,
                      dp::StageContext& context) override {
    dp::cpp_main::CppMainVectorAccessor accessor(context.workspace, dataset);
    std::vector<std::vector<dp::GlobalNodeId>> clusters{{}, {}};
    for (dp::GlobalNodeId id = 0;
         id < accessor.dataset_identity().global_node_count; ++id) {
      clusters[id % 2].push_back(id);
    }
    auto candidate = ogp::materialize_partition_candidate(
        context.temporary_storage.stage_root, dataset,
        accessor.dataset_identity(), clusters);
    const auto map = context.temporary_storage.stage_root / "candidate" /
                     "partition-0-map.bin";
    std::fstream corrupt(map, std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekp(-1, std::ios::end);
    const char replacement = '\x7f';
    corrupt.write(&replacement, 1);
    return {dp::StageStatus::kSucceeded, candidate, {}};
  }
};

class RejectingLocalBuild final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef&, dp::PartitionId,
                        const dp::LocalBuildConfig&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kFailed, std::nullopt, {}};
  }
};

class InvalidClusterPartition final : public dp::PartitionStrategy {
 public:
  explicit InvalidClusterPartition(std::string output)
      : output_(std::move(output)) {}

  dp::StageResult run(const dp::ArtifactRef&,
                      const std::filesystem::path&,
                      dp::StageContext& context) override {
    const auto path = context.temporary_storage.stage_root / "clusters.txt";
    dp::cpp_main::test::write_text(path, output_);
    try {
      static_cast<void>(ogp::parse_and_canonicalize_clusters(path, 8));
    } catch (const dp::PipelineError&) {
      return {dp::StageStatus::kFailed, std::nullopt, {}};
    }
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }

 private:
  std::string output_;
};

dp::PipelineExecutionRequest checksum_request(
    const dp::ArtifactRef& dataset) {
  auto request = dp::test::canonical_execution_request(
      "ogp-checksum-corruption", dataset);
  request.partition_implementation_id = ogp::kImplementationId;
  request.partition_implementation_version = ogp::kImplementationVersion;
  request.partition_parameters_json =
      R"({"requested_num_clusters":2,"requested_overlap":0.2,"partitioning_mode":"default"})";
  return request;
}

void test_descriptor_and_parameters() {
  const auto value = ogp::descriptor();
  DP_EXPECT_EQ(value.implementation_id, std::string(ogp::kImplementationId));
  DP_EXPECT_EQ(value.implementation_version,
               std::string(ogp::kImplementationVersion));
  DP_EXPECT_EQ(value.stage, dp::StageKind::kPartition);
  DP_EXPECT_EQ(value.requires.input_artifacts.size(), std::size_t{1});
  DP_EXPECT_EQ(value.requires.input_artifacts.front().artifact_type,
               dp::ArtifactType::kDataset);
  DP_EXPECT_EQ(value.requires.input_artifacts.front().input_id_space,
               dp::IdSpace::kNone);
  DP_EXPECT_EQ(value.provides.artifact_type, dp::ArtifactType::kPartition);
  DP_EXPECT_EQ(value.provides.format_version,
               std::string("partition-artifact/v1"));
  DP_EXPECT_EQ(value.provides.output_id_space, dp::IdSpace::kLocal);
  DP_EXPECT_TRUE(std::find(value.provides.capabilities.begin(),
                           value.provides.capabilities.end(),
                           "partition.centroids/v1") ==
                 value.provides.capabilities.end());
  DP_EXPECT_EQ(value.parameter_schema_path,
               std::filesystem::path(
                   "contracts/papers/gp-ann-ogp-v1/schemas/"
                   "ogp-parameters.schema.json"));
  DP_EXPECT_TRUE(!value.test_only);

  const auto parameters = ogp::parse_parameters(
      R"({"requested_num_clusters":4,"requested_overlap":0.2,"partitioning_mode":"default"})",
      64);
  DP_EXPECT_EQ(parameters.requested_num_clusters, std::uint32_t{4});
  DP_EXPECT_TRUE(std::abs(parameters.requested_overlap - 0.2) < 1e-12);
  DP_EXPECT_EQ(parameters.partitioning_mode, std::string("default"));
  DP_EXPECT_THROW(ogp::parse_parameters(
      R"({"requested_num_clusters":4,"requested_overlap":0.2,"partitioning_mode":"default","epsilon":0.1})",
      64));
  DP_EXPECT_THROW(ogp::parse_parameters(
      R"({"requested_num_clusters":65,"requested_overlap":0.2,"partitioning_mode":"default"})",
      64));
  DP_EXPECT_THROW(ogp::parse_parameters(
      R"({"requested_num_clusters":4,"requested_overlap":-0.1,"partitioning_mode":"default"})",
      64));
}

void test_source_build_admission() {
  const auto configured = ogp::configured_source_build_admission();
  const auto admitted = ogp::admit_source_and_build(configured);
  DP_EXPECT_EQ(admitted.source.repository, std::string(ogp::kRepository));
  DP_EXPECT_EQ(admitted.source.branch, std::string(ogp::kBranch));
  DP_EXPECT_EQ(admitted.source.commit, std::string(ogp::kCommit));
  DP_EXPECT_TRUE(!admitted.source.dirty_worktree);
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      admitted.partition_executable));
  DP_EXPECT_EQ(dp::sha256_file(admitted.partition_executable),
               admitted.partition_executable_sha256);
  DP_EXPECT_EQ(admitted.dependencies.size(), std::size_t{4});

  auto wrong_commit = configured;
  wrong_commit.expected_commit = std::string(40, '0');
  DP_EXPECT_THROW(ogp::admit_source_and_build(wrong_commit));
  auto missing = configured;
  missing.partition_executable = configured.build_root / "missing-Partition";
  DP_EXPECT_THROW(ogp::admit_source_and_build(missing));
  auto wrong_binary = configured;
  wrong_binary.partition_executable_sha256 = std::string(64, '0');
  DP_EXPECT_THROW(ogp::admit_source_and_build(wrong_binary));
  auto unverifiable = configured;
  unverifiable.source_root = configured.build_root / "not-a-repository";
  DP_EXPECT_THROW(ogp::admit_source_and_build(unverifiable));
}

void test_official_process_and_subordinate_manifest() {
  dp::test::FakeStageEnvironment environment("ogp-official-process");
  const auto input = environment.workspace().normal_root / "small.fbin";
  write_small_fbin(input, 64, 4);
  const auto input_sha = dp::sha256_file(input);
  const dp::DatasetIdentity dataset{"ogp-small-64x4", input_sha, 64, 4,
                                    "float32", "l2"};
  const auto admitted =
      ogp::admit_source_and_build(ogp::configured_source_build_admission());
  auto context = environment.make_context("ogp-process-stage");
  context.thread_budget = 2;
  const auto result = ogp::run_official_partition(
      admitted, dataset, input, std::filesystem::file_size(input), input_sha,
      {4, 0.2, "default"}, "ogp-process-run", context);
  DP_EXPECT_EQ(result.process_status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.exit_status, 0);
  DP_EXPECT_EQ(result.argv.size(), std::size_t{7});
  DP_EXPECT_EQ(result.argv.at(4), std::string("OGP"));
  DP_EXPECT_EQ(result.argv.at(5), std::string("default"));
  DP_EXPECT_EQ(result.argv.at(6), std::string("0.2"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.stdout_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.stderr_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.expected_output_path));
  DP_EXPECT_TRUE(result.expected_output_sha256.has_value());
  DP_EXPECT_EQ(dp::sha256_file(result.expected_output_path),
               *result.expected_output_sha256);
  const auto manifest =
      dp::parse_build_manifest(read_text(result.build_manifest_path));
  DP_EXPECT_EQ(manifest.run_id, std::string("ogp-process-run"));
  DP_EXPECT_EQ(manifest.stage, std::string("partition"));
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
  const auto evidence = Json::parse(manifest.parameters_json);
  DP_EXPECT_EQ(evidence.at("effective_epsilon"), 0.05);
  DP_EXPECT_EQ(evidence.at("effective_epsilon_source"),
               std::string("upstream_cli_constant"));
  DP_EXPECT_EQ(evidence.at("configured_environment")
                   .at("PARLAY_NUM_THREADS"),
               std::string("2"));
  DP_EXPECT_TRUE(!evidence.contains("observed_parlay_workers"));
  DP_EXPECT_TRUE(!evidence.contains("observed_kaminpar_threads"));
}

void test_nonzero_official_exit_retains_failed_subordinate_manifest() {
  dp::test::FakeStageEnvironment environment("ogp-official-nonzero");
  const auto input = environment.workspace().normal_root / "empty.fbin";
  write_small_fbin(input, 0, 4);
  const auto input_sha = dp::sha256_file(input);
  const dp::DatasetIdentity declared_dataset{
      "ogp-process-negative-fixture", input_sha, 1, 4, "float32", "l2"};
  const auto admitted =
      ogp::admit_source_and_build(ogp::configured_source_build_admission());
  auto context = environment.make_context("ogp-nonzero-stage");
  const auto result = ogp::run_official_partition(
      admitted, declared_dataset, input, std::filesystem::file_size(input),
      input_sha, {1, 0.0, "default"}, "ogp-nonzero-run", context);
  DP_EXPECT_EQ(result.process_status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(result.exit_status != 0);
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.stdout_path));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.stderr_path));
  const auto manifest =
      dp::parse_build_manifest(read_text(result.build_manifest_path));
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(manifest.failure.has_value());
  DP_EXPECT_EQ(manifest.failure->exit_code,
               std::optional<int>{result.exit_status});
}

void test_cluster_validation_and_native_map_adaptation() {
  dp::cpp_main::test::TemporaryDirectory temporary("ogp-clusters");
  const auto output = temporary.path / "clusters.txt";

  dp::cpp_main::test::write_text(output, "0 nope\n1 2 3\n");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));
  dp::cpp_main::test::write_text(output, "");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));
  dp::cpp_main::test::write_text(output, "\n0 1 2 3\n");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));
  dp::cpp_main::test::write_text(output, "0 4\n1 2 3\n");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));
  dp::cpp_main::test::write_text(output, "0 0 1\n2 3\n");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));
  dp::cpp_main::test::write_text(output, "0 1\n3\n");
  DP_EXPECT_THROW(ogp::parse_and_canonicalize_clusters(output, 4));

  dp::cpp_main::test::write_text(output, "3 1 0\n3 2\n");
  const auto clusters = ogp::parse_and_canonicalize_clusters(output, 4);
  DP_EXPECT_EQ(clusters.size(), std::size_t{2});
  DP_EXPECT_EQ(clusters.at(0),
               (std::vector<dp::GlobalNodeId>{0, 1, 3}));
  DP_EXPECT_EQ(clusters.at(1),
               (std::vector<dp::GlobalNodeId>{2, 3}));

  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  dp::cpp_main::test::NativeDatasetFixture dataset(workspace, 4, 4);
  const auto stage_root = temporary.path / "candidate-stage";
  const auto candidate = ogp::materialize_partition_candidate(
      stage_root, dataset.reference, dataset.identity, clusters);
  const auto metadata = dp::cpp_main::test::read_json(
      stage_root / candidate.metadata_path);
  DP_EXPECT_EQ(metadata.at("structural").at("partition_count"), 2);
  DP_EXPECT_EQ(metadata.at("structural").at("partitions").at(0)
                   .at("partition_id"),
               0);
  DP_EXPECT_EQ(metadata.at("structural").at("partitions").at(1)
                   .at("partition_id"),
               1);
  const auto capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  DP_EXPECT_TRUE(std::find(capabilities.begin(), capabilities.end(),
                           dp::kPartitionCentroidsCapability) ==
                 capabilities.end());
  DP_EXPECT_EQ(metadata.at("payloads").size(), std::size_t{2});
  DP_EXPECT_EQ(dp::cpp_main::validate_partition_id_map(
                   stage_root / "candidate" / "partition-0-map.bin", 3, 4),
               clusters.at(0));
  DP_EXPECT_EQ(dp::cpp_main::validate_partition_id_map(
                   stage_root / "candidate" / "partition-1-map.bin", 2, 4),
               clusters.at(1));
}

void test_checksum_corruption_stops_downstream_publication() {
  dp::cpp_main::test::TemporaryDirectory temporary("ogp-corrupt-gate");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  dp::cpp_main::test::NativeDatasetFixture dataset(workspace, 8, 4);
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(
      ogp::descriptor(),
      [] { return std::make_unique<CorruptOgpPartition>(); });
  registry.register_local_build(dp::test::fake_local_graph_descriptor(),
                                dp::test::make_fake_local_graph);
  registry.register_merge(dp::test::fake_merge_descriptor(),
                          dp::test::make_fake_merge);
  registry.seal();
  dp::cpp_main::test::TestContext environment(workspace, "corrupt-gate");
  dp::cpp_main::CppMainVectorAccessor vectors(workspace, dataset.reference);
  environment.context.vector_accessor = &vectors;
  const auto result = dp::PipelineExecutor(registry, workspace).execute(
      checksum_request(dataset.reference), environment.context);
  if (result.failure) {
    std::cerr << "checksum gate failure: " << result.failure->reason << '\n';
  }
  DP_EXPECT_EQ(result.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(result.failure.has_value());
  DP_EXPECT_EQ(result.failure->category, dp::ErrorCategory::kChecksumMismatch);
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!result.partition_artifact.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(
      workspace.normal_root / "ogp-checksum-corruption" / "partition"));
}

void test_invalid_cluster_outputs_never_reach_downstream() {
  const std::vector<std::pair<std::string, std::string>> cases{
      {"malformed", "0 nope\n1 2 3 4 5 6 7\n"},
      {"empty", "\n0 1 2 3 4 5 6 7\n"},
      {"range", "0 8\n1 2 3 4 5 6 7\n"},
      {"duplicate", "0 0 1 2 3\n4 5 6 7\n"},
      {"coverage", "0 1 2 3\n4 5 7\n"}};
  for (const auto& fixture_case : cases) {
    dp::cpp_main::test::TemporaryDirectory temporary(
        "ogp-invalid-" + fixture_case.first);
    const auto workspace = dp::create_workspace(temporary.path / "workspace");
    dp::cpp_main::test::NativeDatasetFixture dataset(workspace, 8, 4);
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
    const auto output = fixture_case.second;
    registry.register_partition(ogp::descriptor(), [output] {
      return std::make_unique<InvalidClusterPartition>(output);
    });
    registry.register_local_build(dp::test::fake_local_graph_descriptor(),
                                  dp::test::make_fake_local_graph);
    registry.register_merge(dp::test::fake_merge_descriptor(),
                            dp::test::make_fake_merge);
    registry.seal();
    dp::cpp_main::test::TestContext environment(
        workspace, "invalid-" + fixture_case.first);
    dp::cpp_main::CppMainVectorAccessor vectors(workspace, dataset.reference);
    environment.context.vector_accessor = &vectors;
    auto request = checksum_request(dataset.reference);
    request.run_id = "ogp-invalid-" + fixture_case.first;
    const auto result = dp::PipelineExecutor(registry, workspace).execute(
        request, environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
    DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
    DP_EXPECT_EQ(result.invocations.local_build, std::size_t{0});
    DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
    DP_EXPECT_TRUE(!result.partition_artifact.has_value());
  }
}

void test_small_real_ogp_partition_publication() {
  dp::cpp_main::test::TemporaryDirectory temporary("ogp-small-real");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  dp::cpp_main::test::NativeDatasetFixture dataset(workspace, 96, 4);
  const auto admitted =
      ogp::admit_source_and_build(ogp::configured_source_build_admission());
  const auto source_head_before = admitted.source.commit;
  const auto executable_sha_before =
      dp::sha256_file(admitted.partition_executable);

  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(ogp::descriptor(), [admitted] {
    return ogp::make_ogp_partition(admitted);
  });
  registry.register_local_build(
      dp::test::fake_local_graph_descriptor(),
      [] { return std::make_unique<RejectingLocalBuild>(); });
  registry.register_merge(dp::test::fake_merge_descriptor(),
                          dp::test::make_fake_merge);
  registry.seal();
  dp::cpp_main::test::TestContext environment(workspace, "small-real");
  dp::cpp_main::CppMainVectorAccessor vectors(workspace, dataset.reference);
  environment.context.vector_accessor = &vectors;
  auto request = checksum_request(dataset.reference);
  request.run_id = "ogp-small-real";
  request.partition_parameters_json =
      R"({"requested_num_clusters":4,"requested_overlap":0.2,"partitioning_mode":"default"})";
  const auto result = dp::PipelineExecutor(registry, workspace).execute(
      request, environment.context);
  if (result.failure) {
    std::cerr << "small real failure: " << result.failure->reason << '\n';
  }
  DP_EXPECT_TRUE(result.partition_artifact.has_value());
  DP_EXPECT_EQ(result.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{1});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{0});
  if (!result.partition_artifact) return;

  const auto dataset_validation = dp::Validator(workspace).validate(
      dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  if (!dataset_validation.valid()) return;
  const auto partition_validation = dp::Validator(workspace).validate(
      *result.partition_artifact,
      {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());
  if (!partition_validation.valid()) return;
  const auto metadata_path = dp::resolve_normal_artifact_metadata(
      workspace, result.partition_artifact->metadata_path);
  const auto metadata = dp::cpp_main::test::read_json(metadata_path);
  DP_EXPECT_TRUE(metadata.at("structural").at("partition_count") >= 2);
  const auto capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  DP_EXPECT_TRUE(std::find(capabilities.begin(), capabilities.end(),
                           dp::kPartitionCentroidsCapability) ==
                 capabilities.end());
  std::size_t assignments = 0;
  for (const auto& record : metadata.at("structural").at("partitions")) {
    const auto count = record.at("local_node_count").get<std::size_t>();
    assignments += count;
    const auto role =
        record.at("local_to_global_map_role").get<std::string>();
    const auto& payloads = metadata.at("payloads");
    const auto payload = std::find_if(
        payloads.begin(), payloads.end(), [&](const auto& value) {
          return value.at("role").template get<std::string>() == role;
        });
    DP_EXPECT_TRUE(payload != payloads.end());
    if (payload == payloads.end()) continue;
    const auto ids = dp::cpp_main::validate_partition_id_map(
        metadata_path.parent_path() /
            payload->at("path").get<std::string>(),
        count, dataset.identity.global_node_count);
    DP_EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
  }
  DP_EXPECT_TRUE(assignments > dataset.identity.global_node_count);
  DP_EXPECT_EQ(dp::sha256_file(admitted.partition_executable),
               executable_sha_before);
  const auto readmitted =
      ogp::admit_source_and_build(ogp::configured_source_build_admission());
  DP_EXPECT_EQ(readmitted.source.commit, source_head_before);
  DP_EXPECT_TRUE(!readmitted.source.dirty_worktree);
}

}  // namespace

int main() {
  test_descriptor_and_parameters();
  test_source_build_admission();
  test_official_process_and_subordinate_manifest();
  test_nonzero_official_exit_retains_failed_subordinate_manifest();
  test_cluster_validation_and_native_map_adaptation();
  test_checksum_corruption_stops_downstream_publication();
  test_invalid_cluster_outputs_never_reach_downstream();
  test_small_real_ogp_partition_publication();
  return dp::test::finish();
}
