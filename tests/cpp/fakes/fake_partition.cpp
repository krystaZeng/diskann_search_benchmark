#include "fakes/fake_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace diskann_pipeline::test {
namespace {

std::string mode(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  const auto json = nlohmann::json::parse(input);
  return json.value("mode", "");
}

class FakePartition final : public PartitionStrategy {
 public:
  StageResult run(const ArtifactRef& dataset,
                  const std::filesystem::path& stage_manifest_path,
                  StageContext& context) override {
    context.metrics_sink->event("partition.start");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    if (dataset.artifact_type != ArtifactType::kDataset ||
        dataset.aggregate_checksum != kDatasetAggregateChecksum) {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("partition.after_input");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    if (mode(stage_manifest_path) == "fail") {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("partition.before_publish");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    return {StageStatus::kSucceeded,
            materialize_partition_candidate(
                context.temporary_storage.stage_root, dataset),
            {}};
  }
};

}  // namespace

std::unique_ptr<PartitionStrategy> make_fake_partition() {
  return std::make_unique<FakePartition>();
}

}  // namespace diskann_pipeline::test
