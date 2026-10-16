#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "support/test_assert.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>

namespace dp = diskann_pipeline;
namespace native = dp::cpp_main;
namespace f = native::test;
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
constexpr const char* kKmeans = "diskann_78256bbab4685e1774e78d331e081a153be26823_kmeans_configurable_k_v1";
constexpr const char* kOgp = "gp_ann_ogp_partition_v1";
constexpr const char* kB1 = "xn_graph_xn_descent_local_build_v1";
constexpr const char* kM1 = "hnsw_merger_vamana_merge_v1";
std::string quote(const fs::path& value) {
  std::string result = "'";
  for (char c : value.string()) result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
}
Json selected(const std::string& id, Json parameters) {
  return {{"implementation_id", id}, {"implementation_version", "1.0.0"}, {"parameters", parameters}};
}
Json b1_parameters() {
  return {{"schema_version", "xn-descent-parameters/v1"}, {"validation_profile", "pipeline-compatible"},
         {"omega", 8}, {"lambda", 16}, {"alpha", 1.2}, {"seed", 42}, {"iteration_cap", 100},
         {"prng_policy", "mt19937_64-splitmix64-per-node-v1"}, {"sampling_policy", "unbiased-rejection-unique-nonself-v1"},
         {"tie_break_policy", "squared-distance-then-local-id-v1"}, {"round_model", "deterministic-synchronous-round-v1"},
         {"convergence_rule", "full-state-fixed-point-or-three-round-c-g-stability-v1"},
         {"entry_point_rule", "local-centroid-nearest-v1"}, {"distance_implementation", "squared-l2/v1"}, {"reproduction_mode", "faithful_adaptation"}};
}
Json prepare(const fs::path& root) {
  const auto workspace = dp::create_workspace(root / "workspace");
  f::NativeDatasetFixture data(workspace, 96, 4);
  return {{"config_version", "module-composition-run/v1"}, {"run_id", "small"},
          {"workspace", {{"root", workspace.root.string()}, {"memory_budget_bytes", 1073741824}, {"thread_budget", 6}}},
          {"dataset", Json::parse(dp::serialize_artifact_ref(data.reference))},
          {"partition", selected(native::kP0ImplementationId, {{"R", 16}, {"build_DRAM_budget_gib", 0.6}, {"replication_factor", 2}, {"sample_probability", 1.0}})},
          {"local_build", selected(native::kB0ImplementationId, {{"L", 32}, {"R", 16}, {"alpha", 1.2}, {"build_pq_bytes", 0}, {"max_occlusion_size", 750}, {"num_threads", 6}, {"saturation", false}, {"use_opq", false}})},
          {"merge", selected(native::kM0ImplementationId, {{"R", 16}, {"use_filters", false}})}};
}
int invoke(const fs::path& root, const Json& config, const fs::path& output = {}) {
  f::write_text(root / "request.json", config.dump(2));
  const auto result = output.empty() ? root / "workspace/reports/result.json" : output;
  const auto command = quote(COMPOSITION_RUNNER) + " execute --request " + quote(root / "request.json") +
      " --result " + quote(result) + " >" + quote(root / "stdout.log") + " 2>" + quote(root / "stderr.log");
  f::write_text(root / "command.txt", command + "\n");
  const int status = std::system(command.c_str());
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
void rejected(const fs::path& root, const Json& config, const std::string& reason) {
  DP_EXPECT_EQ(invoke(root, config), 2);
  const auto result = f::read_json(root / "workspace/reports/result.json");
  DP_EXPECT_EQ(result.at("outcome"), "FAILED");
  DP_EXPECT_TRUE(result.at("failure").at("reason").get<std::string>().find(reason) != std::string::npos);
  DP_EXPECT_EQ(result.at("stage_invocations"), Json({{"partition", 0}, {"local_build", 0}, {"merge", 0}}));
  DP_EXPECT_TRUE(!result.contains("global_graph"));
  DP_EXPECT_EQ(f::read_json(root / "stderr.log").at("outcome").get<std::string>(), "FAILED");
}
void requests(const fs::path& base) {
  int index = 0;
  auto negative = [&](auto mutate, const std::string& reason) {
    const auto root = base / ("reject-" + std::to_string(index++));
    auto config = prepare(root);
    mutate(config);
    rejected(root, config, reason);
  };
  negative([](Json& c) { c.erase("merge"); }, "missing");
  negative([](Json& c) { c["config_version"] = "v2"; }, "version");
  negative([](Json& c) { c["run_id"] = "../bad"; }, "run_id");
  negative([](Json& c) { c["workspace"]["memory_budget_bytes"] = 0; }, "positive");
  negative([](Json& c) { c["workspace"]["thread_budget"] = 1.5; }, "integer");
  negative([](Json& c) { c["workspace"]["thread_budget"] = 2; }, "thread_budget");
  negative([](Json& c) { c["local_build"]["parameters"] = Json::array(); }, "parameters");
  negative([](Json& c) { c["partition"]["implementation_id"] = "missing"; }, "identity");
  negative([](Json& c) { c["merge"]["implementation_version"] = "2.0.0"; }, "identity");
  negative([](Json& c) { c["partition"]["implementation_id"] = native::kM0ImplementationId; }, "stage");
  negative([](Json& c) { c["partition"]["parameters"]["sample_probability"] = 0.5; }, "P0 parameters");
  negative([](Json& c) { c["local_build"]["parameters"]["alpha"] = 1.3; }, "constant");
  negative([](Json& c) { c["merge"]["parameters"]["R"] = -1; }, "R");
  negative([](Json& c) { c["local_build"]["parameters"]["partition_id"] = 0; }, "executor-owned");
#ifdef DISKANN_KMEANS_PARTITION
  negative([](Json& c) { c["partition"] = selected(kKmeans, {{"K", 97}}); }, "K");
  negative([](Json& c) { c["partition"] = selected(kKmeans, {{"K", 2}, {"max_k_means_reps", 0}}); }, "max_k_means_reps");
#else
  negative([](Json& c) { c["partition"] = selected(kKmeans, {{"K", 2}}); }, "identity");
#endif
#ifdef DISKANN_GP_ANN_OGP
  negative([](Json& c) { c["partition"] = selected(kOgp, {{"requested_num_clusters", 0}, {"requested_overlap", 0.2}, {"partitioning_mode", "default"}}); }, "OGP parameter");
#else
  negative([](Json& c) { c["partition"] = selected(kOgp, Json::object()); }, "identity");
#endif
#ifdef DISKANN_XN_DESCENT
  negative([](Json& c) { auto p = b1_parameters(); p["omega"] = 0; c["local_build"] = selected(kB1, p); }, "omega");
  negative([](Json& c) { auto p = b1_parameters(); p["alpha"] = 0.9; c["local_build"] = selected(kB1, p); }, "alpha");
  negative([](Json& c) { auto p = b1_parameters(); p["distance_implementation"] = "l2"; c["local_build"] = selected(kB1, p); }, "distance_implementation");
#else
  negative([](Json& c) { c["local_build"] = selected(kB1, Json::object()); }, "identity");
#endif
#ifdef DISKANN_HNSW_MERGER
  negative([](Json& c) { c["merge"] = selected(kM1, {{"R", 8}, {"alpha", 1.2}, {"max_candidate_size", 750}, {"metric", "l2"}, {"use_filters", false}, {"multi_shard_strategy", "large_first_pairwise_v1"}, {"lambda_policy", "paper_adaptive_log_v1"}}); }, "constant");
#else
  negative([](Json& c) { c["merge"] = selected(kM1, Json::object()); }, "identity");
#endif
  {
    const auto root = base / "bad-checksum";
    const auto config = prepare(root);
    std::ofstream(root / "workspace/normal/dataset/vectors.bin", std::ios::app) << 'x';
    rejected(root, config, "Dataset");
  }
  {
    const auto root = base / "inconsistent-large-declaration";
    auto config = prepare(root);
    const auto path = root / "workspace/normal/dataset/artifact.json";
    auto metadata = f::read_json(path);
    // Declaration-only negative: no large payload is created or read.
    metadata["dataset"]["global_node_count"] = 1000001;
    metadata["structural"]["global_node_count"] = 1000001;
    metadata["aggregate_checksum"] = f::aggregate_for(metadata);
    config["dataset"]["aggregate_checksum"] = metadata.at("aggregate_checksum");
    f::write_text(path, metadata.dump());
    rejected(root, config, "header differs");
  }
  {
    const auto root = base / "unsupported-distance";
    auto config = prepare(root);
    const auto path = root / "workspace/normal/dataset/artifact.json";
    auto metadata = f::read_json(path);
    metadata["dataset"]["distance_metric"] = "cosine";
    metadata["aggregate_checksum"] = f::aggregate_for(metadata);
    config["dataset"]["aggregate_checksum"] = metadata.at("aggregate_checksum");
    f::write_text(path, metadata.dump());
    rejected(root, config, "Dataset");
  }
  {
    const auto root = base / "occupied";
    const auto config = prepare(root);
    fs::create_directories(root / "workspace/manifests/small");
    rejected(root, config, "occupied");
  }
  for (const auto& scenario : {"outside", "existing", "symlink", "upstream", "reserved", "nested"}) {
    const auto root = base / scenario;
    auto config = prepare(root);
    auto output = root / "outside.json";
    if (std::string(scenario) == "existing") {
      output = root / "workspace/reports/existing.json";
      f::write_text(output, "preserved");
    } else if (std::string(scenario) == "symlink") {
      fs::create_directory_symlink(base, root / "workspace/reports/escape");
      output = root / "workspace/reports/escape/result.json";
    } else if (std::string(scenario) == "upstream") {
      config["workspace"]["root"] = std::string(COMPOSITION_TEST_ROOT) + "/upstream/diskann-cpp-main";
    } else if (std::string(scenario) == "reserved") {
      output = root / "workspace/reports/small-evaluation.json";
    } else if (std::string(scenario) == "nested") {
      output = root / "workspace/reports/gp-ann-ogp/small/result.json";
    }
    DP_EXPECT_EQ(invoke(root, config, output), 2);
    DP_EXPECT_EQ(f::read_json(root / "stderr.log").at("outcome").get<std::string>(), "FAILED");
    if (std::string(scenario) == "existing") DP_EXPECT_EQ(dp::sha256_file(output), dp::sha256_bytes("preserved"));
    else DP_EXPECT_TRUE(!fs::exists(output));
    DP_EXPECT_TRUE(!fs::exists(root / "workspace/manifests/small"));
  }
}

void inspect(const fs::path& root, const Json& config) {
  const auto result = f::read_json(root / "workspace/reports/result.json");
  DP_EXPECT_EQ(result.at("outcome"), "SUCCEEDED");
  const auto workspace = dp::create_workspace(root / "workspace");
  dp::Validator validator(workspace);
  const auto dataset = validator.validate(dp::parse_artifact_ref(config.at("dataset").dump()));
  DP_EXPECT_TRUE(dataset.valid());
  const auto partition = validator.validate(dp::parse_artifact_ref(result.at("partition").dump()), {*dataset.validated_artifact});
  DP_EXPECT_TRUE(partition.valid());
  std::vector<dp::ValidatedArtifact> inputs{*partition.validated_artifact};
  const auto partition_meta = f::read_json(workspace.normal_root / result.at("partition").at("metadata_path").get<std::string>());
  const auto count = partition_meta.at("structural").at("partition_count").get<std::size_t>();
  if (config.at("partition").at("implementation_id") == kKmeans) DP_EXPECT_EQ(count, std::size_t{3});
  DP_EXPECT_EQ(result.at("local_graphs").size(), count);
  for (const auto& local : result.at("local_graphs")) {
    const auto validation = validator.validate(dp::parse_artifact_ref(local.dump()), {*partition.validated_artifact});
    DP_EXPECT_TRUE(validation.valid());
    inputs.push_back(*validation.validated_artifact);
  }
  DP_EXPECT_TRUE(validator.validate(dp::parse_artifact_ref(result.at("global_graph").dump()), inputs).valid());
  DP_EXPECT_EQ(result.at("stage_invocations"), Json({{"partition", 1}, {"local_build", count}, {"merge", 1}}));
  for (const auto* stage : {"partition", "local_build", "merge"}) {
    DP_EXPECT_EQ(result.at("effective_config").at(stage).at("implementation_id"), config.at(stage).at("implementation_id"));
    DP_EXPECT_EQ(result.at("effective_config").at(stage).at("implementation_version"), "1.0.0");
  }
  for (const auto& record : result.at("build_manifests")) {
    const auto path = workspace.root / record.at("path").get<std::string>();
    DP_EXPECT_EQ(dp::sha256_file(path), record.at("sha256"));
    const auto manifest = dp::parse_build_manifest(f::read_json(path).dump());
    const auto stage = manifest.stage;
    const auto selected_stage = stage == "local_build" ? "local_build" : stage;
    DP_EXPECT_EQ(manifest.implementation.implementation_id, config.at(selected_stage).at("implementation_id"));
    DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
    const auto parameters = Json::parse(manifest.parameters_json);
    auto effective = parameters.at("stage_parameters");
    effective.erase("partition_id");
    DP_EXPECT_EQ(effective, result.at("effective_config").at(selected_stage).at("parameters"));
    DP_EXPECT_EQ(parameters.at("source_config").at("entry_build_manifest"), result.at("entry_build_manifest"));
    if (manifest.implementation.implementation_id == kOgp)
      DP_EXPECT_EQ(manifest.source.commit, "df5efb4a6b303011c795666ed267f29d994b558c");
    else if (manifest.implementation.implementation_id != kM1 && manifest.implementation.implementation_id != kB1)
      DP_EXPECT_EQ(manifest.source.commit, "78256bbab4685e1774e78d331e081a153be26823");
    else DP_EXPECT_EQ(manifest.source.commit.size(), std::size_t{40});
    if (parameters.contains("stage_execution_manifest")) {
      const auto& execution = parameters.at("stage_execution_manifest");
      DP_EXPECT_EQ(dp::sha256_file(workspace.root / execution.at("path").get<std::string>()), execution.at("sha256"));
    }
  }
  for (const auto& record : result.at("validation_evidence")) {
    const auto path = workspace.root / record.at("path").get<std::string>();
    DP_EXPECT_EQ(dp::sha256_file(path), record.at("sha256"));
    DP_EXPECT_EQ(f::read_json(path).at("result").get<std::string>(), "VALID");
  }
  const auto& entry = result.at("entry_build_manifest");
  const auto path = workspace.root / entry.at("path").get<std::string>();
  DP_EXPECT_EQ(dp::sha256_file(path), entry.at("sha256"));
  const auto build = f::read_json(path);
  DP_EXPECT_EQ(build.at("parameters").at("runner_binary").at("sha256"), dp::sha256_file(COMPOSITION_RUNNER));
  DP_EXPECT_EQ(build.at("implementation").at("implementation_id"), "module_composition_runner_v1");
}
void native_runs(const fs::path& base) {
  std::vector<std::string> combinations{"p0-b0-m0"};
#ifdef DISKANN_KMEANS_PARTITION
  combinations.push_back("kmeans-b0-m0");
#ifdef DISKANN_HNSW_MERGER
  combinations.push_back("kmeans-b0-m1");
#endif
#endif
#ifdef DISKANN_GP_ANN_OGP
  combinations.push_back("ogp-b0-m0");
#endif
#ifdef DISKANN_XN_DESCENT
  combinations.push_back("p0-b1-m0");
#endif
  for (const auto& combination : combinations) {
    const auto root = base / combination;
    auto config = prepare(root);
    // A different run whose name starts with this ID does not own this run.
    fs::create_directories(root / "workspace/manifests/small-previous");
    if (combination.find("kmeans") == 0) config["partition"] = selected(kKmeans, {{"K", 3}});
    if (combination.find("ogp") == 0) config["partition"] = selected(kOgp, {{"requested_num_clusters", 4}, {"requested_overlap", 0.2}, {"partitioning_mode", "default"}});
    if (combination == "kmeans-b0-m1") config["merge"] = selected(kM1, {{"R", 16}, {"alpha", 1.2}, {"max_candidate_size", 750}, {"metric", "l2"}, {"use_filters", false}, {"multi_shard_strategy", "large_first_pairwise_v1"}, {"lambda_policy", "paper_adaptive_log_v1"}});
    if (combination == "p0-b1-m0") config["local_build"] = selected(kB1, b1_parameters());
    const auto status = invoke(root, config);
    DP_EXPECT_EQ(status, 0);
    std::cout << combination << " exit=" << status << " evidence=" << root << '\n';
    if (status == 0) inspect(root, config);
  }
#ifdef DISKANN_KMEANS_PARTITION
  const auto root = base / "invalid-partition";
  auto config = prepare(root);
  const auto path = root / "workspace/normal/dataset/artifact.json";
  auto meta = f::read_json(path);
  const auto payload = path.parent_path() / "vectors.bin";
  f::write_bytes(payload, f::float_matrix(96, 4, std::vector<float>(384, 0)));
  meta["dataset"]["dataset_checksum"] = dp::sha256_file(payload);
  meta["payloads"][0] = f::payload("vectors", "vectors.bin", native::kFloat32DatasetMediaType, payload);
  meta["aggregate_checksum"] = f::aggregate_for(meta);
  f::write_text(path, meta.dump());
  config["dataset"]["aggregate_checksum"] = meta.at("aggregate_checksum");
  config["partition"] = selected(kKmeans, {{"K", 2}});
  DP_EXPECT_EQ(invoke(root, config), 4);
  const auto failed = f::read_json(root / "workspace/reports/result.json");
  DP_EXPECT_EQ(failed.at("outcome"), "FAILED");
  DP_EXPECT_EQ(failed.at("stage_invocations").at("partition"), 1);
  DP_EXPECT_EQ(failed.at("stage_invocations").at("local_build"), 0);
  DP_EXPECT_EQ(failed.at("stage_invocations").at("merge"), 0);
  DP_EXPECT_TRUE(!failed.contains("global_graph"));
#endif
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto base = fs::path(std::getenv("COMPOSITION_TEST_OUTPUT_ROOT") ? std::getenv("COMPOSITION_TEST_OUTPUT_ROOT") : "/tmp/module-composition") / nonce;
    if (std::string(argv[1]) == "requests") requests(base);
    else if (std::string(argv[1]) == "native") native_runs(base);
    else return 2;
    std::cout << "COMPOSITION_VALIDATION_OUTPUT=" << base << '\n';
  } catch (const std::exception& error) {
    dp::test::fail("no unexpected exception", __FILE__, __LINE__, error.what());
  }
  return dp::test::finish();
}
