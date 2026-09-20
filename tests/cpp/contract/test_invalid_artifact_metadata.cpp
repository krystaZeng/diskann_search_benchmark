#include "diskann_pipeline/validation.hpp"

#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "search/search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

#include <functional>
#include <map>

namespace dp = diskann_pipeline;

namespace {

dp::ArtifactValidationResult validate_partition(dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  return validator.validate(fixture.partition, {*dataset.validated_artifact});
}

dp::ArtifactValidationResult validate_direct_v2(
    const std::function<void(dp::test::Json&)>& mutation,
    bool recompute = true) {
  diskann_pipeline::cpp_main::test::TemporaryDirectory temporary(
      "direct-native-v2-invalid");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  diskann_pipeline::cpp_main::test::NativeDatasetFixture dataset(workspace, 4,
                                                                  2);
  auto graph =
      diskann_pipeline::cpp_main::test::materialize_direct_native_global_v2(
          workspace, dataset, {{1, 2}, {0, 3}, {0, 3}, {1, 2}}, {0, 2});
  dp::test::mutate_metadata(workspace, graph, mutation, recompute);
  dp::Validator validator(workspace);
  const auto validated_dataset = validator.validate(dataset.reference);
  return validator.validate(graph, {*validated_dataset.validated_artifact});
}

auto normal_checksums(const dp::WorkspaceRef& workspace) {
  std::map<std::filesystem::path, std::string> checksums;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(workspace.normal_root)) {
    if (entry.is_regular_file()) {
      checksums.emplace(entry.path(), dp::sha256_file(entry.path()));
    }
  }
  return checksums;
}

void expect_requested_identity(const dp::ArtifactValidationResult& result,
                               const dp::ArtifactRef& requested) {
  const auto& candidate = result.evidence.artifact_candidate;
  DP_EXPECT_EQ(candidate.metadata_path, requested.metadata_path);
  DP_EXPECT_EQ(candidate.artifact_type, requested.artifact_type);
  DP_EXPECT_EQ(candidate.format_version, requested.format_version);
  DP_EXPECT_EQ(candidate.claimed_aggregate_checksum,
               std::optional<std::string>{requested.aggregate_checksum});
}

void lineage_target_identity() {
  for (const bool catalogued : {true, false}) {
    for (const bool global : {false, true}) {
      dp::test::ContractArtifactFixture fixture;
      auto target = global ? fixture.global : fixture.dataset;
      if (!catalogued) {
        const auto original = dp::test::metadata_path(fixture.workspace, target);
        target.metadata_path.replace_filename("uncatalogued.json");
        std::filesystem::rename(
            original, dp::test::metadata_path(fixture.workspace, target));
      }
      const auto before = normal_checksums(fixture.workspace);
      dp::Validator validator(fixture.workspace);
      if (global) {
        // This graph cannot pass without resolving its Partition and both
        // LocalGraphs (which in turn require the Dataset and Partition).
        const auto without_sources = validator.validate(target);
        DP_EXPECT_TRUE(!without_sources.valid());
        DP_EXPECT_EQ(without_sources.evidence.failure_category,
                     std::optional{dp::ErrorCategory::kSourceMismatch});
      }
      const auto valid = validator.validate_with_lineage(target);
      DP_EXPECT_TRUE(valid.valid());
      expect_requested_identity(valid, target);
      if (valid.validated_artifact) {
        const auto& granted = valid.validated_artifact->reference();
        DP_EXPECT_EQ(granted.metadata_path, target.metadata_path);
        DP_EXPECT_EQ(granted.artifact_type, target.artifact_type);
        DP_EXPECT_EQ(granted.format_version, target.format_version);
        DP_EXPECT_EQ(granted.aggregate_checksum, target.aggregate_checksum);
        DP_EXPECT_EQ(valid.validated_artifact->source_artifacts().size(),
                     global ? std::size_t{3} : std::size_t{0});
      }
      for (const auto category : {dp::ErrorCategory::kChecksumMismatch,
                                  dp::ErrorCategory::kSchemaInvalid,
                                  dp::ErrorCategory::kUnsupportedVersion}) {
        auto requested = target;
        if (category == dp::ErrorCategory::kChecksumMismatch) {
          requested.aggregate_checksum = std::string(64, '0');
        } else if (category == dp::ErrorCategory::kSchemaInvalid) {
          requested.artifact_type = dp::ArtifactType::kPartition;
        } else {
          requested.format_version = global ? "global-graph-artifact/v2"
                                            : "fake-dataset-json/v2";
        }
        std::cerr << "ArtifactRef identity: " << (global ? "global" : "dataset")
                  << " catalogued=" << catalogued
                  << " expected=" << dp::to_string(category) << '\n';
        const auto rejected = validator.validate_with_lineage(requested);
        DP_EXPECT_TRUE(!rejected.valid());
        DP_EXPECT_TRUE(!rejected.validated_artifact.has_value());
        DP_EXPECT_EQ(rejected.evidence.result, dp::ValidationResult::kInvalid);
        DP_EXPECT_EQ(rejected.evidence.failure_category, std::optional{category});
        expect_requested_identity(rejected, requested);
      }
      DP_EXPECT_EQ(normal_checksums(fixture.workspace), before);
    }
  }
}

}  // namespace

int main() {
  lineage_target_identity();
  {
    dp::test::ContractArtifactFixture fixture;
    std::filesystem::remove(dp::test::metadata_path(fixture.workspace,
                                                    fixture.partition)
                                .parent_path() /
                            "p0-map.json");
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kPayloadMissing);
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["format_version"] = "global-graph-artifact/v1"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["lineage_mode"] = "other"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["source_artifacts"] = dp::test::Json::array(); });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2([](auto& json) {
      json["dataset"]["dataset_checksum"] = std::string(64, '0');
    });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["observed_max_degree"] = 1; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["structural"]["medoid_count"] = 3; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    auto result = validate_direct_v2(
        [](auto& json) { json["payloads"][1]["role"] = "centroids"; });
    DP_EXPECT_TRUE(!result.valid());
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.base.workspace, fixture.pq,
        [](auto& json) { json["payloads"][0]["path"] = "../escape.bin"; });
    dp::Validator validator(fixture.base.workspace);
    const auto dataset = validator.validate(fixture.base.dataset);
    const auto rejected =
        validator.validate(fixture.pq, {*dataset.validated_artifact});
    DP_EXPECT_EQ(rejected.evidence.failure_category.value(),
                 dp::ErrorCategory::kPathEscape);
  }
  {
    dp::search::test::SearchArtifactFixture fixture;
    fixture.layout.format_version = "disk-layout-artifact/v2";
    dp::Validator validator(fixture.base.workspace);
    const auto dataset = validator.validate(fixture.base.dataset);
    const auto rejected =
        validator.validate(fixture.layout, {*dataset.validated_artifact});
    DP_EXPECT_EQ(rejected.evidence.failure_category.value(),
                 dp::ErrorCategory::kUnsupportedVersion);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["payloads"][0]["byte_length"] = 38; });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kPayloadLengthMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["payloads"][0]["sha256"] = std::string(64, '0'); });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kChecksumMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.aggregate_checksum = std::string(64, '0');
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kChecksumMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.format_version = "partition-artifact/v2";
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kUnsupportedVersion);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    fixture.partition.artifact_type = dp::ArtifactType::kLocalGraph;
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kSchemaInvalid);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) { json["id_space"] = "global"; });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kIdSpaceMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& json) {
          json["source_artifacts"][0]["aggregate_checksum"] =
              std::string(64, '0');
        });
    DP_EXPECT_EQ(validate_partition(fixture).evidence.failure_category.value(),
                 dp::ErrorCategory::kSourceMismatch);
  }

  return dp::test::finish();
}
