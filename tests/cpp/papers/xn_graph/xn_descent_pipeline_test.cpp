#include "papers/xn_graph/xn_descent/stage_binding.hpp"
#include "papers/xn_graph/xn_descent/parameters.hpp"

#include "baseline/cpp_main/source_build_identity.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/registry.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dp = diskann_pipeline;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::cpp_main::test;
namespace xn = diskann_pipeline::xn_descent;
using Json = nlohmann::json;

namespace {

struct FactoryCounts {
  std::size_t partition{0};
  std::size_t local_build{0};
  std::size_t merge{0};
};

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Json local_parameters(const std::string& profile, std::uint32_t lambda) {
  return {{"schema_version", xn::kParameterSchemaVersion},
          {"validation_profile", profile},
          {"omega", 4},
          {"lambda", lambda},
          {"alpha", 1.2},
          {"seed", 42},
          {"iteration_cap", 128},
          {"prng_policy", xn::kPrngPolicy},
          {"sampling_policy", xn::kSamplingPolicy},
          {"tie_break_policy", xn::kTieBreakPolicy},
          {"round_model", xn::kRoundModel},
          {"convergence_rule", xn::kConvergenceRule},
          {"entry_point_rule", xn::kEntryPointRule},
          {"distance_implementation", xn::kDistanceImplementation},
          {"reproduction_mode", xn::kReproductionMode}};
}

Json request(const std::filesystem::path& workspace,
             const dp::ArtifactRef& dataset, const std::string& run_id,
             const std::string& profile, std::uint32_t lambda) {
  return {{"request_version", "1.0.0"},
          {"operation", "execute"},
          {"request_id", run_id},
          {"study_class", "single_paper_reproduction"},
          {"reproduction_mode", "faithful_adaptation"},
          {"replacement_scope", "local_build_only"},
          {"profile", profile},
          {"workspace",
           {{"root", std::filesystem::absolute(workspace).string()},
            {"memory_budget_bytes", 1024ULL * 1024ULL * 1024ULL},
            {"thread_budget", 2}}},
          {"dataset", Json::parse(dp::serialize_artifact_ref(dataset))},
          {"implementations",
           {{"partition", native::kP0ImplementationId},
            {"local_build", xn::kImplementationId},
            {"merge", native::kM0ImplementationId}}},
          {"stage_parameters",
           {{"partition",
             {{"R", 16},
              {"build_DRAM_budget_gib", 0.6},
              {"replication_factor", 2},
              {"sample_probability", 1.0}}},
            {"local_build", local_parameters(profile, lambda)},
            {"merge", {{"R", 16}, {"use_filters", false}}}}},
          {"source_config",
           {{"yaml_sha256", std::string(64, 'a')},
            {"normalized_json_sha256", std::string(64, 'b')}}}};
}

xn::ExperimentStageFactories counting_factories(FactoryCounts& counts) {
  return {[&counts] {
            ++counts.partition;
            return native::make_p0_partition();
          },
          [&counts] {
            ++counts.local_build;
            return xn::make_xn_descent_local_build();
          },
          [&counts] {
            ++counts.merge;
            return native::make_m0_merge();
          }};
}

void expect_zero_invocations(const xn::ExperimentRunnerResult& result,
                             const FactoryCounts& factories) {
  DP_EXPECT_EQ(result.execution.invocations.partition, std::size_t{0});
  DP_EXPECT_EQ(result.execution.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(result.execution.invocations.merge, std::size_t{0});
  DP_EXPECT_EQ(factories.partition, std::size_t{0});
  DP_EXPECT_EQ(factories.local_build, std::size_t{0});
  DP_EXPECT_EQ(factories.merge, std::size_t{0});
  const auto document = Json::parse(result.result_json);
  DP_EXPECT_EQ(document.at("stage_invocations").at("partition"), 0);
  DP_EXPECT_EQ(document.at("stage_invocations").at("local_build"), 0);
  DP_EXPECT_EQ(document.at("stage_invocations").at("merge"), 0);
}

void test_descriptor_and_composition() {
  const auto descriptor = xn::descriptor();
  DP_EXPECT_EQ(descriptor.implementation_id,
               std::string("xn_graph_xn_descent_local_build_v1"));
  DP_EXPECT_EQ(descriptor.implementation_version, std::string("1.0.0"));
  DP_EXPECT_EQ(descriptor.stage, dp::StageKind::kLocalBuild);
  DP_EXPECT_EQ(descriptor.requires.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[0].artifact_type,
               dp::ArtifactType::kDataset);
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[0].accepted_format_versions,
               std::vector<std::string>{"diskann-float32-dataset/v1"});
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[0].input_id_space,
               dp::IdSpace::kNone);
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[1].artifact_type,
               dp::ArtifactType::kPartition);
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[1].accepted_format_versions,
               std::vector<std::string>{"partition-artifact/v1"});
  DP_EXPECT_EQ(descriptor.requires.input_artifacts[1].input_id_space,
               dp::IdSpace::kLocal);
  DP_EXPECT_EQ(
      descriptor.requires.required_capabilities,
      (std::vector<std::string>{
          "dataset.diskann_float32_bin/v1",
          "partition.complete_local_to_global_map/v1"}));
  DP_EXPECT_EQ(
      descriptor.requires.required_services,
      (std::vector<std::string>{"vector_accessor/v1",
                                "distance_function/l2-v1",
                                "cancellation_token/v1",
                                "metrics_sink/v1"}));
  DP_EXPECT_EQ(descriptor.provides.artifact_type,
               dp::ArtifactType::kLocalGraph);
  DP_EXPECT_EQ(descriptor.provides.format_version,
               std::string("local-graph-artifact/v1"));
  DP_EXPECT_EQ(descriptor.provides.output_id_space, dp::IdSpace::kLocal);
  DP_EXPECT_EQ(descriptor.provides.capabilities,
               std::vector<std::string>{"graph.local_adjacency/v1"});
  DP_EXPECT_EQ(
      descriptor.parameter_schema_path.generic_string(),
      std::string("contracts/papers/xn-graph-xn-descent-v1/schemas/"
                  "xn-descent-parameters.schema.json"));
  DP_EXPECT_TRUE(!descriptor.test_only);

  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_partition(native::p0_descriptor(),
                              native::make_p0_partition);
  registry.register_local_build(descriptor,
                                xn::make_xn_descent_local_build);
  registry.register_merge(native::m0_descriptor(), native::make_m0_merge);
  registry.seal();
  DP_EXPECT_EQ(registry.size(), std::size_t{3});
  DP_EXPECT_EQ(registry.resolve_descriptor(native::kP0ImplementationId,
                                           "1.0.0",
                                           dp::StageKind::kPartition)
                   .implementation_id,
               std::string(native::kP0ImplementationId));
  DP_EXPECT_EQ(registry.resolve_descriptor(xn::kImplementationId, "1.0.0",
                                           dp::StageKind::kLocalBuild)
                   .implementation_id,
               std::string(xn::kImplementationId));
  DP_EXPECT_EQ(registry.resolve_descriptor(native::kM0ImplementationId,
                                           "1.0.0", dp::StageKind::kMerge)
                   .implementation_id,
               std::string(native::kM0ImplementationId));
  DP_EXPECT_THROW(registry.resolve_descriptor(native::kB0ImplementationId,
                                              "1.0.0",
                                              dp::StageKind::kLocalBuild));
}

void test_preflight_rejections() {
  fixture::TemporaryDirectory temporary("xn-preflight");
  const dp::ArtifactRef placeholder{
      "dataset/artifact.json", dp::ArtifactType::kDataset,
      "diskann-float32-dataset/v1", std::string(64, 'c')};

  const auto lambda_workspace = temporary.path / "lambda-workspace";
  FactoryCounts lambda_counts;
  const auto lambda_factories = counting_factories(lambda_counts);
  const auto lambda_result = xn::execute_pipeline_experiment_request(
      request(lambda_workspace, placeholder, "xn-lambda-preflight",
              "pipeline-compatible", 11)
          .dump(),
      &lambda_factories);
  DP_EXPECT_EQ(lambda_result.execution.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(lambda_result.execution.failure.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(lambda_workspace));
  expect_zero_invocations(lambda_result, lambda_counts);

  const auto paper_workspace = temporary.path / "paper-workspace";
  FactoryCounts paper_counts;
  const auto paper_factories = counting_factories(paper_counts);
  const auto paper_result = xn::execute_pipeline_experiment_request(
      request(paper_workspace, placeholder, "xn-paper-merge-preflight",
              "paper-style", 64)
          .dump(),
      &paper_factories);
  DP_EXPECT_EQ(paper_result.execution.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(paper_result.execution.failure.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(paper_workspace));
  expect_zero_invocations(paper_result, paper_counts);
}

class MissingOutputLocal final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef&, dp::PartitionId,
                        const dp::LocalBuildConfig&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class CorruptingLocal final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef& partition,
                        dp::PartitionId partition_id,
                        const dp::LocalBuildConfig& config,
                        dp::StageContext& context) override {
    auto result = xn::make_xn_descent_local_build()->build(
        partition, partition_id, config, context);
    if (result.status != dp::StageStatus::kSucceeded ||
        !result.output_candidate) {
      return result;
    }
    const auto metadata_path = dp::resolve_confined_path(
        context.temporary_storage.stage_root,
        result.output_candidate->metadata_path);
    const auto metadata = Json::parse(read_text(metadata_path));
    const auto payload = std::find_if(
        metadata.at("payloads").begin(), metadata.at("payloads").end(),
        [](const auto& item) { return item.at("role") == "local_graph"; });
    if (payload == metadata.at("payloads").end()) {
      throw std::runtime_error("XN corrupt fixture cannot find local graph");
    }
    const auto graph_path = dp::resolve_confined_path(
        metadata_path.parent_path(),
        payload->at("path").template get<std::string>());
    std::fstream graph(graph_path,
                       std::ios::binary | std::ios::in | std::ios::out);
    char byte = 0;
    graph.read(&byte, 1);
    byte ^= 0x01;
    graph.seekp(0);
    graph.write(&byte, 1);
    if (!graph) throw std::runtime_error("XN corrupt fixture write failed");
    return result;
  }
};

struct SmallRun {
  std::unique_ptr<fixture::TemporaryDirectory> temporary;
  xn::ExperimentRunnerResult result;
};

SmallRun execute_small(
    const std::string& label, dp::LocalBuildFactory local_factory = {}) {
  auto temporary = std::make_unique<fixture::TemporaryDirectory>(label);
  const auto workspace =
      dp::create_workspace(temporary->path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 96, 4);
  const auto document = request(workspace.root, dataset.reference, label,
                                "pipeline-compatible", 4);
  if (!local_factory) {
    return {std::move(temporary),
            xn::execute_pipeline_experiment_request(document.dump())};
  }
  xn::ExperimentStageFactories factories{native::make_p0_partition,
                                         std::move(local_factory),
                                         native::make_m0_merge};
  return {std::move(temporary),
          xn::execute_pipeline_experiment_request(document.dump(),
                                                  &factories)};
}

void test_small_pipeline_and_validation_gate() {
  const auto success_run = execute_small("xn-small-success");
  const auto& success = success_run.result;
  if (success.execution.failure) {
    std::cerr << "XN small pipeline failure: "
              << success.execution.failure->reason << '\n';
  }
  DP_EXPECT_EQ(success.execution.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(success.execution.invocations.partition, std::size_t{1});
  DP_EXPECT_TRUE(success.execution.invocations.local_build >= std::size_t{2});
  DP_EXPECT_EQ(success.execution.invocations.local_build,
               success.execution.local_artifacts.size());
  DP_EXPECT_EQ(success.execution.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(success.execution.partition_artifact.has_value());
  DP_EXPECT_TRUE(success.execution.artifact.has_value());
  DP_EXPECT_TRUE(success.execution.evaluation_report_path.has_value());

  std::size_t valid_local_evidence = 0;
  for (const auto& path : success.execution.validation_evidence_paths) {
    const auto evidence = dp::parse_validation_evidence(read_text(path));
    if (evidence.artifact_candidate.artifact_type ==
            dp::ArtifactType::kLocalGraph &&
        evidence.result == dp::ValidationResult::kValid) {
      ++valid_local_evidence;
    }
  }
  DP_EXPECT_EQ(valid_local_evidence,
               success.execution.local_artifacts.size());
  const auto result_document = Json::parse(success.result_json);
  DP_EXPECT_EQ(result_document.at("claim_scope"),
               "component-level_faithful_adaptation");
  DP_EXPECT_EQ(result_document.at("replacement_scope"), "local_build_only");
  DP_EXPECT_EQ(result_document.at("full_xn_graph_system_reproduction"), false);

  const auto partial_run = execute_small(
      "xn-small-partial",
      [] { return std::make_unique<MissingOutputLocal>(); });
  const auto& partial = partial_run.result;
  DP_EXPECT_EQ(partial.execution.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(partial.execution.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(partial.execution.invocations.local_build, std::size_t{1});
  DP_EXPECT_EQ(partial.execution.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partial.execution.artifact.has_value());

  const auto invalid_run = execute_small(
      "xn-small-invalid",
      [] { return std::make_unique<CorruptingLocal>(); });
  const auto& invalid = invalid_run.result;
  DP_EXPECT_EQ(invalid.execution.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(invalid.execution.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(invalid.execution.invocations.local_build, std::size_t{1});
  DP_EXPECT_EQ(invalid.execution.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!invalid.execution.artifact.has_value());
  DP_EXPECT_TRUE(invalid.execution.quarantine_id.has_value());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    throw std::runtime_error(
        "usage: xn_descent_pipeline_test preflight|small_pipeline");
  }
  const std::string mode = argv[1];
  if (mode == "preflight") {
    test_descriptor_and_composition();
    test_preflight_rejections();
  } else if (mode == "small_pipeline") {
    test_small_pipeline_and_validation_gate();
  } else {
    throw std::runtime_error("unknown XN pipeline test mode");
  }
  return dp::test::finish();
}
