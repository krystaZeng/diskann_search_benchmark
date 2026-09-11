#include "baseline/cpp_main/stage_bindings.hpp"

#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/registry.hpp"
#include "support/test_assert.hpp"
#include "validation/parameter_validator.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;

namespace {

const std::filesystem::path kRepository = CPP_MAIN_TEST_REPOSITORY_ROOT;

dp::RuntimeServiceCapabilities services() {
  return {{"vector_accessor/v1", "metrics_sink/v1",
           "cancellation_token/v1"},
          false};
}

}  // namespace

int main() {
  const auto p0 = native::p0_descriptor();
  const auto b0 = native::b0_descriptor();
  const auto m0 = native::m0_descriptor();

  DP_EXPECT_EQ(p0.implementation_id,
               std::string(native::kP0ImplementationId));
  DP_EXPECT_EQ(b0.implementation_id,
               std::string(native::kB0ImplementationId));
  DP_EXPECT_EQ(m0.implementation_id,
               std::string(native::kM0ImplementationId));
  DP_EXPECT_TRUE(!p0.test_only && !b0.test_only && !m0.test_only);
  DP_EXPECT_EQ(p0.requires.input_artifacts.size(), std::size_t{1});
  DP_EXPECT_EQ(b0.requires.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_EQ(m0.requires.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_EQ(p0.provides.artifact_type, dp::ArtifactType::kPartition);
  DP_EXPECT_EQ(b0.provides.artifact_type, dp::ArtifactType::kLocalGraph);
  DP_EXPECT_EQ(m0.provides.artifact_type, dp::ArtifactType::kGlobalGraph);
  DP_EXPECT_EQ(p0.provides.output_id_space, dp::IdSpace::kLocal);
  DP_EXPECT_EQ(b0.provides.output_id_space, dp::IdSpace::kLocal);
  DP_EXPECT_EQ(m0.provides.output_id_space, dp::IdSpace::kGlobal);
  DP_EXPECT_TRUE(std::find(m0.provides.capabilities.begin(),
                           m0.provides.capabilities.end(),
                           "graph.medoids/v1") !=
                 m0.provides.capabilities.end());
  DP_EXPECT_EQ(
      m0.parameter_schema_path.generic_string(),
      std::string("contracts/baseline-v1/schemas/"
                  "cpp-main-m0-parameters-v1.1.0.schema.json"));
  const auto m0_schema_path = kRepository / m0.parameter_schema_path;
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(m0_schema_path));
  if (std::filesystem::is_regular_file(m0_schema_path)) {
    std::ifstream input(m0_schema_path, std::ios::binary);
    const auto schema = nlohmann::json::parse(input);
    DP_EXPECT_EQ(
        schema.at("$id").get<std::string>(),
        std::string("https://diskann-repro.local/contracts/baseline-v1/"
                    "cpp-main-m0-parameters-v1.1.0.schema.json"));
    const auto& degree =
        schema.at("$defs").at("merge").at("properties").at("R");
    DP_EXPECT_EQ(degree.at("minimum"), 1);
    DP_EXPECT_EQ(degree.at("maximum"), 4294967295ULL);
  }

  dp::detail::validate_implementation_parameters(
      p0,
      R"({"R":16,"build_DRAM_budget_gib":0.6,"replication_factor":2,"sample_probability":0.256})",
      kRepository);
  dp::detail::validate_implementation_parameters(
      b0,
      R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})",
      kRepository);
  dp::detail::validate_implementation_parameters(
      m0, R"({"R":16,"use_filters":false})", kRepository);
  try {
    dp::detail::validate_implementation_parameters(
        m0, R"({"R":7,"use_filters":false})", kRepository);
  } catch (const std::exception& error) {
    dp::test::fail("M0 accepts R=7", __FILE__, __LINE__, error.what());
  }
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      p0, R"({"R":16})", kRepository));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      b0,
      R"({"L":"32","R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})",
      kRepository));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      m0, R"({"R":16,"use_filters":false,"extra":true})", kRepository));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      m0, R"({"use_filters":false})", kRepository));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      m0, R"({"R":7.5,"use_filters":false})", kRepository));

  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  native::register_cpp_main_stage_bindings(registry);
  DP_EXPECT_EQ(registry.size(), std::size_t{3});
  DP_EXPECT_THROW(native::register_cpp_main_stage_bindings(registry));
  DP_EXPECT_THROW(registry.resolve_partition_factory("missing", "1.0.0"));

  dp::CompatibilityChecker checker;
  const std::vector<dp::AvailableArtifactContract> dataset{
      {dp::ArtifactType::kDataset,
       "diskann-float32-dataset/v1",
       dp::IdSpace::kNone,
       {"dataset.diskann_float32_bin/v1"}}};
  DP_EXPECT_TRUE(checker.check(p0, dataset, services()).compatible);
  auto available = dataset;
  available.push_back({dp::ArtifactType::kPartition,
                       "partition-artifact/v1",
                       dp::IdSpace::kLocal,
                       {"partition.complete_local_to_global_map/v1",
                        "partition.local_to_global_ascending/v1"}});
  DP_EXPECT_TRUE(checker.check(b0, available, services()).compatible);
  available.push_back({dp::ArtifactType::kLocalGraph,
                       "local-graph-artifact/v1",
                       dp::IdSpace::kLocal,
                       {"graph.local_adjacency/v1"}});
  DP_EXPECT_TRUE(checker.check(m0, available, services()).compatible);

  auto missing = available;
  missing.erase(missing.begin());
  DP_EXPECT_TRUE(!checker.check(b0, missing, services()).compatible);
  auto wrong_version = available;
  wrong_version[1].format_version = "partition-artifact/v2";
  DP_EXPECT_TRUE(!checker.check(b0, wrong_version, services()).compatible);
  auto wrong_id_space = available;
  wrong_id_space[1].id_space = dp::IdSpace::kGlobal;
  DP_EXPECT_EQ(checker.check(b0, wrong_id_space, services())
                   .failure->category,
               dp::ErrorCategory::kIdSpaceMismatch);
  auto missing_capability = available;
  missing_capability[1].capabilities.clear();
  DP_EXPECT_TRUE(!checker.check(b0, missing_capability, services()).compatible);
  DP_EXPECT_TRUE(!checker.check(b0, available, {{}, false}).compatible);

  int factory_invocations = 0;
  dp::ImplementationRegistry counted(dp::RegistryAdmissionPolicy::kProduction);
  counted.register_local_build(b0, [&] {
    ++factory_invocations;
    return native::make_b0_local_build();
  });
  if (checker.check(b0, missing, services()).compatible) {
    counted.resolve_local_build_factory(native::kB0ImplementationId,
                                        "1.0.0")();
  }
  DP_EXPECT_EQ(factory_invocations, 0);
  return dp::test::finish();
}
