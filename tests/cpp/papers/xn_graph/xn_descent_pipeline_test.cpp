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
#include <limits>
#include <memory>
#include <optional>
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
             const std::string& profile, std::uint32_t lambda,
             Json merge_r = 16) {
  const auto fixture_path =
      std::filesystem::path(XN_DESCENT_TEST_PROJECT_ROOT) /
      "tests/fixtures/xn_descent/pipeline-compatible-request.json";
  auto document = Json::parse(read_text(fixture_path));
  document["request_id"] = run_id;
  document["profile"] = profile;
  document["workspace"]["root"] =
      std::filesystem::absolute(workspace).string();
  document["dataset"] = Json::parse(dp::serialize_artifact_ref(dataset));
  document["stage_parameters"]["local_build"] =
      local_parameters(profile, lambda);
  document["stage_parameters"]["merge"]["R"] = std::move(merge_r);
  return document;
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
                  "xn-descent-parameters-v1.1.0.schema.json"));
  DP_EXPECT_EQ(
      native::m0_descriptor().parameter_schema_path.generic_string(),
      std::string("contracts/baseline-v1/schemas/"
                  "cpp-main-m0-parameters-v1.1.0.schema.json"));
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

  const auto expect_merge_rejected =
      [&](const std::string& label, const std::optional<Json>& merge_r) {
        const auto workspace = temporary.path / (label + "-workspace");
        FactoryCounts counts;
        const auto factories = counting_factories(counts);
        auto document = request(workspace, placeholder, label,
                                "pipeline-compatible", 12, 7);
        if (merge_r) {
          document["stage_parameters"]["merge"]["R"] = *merge_r;
        } else {
          document["stage_parameters"]["merge"].erase("R");
        }
        const auto result = xn::execute_pipeline_experiment_request(
            document.dump(), &factories);
        DP_EXPECT_EQ(result.execution.status, dp::StageStatus::kFailed);
        DP_EXPECT_TRUE(result.execution.failure.has_value());
        if (result.execution.failure) {
          DP_EXPECT_TRUE(result.execution.failure->reason.find("merge.R") !=
                         std::string::npos);
        }
        DP_EXPECT_TRUE(!std::filesystem::exists(workspace));
        expect_zero_invocations(result, counts);
      };
  expect_merge_rejected("xn-merge-missing", std::nullopt);
  expect_merge_rejected("xn-merge-bool", Json(true));
  expect_merge_rejected("xn-merge-string", Json("7"));
  expect_merge_rejected("xn-merge-fraction", Json(7.5));
  expect_merge_rejected("xn-merge-negative", Json(-1));
  expect_merge_rejected("xn-merge-zero", Json(0));
  expect_merge_rejected(
      "xn-merge-overflow",
      Json(static_cast<std::uint64_t>(
               std::numeric_limits<std::uint32_t>::max()) +
           1ULL));

  const auto semantic_workspace = temporary.path / "semantic-workspace";
  FactoryCounts semantic_counts;
  const auto semantic_factories = counting_factories(semantic_counts);
  const auto semantic_result = xn::execute_pipeline_experiment_request(
      request(semantic_workspace, placeholder, "xn-semantic-preflight",
              "semantic-small", 4, 7)
          .dump(),
      &semantic_factories);
  DP_EXPECT_EQ(semantic_result.execution.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(semantic_result.execution.failure.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(semantic_workspace));
  expect_zero_invocations(semantic_result, semantic_counts);
}

class MissingOutputLocal final : public dp::LocalGraphBuilder {
 public:
  dp::StageResult build(const dp::ArtifactRef&, dp::PartitionId,
                        const dp::LocalBuildConfig&,
                        dp::StageContext&) override {
    return {dp::StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class MissingAfterFirstLocal final : public dp::LocalGraphBuilder {
 public:
  explicit MissingAfterFirstLocal(std::shared_ptr<std::size_t> calls)
      : calls_(std::move(calls)) {}

  dp::StageResult build(const dp::ArtifactRef& partition,
                        dp::PartitionId partition_id,
                        const dp::LocalBuildConfig& config,
                        dp::StageContext& context) override {
    ++*calls_;
    if (*calls_ == 2) {
      return {dp::StageStatus::kSucceeded, std::nullopt, {}};
    }
    return xn::make_xn_descent_local_build()->build(
        partition, partition_id, config, context);
  }

 private:
  std::shared_ptr<std::size_t> calls_;
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
  dp::WorkspaceRef workspace;
  dp::ArtifactRef dataset;
  xn::ExperimentRunnerResult result;
  FactoryCounts factories;
};

SmallRun execute_small(const std::string& label, const std::string& profile,
                       std::uint32_t lambda, std::uint32_t merge_r,
                       dp::LocalBuildFactory local_factory = {}) {
  auto temporary = std::make_unique<fixture::TemporaryDirectory>(label);
  const auto workspace = dp::create_workspace(temporary->path / "workspace");
  fixture::NativeDatasetFixture dataset(workspace, 96, 4);
  const auto document = request(workspace.root, dataset.reference, label,
                                profile, lambda, merge_r);
  SmallRun result{std::move(temporary), workspace, dataset.reference, {}, {}};
  result.factories = {};
  auto factories = counting_factories(result.factories);
  if (local_factory) factories.local_build = std::move(local_factory);
  result.result = xn::execute_pipeline_experiment_request(document.dump(),
                                                          &factories);
  return result;
}

void verify_successful_partitioned_merge(const SmallRun& run,
                                         std::uint32_t lambda,
                                         std::uint32_t merge_r) {
  const auto& success = run.result;
  if (success.execution.failure) {
    std::cerr << "XN small pipeline failure: "
              << success.execution.failure->reason << '\n';
  }
  DP_EXPECT_EQ(success.execution.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(success.execution.invocations.partition, std::size_t{1});
  DP_EXPECT_TRUE(success.execution.invocations.local_build >= std::size_t{2});
  DP_EXPECT_EQ(success.execution.invocations.local_build,
               success.execution.local_artifacts.size());
  DP_EXPECT_EQ(success.execution.local_partition_ids.size(),
               success.execution.local_artifacts.size());
  DP_EXPECT_TRUE(std::is_sorted(success.execution.local_partition_ids.begin(),
                                success.execution.local_partition_ids.end()));
  DP_EXPECT_TRUE(std::adjacent_find(
                     success.execution.local_partition_ids.begin(),
                     success.execution.local_partition_ids.end()) ==
                 success.execution.local_partition_ids.end());
  DP_EXPECT_EQ(success.execution.invocations.partition,
               run.factories.partition);
  DP_EXPECT_EQ(success.execution.invocations.local_build,
               run.factories.local_build);
  DP_EXPECT_EQ(success.execution.invocations.merge, run.factories.merge);
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
  const auto validator = dp::Validator(run.workspace);
  for (const auto& local : success.execution.local_artifacts) {
    DP_EXPECT_TRUE(validator.validate_with_lineage(local).valid());
    const auto metadata_path = dp::resolve_normal_artifact_metadata(
        run.workspace, local.metadata_path);
    const auto metadata = Json::parse(read_text(metadata_path));
    DP_EXPECT_EQ(metadata.at("structural").at("declared_max_degree"),
                 lambda);
    DP_EXPECT_TRUE(metadata.at("structural")
                       .at("observed_max_degree")
                       .get<std::uint32_t>() <= lambda);
  }
  if (success.execution.artifact) {
    DP_EXPECT_TRUE(
        validator.validate_with_lineage(*success.execution.artifact).valid());
    const auto metadata_path = dp::resolve_normal_artifact_metadata(
        run.workspace, success.execution.artifact->metadata_path);
    const auto metadata = Json::parse(read_text(metadata_path));
    DP_EXPECT_EQ(metadata.at("structural").at("declared_max_degree"),
                 merge_r);
    DP_EXPECT_TRUE(metadata.at("structural")
                       .at("observed_max_degree")
                       .get<std::uint32_t>() <= merge_r);
  }
  const auto result_document = Json::parse(success.result_json);
  DP_EXPECT_EQ(result_document.at("claim_scope"),
               "component-level_faithful_adaptation");
  DP_EXPECT_EQ(result_document.at("replacement_scope"), "local_build_only");
  DP_EXPECT_EQ(result_document.at("full_xn_graph_system_reproduction"), false);
}

void test_small_pipeline_and_validation_gate() {
  const auto pipeline_run = execute_small(
      "xn-small-pipeline-lambda12-r7", "pipeline-compatible", 12, 7);
  verify_successful_partitioned_merge(pipeline_run, 12, 7);

  const auto paper_run =
      execute_small("xn-small-paper-lambda64-r9", "paper-style", 64, 9);
  verify_successful_partitioned_merge(paper_run, 64, 9);

  const auto partial_calls = std::make_shared<std::size_t>(0);
  const auto partial_run = execute_small(
      "xn-small-partial", "pipeline-compatible", 12, 7,
      [partial_calls] {
        return std::make_unique<MissingAfterFirstLocal>(partial_calls);
      });
  const auto& partial = partial_run.result;
  DP_EXPECT_EQ(partial.execution.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(partial.execution.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(partial.execution.invocations.local_build, std::size_t{2});
  DP_EXPECT_EQ(partial.execution.local_artifacts.size(), std::size_t{1});
  DP_EXPECT_EQ(*partial_calls, std::size_t{2});
  DP_EXPECT_EQ(partial.execution.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partial.execution.artifact.has_value());

  const auto invalid_run = execute_small(
      "xn-small-invalid", "pipeline-compatible", 12, 7,
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
