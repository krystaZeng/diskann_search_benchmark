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
constexpr const char* kFixedK = "cpp_main_78256bbab4685e1774e78d331e081a153be26823_p0_fixed_k_v1";
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
  for (const auto* stage : {"partition", "local_build"}) {
    negative([stage](Json& c) { c[stage]["implementation_version"] = "1.2.0"; }, "identity");
    negative([stage](Json& c) { c[stage]["parameters"]["R"] = 64; }, "constant");
    for (const auto& bad : std::vector<Json>{0, 1, -1, 64.0, true, "64", nullptr,
                                            4294967296ULL, 4294967360ULL}) {
      // The message varies between schema type and numeric-parser rejection.
      negative([stage, bad](Json& c) {
        c[stage]["implementation_version"] = "1.1.0";
        c[stage]["parameters"]["R"] = bad;
      }, stage);
    }
  }
  for (const auto& bad : std::vector<Json>{0, -1, 100.0, true, "100", nullptr, 4294967396ULL}) {
    negative([bad](Json& c) {
      c["local_build"]["implementation_version"] = "1.1.0";
      c["local_build"]["parameters"]["L"] = bad;
    }, "local_build");
  }
  negative([](Json& c) { c["local_build"]["parameters"]["L"] = 100; }, "constant");
  negative([](Json& c) {
    c["local_build"]["implementation_version"] = "1.1.0";
    c["local_build"]["parameters"]["num_threads"] = 7;
  }, "constant");
  {
    const auto root = base / "p0-v1-1-near-budget";
    auto config = prepare(root);
    config["partition"]["implementation_version"] = "1.1.0";
    config["partition"]["parameters"]["build_DRAM_budget_gib"] = 0.6000000000005;
    rejected(root, config, "build_DRAM_budget_gib does not equal its required constant");
  }
  {
    const auto root = base / "b0-v1-1-near-alpha";
    auto config = prepare(root);
    config["local_build"]["implementation_version"] = "1.1.0";
    config["local_build"]["parameters"]["alpha"] = 1.2000005;
    rejected(root, config, "alpha does not equal its required constant");
  }
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
    if (config.at("local_build").at("implementation_id") == native::kB0ImplementationId) {
      const auto& selected_b0 = config.at("local_build");
      const auto parsed = native::parse_b0_parameters(selected_b0.at("parameters").dump(),
                                                       selected_b0.at("implementation_version"));
      const auto meta = f::read_json(workspace.normal_root / local.at("metadata_path").get<std::string>());
      const auto& structure = meta.at("structural");
      DP_EXPECT_EQ(structure.at("declared_max_degree"), parsed.local_degree);
      DP_EXPECT_TRUE(structure.at("observed_max_degree") <= parsed.local_degree);
      if (selected_b0.at("implementation_version") == "1.1.0") {
        const auto prefix = "cpp_main.b0.v1_1.partition." +
            std::to_string(structure.at("partition_id").get<unsigned>()) + ".";
        const auto& observed = result.at("native_parameter_observations");
        DP_EXPECT_EQ(observed.at(prefix + "max_degree"), parsed.local_degree);
        DP_EXPECT_EQ(observed.at(prefix + "search_list_size"), parsed.l);
        DP_EXPECT_EQ(observed.at(prefix + "filter_list_size"), parsed.l);
        DP_EXPECT_EQ(observed.at(prefix + "validation_max_degree"), parsed.local_degree);
      }
    }
  }
  DP_EXPECT_TRUE(validator.validate(dp::parse_artifact_ref(result.at("global_graph").dump()), inputs).valid());
  if (config.at("merge").at("implementation_id") == native::kM0ImplementationId) {
    const auto global = f::read_json(workspace.normal_root /
        result.at("global_graph").at("metadata_path").get<std::string>());
    DP_EXPECT_EQ(global.at("id_space"), "global");
    DP_EXPECT_EQ(global.at("dataset").at("global_node_count"), dataset.validated_artifact->dataset().global_node_count);
    DP_EXPECT_EQ(global.at("structural").at("declared_max_degree"), config.at("merge").at("parameters").at("R"));
    DP_EXPECT_TRUE(global.at("structural").at("observed_max_degree") <= config.at("merge").at("parameters").at("R"));
  }
  if (config.at("partition").at("implementation_id") == native::kP0ImplementationId &&
      config.at("partition").at("implementation_version") == "1.1.0") {
    const auto parsed = native::parse_p0_parameters(config.at("partition").at("parameters").dump(),
        dataset.validated_artifact->dataset().global_node_count, "1.1.0");
    DP_EXPECT_EQ(result.at("native_parameter_observations").at("cpp_main.p0.v1_1.graph_degree"), parsed.local_degree);
    std::vector<unsigned> owners(dataset.validated_artifact->dataset().global_node_count, 0);
    const auto directory = (workspace.normal_root / result.at("partition").at("metadata_path").get<std::string>()).parent_path();
    for (const auto& record : partition_meta.at("structural").at("partitions")) {
      const auto id = record.at("partition_id").get<unsigned>();
      const auto map = native::validate_partition_id_map(directory / ("partition-" + std::to_string(id) + "-map.bin"),
          record.at("local_node_count"), owners.size());
      DP_EXPECT_TRUE(std::is_sorted(map.begin(), map.end()));
      for (auto node : map) ++owners[node];
    }
    DP_EXPECT_TRUE(std::all_of(owners.begin(), owners.end(), [](unsigned n) { return n == 2; }));
  }
  DP_EXPECT_EQ(result.at("stage_invocations"), Json({{"partition", 1}, {"local_build", count}, {"merge", 1}}));
  for (const auto* stage : {"partition", "local_build", "merge"}) {
    DP_EXPECT_EQ(result.at("effective_config").at(stage).at("implementation_id"), config.at(stage).at("implementation_id"));
    DP_EXPECT_EQ(result.at("effective_config").at(stage).at("implementation_version"), config.at(stage).at("implementation_version"));
  }
  for (const auto& record : result.at("build_manifests")) {
    const auto path = workspace.root / record.at("path").get<std::string>();
    DP_EXPECT_EQ(dp::sha256_file(path), record.at("sha256"));
    const auto manifest = dp::parse_build_manifest(f::read_json(path).dump());
    const auto stage = manifest.stage;
    const auto selected_stage = stage == "local_build" ? "local_build" : stage;
    DP_EXPECT_EQ(manifest.implementation.implementation_id, config.at(selected_stage).at("implementation_id"));
    DP_EXPECT_EQ(manifest.implementation.implementation_version, config.at(selected_stage).at("implementation_version"));
    DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
    const auto parameters = Json::parse(manifest.parameters_json);
    auto effective = parameters.at("stage_parameters");
    effective.erase("partition_id");
    DP_EXPECT_EQ(effective, result.at("effective_config").at(selected_stage).at("parameters"));
    DP_EXPECT_EQ(parameters.at("source_config").at("entry_build_manifest"), result.at("entry_build_manifest"));
    if (manifest.implementation.implementation_version == "1.1.0") {
      const auto& derived = parameters.at("source_config").at("native_effective_parameters").at(selected_stage);
      const auto r = effective.at("R").get<std::uint64_t>();
      DP_EXPECT_EQ(derived.at(stage == "partition" ? "graph_degree" : "local_degree"), 2 * r / 3);
      if (stage == "local_build") DP_EXPECT_EQ(derived.at("filter_list_size"), effective.at("L"));
    }
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
  if (config.at("local_build").at("implementation_version") == "1.1.0") {
    const auto& derived = build.at("parameters").at("native_effective_parameters").at("local_build");
    DP_EXPECT_EQ(derived.at("L"), config.at("local_build").at("parameters").at("L"));
    DP_EXPECT_EQ(derived.at("R"), config.at("local_build").at("parameters").at("R"));
  }
}
void native_runs(const fs::path& base) {
  std::vector<std::string> combinations{"p0-b0-m0", "p0-b0-v1-1-r16", "p0-b0-v1-1-r64",
                                         "p0-v1-1-b0-old", "p0-old-b0-v1-1"};
#ifdef DISKANN_KMEANS_PARTITION
  combinations.push_back("kmeans-b0-m0");
#ifdef DISKANN_HNSW_MERGER
  combinations.push_back("kmeans-b0-m1");
#endif
#endif
#ifdef DISKANN_GP_ANN_OGP
  combinations.push_back("ogp-b0-m0");
  combinations.push_back("ogp-b0-v1-1-r64");
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
    if (combination == "p0-b0-v1-1-r16" || combination == "p0-b0-v1-1-r64" ||
        combination == "ogp-b0-v1-1-r64") {
      const auto r = combination.find("r64") != std::string::npos ? 64 : 16;
      if (combination.find("p0") == 0) {
        config["partition"]["implementation_version"] = "1.1.0";
        config["partition"]["parameters"]["R"] = r;
      }
      config["local_build"]["implementation_version"] = "1.1.0";
      config["local_build"]["parameters"]["R"] = r;
      config["local_build"]["parameters"]["L"] = r == 64 ? 100 : 32;
      config["merge"]["parameters"]["R"] = r;
    }
    if (combination == "p0-v1-1-b0-old") config["partition"]["implementation_version"] = "1.1.0";
    if (combination == "p0-old-b0-v1-1") {
      // Stage parameters are independently selected; there is no all-stage equal-R gate.
      config["local_build"]["implementation_version"] = "1.1.0";
      config["local_build"]["parameters"]["R"] = 64;
      config["local_build"]["parameters"]["L"] = 100;
      config["merge"]["parameters"]["R"] = 64;
    }
    if (combination == "p0-b0-v1-1-r64" || combination == "ogp-b0-v1-1-r64") {
      const auto filename = combination == "p0-b0-v1-1-r64"
          ? "p0-b0-m0-default-rl.json" : "ogp-b0-m0-default-rl.json";
      auto example = f::read_json(fs::path(COMPOSITION_TEST_ROOT) /
          "experiments/configs/module-composition" / filename);
      DP_EXPECT_EQ(example.at("dataset"), config.at("dataset"));
      for (const auto* stage : {"partition", "local_build", "merge"})
        DP_EXPECT_EQ(example.at(stage), config.at(stage));
      example["workspace"] = config.at("workspace");
      example["run_id"] = config.at("run_id");
      config = example;
    }
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
void fixed_k_runs(const fs::path& base) {
  const auto root = base / "fixed-k4-b0-v1-1-m0";
  auto config = prepare(root);
  config["partition"] = selected(kFixedK, {{"K", 4}});
  config["local_build"]["implementation_version"] = "1.1.0";
  config["local_build"]["parameters"]["R"] = 64;
  config["local_build"]["parameters"]["L"] = 100;
  config["merge"]["parameters"]["R"] = 64;
  const auto status = invoke(root, config);
  std::cout << "fixed-k4-b0-v1-1-m0 exit=" << status << " evidence=" << root << '\n';
  DP_EXPECT_EQ(status, 0);
  if (status != 0) return;
  inspect(root, config);  // Existing degree, provenance, checksum and evidence assertions.
  const auto result = f::read_json(root / "workspace/reports/result.json");
  DP_EXPECT_EQ(result.at("stage_invocations"), Json({{"partition", 1}, {"local_build", 4}, {"merge", 1}}));
  const auto workspace = dp::create_workspace(root / "workspace");
  const auto partition_path = workspace.normal_root / result.at("partition").at("metadata_path").get<std::string>();
  const auto meta = f::read_json(partition_path);
  DP_EXPECT_EQ(meta.at("structural").at("partition_count"), 4);
  std::vector<unsigned> owners(96, 0);
  std::uint64_t members = 0;
  for (const auto& record : meta.at("structural").at("partitions")) {
    const auto count = record.at("local_node_count").get<dp::NodeCount>();
    DP_EXPECT_TRUE(count > 0);
    DP_EXPECT_TRUE(!record.contains("shard_vectors_role"));
    const auto map = native::validate_partition_id_map(partition_path.parent_path() /
        ("partition-" + std::to_string(record.at("partition_id").get<unsigned>()) + "-map.bin"), count, 96);
    DP_EXPECT_TRUE(std::is_sorted(map.begin(), map.end()));
    for (auto id : map) ++owners[id];
    members += count;
  }
  DP_EXPECT_EQ(members, std::uint64_t{192});
  DP_EXPECT_TRUE(std::all_of(owners.begin(), owners.end(), [](unsigned count) { return count == 2; }));
  const auto manifest = f::read_json(workspace.manifest_root / "small/stage-execution/partition.json");
  DP_EXPECT_EQ(manifest.at("implementation").at("implementation_id"), kFixedK);
  const auto& parameters = manifest.at("parameters");
  DP_EXPECT_EQ(parameters.at("requested_parameters"), Json({{"K", 4}}));
  DP_EXPECT_EQ(parameters.at("actual_training_sample_count"), 96);
  DP_EXPECT_EQ(parameters.at("actual_partition_count"), 4);
  DP_EXPECT_EQ(parameters.at("total_memberships"), members);
  DP_EXPECT_EQ(parameters.at("effective_parameters").at("k_base"), 2);
  DP_EXPECT_EQ(parameters.at("effective_parameters").at("max_k_means_reps"), 10);
  DP_EXPECT_EQ(parameters.at("effective_parameters").at("sample_probability"), 1.0);
  DP_EXPECT_EQ(parameters.at("dataset_node_count"), 96);
  DP_EXPECT_EQ(parameters.at("vector_dimension"), 4);
  DP_EXPECT_TRUE(!manifest.at("toolchain").at("compile_commands").empty());
  DP_EXPECT_TRUE(!manifest.at("toolchain").at("link_commands").empty());
  const auto original = dp::sha256_file(root / "workspace/reports/result.json");
  DP_EXPECT_EQ(invoke(root, config), 2);
  DP_EXPECT_EQ(dp::sha256_file(root / "workspace/reports/result.json"), original);

  unsigned index = 0;
  for (const auto& bad : std::vector<Json>{Json::object(), {{"K", 0}}, {{"K", 1}}, {{"K", -1}},
      {{"K", 97}}, {{"K", 4.0}}, {{"K", true}}, {{"K", "4"}}, {{"K", nullptr}},
      {{"K", 4294967296ULL}}, {{"K", 4}, {"R", 64}}, {{"K", 4}, {"seed", 1}}}) {
    const auto negative = base / ("fixed-k-invalid-" + std::to_string(index++));
    auto request = prepare(negative);
    request["partition"] = selected(kFixedK, bad);
    rejected(negative, request, "partition");
  }
  for (const auto& version : {"1.1.0", "", "unknown"}) {
    const auto negative = base / ("fixed-k-version-" + std::to_string(index++));
    auto request = prepare(negative);
    request["partition"] = selected(kFixedK, {{"K", 4}});
    request["partition"]["implementation_version"] = version;
    rejected(negative, request, "identity");
  }
  const auto empty = base / "fixed-k-empty-shard";
  auto request = prepare(empty);
  request["partition"] = selected(kFixedK, {{"K", 4}});
  const auto dataset_path = empty / "workspace/normal/dataset/artifact.json";
  auto dataset = f::read_json(dataset_path);
  const auto vectors = dataset_path.parent_path() / "vectors.bin";
  f::write_bytes(vectors, f::float_matrix(96, 4, std::vector<float>(384, 0)));
  dataset["dataset"]["dataset_checksum"] = dp::sha256_file(vectors);
  dataset["payloads"][0] = f::payload("vectors", "vectors.bin", native::kFloat32DatasetMediaType, vectors);
  dataset["aggregate_checksum"] = f::aggregate_for(dataset);
  f::write_text(dataset_path, dataset.dump());
  request["dataset"]["aggregate_checksum"] = dataset.at("aggregate_checksum");
  DP_EXPECT_EQ(invoke(empty, request), 4);
  const auto failure = f::read_json(empty / "workspace/reports/result.json");
  DP_EXPECT_EQ(failure.at("outcome"), "FAILED");
  DP_EXPECT_EQ(failure.at("stage_invocations"), Json({{"partition", 1}, {"local_build", 0}, {"merge", 0}}));
  DP_EXPECT_TRUE(!failure.contains("partition") && !failure.contains("global_graph"));
  const auto failed_manifest = f::read_json(empty / "workspace/manifests/small/stage-execution/partition.json");
  DP_EXPECT_EQ(failed_manifest.at("status"), "FAILED");
  DP_EXPECT_TRUE(failed_manifest.at("failure").at("reason").get<std::string>().find("empty") != std::string::npos);
  DP_EXPECT_EQ(failed_manifest.at("parameters").at("actual_training_sample_count"), 96);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto base = fs::path(std::getenv("COMPOSITION_TEST_OUTPUT_ROOT") ? std::getenv("COMPOSITION_TEST_OUTPUT_ROOT") : "/tmp/module-composition") / nonce;
    if (std::string(argv[1]) == "requests") requests(base);
    else if (std::string(argv[1]) == "native") native_runs(base);
    else if (std::string(argv[1]) == "fixed_k") fixed_k_runs(base);
    else return 2;
    std::cout << "COMPOSITION_VALIDATION_OUTPUT=" << base << '\n';
  } catch (const std::exception& error) {
    dp::test::fail("no unexpected exception", __FILE__, __LINE__, error.what());
  }
  return dp::test::finish();
}
