#include "diskann_pipeline/stage_context.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"

#include "support/test_assert.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace dp = diskann_pipeline;

namespace {

class VectorAccessor final : public dp::VectorAccessor {
 public:
  std::size_t dimension() const noexcept override { return 2; }
  void read(dp::GlobalNodeId, std::vector<float>& destination) const override {
    destination = {0.0F, 0.0F};
  }
};

class Distance final : public dp::DistanceFunction {
 public:
  double evaluate(const std::vector<float>&,
                  const std::vector<float>&) const override {
    return 0.0;
  }
};

class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string&) override {}
  void increment(const std::string&, std::uint64_t) override {}
};

class Token final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

}  // namespace

int main() {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root = std::filesystem::temp_directory_path() /
                    ("diskann-pipeline-phase1-" + unique);

  const auto workspace = dp::create_workspace(root);
  DP_EXPECT_TRUE(workspace.root.is_absolute());
  DP_EXPECT_TRUE(workspace.normal_root != workspace.quarantine_root);
  DP_EXPECT_TRUE(workspace.manifest_root != workspace.report_root);
  DP_EXPECT_EQ(dp::resolve_confined_path(workspace.root, "normal/a.json"),
               workspace.root / "normal/a.json");
  DP_EXPECT_THROW(dp::resolve_confined_path(workspace.root, "../escape"));
  DP_EXPECT_THROW(dp::resolve_confined_path(workspace.root,
                                             std::filesystem::path("/tmp/escape")));

  const auto temporary = dp::create_temporary_storage(workspace, "partition-0");
  DP_EXPECT_TRUE(std::filesystem::exists(temporary.stage_root));
  const auto target = workspace.manifest_root / "atomic.json";
  dp::write_immutable_text(target, R"({"ok":true})");
  DP_EXPECT_TRUE(std::filesystem::exists(target));
  DP_EXPECT_THROW(dp::write_immutable_text(target, R"({"ok":false})"));

  VectorAccessor vectors;
  Distance distance;
  Metrics metrics;
  Token token;
  dp::UnavailableCheckpointStore checkpoints;
  dp::StageContext context{workspace, 1024, 1, temporary, &vectors, &distance,
                           &checkpoints, &metrics, &token};
  dp::validate_stage_context(context);
  DP_EXPECT_TRUE(!checkpoints.available());
  DP_EXPECT_THROW(checkpoints.reject_unavailable_operation());

  auto invalid = context;
  invalid.thread_budget = 0;
  DP_EXPECT_THROW(dp::validate_stage_context(invalid));
  invalid = context;
  invalid.memory_budget_bytes = 0;
  DP_EXPECT_THROW(dp::validate_stage_context(invalid));

  const dp::ArtifactRef artifact{
      "normal/dataset/metadata.json", dp::ArtifactType::kDataset,
      "fake-dataset-json/v1",
      "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d"};
  const dp::DatasetIdentity dataset{
      "fake-six-node-v1",
      "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b",
      6, 2, "float32", "l2"};
  dp::BuildManifest manifest;
  manifest.run_id = "phase1-store";
  manifest.stage = "build";
  manifest.implementation = {"phase1_contracts", "1.0.0"};
  manifest.source = {"local", "master",
                     "0123456789abcdef0123456789abcdef01234567", false,
                     std::nullopt};
  manifest.toolchain = {"test", "/usr/bin/c++", "test", "4.3.3", "test",
                        "Debug", {}, {}, {}};
  manifest.dataset = dataset;
  manifest.resource_budget = {1024, 1};
  manifest.status = dp::StageStatus::kSucceeded;
  manifest.started_at = "2026-08-05T00:00:00Z";
  manifest.ended_at = "2026-08-05T00:00:01Z";
  const auto manifest_path =
      dp::persist_terminal_build_manifest(workspace, manifest);
  DP_EXPECT_TRUE(std::filesystem::exists(manifest_path));
  DP_EXPECT_TRUE(manifest_path.parent_path() == workspace.manifest_root);

  dp::EvaluationReport report;
  report.report_id = "phase1-report";
  report.artifact = artifact;
  report.validation_evidence_path = "manifests/evidence/valid.json";
  report.evaluator = {"phase1_evaluator", "1.0.0"};
  report.generated_at = "2026-08-05T00:00:02Z";
  report.findings = {"VALID", 6, 0, 3, 3, true};
  const auto report_path = dp::persist_evaluation_report(workspace, report);
  DP_EXPECT_TRUE(report_path.parent_path() == workspace.report_root);

  dp::ValidationEvidence evidence;
  evidence.validator_version = "shape/v1";
  evidence.artifact_candidate = {dp::ArtifactType::kDataset,
                                 "fake-dataset-json/v1",
                                 artifact.metadata_path,
                                 artifact.aggregate_checksum};
  evidence.checks = {{"shape", true, "shape recorded", std::nullopt,
                      std::nullopt}};
  evidence.result = dp::ValidationResult::kValid;
  const auto evidence_path =
      dp::persist_validation_evidence(workspace, "valid", evidence);
  DP_EXPECT_TRUE(evidence_path.parent_path().filename() == "evidence");

  dp::QuarantineRecord quarantine;
  quarantine.quarantine_id = "phase1-quarantine";
  quarantine.candidate_metadata_path = "candidate/metadata.json";
  quarantine.build_manifest_path = "build-manifest.json";
  quarantine.validation_evidence_path = "evidence.json";
  quarantine.failure = {dp::ErrorCategory::kChecksumMismatch,
                        "checksum mismatch", 5};
  quarantine.retained_at = "2026-08-05T00:00:03Z";
  const auto quarantine_path =
      dp::persist_quarantine_record(workspace, quarantine);
  DP_EXPECT_TRUE(quarantine_path.string().find(
                     workspace.quarantine_root.string()) == 0);

  std::filesystem::remove_all(root);
  return dp::test::finish();
}
