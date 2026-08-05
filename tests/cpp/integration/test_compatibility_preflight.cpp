#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/registry.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

namespace {

dp::ImplementationDescriptor merge_descriptor() {
  return {"contract.merge",
          "1.0.0",
          dp::StageKind::kMerge,
          {{{dp::ArtifactType::kPartition, {"partition-artifact/v1"},
             dp::IdSpace::kLocal},
            {dp::ArtifactType::kLocalGraph, {"local-graph-artifact/v1"},
             dp::IdSpace::kLocal}},
           {"partition.complete_local_to_global_map/v1",
            "graph.local_adjacency/v1"},
           {"metrics_sink/v1"}},
          {dp::ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
           dp::IdSpace::kGlobal, {"graph.global_adjacency/v1"}},
          "contracts/merge.schema.json",
          false};
}

std::vector<dp::AvailableArtifactContract> valid_inputs() {
  return {{dp::ArtifactType::kPartition,
           "partition-artifact/v1",
           dp::IdSpace::kLocal,
           {"partition.complete_local_to_global_map/v1"}},
          {dp::ArtifactType::kLocalGraph,
           "local-graph-artifact/v1",
           dp::IdSpace::kLocal,
           {"graph.local_adjacency/v1"}}};
}

}  // namespace

int main() {
  dp::CompatibilityChecker checker;
  const dp::RuntimeServiceCapabilities services{{"metrics_sink/v1"}, false};
  DP_EXPECT_TRUE(checker.check(merge_descriptor(), valid_inputs(), services).compatible);

  auto missing = valid_inputs();
  missing.pop_back();
  DP_EXPECT_TRUE(!checker.check(merge_descriptor(), missing, services).compatible);

  auto wrong_version = valid_inputs();
  wrong_version[1].format_version = "local-graph-artifact/v2";
  DP_EXPECT_TRUE(
      !checker.check(merge_descriptor(), wrong_version, services).compatible);

  auto wrong_id = valid_inputs();
  wrong_id[1].id_space = dp::IdSpace::kGlobal;
  DP_EXPECT_EQ(checker.check(merge_descriptor(), wrong_id, services)
                   .failure->category,
               dp::ErrorCategory::kIdSpaceMismatch);

  auto missing_capability = valid_inputs();
  missing_capability[1].capabilities.clear();
  DP_EXPECT_EQ(checker.check(merge_descriptor(), missing_capability, services)
                   .failure->category,
               dp::ErrorCategory::kCapabilityUnsatisfied);

  DP_EXPECT_TRUE(!checker
                      .check(merge_descriptor(), valid_inputs(), {{}, false})
                      .compatible);

  auto checkpoint_merge = merge_descriptor();
  checkpoint_merge.requires.required_services.push_back("checkpoint_store/v1");
  const auto checkpoint =
      checker.check(checkpoint_merge, valid_inputs(), services);
  DP_EXPECT_TRUE(!checkpoint.compatible);
  DP_EXPECT_EQ(checkpoint.failure->category,
               dp::ErrorCategory::kCheckpointUnavailable);

  int factory_invocations = 0;
  int stage_invocations = 0;
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_merge(merge_descriptor(), [&] {
    ++factory_invocations;
    return std::unique_ptr<dp::GraphMerger>{};
  });
  const auto rejected = checker.check(merge_descriptor(), missing, services);
  if (rejected.compatible) {
    auto implementation =
        registry.resolve_merge_factory("contract.merge", "1.0.0")();
    if (implementation) {
      ++stage_invocations;
    }
  }
  DP_EXPECT_EQ(factory_invocations, 0);
  DP_EXPECT_EQ(stage_invocations, 0);

  const dp::ImplementationDescriptor partition{
      "contract.partition", "1.0.0", dp::StageKind::kPartition,
      {{{dp::ArtifactType::kDataset, {"fake-dataset-json/v1"},
         dp::IdSpace::kNone}}, {}, {}},
      {dp::ArtifactType::kPartition, "partition-artifact/v1",
       dp::IdSpace::kLocal,
       {"partition.complete_local_to_global_map/v1"}},
      "contracts/partition.schema.json", false};
  const dp::ImplementationDescriptor local{
      "contract.local", "1.0.0", dp::StageKind::kLocalBuild,
      {{{dp::ArtifactType::kPartition, {"partition-artifact/v1"},
         dp::IdSpace::kLocal}},
       {"partition.complete_local_to_global_map/v1"}, {}},
      {dp::ArtifactType::kLocalGraph, "local-graph-artifact/v1",
       dp::IdSpace::kLocal, {"graph.local_adjacency/v1"}},
      "contracts/local.schema.json", false};
  DP_EXPECT_TRUE(checker
                     .check_chain({partition, local, merge_descriptor()},
                                  {{dp::ArtifactType::kDataset,
                                    "fake-dataset-json/v1",
                                    dp::IdSpace::kNone,
                                    {"dataset.vector_access/v1"}}},
                                  services)
                     .compatible);

  return dp::test::finish();
}
