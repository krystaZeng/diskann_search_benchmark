#include "diskann_pipeline/quarantine.hpp"
#include "diskann_pipeline/workspace.hpp"

#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <fstream>

namespace dp = diskann_pipeline;

int main() {
  dp::test::ContractArtifactFixture fixture;
  const auto candidate_root = fixture.workspace.temporary_root / "invalid-candidate";
  std::filesystem::create_directories(candidate_root);
  dp::test::overwrite_file(candidate_root / "artifact.json", "{\"invalid\":true}");

  const auto manifest = fixture.workspace.manifest_root / "invalid-run.json";
  const auto evidence = fixture.workspace.manifest_root / "evidence/invalid.json";
  dp::test::overwrite_file(manifest, "{\"status\":\"INVALID_OUTPUT\"}");
  dp::test::overwrite_file(evidence, "{\"result\":\"INVALID\"}");

  dp::QuarantineManager manager(fixture.workspace);
  const auto publication = manager.publish(
      "invalid-output-001", candidate_root, candidate_root / "artifact.json",
      manifest, evidence,
      {dp::ErrorCategory::kChecksumMismatch, "payload checksum mismatch",
       std::nullopt},
      "2026-08-05T00:00:00Z");

  DP_EXPECT_TRUE(!std::filesystem::exists(candidate_root));
  DP_EXPECT_TRUE(std::filesystem::exists(publication.record_path));
  DP_EXPECT_TRUE(publication.record.capabilities.empty());
  DP_EXPECT_TRUE(publication.record.isolated_from_normal_resolver);
  DP_EXPECT_THROW(dp::resolve_normal_artifact_metadata(
      fixture.workspace, "../quarantine/invalid-output-001/candidate/artifact.json"));

  dp::DiagnosticQuarantineResolver diagnostics(fixture.workspace);
  const auto record = diagnostics.read_record("invalid-output-001");
  DP_EXPECT_EQ(record.failure.category, dp::ErrorCategory::kChecksumMismatch);
  DP_EXPECT_EQ(diagnostics.read_text("invalid-output-001", "evidence.json"),
               std::string("{\"result\":\"INVALID\"}"));
  DP_EXPECT_TRUE(std::filesystem::exists(
      diagnostics.resolve("invalid-output-001", "candidate/artifact.json")));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      fixture.workspace.normal_root / "invalid-output-001"));

  diagnostics.remove("invalid-output-001");
  DP_EXPECT_TRUE(!std::filesystem::exists(publication.entry_root));
  DP_EXPECT_THROW(diagnostics.read_record("invalid-output-001"));

  return dp::test::finish();
}
