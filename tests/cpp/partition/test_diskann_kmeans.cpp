#include "partition/diskann_kmeans/stage_binding.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "support/test_assert.hpp"
#include "validation/parameter_validator.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace dp = diskann_pipeline;
namespace km = dp::diskann_kmeans;
namespace native = dp::cpp_main;
namespace f = native::test;
using Json = nlohmann::json;

namespace {
std::string quote(const std::filesystem::path& path) {
  std::string text = "'";
  for (char ch : path.string()) text += ch == '\'' ? "'\\''" : std::string(1, ch);
  return text + "'";
}

void unit() {
  for (const auto k : {1, 2, 3, 96}) {
    const auto config = km::parse_parameters(Json{{"K", k}}.dump(), 96);
    DP_EXPECT_EQ(config.k, static_cast<std::uint32_t>(k));
    DP_EXPECT_EQ(config.max_k_means_reps, std::size_t{15});
    dp::detail::validate_implementation_parameters(km::descriptor(), Json{{"K", k}}.dump(), KMEANS_TEST_ROOT);
  }
  DP_EXPECT_EQ(km::parse_parameters(R"({"K":2,"max_k_means_reps":1})", 96).max_k_means_reps, std::size_t{1});
  for (const auto text : {"{}", R"({"K":0})", R"({"K":-1})", R"({"K":97})",
                          R"({"K":2.0})", R"({"K":true})", R"({"K":"2"})",
                          R"({"K":18446744073709551615})", R"({"K":2,"seed":1})",
                          R"({"K":2,"sample_probability":1})", R"({"K":2,"k_base":2})",
                          R"({"K":2,"max_k_means_reps":0})", R"({"K":2,"max_k_means_reps":-1})",
                          R"({"K":2,"max_k_means_reps":1.5})", R"({"K":2,"max_k_means_reps":true})"}) {
    DP_EXPECT_THROW(km::parse_parameters(text, 96));
  }
  DP_EXPECT_THROW(km::parse_parameters(R"({"K":1})", 0));
  DP_EXPECT_EQ(km::sample_probability(96), 1.0);
  DP_EXPECT_EQ(km::sample_probability(256000), 1.0);
  DP_EXPECT_TRUE(std::abs(km::sample_probability(10000000) - 0.0256) < 1e-12);
  DP_EXPECT_THROW(km::sample_probability(0));
  DP_EXPECT_THROW(km::validate_training_sample_count(0, 1));
  DP_EXPECT_THROW(km::validate_training_sample_count(2, 3));
  DP_EXPECT_THROW(km::validate_training_sample_count(8388609, 2));
  km::validate_training_sample_count(3, 3);
  km::validate_training_sample_count(8388608, 2);  // Count only; no large allocation/run.
  const auto capabilities = km::descriptor().provides.capabilities;
  DP_EXPECT_TRUE(std::find(capabilities.begin(), capabilities.end(), "partition.overlap/v1") == capabilities.end());
  DP_EXPECT_TRUE(std::string(km::kImplementationId) != native::kP0ImplementationId);

  // Adversarial native outputs exercise the production pre-publication checks.
  f::TemporaryDirectory temporary("kmeans-output-contract");
  const auto prefix = temporary.path / "native";
  dp::DatasetIdentity dataset{"small", std::string(64, 'a'), 4, 2, "float32", "l2"};
  const auto centers = prefix.string() + "_centroids.bin";
  const auto first = prefix.string() + "_subshard-0_ids_uint32.bin";
  const auto second = prefix.string() + "_subshard-1_ids_uint32.bin";
  const auto reset = [&] {
    f::write_bytes(centers, f::float_matrix(2, 2, {0, 0, 1, 1}));
    f::write_bytes(first, f::id_matrix({0, 1}));
    f::write_bytes(second, f::id_matrix({2, 3}));
  };
  reset();
  DP_EXPECT_EQ(km::validate_native_output(prefix, dataset, 2), std::vector<dp::NodeCount>({2, 2}));
  for (const auto& ids : std::vector<std::vector<dp::GlobalNodeId>>{{}, {1, 3}, {2}, {2, 4}, {3, 2}, {2, 2}}) {
    reset(); f::write_bytes(second, f::id_matrix(ids));
    DP_EXPECT_THROW(km::validate_native_output(prefix, dataset, 2));
  }
  reset(); auto truncated = f::id_matrix({2, 3}); truncated.pop_back(); f::write_bytes(second, truncated);
  DP_EXPECT_THROW(km::validate_native_output(prefix, dataset, 2));
  reset();
  DP_EXPECT_THROW(km::validate_native_output(prefix, dataset, 3));
  f::write_bytes(centers, f::float_matrix(2, 2, {0, 0, 1, std::numeric_limits<float>::infinity()}));
  DP_EXPECT_THROW(km::validate_native_output(prefix, dataset, 2));
  reset(); f::write_bytes(centers, f::float_matrix(2, 1, {0, 1}));
  DP_EXPECT_THROW(km::validate_native_output(prefix, dataset, 2));
}

Json prepare(const std::filesystem::path& root, int k = 2, bool identical = false) {
  if (std::filesystem::exists(root)) throw std::runtime_error("fixture destination already exists");
  const auto workspace = dp::create_workspace(std::filesystem::absolute(root) / "workspace");
  f::NativeDatasetFixture data(workspace, 96, 4);
  if (identical) {
    const auto path = workspace.normal_root / "dataset/artifact.json";
    auto metadata = f::read_json(path);
    const auto vectors = path.parent_path() / "vectors.bin";
    f::write_bytes(vectors, f::float_matrix(96, 4, std::vector<float>(384, 0)));
    metadata["dataset"]["dataset_checksum"] = dp::sha256_file(vectors);
    metadata["payloads"][0] = f::payload("vectors", "vectors.bin", native::kFloat32DatasetMediaType, vectors);
    metadata["aggregate_checksum"] = f::aggregate_for(metadata);
    f::write_text(path, metadata.dump());
    data.reference.aggregate_checksum = metadata.at("aggregate_checksum");
  }
  auto config = f::read_json(KMEANS_CONFIG);
  config["workspace"]["root"] = workspace.root.string();
  config["dataset"] = Json::parse(dp::serialize_artifact_ref(data.reference));
  config["run_id"] = "kmeans-k" + std::to_string(k);
  config["partition"]["K"] = k;
  f::write_text(root / "config.json", config.dump(2));
  return config;
}

int invoke(const std::filesystem::path& root, const std::string& suffix = "") {
  return std::system((quote(KMEANS_RUNNER) + " execute --request " + quote(root / "config.json") +
      " --result " + quote(root / "workspace/result.json") + " >" + quote(root / ("stdout" + suffix + ".log")) +
      " 2>" + quote(root / ("stderr" + suffix + ".log"))).c_str());
}

void inspect(const std::filesystem::path& root, int k, int reps) {
  const auto config = f::read_json(root / "config.json");
  const auto workspace = dp::create_workspace(root / "workspace");
  const auto result = f::read_json(workspace.root / "result.json");
  DP_EXPECT_EQ(result.at("outcome"), "SUCCEEDED");
  DP_EXPECT_EQ(result.at("stage_invocations").at("partition"), 1);
  DP_EXPECT_EQ(result.at("stage_invocations").at("local_build"), k);
  DP_EXPECT_EQ(result.at("stage_invocations").at("merge"), 1);
  const auto partition = dp::parse_artifact_ref(result.at("partition").dump());
  const auto dataset = dp::parse_artifact_ref(config.at("dataset").dump());
  const auto validated_dataset = dp::Validator(workspace).validate(dataset);
  const auto validated = dp::Validator(workspace).validate(partition, {*validated_dataset.validated_artifact});
  DP_EXPECT_TRUE(validated.valid());
  DP_EXPECT_TRUE(native::b0_input_admission({*validated.validated_artifact},
      f::TestContext(workspace, "admission-inspection").context).compatible == false); // Missing native vector binding.
  native::CppMainVectorAccessor vectors(workspace, dataset);
  f::TestContext context(workspace, "native-admission-inspection");
  context.context.vector_accessor = &vectors;
  DP_EXPECT_TRUE(native::b0_input_admission({*validated_dataset.validated_artifact, *validated.validated_artifact},
                                          context.context).compatible);
  const auto metadata_path = workspace.normal_root / partition.metadata_path;
  const auto metadata = f::read_json(metadata_path);
  DP_EXPECT_EQ(metadata.at("structural").at("partition_count"), k);
  DP_EXPECT_EQ(metadata.at("payloads").size(), std::size_t(k + 1));
  std::vector<float> centers(static_cast<std::size_t>(k) * 4);
  std::ifstream centroids(metadata_path.parent_path() / "centroids.bin", std::ios::binary);
  centroids.seekg(8); centroids.read(reinterpret_cast<char*>(centers.data()), centers.size() * sizeof(float));
  DP_EXPECT_TRUE(bool(centroids));
  std::vector<int> seen(96, 0);
  for (int cluster = 0; cluster < k; ++cluster) {
    const auto count = metadata.at("structural").at("partitions").at(cluster).at("local_node_count").get<dp::NodeCount>();
    DP_EXPECT_TRUE(count > 0);
    const auto ids = native::validate_partition_id_map(metadata_path.parent_path() /
        ("partition-" + std::to_string(cluster) + "-map.bin"), count, 96);
    DP_EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
    for (const auto id : ids) {
      ++seen[id];
      std::vector<float> point;
      vectors.read(id, point);
      std::vector<double> distances(k, 0);
      for (int c = 0; c < k; ++c) for (int d = 0; d < 4; ++d) {
        const double delta = static_cast<double>(point[d]) - centers[c * 4 + d];
        distances[c] += delta * delta;
      }
      const auto best = *std::min_element(distances.begin(), distances.end());
      // BLAS uses float arithmetic. Any tied/near-tied nearest center is valid.
      DP_EXPECT_TRUE(distances[cluster] <= best + 1e-4 * std::max(1.0, best));
    }
    const auto local = dp::parse_artifact_ref(result.at("local_graphs").at(cluster).dump());
    DP_EXPECT_TRUE(dp::Validator(workspace).validate(local, {*validated.validated_artifact}).valid());
    const auto local_path = workspace.normal_root / local.metadata_path;
    const auto local_meta = f::read_json(local_path);
    DP_EXPECT_EQ(local_meta.at("structural").at("partition_id"), cluster);
    DP_EXPECT_EQ(local_meta.at("structural").at("local_node_count"), count);
    const auto local_map = native::validate_partition_id_map(local_path.parent_path() /
        local_meta.at("payloads").at(0).at("path").get<std::string>(), count, 96);
    DP_EXPECT_EQ(ids, local_map);
  }
  DP_EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](int count) { return count == 1; }));
  const auto parent = f::read_json(workspace.manifest_root / config.at("run_id").get<std::string>() / "stages/partition.json");
  const auto execution = parent.at("parameters").at("stage_execution_manifest");
  const auto manifest_path = workspace.root / execution.at("path").get<std::string>();
  DP_EXPECT_EQ(dp::sha256_file(manifest_path), execution.at("sha256"));
  const auto manifest = f::read_json(manifest_path);
  DP_EXPECT_EQ(manifest.at("status"), "SUCCEEDED");
  DP_EXPECT_EQ(manifest.at("implementation").at("implementation_id"), km::kImplementationId);
  DP_EXPECT_EQ(manifest.at("source").at("commit"), native::kPinnedCommit);
  DP_EXPECT_EQ(manifest.at("parameters").at("actual_training_sample_count"), 96);
  const auto effective = manifest.at("parameters").at("effective_parameters");
  DP_EXPECT_EQ(effective.at("K"), k);
  DP_EXPECT_EQ(effective.at("max_k_means_reps"), reps);
  DP_EXPECT_EQ(effective.at("sample_probability"), 1.0);
  DP_EXPECT_EQ(effective.at("k_base"), 1);
  DP_EXPECT_TRUE(!manifest.at("toolchain").at("compile_commands").empty());
  DP_EXPECT_TRUE(manifest.at("metrics").at("elapsed_seconds").get<double>() > 0);

  // Corrupt copies only; immutable published artifacts remain untouched.
  const auto staging = dp::create_workspace(root / "corruption-check");
  const auto copy_dir = staging.normal_root / "partition";
  std::filesystem::copy(metadata_path.parent_path(), copy_dir, std::filesystem::copy_options::recursive);
  auto copy = partition; copy.metadata_path = "partition/artifact.json";
  auto broken = metadata;
  broken["source_artifacts"][0]["aggregate_checksum"] = std::string(64, 'f');
  broken["aggregate_checksum"] = f::aggregate_for(broken);
  copy.aggregate_checksum = broken.at("aggregate_checksum");
  f::write_text(copy_dir / "artifact.json", broken.dump());
  DP_EXPECT_TRUE(!dp::Validator(staging).validate(copy, {*validated_dataset.validated_artifact}).valid());
  f::write_text(copy_dir / "artifact.json", metadata.dump()); copy.aggregate_checksum = partition.aggregate_checksum;
  f::write_bytes(copy_dir / "centroids.bin", f::float_matrix(k, 4, std::vector<float>(k * 4, 99)));
  DP_EXPECT_TRUE(!dp::Validator(staging).validate(copy, {*validated_dataset.validated_artifact}).valid());
}

void integration(const std::filesystem::path& base) {
  std::filesystem::create_directories(base);
  // K=1 is a valid Partition boundary even though the example entry closes a
  // sharded B0/M0 pipeline. Exercise the module itself, plus cancellation.
  {
    const auto root = base / "direct-boundaries";
    const auto config = prepare(root, 1);
    const auto workspace = dp::create_workspace(root / "workspace");
    const auto dataset = dp::parse_artifact_ref(config.at("dataset").dump());
    native::CppMainVectorAccessor vectors(workspace, dataset);
    const auto evidence = native::admit_source_and_build(native::configured_source_build_admission());
    for (const auto& scenario : {std::string("one"), std::string("cancelled"), std::string("invalid")}) {
      f::TestContext environment(workspace, scenario);
      environment.context.vector_accessor = &vectors;
      environment.cancellation.cancelled = scenario == "cancelled";
      const auto path = workspace.manifest_root / scenario / "parameters/partition.json";
      f::write_text(path, scenario == "invalid" ? R"({"K":97})" : R"({"K":1})");
      const auto stage = km::make_partition(evidence)->run(dataset, path, environment.context);
      const auto manifest = f::read_json(stage.build_manifest_path);
      if (scenario == "one") {
        DP_EXPECT_EQ(stage.status, dp::StageStatus::kSucceeded);
        DP_EXPECT_TRUE(dp::Validator(f::candidate_workspace(environment.context))
            .validate(*stage.output_candidate, {vectors.validated_dataset()}).valid());
        const auto metadata = f::read_json(environment.context.temporary_storage.stage_root /
                                           stage.output_candidate->metadata_path);
        DP_EXPECT_EQ(metadata.at("structural").at("partition_count"), 1);
        DP_EXPECT_EQ(metadata.at("structural").at("partitions").at(0).at("local_node_count"), 96);
      } else {
        DP_EXPECT_EQ(stage.status, scenario == "cancelled" ? dp::StageStatus::kCancelled : dp::StageStatus::kFailed);
        DP_EXPECT_TRUE(!stage.output_candidate);
        DP_EXPECT_TRUE(!environment.metrics.saw("diskann_kmeans.kmeanspp_selecting_pivots"));
        DP_EXPECT_TRUE(manifest.at("parameters").at("actual_training_sample_count").is_null());
        DP_EXPECT_TRUE(!manifest.at("failure").at("reason").get<std::string>().empty());
      }
    }
  }
  for (int k : {2, 3}) {
    const auto root = base / ("k" + std::to_string(k));
    auto config = prepare(root, k);
    if (k == 2) config["partition"].erase("max_k_means_reps");
    else config["partition"]["max_k_means_reps"] = 3;
    f::write_text(root / "config.json", config.dump(2));
    const auto status = invoke(root);
    DP_EXPECT_EQ(status, 0);
    if (status == 0) inspect(root, k, k == 2 ? 15 : 3);
    const auto checksum = dp::sha256_file(root / "workspace/result.json");
    DP_EXPECT_TRUE(invoke(root, "-repeat") != 0);
    DP_EXPECT_EQ(dp::sha256_file(root / "workspace/result.json"), checksum);
  }
  const auto failed = base / "empty-shard";
  prepare(failed, 2, true);
  DP_EXPECT_TRUE(invoke(failed) != 0);
  const auto result = f::read_json(failed / "workspace/result.json");
  DP_EXPECT_EQ(result.at("outcome"), "FAILED");
  DP_EXPECT_EQ(result.at("stage_invocations").at("local_build"), 0);
  const auto manifest = f::read_json(failed / "workspace/manifests/kmeans-k2/stage-execution/partition.json");
  DP_EXPECT_EQ(manifest.at("status"), "FAILED");
  DP_EXPECT_TRUE(manifest.at("failure").at("reason").get<std::string>().find("empty") != std::string::npos);
  DP_EXPECT_EQ(manifest.at("parameters").at("actual_training_sample_count"), 96);
  std::cout << "KMEANS_VALIDATION_OUTPUT=" << base << '\n';
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--prepare") {
      prepare(std::filesystem::absolute(argv[2]));
    } else if (argc == 2 && std::string(argv[1]) == "unit") {
      unit();
    } else if (argc == 2 && std::string(argv[1]) == "integration") {
      const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      if (const auto output = std::getenv("KMEANS_TEST_OUTPUT_ROOT")) {
        integration(std::filesystem::path(output) / nonce);
      } else {
        f::TemporaryDirectory temporary("kmeans-native-" + nonce);
        integration(temporary.path / "cases");
      }
    } else return 2;
  } catch (const std::exception& error) {
    dp::test::fail("no unexpected exception", __FILE__, __LINE__, error.what());
  }
  return dp::test::finish();
}
