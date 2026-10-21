#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/merge_call_record.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <cmath>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;

namespace {

class CancelAfterCall final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return ++checks >= 3; }
  mutable unsigned checks{0};
};

class CorruptStagedMap final : public dp::MetricsSink {
 public:
  explicit CorruptStagedMap(std::filesystem::path path) : path_(std::move(path)) {}
  void event(const std::string& name) override {
    if (name == "cpp_main.m0.merge_shards") {
      // The source Artifact remains valid and untouched. Inject a native-read
      // failure in the wrapper's private copy, after its input preparation.
      auto bytes = fixture::id_matrix({0});
      bytes[4] = 2;
      fixture::write_bytes(path_, bytes);
    }
  }
  void increment(const std::string&, std::uint64_t) override {}
 private:
  std::filesystem::path path_;
};

void check_ascending_input(bool ascending, bool declared) {
  fixture::TemporaryDirectory temporary("m0-ascending");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace, 3, 2);
  const std::vector<std::vector<std::uint32_t>> global_graph{{1}, {2}, {0}};
  const std::vector<dp::GlobalNodeId> map = ascending
      ? std::vector<dp::GlobalNodeId>{0, 1, 2}
      : std::vector<dp::GlobalNodeId>{1, 2, 0};
  std::vector<dp::LocalNodeId> inverse(3);
  for (dp::LocalNodeId local = 0; local < 3; ++local) inverse[map[local]] = local;
  std::vector<std::vector<std::uint32_t>> local_graph(3), round_trip(3);
  for (dp::LocalNodeId local = 0; local < 3; ++local) {
    for (const auto neighbor : global_graph[map[local]]) {
      local_graph[local].push_back(inverse[neighbor]);
      round_trip[map[local]].push_back(map[local_graph[local].back()]);
    }
  }
  DP_EXPECT_EQ(round_trip, global_graph);
  const auto local_medoid = inverse[0];
  DP_EXPECT_EQ(map[local_medoid], dp::GlobalNodeId{0});

  auto partition = fixture::materialize_partition(workspace, dataset, {map});
  if (!declared) {
    dp::test::mutate_metadata(workspace, partition, [](auto& metadata) {
      auto& caps = metadata.at("capabilities");
      caps.erase(std::remove(caps.begin(), caps.end(),
                              "partition.local_to_global_ascending/v1"),
                 caps.end());
    });
  }
  auto local = fixture::materialize_local(
      workspace, dataset, partition, 0, map, local_graph);
  const auto local_directory =
      (workspace.normal_root / local.metadata_path).parent_path();
  fixture::write_bytes(local_directory / "graph.index",
                        fixture::native_graph(local_medoid, local_graph));
  dp::test::mutate_metadata(workspace, local, [&](auto& metadata) {
    metadata["payloads"][1] = fixture::payload(
        "local_graph", "graph.index", native::kLocalGraphMediaType,
        local_directory / "graph.index");
  });
  const auto local_facts = native::validate_local_graph(
      local_directory / "graph.index", 3, 1);
  DP_EXPECT_EQ(map[local_facts.entry_point], dp::GlobalNodeId{0});
  const auto input_map_sha = dp::sha256_file(local_directory / "map.bin");
  const auto input_graph_sha = dp::sha256_file(local_directory / "graph.index");

  dp::Validator validator(workspace);
  const auto dataset_result = validator.validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_result.valid());
  if (!dataset_result.valid()) return;
  const auto partition_result = validator.validate(
      partition, {*dataset_result.validated_artifact});
  DP_EXPECT_EQ(partition_result.valid(), ascending || !declared);
  std::optional<dp::ValidatedArtifact> validated_local;
  if (partition_result.valid()) {
    const auto local_result = validator.validate(
        local, {*partition_result.validated_artifact});
    DP_EXPECT_TRUE(local_result.valid());
    DP_EXPECT_TRUE(validator.validate_with_lineage(local).valid());
    validated_local = local_result.validated_artifact;
    if (!validated_local) return;
    const auto& validated_partition = *partition_result.validated_artifact;
    const auto compatible = dp::CompatibilityChecker().check(
        native::m0_descriptor(),
        {{dp::ArtifactType::kPartition, "partition-artifact/v1",
          dp::IdSpace::kLocal, validated_partition.capabilities()},
         {dp::ArtifactType::kLocalGraph, "local-graph-artifact/v1",
          dp::IdSpace::kLocal, validated_local->capabilities()}},
        {{"cancellation_token/v1", "metrics_sink/v1"}, false});
    DP_EXPECT_EQ(compatible.compatible, declared);
  }

  // Call the real wrapper even when validation/preflight would reject the input.
  // One edge per node and R=1 remove shuffle/truncation ambiguity.
  fixture::TestContext context(workspace, "m0-ascending-input");
  const auto parameters = fixture::write_parameters(
      context.context.temporary_storage, R"({"R":1,"use_filters":false})");
  const auto result = native::make_m0_merge()->merge(
      partition, {local}, parameters, context.context);
  const bool expected_success = ascending && declared;
  const bool native_called = context.metrics.saw("cpp_main.m0.merge_shards");
  std::cout << "m0-ascending ascending=" << ascending << " declared=" << declared
            << " status=" << dp::to_string(result.status)
            << " native_called=" << native_called << '\n';
  DP_EXPECT_EQ(native_called, expected_success);
  DP_EXPECT_EQ(dp::sha256_file(local_directory / "map.bin"), input_map_sha);
  DP_EXPECT_EQ(dp::sha256_file(local_directory / "graph.index"), input_graph_sha);
  DP_EXPECT_EQ(dp::sha256_file(workspace.normal_root / "partition/map-0.bin"),
               input_map_sha);
  if (!expected_success) {
    DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!result.output_candidate.has_value());
    DP_EXPECT_TRUE(!std::filesystem::exists(
        context.context.temporary_storage.stage_root / "native"));
    DP_EXPECT_TRUE(!std::filesystem::exists(
        context.context.temporary_storage.stage_root / "candidate"));
    return;
  }
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  DP_EXPECT_TRUE(!result.build_manifest_path.empty());
  if (!result.build_manifest_path.empty()) {
    const auto record = fixture::read_json(result.build_manifest_path);
    const auto timing = record.at("metrics").at("global_graph_merge_time");
    DP_EXPECT_EQ(timing.at("call_status"), "SUCCEEDED");
    DP_EXPECT_EQ(timing.at("scope"), "diskann.merge_shards_call");
    DP_EXPECT_EQ(timing.at("unit"), "us");
    DP_EXPECT_TRUE(std::isfinite(timing.at("value").get<double>()));
    DP_EXPECT_TRUE(timing.at("value").get<double>() >= 0);
  }
  if (!result.output_candidate || !partition_result.valid() || !validated_local) return;
  const auto output_validation =
      dp::Validator(fixture::candidate_workspace(context.context)).validate(
          *result.output_candidate,
          {*partition_result.validated_artifact, *validated_local});
  DP_EXPECT_TRUE(output_validation.valid());
  const auto metadata_path = context.context.temporary_storage.stage_root /
                             result.output_candidate->metadata_path;
  const auto metadata = fixture::read_json(metadata_path);
  const auto expected_bytes = fixture::native_graph(0, global_graph);
  DP_EXPECT_EQ(dp::sha256_file(metadata_path.parent_path() /
                   metadata["payloads"][0]["path"].get<std::string>()),
               dp::sha256_bytes(std::string(expected_bytes.begin(), expected_bytes.end())));
  const auto medoids = native::validate_medoids(
      metadata_path.parent_path() / metadata["payloads"][1]["path"].get<std::string>(),
      1, 3);
  DP_EXPECT_EQ(medoids, std::vector<dp::GlobalNodeId>{0});
}

}  // namespace

int main() {
  dp::merge_record::CallTiming frozen("diskann.merge_shards_call");
  DP_EXPECT_TRUE(frozen.metrics().empty());
  frozen.start();
  frozen.finish(dp::merge_record::CallTiming::Clock::now(), "SUCCEEDED");
  const auto frozen_value = frozen.metrics();
  frozen.finish(dp::merge_record::CallTiming::Clock::now() + std::chrono::seconds(10), "FAILED");
  DP_EXPECT_EQ(frozen.metrics(), frozen_value);  // post-call work cannot retime it
  for (bool ascending : {false, true}) {
    for (bool declared : {false, true}) check_ascending_input(ascending, declared);
  }
  fixture::TemporaryDirectory temporary("m0");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::NativeDatasetFixture dataset(workspace, 20, 4);
  std::vector<dp::GlobalNodeId> map0(20);
  std::iota(map0.begin(), map0.end(), 0U);
  const auto map1 = map0;
  std::vector<std::vector<std::uint32_t>> adjacency0(20);
  std::vector<std::vector<std::uint32_t>> adjacency1(20);
  for (std::uint32_t node = 0; node < 20; ++node) {
    for (std::uint32_t offset = 1; offset <= 10; ++offset) {
      adjacency0[node].push_back((node + offset) % 20);
    }
    for (std::uint32_t offset = 10; offset <= 19; ++offset) {
      adjacency1[node].push_back((node + offset) % 20);
    }
  }
  const auto partition =
      fixture::materialize_partition(workspace, dataset, {map0, map1});
  const auto local0 = fixture::materialize_local(
      workspace, dataset, partition, 0, map0, adjacency0);
  const auto local1 = fixture::materialize_local(
      workspace, dataset, partition, 1, map1, adjacency1);
  const auto dataset_validation = dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  const auto partition_validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());
  const auto local0_validation = dp::Validator(workspace).validate(
      local0, {*partition_validation.validated_artifact});
  const auto local1_validation = dp::Validator(workspace).validate(
      local1, {*partition_validation.validated_artifact});
  DP_EXPECT_TRUE(local0_validation.valid() && local1_validation.valid());

  fixture::TestContext environment(workspace, "m0-success");
  const auto parameters = fixture::write_parameters(
      environment.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto result = native::make_m0_merge()->merge(
      partition, {local0, local1}, parameters, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  const auto validation = dp::Validator(fixture::candidate_workspace(
                                      environment.context))
                              .validate(*result.output_candidate,
                                        {*partition_validation.validated_artifact,
                                         *local0_validation.validated_artifact,
                                         *local1_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());
  DP_EXPECT_TRUE(environment.metrics.saw("cpp_main.m0.merge_shards"));
  const auto metadata = fixture::read_json(
      environment.context.temporary_storage.stage_root /
      result.output_candidate->metadata_path);
  DP_EXPECT_EQ(metadata.at("payloads").size(), std::size_t{2});
  DP_EXPECT_EQ(metadata.at("payloads").at(0).at("role"), "global_graph");
  DP_EXPECT_EQ(metadata.at("payloads").at(1).at("role"), "medoids");
  DP_EXPECT_EQ(metadata.at("structural").at("global_node_count"), 20);

  fixture::TestContext configurable_r(workspace, "m0-configurable-r");
  const auto configurable_r_parameters = fixture::write_parameters(
      configurable_r.context.temporary_storage,
      R"({"R":7,"use_filters":false})");
  const auto configurable_r_result = native::make_m0_merge()->merge(
      partition, {local0, local1}, configurable_r_parameters,
      configurable_r.context);
  DP_EXPECT_EQ(configurable_r_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(configurable_r_result.output_candidate.has_value());
  DP_EXPECT_TRUE(configurable_r.metrics.saw("cpp_main.m0.merge_shards"));
  if (configurable_r_result.output_candidate) {
    const auto configurable_r_validation =
        dp::Validator(fixture::candidate_workspace(configurable_r.context))
            .validate(*configurable_r_result.output_candidate,
                      {*partition_validation.validated_artifact,
                       *local0_validation.validated_artifact,
                       *local1_validation.validated_artifact});
    DP_EXPECT_TRUE(configurable_r_validation.valid());
    const auto configurable_r_metadata = fixture::read_json(
        configurable_r.context.temporary_storage.stage_root /
        configurable_r_result.output_candidate->metadata_path);
    DP_EXPECT_EQ(configurable_r_metadata.at("structural")
                     .at("declared_max_degree"),
                 7);
    DP_EXPECT_TRUE(configurable_r_metadata.at("structural")
                       .at("observed_max_degree")
                       .get<std::uint32_t>() <= 7U);
  }

  {
    fixture::TemporaryDirectory degree_eleven_temporary("m0-degree-eleven");
    const auto degree_eleven_workspace = dp::create_workspace(
        degree_eleven_temporary.path / "workspace");
    const fixture::NativeDatasetFixture degree_eleven_dataset(
        degree_eleven_workspace, 20, 4);
    std::vector<dp::GlobalNodeId> degree_eleven_map(20);
    std::iota(degree_eleven_map.begin(), degree_eleven_map.end(), 0U);
    std::vector<std::vector<std::uint32_t>> degree_eleven_adjacency(20);
    for (std::uint32_t node = 0; node < 20; ++node) {
      for (std::uint32_t offset = 1; offset <= 11; ++offset) {
        degree_eleven_adjacency[node].push_back((node + offset) % 20);
      }
    }
    const auto degree_eleven_partition = fixture::materialize_partition(
        degree_eleven_workspace, degree_eleven_dataset,
        {degree_eleven_map, degree_eleven_map});
    const auto degree_eleven_local0 = fixture::materialize_local(
        degree_eleven_workspace, degree_eleven_dataset,
        degree_eleven_partition, 0, degree_eleven_map,
        degree_eleven_adjacency);
    const auto degree_eleven_local1 = fixture::materialize_local(
        degree_eleven_workspace, degree_eleven_dataset,
        degree_eleven_partition, 1, degree_eleven_map,
        degree_eleven_adjacency);

    const auto degree_eleven_dataset_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_dataset.reference);
    DP_EXPECT_TRUE(degree_eleven_dataset_validation.valid());
    const auto degree_eleven_partition_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_partition,
                      {*degree_eleven_dataset_validation.validated_artifact});
    DP_EXPECT_TRUE(degree_eleven_partition_validation.valid());
    const auto degree_eleven_local0_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_local0,
                      {*degree_eleven_partition_validation.validated_artifact});
    const auto degree_eleven_local1_validation =
        dp::Validator(degree_eleven_workspace)
            .validate(degree_eleven_local1,
                      {*degree_eleven_partition_validation.validated_artifact});
    DP_EXPECT_TRUE(degree_eleven_local0_validation.valid());
    DP_EXPECT_TRUE(degree_eleven_local1_validation.valid());

    fixture::TestContext degree_eleven(degree_eleven_workspace,
                                       "m0-degree-eleven");
    const auto degree_eleven_parameters = fixture::write_parameters(
        degree_eleven.context.temporary_storage,
        R"({"R":16,"use_filters":false})");
    const auto degree_eleven_result = native::make_m0_merge()->merge(
        degree_eleven_partition,
        {degree_eleven_local0, degree_eleven_local1},
        degree_eleven_parameters, degree_eleven.context);
    DP_EXPECT_EQ(degree_eleven_result.status, dp::StageStatus::kSucceeded);
    DP_EXPECT_TRUE(degree_eleven_result.output_candidate.has_value());
    DP_EXPECT_TRUE(degree_eleven.metrics.saw("cpp_main.m0.merge_shards"));
    if (degree_eleven_result.output_candidate) {
      const auto degree_eleven_output_validation =
          dp::Validator(fixture::candidate_workspace(degree_eleven.context))
              .validate(
                  *degree_eleven_result.output_candidate,
                  {*degree_eleven_partition_validation.validated_artifact,
                   *degree_eleven_local0_validation.validated_artifact,
                   *degree_eleven_local1_validation.validated_artifact});
      DP_EXPECT_TRUE(degree_eleven_output_validation.valid());
      const auto degree_eleven_metadata = fixture::read_json(
          degree_eleven.context.temporary_storage.stage_root /
          degree_eleven_result.output_candidate->metadata_path);
      DP_EXPECT_EQ(degree_eleven_metadata.at("structural")
                       .at("declared_max_degree"),
                   16);
      DP_EXPECT_TRUE(degree_eleven_metadata.at("structural")
                         .at("observed_max_degree")
                         .get<std::uint32_t>() <= 16U);
    }
  }

  const std::array<std::pair<const char*, const char*>, 5>
      invalid_r_parameters{{
          {"m0-r-missing", R"({"use_filters":false})"},
          {"m0-r-zero", R"({"R":0,"use_filters":false})"},
          {"m0-r-negative", R"({"R":-1,"use_filters":false})"},
          {"m0-r-non-integer", R"({"R":7.5,"use_filters":false})"},
          {"m0-r-too-large",
           R"({"R":4294967296,"use_filters":false})"},
      }};
  for (const auto& invalid_r : invalid_r_parameters) {
    fixture::TestContext invalid(workspace, invalid_r.first);
    const auto invalid_parameters = fixture::write_parameters(
        invalid.context.temporary_storage, invalid_r.second);
    const auto invalid_result = native::make_m0_merge()->merge(
        partition, {local0, local1}, invalid_parameters, invalid.context);
    DP_EXPECT_EQ(invalid_result.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!invalid_result.output_candidate.has_value());
    DP_EXPECT_TRUE(!invalid.metrics.saw("cpp_main.m0.merge_shards"));
  }

  fixture::TestContext missing(workspace, "m0-missing");
  const auto missing_parameters = fixture::write_parameters(
      missing.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto missing_result = native::make_m0_merge()->merge(
      partition, {local0}, missing_parameters, missing.context);
  DP_EXPECT_EQ(missing_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!missing_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!missing.metrics.saw("cpp_main.m0.merge_shards"));

  fixture::TestContext duplicate(workspace, "m0-duplicate");
  const auto duplicate_parameters = fixture::write_parameters(
      duplicate.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto duplicate_result = native::make_m0_merge()->merge(
      partition, {local0, local0}, duplicate_parameters, duplicate.context);
  DP_EXPECT_EQ(duplicate_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!duplicate.metrics.saw("cpp_main.m0.merge_shards"));

  fixture::TestContext cancelled(workspace, "m0-cancelled");
  cancelled.cancellation.cancelled = true;
  const auto cancelled_parameters = fixture::write_parameters(
      cancelled.context.temporary_storage,
      R"({"R":16,"use_filters":false})");
  const auto cancelled_result = native::make_m0_merge()->merge(
      partition, {local0, local1}, cancelled_parameters, cancelled.context);
  DP_EXPECT_EQ(cancelled_result.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!cancelled_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!fixture::read_json(cancelled_result.build_manifest_path).at("metrics").contains("global_graph_merge_time"));
  fixture::TestContext after_call(workspace, "m0-cancel-after-call");
  CancelAfterCall cancel_after;
  after_call.context.cancellation_token = &cancel_after;
  const auto after_parameters = fixture::write_parameters(after_call.context.temporary_storage, R"({"R":16,"use_filters":false})");
  const auto after = native::make_m0_merge()->merge(partition, {local0, local1}, after_parameters, after_call.context);
  DP_EXPECT_EQ(after.status, dp::StageStatus::kCancelled);
  DP_EXPECT_TRUE(!after.output_candidate.has_value());
  const auto after_record = fixture::read_json(after.build_manifest_path);
  DP_EXPECT_EQ(after_record.at("metrics").at("global_graph_merge_time").at("call_status"), "SUCCEEDED");
  fixture::TestContext call_failure(workspace, "m0-native-failure");
  CorruptStagedMap corruption(call_failure.context.temporary_storage.stage_root / "native/shard-0.ids");
  call_failure.context.metrics_sink = &corruption;
  const auto failed_parameters = fixture::write_parameters(call_failure.context.temporary_storage, R"({"R":16,"use_filters":false})");
  const auto failed_call = native::make_m0_merge()->merge(partition, {local0, local1}, failed_parameters, call_failure.context);
  DP_EXPECT_EQ(failed_call.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!failed_call.output_candidate.has_value());
  const auto failed_call_record = fixture::read_json(failed_call.build_manifest_path);
  DP_EXPECT_EQ(failed_call_record.at("metrics").at("global_graph_merge_time").at("call_status"), "FAILED");
  for (bool block_manifest : {false, true}) {
    const std::string label = block_manifest ? "m0-audit-failure" : "m0-publish-failure";
    fixture::TestContext failed(workspace, label);
    const auto parameters = workspace.manifest_root / label / "parameters/merge.json";
    dp::write_immutable_text(parameters, R"({"R":16,"use_filters":false})");
    const auto occupied = failed.context.temporary_storage.stage_root / "candidate/global.index";
    dp::write_immutable_text(occupied, "existing user content");
    const auto audit = workspace.report_root / "cpp-main-m0" / label / "merge-execution.json";
    if (block_manifest) dp::write_immutable_text(audit, "existing audit");
    try {
      const auto failed_result = native::make_m0_merge()->merge(partition, {local0, local1}, parameters, failed.context);
      DP_EXPECT_TRUE(!block_manifest);
      DP_EXPECT_EQ(failed_result.status, dp::StageStatus::kFailed);
      DP_EXPECT_TRUE(!failed_result.output_candidate.has_value());
      const auto record = fixture::read_json(failed_result.build_manifest_path);
      DP_EXPECT_EQ(record.at("status"), "FAILED");
      DP_EXPECT_EQ(record.at("metrics").at("global_graph_merge_time").at("call_status"), "SUCCEEDED");
      DP_EXPECT_TRUE(record.at("failure").at("reason").get<std::string>().find("global.index") != std::string::npos);
    } catch (const std::exception& error) {
      DP_EXPECT_TRUE(block_manifest);
      DP_EXPECT_TRUE(std::string(error.what()).find("global.index") != std::string::npos);
    }
    DP_EXPECT_EQ(dp::sha256_file(occupied), dp::sha256_bytes("existing user content"));
    if (block_manifest) DP_EXPECT_EQ(dp::sha256_file(audit), dp::sha256_bytes("existing audit"));
  }
  return dp::test::finish();
}
