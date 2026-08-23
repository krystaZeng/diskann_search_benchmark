#include "diskann_pipeline/validation.hpp"

#include "search_artifact_test_support.hpp"
#include "support/test_assert.hpp"

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
  return dp::test::finish();
}
