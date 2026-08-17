#pragma once

#include "diskann_pipeline/artifact_ref.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace diskann_pipeline::hnsw_merger_vamana {

struct LogicalGraph {
  std::vector<PartitionId> lineage;
  std::vector<ArtifactRef> source_local_graphs;
  std::map<GlobalNodeId, std::vector<GlobalNodeId>> adjacency;
  GlobalNodeId entry{0};
};

struct PairwiseMergeDecision {
  std::size_t step{0};
  std::vector<PartitionId> left_lineage;
  std::vector<PartitionId> right_lineage;
  std::uint64_t left_size{0};
  std::uint64_t right_size{0};
  std::vector<PartitionId> source_lineage;
  std::vector<PartitionId> target_lineage;
  std::uint64_t epoch_n0{0};
  std::uint32_t lambda_policy{4};
  std::uint32_t effective_lambda{0};
  std::uint64_t shared_node_count{0};
  std::vector<PartitionId> output_lineage;
  std::uint64_t output_size{0};
};

struct LoadedMergeInput {
  DatasetIdentity dataset;
  std::vector<LogicalGraph> graphs;
  std::vector<GlobalNodeId> medoids;
};

struct MultiShardMergeResult {
  LogicalGraph graph;
  std::vector<PairwiseMergeDecision> decisions;
};

std::uint64_t m1_vector_cache_required_bytes(
    NodeCount global_node_count, std::size_t vector_dimension);

class M1VectorCache final {
 public:
  M1VectorCache(const VectorAccessor& source, NodeCount global_node_count,
                std::uint64_t memory_budget_bytes,
                CancellationToken* cancellation);

  double distance(GlobalNodeId lhs, GlobalNodeId rhs,
                  const DistanceFunction& distance);
  std::uint64_t footprint_bytes() const noexcept { return footprint_bytes_; }

 private:
  void ensure_loaded(GlobalNodeId id);
  std::size_t row_offset(GlobalNodeId id) const;

  const VectorAccessor& source_;
  NodeCount global_node_count_;
  std::size_t dimension_;
  CancellationToken* cancellation_;
  std::vector<float> storage_;
  std::vector<std::uint8_t> loaded_;
  std::vector<float> left_scratch_;
  std::vector<float> right_scratch_;
  std::uint64_t footprint_bytes_;
};

LoadedMergeInput load_merge_inputs(
    const WorkspaceRef& workspace, const ArtifactRef& partition,
    const std::vector<ArtifactRef>& local_graphs);

std::vector<GlobalNodeId> hnsw_merger_single_layer_search(
    const LogicalGraph& immutable_target, GlobalNodeId query,
    std::uint32_t result_count, M1VectorCache& vectors,
    const DistanceFunction& distance);

std::vector<GlobalNodeId> hnsw_merger_vamana_prune(
    GlobalNodeId node, const std::vector<GlobalNodeId>& candidates,
    std::uint32_t degree_bound, double alpha, std::uint32_t max_candidates,
    M1VectorCache& vectors, const DistanceFunction& distance);

LogicalGraph merge_pair(
    const LogicalGraph& source, const LogicalGraph& target,
    std::uint32_t lambda, std::uint32_t degree_bound, double alpha,
    std::uint32_t max_candidates, M1VectorCache& vectors,
    const DistanceFunction& distance, PairwiseMergeDecision* decision);

MultiShardMergeResult merge_large_first(
    std::vector<LogicalGraph> graphs, std::uint32_t degree_bound,
    double alpha, std::uint32_t max_candidates, const VectorAccessor& vectors,
    const DistanceFunction& distance, NodeCount global_node_count,
    std::uint64_t memory_budget_bytes, CancellationToken* cancellation,
    MetricsSink* metrics);

}  // namespace diskann_pipeline::hnsw_merger_vamana
