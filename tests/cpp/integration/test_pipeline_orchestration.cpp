#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"
#include "diskann_pipeline/runner_protocol.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "baseline/cpp_main/native_formats.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>

namespace dp = diskann_pipeline;

namespace {

void run_success(const std::string& name, bool reuse,
                 bool producer_omits_overlap = false,
                 bool producer_extra_claim = false, bool cli_pair = false,
                 bool reject_encoding = false) {
  dp::test::FakeStageEnvironment environment(name);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  auto request = dp::test::canonical_execution_request(name, dataset);
  if (reuse) {
    auto cached = dp::test::materialize_partition_candidate(
        environment.workspace().normal_root / "seed", dataset);
    cached.metadata_path = std::filesystem::path("seed") / cached.metadata_path;
    const auto validated = dp::Validator(environment.workspace())
                               .validate_with_lineage(cached);
    DP_EXPECT_TRUE(validated.valid());
    if (!validated.valid()) return;
    // Merge's graph capability must come from the future LocalGraph, not Partition.
    DP_EXPECT_TRUE(!validated.validated_artifact->has_capability(
        "graph.local_adjacency/v1"));
    request.reused_partition = cached;
  }
  auto partition = dp::test::fake_partition_descriptor();
  auto local = dp::test::fake_local_graph_descriptor();
  auto merge = dp::test::fake_merge_descriptor();
  if (producer_omits_overlap) {
    auto& capabilities = partition.provides.capabilities;
    capabilities.erase(std::remove(capabilities.begin(), capabilities.end(),
                                   "partition.overlap/v1"), capabilities.end());
    local.requires.required_capabilities.push_back("partition.overlap/v1");
    merge.requires.required_capabilities.push_back("partition.overlap/v1");
  }
  if (producer_extra_claim) {
    partition.provides.capabilities.push_back(
        "partition.local_to_global_ascending/v1");
  }
  std::array<std::size_t, 3> factories{};
  std::vector<std::size_t> cli_known_sizes;
  dp::InputAdmissionCheck cli_admission;
  if (cli_pair) cli_admission = [&](const std::vector<dp::ValidatedArtifact>& known,
                                   const dp::StageContext&) {
    cli_known_sizes.push_back(known.size());
    for (const auto& input : known) {
      if (input.reference().artifact_type == dp::ArtifactType::kPartition) {
        DP_EXPECT_EQ(input.reference().aggregate_checksum, request.reused_partition->aggregate_checksum);
        DP_EXPECT_TRUE(input.has_capability("partition.overlap/v1"));
      }
    }
    return reject_encoding ? dp::cpp_main::check_native_partition_maps(known)
                           : dp::CompatibilityResult{true, std::nullopt};
  };
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(partition, [&] {
    ++factories[0];
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [&] {
    ++factories[1];
    return dp::test::make_fake_local_graph();
  }, cli_admission);
  registry.register_merge(merge, [&] {
    ++factories[2];
    return dp::test::make_fake_merge();
  });
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto context = environment.make_context("executor");
  if (cli_pair) {
    using Json = nlohmann::json;
    const auto& workspace = environment.workspace();
    for (const std::string operation : {"preflight", "execute"}) {
      cli_known_sizes.clear();
      Json document{{"request_version", "1.0.0"},
        {"request_id", name + "-" + operation}, {"operation", operation},
        {"study_class", "infrastructure_validation"},
        {"workspace", {{"root", workspace.root.string()},
          {"normal_root", workspace.normal_root.string()},
          {"temporary_root", workspace.temporary_root.string()},
          {"manifest_root", workspace.manifest_root.string()},
          {"report_root", workspace.report_root.string()},
          {"quarantine_root", workspace.quarantine_root.string()},
          {"memory_budget_bytes", 1048576}, {"thread_budget", 1}}},
        {"dataset", Json::parse(dp::serialize_artifact_ref(dataset))},
        {"reuse_artifact", Json::parse(dp::serialize_artifact_ref(*request.reused_partition))},
        {"implementations", {{"partition", request.partition_implementation_id},
          {"local_build", request.local_implementation_id}, {"merge", request.merge_implementation_id}}},
        {"stage_parameters", {{"partition", Json::parse(request.partition_parameters_json)},
          {"local_build", Json::parse(request.local_parameters_json)},
          {"merge", Json::parse(request.merge_parameters_json)}}},
        {"repetition_policy", {{"count", 2}, {"aggregation", "retain_all"}}}};
      const auto file = workspace.temporary_root / (operation + "-request.json");
      dp::write_immutable_text(file, document.dump());
      const auto dispatched = dp::dispatch_runner_request(file, operation, registry, context);
      const auto result = Json::parse(dispatched.result_json);
      std::cout << name << ' ' << operation << " outcome=" << result.at("outcome")
                << " factories=" << factories[0] << ',' << factories[1] << ',' << factories[2]
                << " stages=" << result.at("stage_invocations") << '\n';
      DP_EXPECT_EQ(dispatched.exit_code, reject_encoding ? (operation == "preflight" ? 3 : 4) : 0);
      DP_EXPECT_EQ(result.at("outcome"), reject_encoding
          ? (operation == "preflight" ? "REJECTED" : "FAILED") : "SUCCEEDED");
      DP_EXPECT_TRUE(std::find(cli_known_sizes.begin(), cli_known_sizes.end(), 1) != cli_known_sizes.end());
      DP_EXPECT_TRUE(std::find(cli_known_sizes.begin(), cli_known_sizes.end(), 2) != cli_known_sizes.end());
      const std::array<std::size_t, 3> expected = operation == "execute" && !reject_encoding
          ? std::array<std::size_t, 3>{0, 2, 1} : std::array<std::size_t, 3>{};
      DP_EXPECT_EQ(factories, expected);
      DP_EXPECT_EQ(result.at("stage_invocations").at("partition"), expected[0]);
      DP_EXPECT_EQ(result.at("stage_invocations").at("local_build"), expected[1]);
      DP_EXPECT_EQ(result.at("stage_invocations").at("merge"), expected[2]);
      if (operation == "preflight") {
        // Check the standalone result before execute can create any evidence.
        const auto& events = environment.metrics().events();
        for (const auto* event : {"partition.start", "local_build.start", "merge.start"}) {
          DP_EXPECT_EQ(std::count(events.begin(), events.end(), event), 0);
        }
        DP_EXPECT_TRUE(std::filesystem::is_empty(workspace.quarantine_root));
        DP_EXPECT_TRUE(result.contains("validation_evidence_path"));
        if (result.contains("validation_evidence_path")) {
          const auto relative = result.at("validation_evidence_path").get<std::string>();
          DP_EXPECT_TRUE(!std::filesystem::path(relative).is_absolute());
          const auto evidence_path = dp::resolve_confined_path(workspace.root, relative);
          DP_EXPECT_TRUE(std::filesystem::is_regular_file(evidence_path));
          std::ifstream evidence_input(evidence_path);
          DP_EXPECT_TRUE(evidence_input.good());
          if (evidence_input.good()) {
            const auto evidence = Json::parse(evidence_input);
            DP_EXPECT_EQ(evidence.at("result"), "VALID");
            const auto& identity = evidence.at("artifact_candidate");
            const auto& reused = document.at("reuse_artifact");
            for (const auto* field : {"artifact_type", "format_version", "metadata_path"}) {
              DP_EXPECT_EQ(identity.at(field), reused.at(field));
            }
            DP_EXPECT_EQ(identity.at("claimed_aggregate_checksum"), reused.at("aggregate_checksum"));
            std::cout << name << " standalone evidence=" << relative
                      << " result=" << evidence.at("result") << '\n';
          }
        }
      }
    }
    DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(*request.reused_partition).valid());
    DP_EXPECT_TRUE(std::filesystem::is_empty(workspace.quarantine_root));
    return;
  }
  const auto result = executor.execute(request, context);
  // Each real Fake stage emits *.start as the first statement in its method.
  const auto& events = environment.metrics().events();
  const std::array<std::size_t, 3> stages{
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "partition.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "local_build.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "merge.start"))};
  const std::array<std::size_t, 3> reported{
      result.invocations.partition, result.invocations.local_build,
      result.invocations.merge};
  const std::array<std::size_t, 3> expected{reuse ? 0u : 1u, 2, 1};
  std::cout << name << " status=" << dp::to_string(result.status)
            << " factories=" << factories[0] << ',' << factories[1] << ',' << factories[2]
            << " stages=" << stages[0] << ',' << stages[1] << ',' << stages[2]
            << " executor=" << reported[0] << ',' << reported[1] << ',' << reported[2] << '\n';
  DP_EXPECT_EQ(factories, expected);
  DP_EXPECT_EQ(stages, expected);
  DP_EXPECT_EQ(reported, expected);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(result.invocations.partition, reuse ? std::size_t{0} : std::size_t{1});
  DP_EXPECT_EQ(result.invocations.local_build, std::size_t{2});
  DP_EXPECT_EQ(result.invocations.merge, std::size_t{1});
  DP_EXPECT_EQ(result.local_partition_ids,
               std::vector<dp::PartitionId>({0, 1}));
  std::vector<dp::StageKind> order;
  if (!reuse) order.push_back(dp::StageKind::kPartition);
  order.insert(order.end(), {dp::StageKind::kLocalBuild,
                            dp::StageKind::kLocalBuild, dp::StageKind::kMerge});
  DP_EXPECT_EQ(result.stage_order, order);
  DP_EXPECT_TRUE(result.artifact.has_value());
  if (!result.artifact) return;
  DP_EXPECT_EQ(result.artifact->aggregate_checksum,
               std::string(dp::test::kGlobalAggregateChecksum));
  DP_EXPECT_TRUE(std::filesystem::exists(
      dp::resolve_normal_artifact_metadata(environment.workspace(),
                                           result.artifact->metadata_path)));
  DP_EXPECT_EQ(result.validation_evidence_paths.size(), std::size_t{4});
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{4});
  // Resolve and validate the completed graph's real dependency chain as well.
  DP_EXPECT_TRUE(dp::Validator(environment.workspace())
                     .validate_with_lineage(*result.artifact).valid());
}

// These cases use the new callback API. Fake consumers intentionally have a
// test-only restriction on known inputs; no optional algorithm is linked.
void run_admission_schedule(const std::string& stop, bool reuse = false) {
  using Json = nlohmann::json;
  const auto name = "admission-" + stop + (reuse ? "-reuse" : "-generated");
  dp::test::FakeStageEnvironment environment(name);
  const auto workspace = environment.workspace();
  const auto dataset = dp::test::materialize_fake_dataset(workspace);
  auto request = dp::test::canonical_execution_request(name, dataset);
  if (reuse) {
    auto cached = dp::test::materialize_partition_candidate(workspace.normal_root / "seed", dataset);
    cached.metadata_path = std::filesystem::path("seed") / cached.metadata_path;
    request.reused_partition = cached;
  }
  auto local = dp::test::fake_local_graph_descriptor();
  auto merge = dp::test::fake_merge_descriptor();
  std::array<std::size_t, 3> factories{};
  std::vector<std::size_t> observed_sizes;
  auto admission = [&](const std::vector<dp::ValidatedArtifact>& inputs,
                       const dp::StageContext&) -> dp::CompatibilityResult {
    observed_sizes.push_back(inputs.size());
    for (const auto& input : inputs) {
      const auto type = input.reference().artifact_type;
      if ((stop == "partition" && type == dp::ArtifactType::kPartition) ||
          (stop == "local" && type == dp::ArtifactType::kLocalGraph))
        return {false, dp::FailureInfo{dp::ErrorCategory::kCapabilityUnsatisfied,
            "test consumer does not support this validated input", std::nullopt}};
    }
    return {true, std::nullopt};
  };
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(dp::test::fake_partition_descriptor(), [&] {
    ++factories[0]; return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [&] {
    ++factories[1]; return dp::test::make_fake_local_graph();
  }, stop == "partition" ? dp::InputAdmissionCheck(admission) : dp::InputAdmissionCheck{});
  registry.register_merge(merge, [&] {
    ++factories[2]; return dp::test::make_fake_merge();
  }, admission);
  auto context = environment.make_context("executor");
  const auto result = dp::PipelineExecutor(registry, workspace).execute(request, context);
  const std::array<std::size_t, 3> expected{
      reuse ? 0u : 1u, stop == "partition" ? 0u : stop == "local" ? 1u : 2u,
      stop == "none" ? 1u : 0u};
  DP_EXPECT_EQ(factories, expected);
  DP_EXPECT_EQ(result.invocations.partition, expected[0]);
  DP_EXPECT_EQ(result.invocations.local_build, expected[1]);
  DP_EXPECT_EQ(result.invocations.merge, expected[2]);
  DP_EXPECT_EQ(result.status, stop == "none" ? dp::StageStatus::kSucceeded : dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!result.quarantine_id);
  DP_EXPECT_TRUE(result.partition_artifact.has_value());
  DP_EXPECT_TRUE(!result.validation_evidence_paths.empty());
  for (const auto& path : result.validation_evidence_paths) {
    std::ifstream input(path);
    const auto evidence = Json::parse(input);
    DP_EXPECT_EQ(evidence.at("result"), "VALID");
  }
  for (const auto& path : result.build_manifest_paths) {
    std::ifstream input(path);
    const auto status = Json::parse(input).at("status");
    DP_EXPECT_TRUE(status == "SUCCEEDED" || status == "REUSED");
  }
  DP_EXPECT_TRUE(std::find(observed_sizes.begin(), observed_sizes.end(), 1) != observed_sizes.end());
  DP_EXPECT_TRUE(std::find(observed_sizes.begin(), observed_sizes.end(), 2) != observed_sizes.end());
  if (stop == "none") {
    DP_EXPECT_TRUE(std::find(observed_sizes.begin(), observed_sizes.end(), 3) != observed_sizes.end());
    DP_EXPECT_TRUE(std::find(observed_sizes.begin(), observed_sizes.end(), 4) != observed_sizes.end());
  }
  std::cout << name << " factories/stages=" << factories[0] << ',' << factories[1]
            << ',' << factories[2] << " status=" << dp::to_string(result.status)
            << " evidence=VALID producer=SUCCEEDED/REUSED quarantine=0\n";
}

}  // namespace

int main() {
  run_admission_schedule("partition");
  run_admission_schedule("partition", true);
  run_admission_schedule("local");
  run_admission_schedule("none");
  run_success("orchestration-run", false);
  run_success("reused-compatible", true);
  run_success("reused-producer-omits-overlap", true, true);
  run_success("reused-producer-extra-claim", true, false, true);
  run_success("cli-reused-producer-omits-overlap", true, true, false, true);
  run_success("cli-physical-admission", true, true, false, true, true);
  return dp::test::finish();
}
