#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <algorithm>
#include <array>

namespace dp = diskann_pipeline;

namespace {

using Counts = std::array<std::size_t, 3>;

void register_counted(dp::ImplementationRegistry& registry, Counts& factories,
                      const dp::ImplementationDescriptor& partition,
                      const dp::ImplementationDescriptor& local,
                      const dp::ImplementationDescriptor& merge) {
  registry.register_partition(partition, [&factories] {
    ++factories[0];
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [&factories] {
    ++factories[1];
    return dp::test::make_fake_local_graph();
  });
  registry.register_merge(merge, [&factories] {
    ++factories[2];
    return dp::test::make_fake_merge();
  });
}

void expect_counts(const std::string& name, const Counts& factories,
                   dp::test::FakeStageEnvironment& environment,
                   const dp::PipelineExecutionResult& result,
                   const Counts& expected) {
  // These events are emitted at the actual Fake stage method entry, independently
  // of both factory creation and the executor's bookkeeping.
  const auto& events = environment.metrics().events();
  const Counts stages{
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "partition.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "local_build.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "merge.start"))};
  const Counts reported{result.invocations.partition, result.invocations.local_build,
                        result.invocations.merge};
  std::cout << name << " status=" << dp::to_string(result.status)
            << " factories=" << factories[0] << ',' << factories[1] << ',' << factories[2]
            << " stages=" << stages[0] << ',' << stages[1] << ',' << stages[2]
            << " executor=" << reported[0] << ',' << reported[1] << ',' << reported[2] << '\n';
  DP_EXPECT_EQ(factories, expected);
  DP_EXPECT_EQ(stages, expected);
  DP_EXPECT_EQ(reported, expected);
}

enum class ReuseCase { kMissingLocal, kMissingMerge, kChecksum, kStructure, kLineage };

void reject_reuse(const std::string& name, ReuseCase scenario,
                  bool producer_claims_capability = false) {
  dp::test::FakeStageEnvironment environment(name);
  const auto& workspace = environment.workspace();
  const auto dataset = dp::test::materialize_fake_dataset(workspace);
  auto cached = dp::test::materialize_partition_candidate(workspace.normal_root / "seed", dataset);
  cached.metadata_path = std::filesystem::path("seed") / cached.metadata_path;
  const dp::Validator validator(workspace);
  const auto checked_dataset = validator.validate(dataset);
  DP_EXPECT_TRUE(checked_dataset.valid());
  if (!checked_dataset.valid()) return;
  const auto sources = std::vector<dp::ValidatedArtifact>{*checked_dataset.validated_artifact};
  DP_EXPECT_TRUE(validator.validate(cached, sources).valid());

  const bool valid_input = scenario == ReuseCase::kMissingLocal ||
                           scenario == ReuseCase::kMissingMerge;
  auto expected_category = dp::ErrorCategory::kCapabilityUnsatisfied;
  if (scenario == ReuseCase::kChecksum) {
    cached.aggregate_checksum = std::string(64, '0');
    expected_category = dp::ErrorCategory::kChecksumMismatch;
  } else if (scenario == ReuseCase::kStructure) {
    dp::test::mutate_metadata(workspace, cached, [](auto& metadata) {
      metadata["structural"]["partition_count"] = 3;
    });
    expected_category = dp::ErrorCategory::kNodeCountMismatch;
  } else if (scenario == ReuseCase::kLineage) {
    dp::test::mutate_metadata(workspace, cached, [](auto& metadata) {
      metadata["source_artifacts"][0]["aggregate_checksum"] = std::string(64, '0');
    });
    expected_category = dp::ErrorCategory::kSourceMismatch;
  }
  const auto independently_checked = validator.validate(cached, sources);
  DP_EXPECT_EQ(independently_checked.valid(), valid_input);
  if (!valid_input) {
    DP_EXPECT_EQ(independently_checked.evidence.failure_category,
                 std::optional<dp::ErrorCategory>{expected_category});
  }
  // Freeze every input byte after fixture preparation, not merely the reference.
  std::map<std::filesystem::path, std::string> input_hashes;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(workspace.normal_root)) {
    if (entry.is_regular_file()) input_hashes.emplace(entry.path(), dp::sha256_file(entry.path()));
  }

  auto partition = dp::test::fake_partition_descriptor();
  auto local = dp::test::fake_local_graph_descriptor();
  auto merge = dp::test::fake_merge_descriptor();
  const std::string capability = "partition.local_to_global_ascending/v1";
  if (producer_claims_capability) partition.provides.capabilities.push_back(capability);
  if (scenario == ReuseCase::kMissingLocal) local.requires.required_capabilities.push_back(capability);
  if (scenario == ReuseCase::kMissingMerge) merge.requires.required_capabilities.push_back(capability);
  Counts factories{};
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  register_counted(registry, factories, partition, local, merge);
  auto request = dp::test::canonical_execution_request(name, dataset);
  request.reused_partition = cached;
  auto context = environment.make_context("executor");
  const auto result = dp::PipelineExecutor(registry, workspace).execute(request, context);
  expect_counts(name, factories, environment, result, {0, 0, 0});
  DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(result.failure.has_value());
  if (result.failure) {
    DP_EXPECT_EQ(result.failure->category, expected_category);
    if (valid_input) DP_EXPECT_TRUE(result.failure->reason.find(capability) != std::string::npos);
  }
  DP_EXPECT_TRUE(result.stage_order.empty());
  DP_EXPECT_TRUE(result.local_partition_ids.empty());
  DP_EXPECT_TRUE(!result.artifact.has_value());
  DP_EXPECT_TRUE(!result.partition_artifact.has_value());
  DP_EXPECT_TRUE(!result.evaluation_report_path.has_value());
  DP_EXPECT_TRUE(!result.quarantine_id.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_empty(workspace.quarantine_root));
  DP_EXPECT_EQ(result.validation_evidence_paths.size(), std::size_t{1});
  if (result.validation_evidence_paths.size() == 1) {
    // A consumer mismatch must not rewrite an independently VALID contract result.
    DP_EXPECT_EQ(dp::test::read_json(result.validation_evidence_paths.front()),
                 dp::test::Json::parse(dp::serialize_validation_evidence(independently_checked.evidence)));
  }
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{1});
  if (result.build_manifest_paths.size() == 1) {
    const auto manifest = dp::parse_build_manifest(
        dp::test::read_json(result.build_manifest_paths.front()).dump());
    DP_EXPECT_EQ(manifest.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(manifest.output_artifacts.empty());
    DP_EXPECT_TRUE(manifest.failure.has_value());
    if (manifest.failure) DP_EXPECT_EQ(manifest.failure->category, expected_category);
    if (valid_input) DP_EXPECT_TRUE(manifest.reuse.has_value());
    if (manifest.reuse) {
      DP_EXPECT_TRUE(!manifest.reuse->reused);
      DP_EXPECT_TRUE(manifest.reuse->source_artifact.has_value());
      if (manifest.reuse->source_artifact) {
        DP_EXPECT_EQ(dp::serialize_artifact_ref(*manifest.reuse->source_artifact),
                     dp::serialize_artifact_ref(cached));
      }
    }
    DP_EXPECT_TRUE(manifest.validation_evidence_path.has_value());
    if (manifest.validation_evidence_path && !result.validation_evidence_paths.empty()) {
      DP_EXPECT_EQ(workspace.root / *manifest.validation_evidence_path,
                   result.validation_evidence_paths.front());
    }
  }
  for (const auto& input : input_hashes) DP_EXPECT_EQ(dp::sha256_file(input.first), input.second);
  DP_EXPECT_EQ(validator.validate(cached, sources).valid(), valid_input);
}

dp::PipelineExecutionResult run_with_cancel(std::size_t observation,
                                            const std::string& identity) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  environment.cancellation().cancel_on_observation(observation);
  auto context = environment.make_context("executor");
  return dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(identity, dataset), context);
}

}  // namespace

int main() {
  reject_reuse("reuse-missing-local-claimed", ReuseCase::kMissingLocal, true);
  reject_reuse("reuse-missing-merge-claimed", ReuseCase::kMissingMerge, true);
  reject_reuse("reuse-missing-local-unclaimed", ReuseCase::kMissingLocal);
  reject_reuse("reuse-missing-merge-unclaimed", ReuseCase::kMissingMerge);
  reject_reuse("reuse-invalid-checksum", ReuseCase::kChecksum);
  reject_reuse("reuse-invalid-structure", ReuseCase::kStructure);
  reject_reuse("reuse-invalid-lineage", ReuseCase::kLineage);
  const auto partition_cancelled = run_with_cancel(1, "cancel-partition");
  DP_EXPECT_EQ(partition_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(partition_cancelled.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(partition_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partition_cancelled.artifact.has_value());

  const auto local_cancelled = run_with_cancel(4, "cancel-local");
  DP_EXPECT_EQ(local_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(local_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!local_cancelled.artifact.has_value());

  const auto merge_cancelled = run_with_cancel(10, "cancel-merge");
  DP_EXPECT_EQ(merge_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(merge_cancelled.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(!merge_cancelled.artifact.has_value());

  dp::test::FakeStageEnvironment invalid_environment("invalid-local");
  const auto dataset =
      dp::test::materialize_fake_dataset(invalid_environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(dp::test::fake_partition_descriptor(),
                              dp::test::make_fake_partition);
  registry.register_local_build(dp::test::fake_local_graph_descriptor(),
                                dp::test::make_invalid_fake_local_graph);
  registry.register_merge(dp::test::fake_merge_descriptor(),
                          dp::test::make_fake_merge);
  const auto invalid_request =
      dp::test::canonical_execution_request("invalid-local", dataset);
  auto context = invalid_environment.make_context("executor");
  const auto invalid =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(invalid_request, context);
  DP_EXPECT_EQ(invalid.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(invalid.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(invalid.quarantine_id.has_value());
  DP_EXPECT_TRUE(!invalid.artifact.has_value());

  auto incompatible_request =
      dp::test::canonical_execution_request("incompatible", dataset);
  incompatible_request.merge_implementation_id = "missing_merge";
  const auto incompatible =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(incompatible_request, context);
  DP_EXPECT_EQ(incompatible.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(incompatible.invocations.partition, std::size_t{0});

  dp::test::FakeStageEnvironment handoff_environment("handoff-mismatch");
  const auto handoff_dataset =
      dp::test::materialize_fake_dataset(handoff_environment.workspace());
  dp::ImplementationRegistry handoff_registry(dp::RegistryAdmissionPolicy::kTest);
  auto partition_descriptor = dp::test::fake_partition_descriptor();
  partition_descriptor.provides.capabilities.push_back(
      "partition.unobserved_claim/v1");
  Counts handoff_factories{};
  register_counted(handoff_registry, handoff_factories, partition_descriptor,
                   dp::test::fake_local_graph_descriptor(),
                   dp::test::fake_merge_descriptor());
  auto handoff_context = handoff_environment.make_context("handoff-executor");
  const auto handoff =
      dp::PipelineExecutor(handoff_registry, handoff_environment.workspace())
          .execute(dp::test::canonical_execution_request(
                       "handoff-mismatch", handoff_dataset),
                   handoff_context);
  DP_EXPECT_EQ(handoff.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(handoff.failure.has_value());
  DP_EXPECT_EQ(handoff.failure->category,
               dp::ErrorCategory::kCapabilityUnsatisfied);
  DP_EXPECT_EQ(handoff.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(handoff.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(handoff.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(handoff.quarantine_id.has_value());
  expect_counts("generated-producer-output-mismatch", handoff_factories,
                 handoff_environment, handoff, {1, 0, 0});

  return dp::test::finish();
}
