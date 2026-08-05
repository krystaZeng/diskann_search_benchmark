#include "fakes/fake_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace diskann_pipeline::test {
namespace {

std::string mode(const std::filesystem::path& path) {
  std::ifstream input(path);
  return input ? nlohmann::json::parse(input).value("mode", "") : "";
}

class FakeLocalGraph final : public LocalGraphBuilder {
 public:
  StageResult build(const ArtifactRef& partition, PartitionId partition_id,
                    const LocalBuildConfig& config,
                    StageContext& context) override {
    context.metrics_sink->event("local_build.start");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    if (partition.artifact_type != ArtifactType::kPartition ||
        partition_id > 1) {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("local_build.after_input");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    const auto selected_mode = mode(config.stage_manifest_path);
    if (selected_mode == "fail") {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("local_build.before_publish");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    return {StageStatus::kSucceeded,
            materialize_local_candidate(context.temporary_storage.stage_root,
                                        context.workspace, partition,
                                        partition_id,
                                        selected_mode == "invalid_output"),
            {}};
  }
};

}  // namespace

std::unique_ptr<LocalGraphBuilder> make_fake_local_graph() {
  return std::make_unique<FakeLocalGraph>();
}

}  // namespace diskann_pipeline::test
