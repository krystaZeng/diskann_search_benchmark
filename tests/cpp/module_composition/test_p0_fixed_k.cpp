#include "baseline/cpp_main/p0_fixed_k_partition.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "support/test_assert.hpp"
#include "validation/parameter_validator.hpp"

#include <mkl.h>
#include <omp.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>

namespace dp = diskann_pipeline;
namespace fk = dp::cpp_main::fixed_k;
namespace native = dp::cpp_main;
namespace f = native::test;
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void unit() {
  for (const auto k : {2U, 4U, 96U}) {
    DP_EXPECT_EQ(fk::parse_parameters(Json{{"K", k}}.dump(), 96).k, k);
    dp::detail::validate_implementation_parameters(fk::descriptor(), Json{{"K", k}}.dump(), FIXED_K_TEST_ROOT);
  }
  for (const auto& text : {"{}", R"({"K":0})", R"({"K":1})", R"({"K":-1})", R"({"K":97})",
      R"({"K":4.0})", R"({"K":true})", R"({"K":"4"})", R"({"K":null})", R"({"K":4294967296})",
      R"({"K":18446744073709551615})", R"({"K":4,"R":64})", R"({"K":4,"seed":1})",
      R"({"K":4,"max_k_means_reps":15})", R"({"K":4,"k_base":1})", R"({"K":4,"sample_probability":1})"}) {
    DP_EXPECT_THROW(fk::parse_parameters(text, 96));
  }
  DP_EXPECT_THROW(fk::parse_parameters(R"({"K":2})", 0));
  DP_EXPECT_THROW(fk::parse_parameters(R"({"K":2})", 4294967296ULL));
  DP_EXPECT_THROW(fk::parse_parameters(R"({"K":2})", 96, "1.1.0"));
  DP_EXPECT_EQ(fk::sample_probability(96), 1.0);
  DP_EXPECT_EQ(fk::sample_probability(256000), 1.0);
  DP_EXPECT_TRUE(std::abs(fk::sample_probability(1000000) - 0.256) < 1e-12);
  DP_EXPECT_TRUE(std::abs(fk::sample_probability(10000000) - 0.0256) < 1e-12);
  DP_EXPECT_THROW(fk::sample_probability(0));
  fk::validate_native_shape(10000000, 128, 4); // Arithmetic only, no Dataset/allocation.
  DP_EXPECT_THROW(fk::validate_native_shape(96, 0, 4));
  fk::validate_native_shape(96, 2147483648U, 4); // Fixed upstream uses MKL ILP64; no allocation.
  DP_EXPECT_THROW(fk::validate_native_shape(4294967295ULL, 2147483647U, 4));
  DP_EXPECT_THROW(fk::validate_training_sample_count(0, 4));
  DP_EXPECT_THROW(fk::validate_training_sample_count(3, 4));
  DP_EXPECT_THROW(fk::validate_training_sample_count(8388609, 4));
  fk::validate_training_sample_count(4, 4);
  fk::validate_training_sample_count(8388608, 4); // Counts only, no large allocation.
  DP_EXPECT_TRUE(fk::descriptor().provides.capabilities == native::p0_descriptor().provides.capabilities);
  DP_EXPECT_TRUE(fk::descriptor().implementation_id != native::kP0ImplementationId);
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  native::register_cpp_main_stage_bindings(registry);
  DP_EXPECT_EQ(registry.size(), std::size_t{3});
  DP_EXPECT_THROW(registry.resolve_partition_factory(fk::kImplementationId, "1.0.0"));

  f::TemporaryDirectory temporary("fixed-k-output");
  const auto prefix = temporary.path / "p0";
  dp::DatasetIdentity dataset{"small", std::string(64, 'a'), 4, 2, "float32", "l2"};
  const auto first = prefix.string() + "_subshard-0_ids_uint32.bin";
  const auto second = prefix.string() + "_subshard-1_ids_uint32.bin";
  const auto centers = prefix.string() + "_centroids.bin";
  const auto reset = [&] {
    f::write_bytes(centers, f::float_matrix(2, 2, {0, 0, 1, 1}));
    f::write_bytes(first, f::id_matrix({0, 1, 2, 3}));
    f::write_bytes(second, f::id_matrix({0, 1, 2, 3}));
  };
  reset();
  DP_EXPECT_EQ(fk::validate_native_output(prefix, dataset, 2), std::vector<dp::NodeCount>({4, 4}));
  for (const auto& ids : std::vector<std::vector<dp::GlobalNodeId>>{{}, {0, 1, 2}, {0, 1, 2, 4}, {0, 2, 1, 3}, {0, 1, 2, 2}}) {
    reset(); f::write_bytes(second, f::id_matrix(ids));
    DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  }
  reset(); auto truncated = f::id_matrix({0, 1, 2, 3}); truncated.pop_back(); f::write_bytes(second, truncated);
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  reset(); fs::remove(second);
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  reset();
  const auto extra = prefix.string() + "_subshard-2_ids_uint32.bin";
  f::write_bytes(extra, f::id_matrix({0}));
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  fs::remove(extra);
  reset(); f::write_bytes(centers, f::float_matrix(2, 2, {0, 0, 1, std::numeric_limits<float>::infinity()}));
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  reset(); f::write_bytes(centers, f::float_matrix(2, 1, {0, 1}));
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 2));
  // A total of 2N alone does not prove two memberships per node.
  f::write_bytes(centers, f::float_matrix(3, 2, {0, 0, 1, 1, 2, 2}));
  f::write_bytes(first, f::id_matrix({0, 1, 2}));
  f::write_bytes(second, f::id_matrix({0, 1, 3}));
  f::write_bytes(extra, f::id_matrix({0, 2}));
  DP_EXPECT_THROW(fk::validate_native_output(prefix, dataset, 3));
}

class CallbackMetrics final : public dp::MetricsSink {
 public:
  std::function<void(const std::string&)> callback;
  void event(const std::string& value) override { callback(value); }
  void increment(const std::string&, std::uint64_t) override {}
};

void inspect(const dp::StageResult& result, f::TestContext& environment,
             native::CppMainVectorAccessor& vectors, unsigned k) {
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  if (!result.output_candidate) return;
  auto staging = f::candidate_workspace(environment.context);
  DP_EXPECT_TRUE(dp::Validator(staging).validate(*result.output_candidate, {vectors.validated_dataset()}).valid());
  const auto path = staging.normal_root / result.output_candidate->metadata_path;
  const auto meta = f::read_json(path);
  DP_EXPECT_EQ(meta.at("structural").at("partition_count"), k);
  DP_EXPECT_EQ(meta.at("payloads").size(), std::size_t(k + 1));
  std::vector<float> centers(k * 4);
  std::ifstream input(path.parent_path() / "centroids.bin", std::ios::binary);
  input.seekg(8); input.read(reinterpret_cast<char*>(centers.data()), centers.size() * sizeof(float));
  DP_EXPECT_TRUE(bool(input));
  std::vector<unsigned> owners(96, 0);
  std::uint64_t total = 0;
  for (unsigned c = 0; c < k; ++c) {
    const auto count = meta.at("structural").at("partitions").at(c).at("local_node_count").get<dp::NodeCount>();
    DP_EXPECT_TRUE(count > 0);
    const auto map = native::validate_partition_id_map(path.parent_path() / ("partition-" + std::to_string(c) + "-map.bin"), count, 96);
    DP_EXPECT_TRUE(std::is_sorted(map.begin(), map.end()));
    total += count;
    for (auto id : map) {
      ++owners[id];
      std::vector<float> point; vectors.read(id, point);
      std::vector<double> distances(k, 0);
      for (unsigned center = 0; center < k; ++center) for (unsigned d = 0; d < 4; ++d) {
        const double delta = double(point[d]) - centers[center * 4 + d];
        distances[center] += delta * delta;
      }
      const auto assigned = distances[c];
      std::sort(distances.begin(), distances.end());
      DP_EXPECT_TRUE(assigned <= distances[1] + 1e-4 * std::max(1.0, distances[1]));
    }
  }
  DP_EXPECT_EQ(total, std::uint64_t{192});
  DP_EXPECT_TRUE(std::all_of(owners.begin(), owners.end(), [](unsigned count) { return count == 2; }));
  const auto manifest = f::read_json(result.build_manifest_path);
  DP_EXPECT_EQ(manifest.at("status"), "SUCCEEDED");
  DP_EXPECT_EQ(manifest.at("implementation").at("implementation_id"), fk::kImplementationId);
  DP_EXPECT_EQ(manifest.at("source").at("commit"), native::kPinnedCommit);
  const auto& p = manifest.at("parameters");
  DP_EXPECT_EQ(p.at("requested_parameters"), Json({{"K", k}}));
  DP_EXPECT_EQ(p.at("actual_training_sample_count"), 96);
  DP_EXPECT_EQ(p.at("actual_partition_count"), k);
  DP_EXPECT_EQ(p.at("total_memberships"), total);
  DP_EXPECT_EQ(p.at("partition_memberships").size(), k);
  DP_EXPECT_EQ(p.at("effective_parameters").at("max_k_means_reps"), 10);
  DP_EXPECT_EQ(p.at("effective_parameters").at("k_base"), 2);
  DP_EXPECT_EQ(p.at("effective_parameters").at("sample_probability"), 1.0);
  DP_EXPECT_EQ(p.at("runner_binary_sha256"), dp::sha256_file("/proc/self/exe"));
  DP_EXPECT_TRUE(!manifest.at("toolchain").at("compile_commands").empty());
  DP_EXPECT_TRUE(!manifest.at("toolchain").at("link_commands").empty());
  for (const auto& item : fs::directory_iterator(environment.context.temporary_storage.stage_root / "native"))
    DP_EXPECT_TRUE(item.path().filename().string().find("_ids_uint32.bin") != std::string::npos ||
                   item.path().filename() == "p0-fixed-k_centroids.bin");

  // Only corrupt an isolated copy; published/candidate data stays unchanged.
  const auto bad = dp::create_workspace(environment.context.temporary_storage.stage_root / "corrupt-copy");
  fs::copy(path.parent_path(), bad.normal_root / "partition", fs::copy_options::recursive);
  auto ref = *result.output_candidate; ref.metadata_path = "partition/artifact.json";
  auto changed = meta;
  changed["source_artifacts"][0]["aggregate_checksum"] = std::string(64, 'f');
  changed["aggregate_checksum"] = f::aggregate_for(changed);
  ref.aggregate_checksum = changed.at("aggregate_checksum");
  f::write_text(bad.normal_root / ref.metadata_path, changed.dump());
  DP_EXPECT_TRUE(!dp::Validator(bad).validate(ref, {vectors.validated_dataset()}).valid());
  f::write_text(bad.normal_root / ref.metadata_path, meta.dump());
  ref.aggregate_checksum = result.output_candidate->aggregate_checksum;
  std::ofstream(bad.normal_root / "partition/centroids.bin", std::ios::app) << 'x';
  DP_EXPECT_TRUE(!dp::Validator(bad).validate(ref, {vectors.validated_dataset()}).valid());
  std::cout << "fixed K=" << k << " nodes=96 samples=96 memberships=" << total << " nearest-two=PASS\n";
}

void native_runs(const fs::path& base) {
  const auto workspace = dp::create_workspace(base / "workspace");
  f::NativeDatasetFixture dataset(workspace, 96, 4);
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  const auto evidence = native::admit_source_and_build(native::configured_source_build_admission());
  DP_EXPECT_THROW(fk::make_partition(evidence, "1.1.0"));
  for (unsigned k : {2U, 4U}) {
    const auto run = "fixed-k" + std::to_string(k);
    f::TestContext environment(workspace, run);
    environment.context.vector_accessor = &vectors;
    const auto parameter_path = workspace.manifest_root / run / "parameters/partition.json";
    f::write_text(parameter_path, Json{{"K", k}}.dump());
    const auto saved_omp = omp_get_max_threads();
    const auto saved_mkl = mkl_set_num_threads_local(3);
    omp_set_num_threads(3);
    const auto result = fk::make_partition(evidence)->run(dataset.reference, parameter_path, environment.context);
    DP_EXPECT_EQ(omp_get_max_threads(), 3);
    DP_EXPECT_EQ(mkl_get_max_threads(), 3);
    omp_set_num_threads(saved_omp); mkl_set_num_threads_local(saved_mkl);
    inspect(result, environment, vectors, k);
    const auto before = dp::sha256_file(result.build_manifest_path);
    DP_EXPECT_THROW(fk::make_partition(evidence)->run(dataset.reference, parameter_path, environment.context));
    DP_EXPECT_EQ(dp::sha256_file(result.build_manifest_path), before);
  }
  {
    const auto tied_workspace = dp::create_workspace(base / "tied-workspace");
    f::NativeDatasetFixture tied(tied_workspace, 96, 4);
    const auto path = tied_workspace.normal_root / "dataset/artifact.json";
    auto meta = f::read_json(path);
    const auto payload = path.parent_path() / "vectors.bin";
    f::write_bytes(payload, f::float_matrix(96, 4, std::vector<float>(384, 0)));
    meta["dataset"]["dataset_checksum"] = dp::sha256_file(payload);
    meta["payloads"][0] = f::payload("vectors", "vectors.bin", native::kFloat32DatasetMediaType, payload);
    meta["aggregate_checksum"] = f::aggregate_for(meta);
    f::write_text(path, meta.dump());
    tied.reference.aggregate_checksum = meta.at("aggregate_checksum");
    native::CppMainVectorAccessor tied_vectors(tied_workspace, tied.reference);
    f::TestContext environment(tied_workspace, "tied-k2");
    environment.context.vector_accessor = &tied_vectors;
    const auto parameters = tied_workspace.manifest_root / "tied-k2/parameters/partition.json";
    f::write_text(parameters, R"({"K":2})");
    const auto result = fk::make_partition(evidence)->run(tied.reference, parameters, environment.context);
    inspect(result, environment, tied_vectors, 2);
  }
  unsigned index = 0;
  for (const auto& text : {"{}", R"({"K":1})", R"({"K":4.0})", R"({"K":true})", R"({"K":97})", R"({"K":4,"seed":1})"}) {
    const auto run = "invalid-" + std::to_string(index++);
    f::TestContext environment(workspace, run);
    environment.context.vector_accessor = &vectors;
    const auto path = workspace.manifest_root / run / "parameters/partition.json";
    f::write_text(path, text);
    const auto result = fk::make_partition(evidence)->run(dataset.reference, path, environment.context);
    DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!result.output_candidate);
    DP_EXPECT_TRUE(!environment.metrics.saw("cpp_main.p0_fixed_k.gen_random_slice"));
    DP_EXPECT_TRUE(!f::read_json(result.build_manifest_path).at("failure").at("reason").get<std::string>().empty());
  }
  for (const auto& scenario : {"cancel-before", "cancel-between", "corrupt-output", "occupied-native", "dangling-native"}) {
    f::TestContext environment(workspace, scenario);
    environment.context.vector_accessor = &vectors;
    const auto path = workspace.manifest_root / scenario / "parameters/partition.json";
    f::write_text(path, R"({"K":4})");
    CallbackMetrics metrics;
    metrics.callback = [&](const std::string& event) {
      if (event == "cpp_main.p0_fixed_k.run_lloyds" && std::string(scenario) == "cancel-between")
        environment.cancellation.cancelled = true;
      if (event == "cpp_main.p0_fixed_k.validate_output" && std::string(scenario) == "corrupt-output")
        f::write_bytes(environment.context.temporary_storage.stage_root / "native/p0-fixed-k_subshard-0_ids_uint32.bin", f::id_matrix({96}));
    };
    environment.context.metrics_sink = &metrics;
    const auto native_dir = environment.context.temporary_storage.stage_root / "native";
    if (std::string(scenario) == "cancel-before") environment.cancellation.cancelled = true;
    if (std::string(scenario) == "occupied-native") f::write_text(native_dir, "preserve");
    if (std::string(scenario) == "dangling-native") fs::create_symlink(base / "absent", native_dir);
    const auto previous_omp = omp_get_max_threads();
    const auto previous_mkl = mkl_set_num_threads_local(3);
    omp_set_num_threads(3);
    const auto result = fk::make_partition(evidence)->run(dataset.reference, path, environment.context);
    DP_EXPECT_EQ(omp_get_max_threads(), 3);
    DP_EXPECT_EQ(mkl_get_max_threads(), 3);
    omp_set_num_threads(previous_omp); mkl_set_num_threads_local(previous_mkl);
    DP_EXPECT_EQ(result.status, std::string(scenario).find("cancel") == 0 ? dp::StageStatus::kCancelled : dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!result.output_candidate);
    DP_EXPECT_TRUE(!f::read_json(result.build_manifest_path).at("failure").at("reason").get<std::string>().empty());
    if (std::string(scenario) == "occupied-native") DP_EXPECT_EQ(dp::sha256_file(native_dir), dp::sha256_bytes("preserve"));
    if (std::string(scenario) == "dangling-native") {
      DP_EXPECT_TRUE(fs::is_symlink(native_dir));
      DP_EXPECT_TRUE(!fs::exists(base / "absent"));
    }
  }
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2) return 2;
    if (std::string(argv[1]) == "unit") unit();
    else if (std::string(argv[1]) == "native") {
      const auto nonce = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      const auto base = fs::path(std::getenv("FIXED_K_TEST_OUTPUT_ROOT") ? std::getenv("FIXED_K_TEST_OUTPUT_ROOT") : "/tmp/p0-fixed-k") / nonce;
      native_runs(base);
      std::cout << "FIXED_K_VALIDATION_OUTPUT=" << base << '\n';
    } else return 2;
  } catch (const std::exception& error) {
    dp::test::fail("no unexpected exception", __FILE__, __LINE__, error.what());
  }
  return dp::test::finish();
}
