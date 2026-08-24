#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/stores.hpp"
#include "search/diskann/native_search_formats.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

#include <filesystem>

namespace dp = diskann_pipeline;

namespace {

struct ValidatedChain {
  dp::ValidatedArtifact dataset;
  dp::ValidatedArtifact global;
};

ValidatedChain validate_base(
    dp::search::test::SearchArtifactFixture& fixture,
    dp::Validator& validator) {
  const auto dataset = validator.validate(fixture.base.dataset);
  DP_EXPECT_TRUE(dataset.valid());
  const auto partition = validator.validate(
      fixture.base.partition, {*dataset.validated_artifact});
  const auto local0 = validator.validate(
      fixture.base.local0, {*partition.validated_artifact});
  const auto local1 = validator.validate(
      fixture.base.local1, {*partition.validated_artifact});
  const auto global = validator.validate(
      fixture.base.global,
      {*partition.validated_artifact, *local0.validated_artifact,
       *local1.validated_artifact});
  DP_EXPECT_TRUE(global.valid());
  return {*dataset.validated_artifact, *global.validated_artifact};
}

dp::search::SearchAdmissionEvidenceRef fresh_evidence(
    dp::search::test::SearchArtifactFixture& fixture,
    const dp::ArtifactRef& artifact, const std::string& identity) {
  const auto result =
      dp::Validator(fixture.base.workspace).validate_with_lineage(artifact);
  DP_EXPECT_TRUE(result.valid());
  const auto path = dp::persist_validation_evidence(
      fixture.base.workspace, identity, result.evidence);
  return {std::filesystem::relative(path, fixture.base.workspace.root),
          dp::sha256_file(path), artifact};
}

void prevalidated_search_artifact_admission() {
  dp::search::test::SearchArtifactFixture fixture;
  const auto pq_evidence = fresh_evidence(fixture, fixture.pq, "fresh-pq");
  const auto layout_evidence =
      fresh_evidence(fixture, fixture.layout, "fresh-layout");
  const auto dataset = dp::search::admit_search_artifact_metadata(
      fixture.base.workspace, fixture.base.dataset);
  const auto global = dp::search::admit_search_artifact_metadata(
      fixture.base.workspace, fixture.base.global);
  const auto pq = dp::search::admit_prevalidated_search_artifact(
      fixture.base.workspace, fixture.pq, pq_evidence);
  const auto layout = dp::search::admit_prevalidated_search_artifact(
      fixture.base.workspace, fixture.layout, layout_evidence);
  dp::search::validate_search_artifact_lineage(dataset, global, pq, layout);

  auto missing = pq_evidence;
  missing.path = "manifests/evidence/missing.json";
  DP_EXPECT_THROW(dp::search::admit_prevalidated_search_artifact(
      fixture.base.workspace, fixture.pq, missing));
  auto bad_sha = pq_evidence;
  bad_sha.sha256 = std::string(64, '0');
  DP_EXPECT_THROW(dp::search::admit_prevalidated_search_artifact(
      fixture.base.workspace, fixture.pq, bad_sha));
  auto bad_ref = pq_evidence;
  bad_ref.expected_artifact.aggregate_checksum = std::string(64, '0');
  DP_EXPECT_THROW(dp::search::admit_prevalidated_search_artifact(
      fixture.base.workspace, fixture.pq, bad_ref));
}

void prevalidated_search_artifact_fail_closed_checks() {
  {
    dp::search::test::SearchArtifactFixture fixture;
    const auto evidence = fresh_evidence(fixture, fixture.pq, "fresh-pq-length");
    const auto metadata = dp::test::metadata_path(fixture.base.workspace,
                                                  fixture.pq);
    dp::test::overwrite_file(metadata.parent_path() / "codes.bin", "short");
    DP_EXPECT_THROW(dp::search::admit_prevalidated_search_artifact(
        fixture.base.workspace, fixture.pq, evidence));
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    const auto evidence = fresh_evidence(fixture, fixture.layout,
                                         "fresh-layout-missing");
    const auto metadata = dp::test::metadata_path(fixture.base.workspace,
                                                  fixture.layout);
    std::filesystem::remove(metadata.parent_path() / "disk.index");
    DP_EXPECT_THROW(dp::search::admit_prevalidated_search_artifact(
        fixture.base.workspace, fixture.layout, evidence));
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    const auto pq_evidence = fresh_evidence(fixture, fixture.pq, "fresh-pq-lineage");
    const auto layout_evidence =
        fresh_evidence(fixture, fixture.layout, "fresh-layout-lineage");
    dp::test::mutate_metadata(
        fixture.base.workspace, fixture.layout,
        [](auto& metadata) {
          metadata["source_artifacts"][1]["aggregate_checksum"] =
              std::string(64, '0');
        },
        false);
    const auto dataset = dp::search::admit_search_artifact_metadata(
        fixture.base.workspace, fixture.base.dataset);
    const auto global = dp::search::admit_search_artifact_metadata(
        fixture.base.workspace, fixture.base.global);
    const auto pq = dp::search::admit_prevalidated_search_artifact(
        fixture.base.workspace, fixture.pq, pq_evidence);
    const auto layout = dp::search::admit_prevalidated_search_artifact(
        fixture.base.workspace, fixture.layout, layout_evidence);
    DP_EXPECT_THROW(dp::search::validate_search_artifact_lineage(
        dataset, global, pq, layout));
  }
}

dp::search::SearchRunnerExecutionIdentity runner_declaration(
    const dp::WorkspaceRef& workspace,
    const std::filesystem::path& execution_root,
    const std::string& branch = "feature/search-evaluation") {
  const auto binary = execution_root / "bin/runner";
  dp::test::overwrite_file(binary, "instrumented-runner");
  const auto binary_sha = dp::sha256_file(binary);
  const auto manifest = workspace.manifest_root / "runner-build.json";
  dp::test::overwrite_file(
      manifest,
      nlohmann::json{
          {"manifest_version", "1.0.0"},
          {"run_id", "runner-build"},
          {"stage", "build"},
          {"implementation",
           {{"implementation_id", "build-owner"},
            {"implementation_version", "1.0.0"}}},
          {"source",
           {{"repository", "https://example.invalid/project.git"},
            {"branch", branch},
            {"commit", std::string(40, 'b')},
            {"dirty_worktree", false}}},
          {"parameters",
           {{"runner_binary", {{"path", "bin/runner"}, {"sha256", binary_sha}}},
            {"diskann_source",
             {{"repository", "https://github.com/microsoft/DiskANN.git"},
              {"branch", "cpp_main"},
              {"commit", "78256bbab4685e1774e78d331e081a153be26823"},
              {"dirty_worktree", false}}}}},
          {"status", "SUCCEEDED"}}
          .dump());
  return {{"bin/runner"},
          binary_sha,
          {"manifests/runner-build.json"},
          dp::sha256_file(manifest),
          {"https://example.invalid/project.git", branch, std::string(40, 'b'),
           false, std::nullopt},
          "cpp_main_pqflash_search_instrumented_v1",
          "1.0.0",
          "instrumented",
          "78256bbab4685e1774e78d331e081a153be26823"};
}

void runner_execution_identity_is_fail_closed() {
  dp::cpp_main::test::TemporaryDirectory temporary("search-runner-identity");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  auto declaration = runner_declaration(workspace, temporary.path);
  const auto executable = temporary.path / declaration.binary_path;
  dp::search::validate_search_runner_execution_identity(
      workspace, temporary.path, executable, declaration);

  auto bad_binary = declaration;
  bad_binary.binary_sha256 = std::string(64, '0');
  DP_EXPECT_THROW(dp::search::validate_search_runner_execution_identity(
      workspace, temporary.path, executable, bad_binary));
  auto bad_manifest = declaration;
  bad_manifest.build_manifest_sha256 = std::string(64, '0');
  DP_EXPECT_THROW(dp::search::validate_search_runner_execution_identity(
      workspace, temporary.path, executable, bad_manifest));
  auto bad_implementation = declaration;
  bad_implementation.implementation_id = "wrong_runner";
  DP_EXPECT_THROW(dp::search::validate_search_runner_execution_identity(
      workspace, temporary.path, executable, bad_implementation));

  declaration = runner_declaration(
      workspace, temporary.path, "wrong-project-branch");
  declaration.project_source.branch = "feature/search-evaluation";
  DP_EXPECT_THROW(dp::search::validate_search_runner_execution_identity(
      workspace, temporary.path, executable, declaration));
}

}  // namespace

int main() {
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::Validator validator(fixture.base.workspace);
    auto sources = validate_base(fixture, validator);
    const auto pq = validator.validate(fixture.pq, {sources.dataset});
    const auto layout =
        validator.validate(fixture.layout, {sources.dataset, sources.global});
    DP_EXPECT_TRUE(pq.valid());
    DP_EXPECT_TRUE(layout.valid());
    DP_EXPECT_TRUE(
        pq.validated_artifact->has_capability("search.pq_in_memory/v1"));
    DP_EXPECT_TRUE(
        layout.validated_artifact->has_capability("search.disk_layout/v1"));
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::Validator validator(fixture.base.workspace);
    auto sources = validate_base(fixture, validator);
    dp::test::mutate_metadata(
        fixture.base.workspace, fixture.pq,
        [](auto& metadata) {
          metadata["source_artifacts"][0]["aggregate_checksum"] =
              std::string(64, '0');
        });
    const auto rejected = validator.validate(fixture.pq, {sources.dataset});
    DP_EXPECT_TRUE(!rejected.valid());
    DP_EXPECT_EQ(rejected.evidence.failure_category.value(),
                 dp::ErrorCategory::kSourceMismatch);
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::Validator validator(fixture.base.workspace);
    auto sources = validate_base(fixture, validator);
    const auto reversed =
        validator.validate(fixture.layout, {sources.global, sources.dataset});
    DP_EXPECT_TRUE(!reversed.valid());
    DP_EXPECT_EQ(reversed.evidence.failure_category.value(),
                 dp::ErrorCategory::kSourceMismatch);
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::Validator validator(fixture.base.workspace);
    auto sources = validate_base(fixture, validator);
    dp::test::mutate_metadata(
        fixture.base.workspace, fixture.layout,
        [](auto& metadata) {
          metadata["structural"]["centroid_policy"] = "declared_centroids";
        });
    const auto rejected =
        validator.validate(fixture.layout, {sources.dataset, sources.global});
    DP_EXPECT_TRUE(!rejected.valid());
  }
  prevalidated_search_artifact_admission();
  prevalidated_search_artifact_fail_closed_checks();
  runner_execution_identity_is_fail_closed();
  return dp::test::finish();
}
