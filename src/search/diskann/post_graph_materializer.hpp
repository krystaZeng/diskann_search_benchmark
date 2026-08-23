#pragma once

#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace diskann_pipeline::search {

struct PqGenerationPolicy {
  double memory_budget_gib{0.03};
  std::uint32_t qd{0};
  std::uint32_t pq_chunks{32};
  double sample_rate{0.256};
  std::uint32_t centroid_count{256};
  bool opq{false};
  std::string distance_metric{"l2"};
};

struct DiskLayoutPolicy {
  std::string scalar_type{"float32"};
  std::uint32_t pq_disk_bytes{0};
  bool reorder{false};
  bool labels{false};
  std::string entry_policy{"end_to_end"};
  std::string centroid_policy{"medoid_vector_fallback"};
};

struct NativePostGraphCalls {
  std::function<void(const std::filesystem::path& dataset,
                     const std::filesystem::path& pivots,
                     const std::filesystem::path& compressed,
                     const PqGenerationPolicy& policy)>
      generate_pq;
  std::function<void(const std::filesystem::path& dataset,
                     const std::filesystem::path& global_graph,
                     const std::filesystem::path& disk_layout,
                     const DiskLayoutPolicy& policy)>
      create_disk_layout;
};

struct MaterializationRequest {
  std::string run_id;
  ArtifactRef dataset;
  std::optional<ArtifactRef> global_graph;
  SourceIdentity source;
  ToolchainIdentity toolchain;
  ResourceBudget resource_budget;
  std::string centroid_policy{"medoid_vector_fallback"};
  std::string started_at;
  std::string ended_at;
};

struct MaterializationResult {
  StageStatus status{StageStatus::kFailed};
  std::optional<ArtifactRef> artifact;
  std::filesystem::path build_manifest_path;
  std::filesystem::path validation_evidence_path;
  std::optional<std::string> quarantine_id;
  std::optional<FailureInfo> failure;
};

NativePostGraphCalls fixed_native_post_graph_calls();

class PostGraphMaterializer {
 public:
  PostGraphMaterializer(WorkspaceRef workspace, NativePostGraphCalls calls,
                        const CancellationToken* cancellation);

  MaterializationResult materialize_pq(
      const MaterializationRequest& request) const;
  MaterializationResult materialize_disk_layout(
      const MaterializationRequest& request) const;

 private:
  WorkspaceRef workspace_;
  NativePostGraphCalls calls_;
  const CancellationToken* cancellation_;
};

}  // namespace diskann_pipeline::search
