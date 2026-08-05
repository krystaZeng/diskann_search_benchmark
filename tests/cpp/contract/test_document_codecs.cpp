#include "diskann_pipeline/document_codec.hpp"

#include "support/test_assert.hpp"

#include <string>

namespace dp = diskann_pipeline;

namespace {

dp::ArtifactRef artifact_ref() {
  return {"artifacts/dataset/metadata.json", dp::ArtifactType::kDataset,
          "fake-dataset-json/v1",
          "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d"};
}

dp::DatasetIdentity dataset() {
  return {"fake-six-node-v1",
          "2fdb9aa4fa5b41129e8f6577ce9a6282b33a5348c089e8e87184d7e83951027b",
          6,
          2,
          "float32",
          "l2"};
}

dp::BuildManifest build_manifest() {
  dp::BuildManifest manifest;
  manifest.run_id = "run-phase1";
  manifest.stage = "build";
  manifest.implementation = {"phase1_contracts", "1.0.0"};
  manifest.source = {"local-repository",
                     "master",
                     "0123456789abcdef0123456789abcdef01234567",
                     false,
                     std::nullopt};
  manifest.toolchain = {"test-os",
                        "/usr/bin/c++",
                        "test-compiler",
                        "4.3.3",
                        "test-generator",
                        "Debug",
                        {"BUILD_TESTING=ON"},
                        {"c++ -std=c++17"},
                        {"c++ test.o"}};
  manifest.dataset = dataset();
  manifest.parameters_json = R"({"finite":1.5,"optional":null})";
  manifest.resource_budget = {1048576, 1};
  manifest.input_artifacts = {artifact_ref()};
  manifest.output_artifacts = {};
  manifest.status = dp::StageStatus::kSucceeded;
  manifest.started_at = "2026-08-05T00:00:00Z";
  manifest.ended_at = "2026-08-05T00:00:01Z";
  manifest.metrics_json = R"({"ratio":1.5,"optional":null})";
  return manifest;
}

}  // namespace

int main() {
  const auto ref_json = dp::serialize_artifact_ref(artifact_ref());
  const auto parsed_ref = dp::parse_artifact_ref(ref_json);
  DP_EXPECT_EQ(parsed_ref.metadata_path, artifact_ref().metadata_path);
  DP_EXPECT_EQ(parsed_ref.aggregate_checksum,
               artifact_ref().aggregate_checksum);
  DP_EXPECT_THROW(dp::parse_artifact_ref(
      R"({"artifact_type":"dataset","format_version":"v1","aggregate_checksum":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","metadata_path":"a.json","unknown":true})"));

  const auto manifest_json = dp::serialize_build_manifest(build_manifest());
  const auto parsed_manifest = dp::parse_build_manifest(manifest_json);
  DP_EXPECT_EQ(parsed_manifest.run_id, std::string("run-phase1"));
  DP_EXPECT_EQ(parsed_manifest.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(manifest_json.find("manifest_version") != std::string::npos);
  DP_EXPECT_TRUE(manifest_json.find("report_version") == std::string::npos);

  dp::EvaluationReport report;
  report.report_id = "report-phase1";
  report.artifact = artifact_ref();
  report.validation_evidence_path = "evidence/valid.json";
  report.evaluator = {"phase1_evaluator", "1.0.0"};
  report.generated_at = "2026-08-05T00:00:02Z";
  report.findings = {"VALID", 6, 0, 3, 3, true};
  report.metrics_json = R"({"ratio":1.5,"optional":null})";
  const auto report_json = dp::serialize_evaluation_report(report);
  const auto parsed_report = dp::parse_evaluation_report(report_json);
  DP_EXPECT_EQ(parsed_report.report_id, std::string("report-phase1"));
  DP_EXPECT_TRUE(parsed_report.findings.passed);
  DP_EXPECT_THROW(dp::parse_evaluation_report(manifest_json));

  dp::ValidationEvidence evidence;
  evidence.validator_version = "phase1-validator-shape/v1";
  evidence.artifact_candidate = {
      dp::ArtifactType::kDataset,
      "fake-dataset-json/v1",
      "artifacts/dataset/metadata.json",
      artifact_ref().aggregate_checksum};
  evidence.checks = {{"shape.only", true, "schema shape is distinct",
                      std::nullopt, std::nullopt}};
  evidence.result = dp::ValidationResult::kValid;
  const auto evidence_json = dp::serialize_validation_evidence(evidence);
  DP_EXPECT_EQ(dp::parse_validation_evidence(evidence_json).checks.size(),
               std::size_t{1});

  dp::QuarantineRecord quarantine;
  quarantine.quarantine_id = "quarantine-phase1";
  quarantine.candidate_metadata_path = "quarantine/candidate/metadata.json";
  quarantine.build_manifest_path = "quarantine/build-manifest.json";
  quarantine.validation_evidence_path = "quarantine/evidence.json";
  quarantine.failure = {dp::ErrorCategory::kChecksumMismatch,
                        "candidate checksum mismatch", 5};
  quarantine.retained_at = "2026-08-05T00:00:03Z";
  const auto quarantine_json = dp::serialize_quarantine_record(quarantine);
  const auto parsed_quarantine =
      dp::parse_quarantine_record(quarantine_json);
  DP_EXPECT_EQ(parsed_quarantine.quarantine_id,
               std::string("quarantine-phase1"));
  DP_EXPECT_TRUE(parsed_quarantine.capabilities.empty());
  DP_EXPECT_TRUE(parsed_quarantine.isolated_from_normal_resolver);

  dp::ImplementationDescriptor descriptor{
      "phase1_merge_contract",
      "1.0.0",
      dp::StageKind::kMerge,
      {{{dp::ArtifactType::kPartition, {"partition-artifact/v1"},
         dp::IdSpace::kLocal},
        {dp::ArtifactType::kLocalGraph, {"local-graph-artifact/v1"},
         dp::IdSpace::kLocal}},
       {"partition.complete_local_to_global_map/v1"},
       {"workspace/v1"}},
      {dp::ArtifactType::kGlobalGraph, "global-graph-artifact/v1",
       dp::IdSpace::kGlobal, {"graph.global_adjacency/v1"}},
      "contracts/v1/schemas/fake-parameters.schema.json",
      true};
  const auto descriptor_json =
      dp::serialize_implementation_descriptor(descriptor);
  const auto parsed_descriptor =
      dp::parse_implementation_descriptor(descriptor_json);
  DP_EXPECT_EQ(parsed_descriptor.requires.input_artifacts.size(),
               std::size_t{2});
  DP_EXPECT_EQ(parsed_descriptor.parameter_schema_path,
               descriptor.parameter_schema_path);
  DP_EXPECT_EQ(parsed_descriptor.provides.artifact_type,
               dp::ArtifactType::kGlobalGraph);

  return dp::test::finish();
}
