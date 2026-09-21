#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

#include <algorithm>
#include <array>

namespace dp = diskann_pipeline;

namespace {

void run_success(const std::string& name, bool reuse,
                 bool producer_omits_overlap = false,
                 bool producer_extra_claim = false) {
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
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(partition, [&] {
    ++factories[0];
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [&] {
    ++factories[1];
    return dp::test::make_fake_local_graph();
  });
  registry.register_merge(merge, [&] {
    ++factories[2];
    return dp::test::make_fake_merge();
  });
  dp::PipelineExecutor executor(registry, environment.workspace());
  auto context = environment.make_context("executor");
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

}  // namespace

int main() {
  run_success("orchestration-run", false);
  run_success("reused-compatible", true);
  run_success("reused-producer-omits-overlap", true, true);
  run_success("reused-producer-extra-claim", true, false, true);
  return dp::test::finish();
}
