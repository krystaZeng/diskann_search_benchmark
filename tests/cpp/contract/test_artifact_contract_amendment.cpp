#include "diskann_pipeline/validation.hpp"

#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"

#include <algorithm>
#include <string>
#include <type_traits>

namespace dp = diskann_pipeline;

static_assert(std::is_same_v<
              decltype(dp::PartitionRecord::shard_vectors_role),
              std::optional<std::string>>);

namespace {

class MatchingVectorAccessor final : public dp::VectorAccessor {
 public:
  std::size_t dimension() const noexcept override { return 2; }
  void read(dp::GlobalNodeId, std::vector<float>& destination) const override {
    destination = {0.0F, 0.0F};
  }
};

dp::ArtifactValidationResult validate_partition(
    dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  DP_EXPECT_TRUE(dataset.valid());
  return validator.validate(fixture.partition,
                            {*dataset.validated_artifact});
}

dp::ArtifactValidationResult validate_global(
    dp::test::ContractArtifactFixture& fixture) {
  dp::Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  const auto partition =
      validator.validate(fixture.partition, {*dataset.validated_artifact});
  const auto local0 =
      validator.validate(fixture.local0, {*partition.validated_artifact});
  const auto local1 =
      validator.validate(fixture.local1, {*partition.validated_artifact});
  DP_EXPECT_TRUE(dataset.valid() && partition.valid() && local0.valid() &&
                 local1.valid());
  return validator.validate(
      fixture.global,
      {*partition.validated_artifact, *local0.validated_artifact,
       *local1.validated_artifact});
}

void remove_shard_vectors(dp::test::Json& metadata) {
  for (auto& record : metadata.at("structural").at("partitions")) {
    record.erase("shard_vectors_role");
  }
  auto& payloads = metadata.at("payloads");
  payloads.erase(
      std::remove_if(payloads.begin(), payloads.end(), [](const auto& payload) {
        return payload.at("role").template get<std::string>().find("/vectors") !=
               std::string::npos;
      }),
      payloads.end());
}

void add_optional_payload(dp::test::ContractArtifactFixture& fixture,
                          dp::ArtifactRef& reference,
                          const std::string& role,
                          const std::string& file_name,
                          const std::string& media_type,
                          const std::string& bytes,
                          const std::string& capability) {
  const auto metadata_file = dp::test::metadata_path(fixture.workspace, reference);
  dp::test::overwrite_file(metadata_file.parent_path() / file_name, bytes);
  dp::test::mutate_metadata(
      fixture.workspace, reference,
      [&](auto& metadata) {
        auto& capabilities = metadata.at("capabilities");
        capabilities.push_back(capability);
        std::sort(capabilities.begin(), capabilities.end());
        metadata.at("payloads").push_back(
            {{"role", role},
             {"path", file_name},
             {"media_type", media_type},
             {"byte_length", bytes.size()},
             {"sha256", dp::sha256_bytes(bytes)}});
      });
}

}  // namespace

int main() {
  {
    dp::test::ContractArtifactFixture fixture;
    DP_EXPECT_TRUE(validate_partition(fixture).valid());
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(fixture.workspace, fixture.partition,
                              remove_shard_vectors);
    const auto result = validate_partition(fixture);
    DP_EXPECT_TRUE(result.valid());
    DP_EXPECT_EQ(dp::resolve_partition_vector_source(
                     *result.validated_artifact, dp::PartitionId{0}),
                 dp::PartitionVectorSource::kVectorAccessor);
    MatchingVectorAccessor accessor;
    dp::require_partition_vector_source(*result.validated_artifact,
                                        dp::PartitionId{0}, &accessor);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    const auto result = validate_partition(fixture);
    DP_EXPECT_EQ(dp::resolve_partition_vector_source(
                     *result.validated_artifact, dp::PartitionId{0}),
                 dp::PartitionVectorSource::kArtifactPayload);
    dp::require_partition_vector_source(*result.validated_artifact,
                                        dp::PartitionId{0}, nullptr);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(fixture.workspace, fixture.partition,
                              remove_shard_vectors);
    const auto result = validate_partition(fixture);
    DP_EXPECT_TRUE(result.valid());
    DP_EXPECT_THROW(dp::require_partition_vector_source(
        *result.validated_artifact, dp::PartitionId{0}, nullptr));
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition, [](auto& metadata) {
          metadata.at("structural").at("partitions").at(0).erase(
              "local_to_global_map_role");
        });
    DP_EXPECT_TRUE(!validate_partition(fixture).valid());
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.partition,
        [](auto& metadata) { metadata.at("source_artifacts").clear(); });
    const auto result = validate_partition(fixture);
    DP_EXPECT_TRUE(!result.valid());
    DP_EXPECT_EQ(result.evidence.failure_category.value(),
                 dp::ErrorCategory::kSourceMismatch);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(fixture.workspace, fixture.partition,
                              remove_shard_vectors);
    add_optional_payload(
        fixture, fixture.partition, "centroids", "centroids.bin",
        "application/vnd.diskann.centroids+octet-stream;version=1",
        "centroids-v1", "partition.centroids/v1");
    DP_EXPECT_TRUE(validate_partition(fixture).valid());
  }
  {
    dp::test::ContractArtifactFixture fixture;
    DP_EXPECT_TRUE(validate_global(fixture).valid());
  }
  {
    dp::test::ContractArtifactFixture fixture;
    add_optional_payload(
        fixture, fixture.global, "medoids", "medoids.bin",
        "application/vnd.diskann.medoids+octet-stream;version=1", "medoids-a",
        "graph.medoids/v1");
    const auto first = validate_global(fixture);
    DP_EXPECT_TRUE(first.valid());
    const auto first_checksum = fixture.global.aggregate_checksum;
    dp::test::replace_payload(fixture.workspace, fixture.global, "medoids",
                              "medoids-b");
    const auto second = validate_global(fixture);
    DP_EXPECT_TRUE(second.valid());
    DP_EXPECT_TRUE(first_checksum != fixture.global.aggregate_checksum);
  }
  {
    dp::test::ContractArtifactFixture fixture;
    dp::test::mutate_metadata(
        fixture.workspace, fixture.global, [](auto& metadata) {
          auto& capabilities = metadata.at("capabilities");
          capabilities.push_back("graph.medoids/v1");
          std::sort(capabilities.begin(), capabilities.end());
        });
    const auto result = validate_global(fixture);
    DP_EXPECT_TRUE(!result.valid());
    DP_EXPECT_EQ(result.evidence.failure_category.value(),
                 dp::ErrorCategory::kPayloadMissing);
  }

  return dp::test::finish();
}
