#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace diskann_pipeline::gp_ann_ogp {
namespace {

bool cancelled(const StageContext& context) {
  return context.cancellation_token != nullptr &&
         context.cancellation_token->is_cancelled();
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "OGP parameter document is missing");
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string run_id_from_parameter_path(const WorkspaceRef& workspace,
                                       const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = std::filesystem::relative(
      path, workspace.manifest_root, error);
  if (error || relative.empty()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to resolve OGP run identity");
  }
  static_cast<void>(resolve_confined_path(workspace.manifest_root, relative));
  auto component = relative.begin();
  if (component == relative.end() || *component == "." || *component == "..") {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "OGP run identity is not confined");
  }
  return component->string();
}

class OgpPartition final : public PartitionStrategy {
 public:
  explicit OgpPartition(GpAnnSourceBuildEvidence evidence)
      : evidence_(std::move(evidence)) {}

  StageResult run(const ArtifactRef& dataset,
                  const std::filesystem::path& stage_manifest_path,
                  StageContext& context) override {
    std::filesystem::path subprocess_manifest;
    try {
      validate_stage_context(context);
      context.metrics_sink->event("gp_ann_ogp.partition.start");
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      cpp_main::CppMainVectorAccessor vectors(context.workspace, dataset);
      const auto parameters = parse_parameters(
          read_text(stage_manifest_path),
          vectors.dataset_identity().global_node_count);
      const auto run_id =
          run_id_from_parameter_path(context.workspace, stage_manifest_path);
      context.metrics_sink->event("gp_ann_ogp.partition.after_input");
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, {}};
      }

      const auto invocation = run_official_partition(
          evidence_, vectors.dataset_identity(), vectors.native_payload_path(),
          std::filesystem::file_size(vectors.native_payload_path()),
          vectors.dataset_identity().dataset_checksum, parameters, run_id,
          context);
      subprocess_manifest = invocation.build_manifest_path;
      if (invocation.process_status != StageStatus::kSucceeded) {
        return {StageStatus::kFailed, std::nullopt, subprocess_manifest};
      }
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, subprocess_manifest};
      }
      if (!invocation.expected_output_sha256 ||
          !std::filesystem::is_regular_file(invocation.expected_output_path)) {
        throw PipelineError(ErrorCategory::kPayloadMissing,
                            "official GP-ANN Partition produced no cluster output");
      }
      const auto clusters = parse_and_canonicalize_clusters(
          invocation.expected_output_path,
          vectors.dataset_identity().global_node_count);
      const auto candidate = materialize_partition_candidate(
          context.temporary_storage.stage_root, dataset,
          vectors.dataset_identity(), clusters);
      context.metrics_sink->event("gp_ann_ogp.partition.before_publish");
      if (cancelled(context)) {
        return {StageStatus::kCancelled, std::nullopt, subprocess_manifest};
      }
      return {StageStatus::kSucceeded, candidate, subprocess_manifest};
    } catch (const PipelineError& error) {
      if (error.category() == ErrorCategory::kCancelled) {
        return {StageStatus::kCancelled, std::nullopt, subprocess_manifest};
      }
      return {StageStatus::kFailed, std::nullopt, subprocess_manifest};
    } catch (const std::exception&) {
      return {StageStatus::kFailed, std::nullopt, subprocess_manifest};
    }
  }

 private:
  GpAnnSourceBuildEvidence evidence_;
};

}  // namespace

std::unique_ptr<PartitionStrategy> make_ogp_partition(
    GpAnnSourceBuildEvidence evidence) {
  return std::make_unique<OgpPartition>(std::move(evidence));
}

}  // namespace diskann_pipeline::gp_ann_ogp
