#include "diskann_pipeline/registry.hpp"

#include "support/test_assert.hpp"

#include <type_traits>

namespace dp = diskann_pipeline;

namespace {

dp::ImplementationDescriptor descriptor(std::string id, dp::StageKind stage,
                                        bool test_only) {
  const auto input_type = stage == dp::StageKind::kPartition
                              ? dp::ArtifactType::kDataset
                              : dp::ArtifactType::kPartition;
  return {std::move(id),
          "1.0.0",
          stage,
          {{{input_type,
             {stage == dp::StageKind::kPartition ? "fake-dataset-json/v1"
                                                 : "partition-artifact/v1"},
             stage == dp::StageKind::kPartition ? dp::IdSpace::kNone
                                                 : dp::IdSpace::kLocal}},
           {},
           {}},
          {stage == dp::StageKind::kPartition ? dp::ArtifactType::kPartition
                                              : dp::ArtifactType::kLocalGraph,
           stage == dp::StageKind::kPartition ? "partition-artifact/v1"
                                              : "local-graph-artifact/v1",
           dp::IdSpace::kLocal,
           {"contract.test/v1"}},
          "contracts/v1/schemas/implementation-descriptor.schema.json",
          test_only};
}

}  // namespace

int main() {
  static_assert(!std::is_default_constructible_v<dp::ImplementationRegistry>);

  dp::ImplementationRegistry production(dp::RegistryAdmissionPolicy::kProduction);
  DP_EXPECT_THROW(production.register_partition(
      descriptor("test.partition", dp::StageKind::kPartition, true),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; }));
  DP_EXPECT_EQ(production.size(), std::size_t{0});

  production.register_partition(
      descriptor("formal.partition", dp::StageKind::kPartition, false),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; });
  DP_EXPECT_EQ(production.size(), std::size_t{1});
  const auto production_entries = production.enumerate();
  DP_EXPECT_EQ(production_entries.front().implementation_id,
               std::string("formal.partition"));
  DP_EXPECT_EQ(production.resolve_descriptor("formal.partition", "1.0.0",
                                             dp::StageKind::kPartition)
                   .stage,
               dp::StageKind::kPartition);
  DP_EXPECT_THROW(production.resolve_descriptor(
      "formal.partition", "9.9.9", dp::StageKind::kPartition));
  DP_EXPECT_THROW(production.resolve_descriptor(
      "missing.partition", "1.0.0", dp::StageKind::kPartition));
  DP_EXPECT_THROW(production.register_partition(
      descriptor("formal.partition", dp::StageKind::kPartition, false),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; }));
  DP_EXPECT_EQ(production.size(), std::size_t{1});
  DP_EXPECT_THROW(production.register_partition(
      descriptor("wrong.stage", dp::StageKind::kMerge, false),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; }));
  DP_EXPECT_EQ(production.size(), std::size_t{1});
  production.seal();
  DP_EXPECT_TRUE(production.sealed());
  DP_EXPECT_THROW(production.register_partition(
      descriptor("late.partition", dp::StageKind::kPartition, false),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; }));

  dp::ImplementationRegistry tests(dp::RegistryAdmissionPolicy::kTest);
  tests.register_partition(
      descriptor("test.partition", dp::StageKind::kPartition, true),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; });
  tests.register_partition(
      descriptor("formal.partition", dp::StageKind::kPartition, false),
      [] { return std::unique_ptr<dp::PartitionStrategy>{}; });
  DP_EXPECT_EQ(tests.size(), std::size_t{2});
  DP_EXPECT_TRUE(static_cast<bool>(tests.resolve_partition_factory(
      "test.partition", "1.0.0")));

  // The legacy API remains valid, and callbacks are resolved by exact identity.
  dp::StageContext context{};
  DP_EXPECT_TRUE(production.check_known_inputs(production_entries.front(), {}, context).compatible);
  std::size_t checks = 0, factories = 0;
  const auto local = descriptor("checked.local", dp::StageKind::kLocalBuild, false);
  tests.register_local_build(local, [&] {
    ++factories; return std::unique_ptr<dp::LocalGraphBuilder>{};
  }, [&](const std::vector<dp::ValidatedArtifact>& inputs, const dp::StageContext& actual) {
    ++checks;
    DP_EXPECT_TRUE(inputs.empty());
    DP_EXPECT_TRUE(&actual == &context);
    return dp::CompatibilityResult{false, dp::FailureInfo{
        dp::ErrorCategory::kCapabilityUnsatisfied, "test admission", std::nullopt}};
  });
  const auto rejected = tests.check_known_inputs(local, {}, context);
  DP_EXPECT_TRUE(!rejected.compatible);
  DP_EXPECT_EQ(checks, std::size_t{1});
  DP_EXPECT_EQ(factories, std::size_t{0});
  auto wrong = local;
  wrong.implementation_version = "2.0.0";
  DP_EXPECT_THROW(tests.check_known_inputs(wrong, {}, context));
  wrong = local;
  wrong.stage = dp::StageKind::kMerge;
  DP_EXPECT_THROW(tests.check_known_inputs(wrong, {}, context));
  DP_EXPECT_EQ(checks, std::size_t{1});
  tests.seal();
  DP_EXPECT_THROW(tests.register_local_build(local, [] {
    return std::unique_ptr<dp::LocalGraphBuilder>{};
  }, {}));

  return dp::test::finish();
}
