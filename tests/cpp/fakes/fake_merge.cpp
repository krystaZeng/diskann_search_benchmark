#include "fakes/fake_pipeline.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace diskann_pipeline::test {
namespace {

std::string mode(const std::filesystem::path& path) {
  std::ifstream input(path);
  return input ? nlohmann::json::parse(input).value("mode", "") : "";
}

class FakeMerge final : public GraphMerger {
 public:
  StageResult merge(const ArtifactRef& partition,
                    const std::vector<ArtifactRef>& local_graphs,
                    const std::filesystem::path& stage_manifest_path,
                    StageContext& context) override {
    context.metrics_sink->event("merge.start");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    if (partition.artifact_type != ArtifactType::kPartition ||
        local_graphs.size() != 2) {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("merge.after_input");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    const auto selected_mode = mode(stage_manifest_path);
    if (selected_mode == "fail") {
      return {StageStatus::kFailed, std::nullopt, {}};
    }
    context.metrics_sink->event("merge.before_publish");
    if (context.cancellation_token->is_cancelled()) {
      return {StageStatus::kCancelled, std::nullopt, {}};
    }
    return {StageStatus::kSucceeded,
            materialize_global_candidate(context.temporary_storage.stage_root,
                                         context.workspace, partition,
                                         local_graphs,
                                         selected_mode == "invalid_output"),
            {}};
  }
};

}  // namespace

std::unique_ptr<GraphMerger> make_fake_merge() {
  return std::make_unique<FakeMerge>();
}

}  // namespace diskann_pipeline::test
