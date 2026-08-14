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

LoadedMergeInput load_merge_inputs(
    const WorkspaceRef& workspace, const ArtifactRef& partition,
    const std::vector<ArtifactRef>& local_graphs);

std::vector<GlobalNodeId> hnsw_merger_single_layer_search(
    const LogicalGraph& immutable_target, GlobalNodeId query,
    std::uint32_t result_count, const VectorAccessor& vectors,
    const DistanceFunction& distance);

std::vector<GlobalNodeId> hnsw_merger_vamana_prune(
    GlobalNodeId node, const std::vector<GlobalNodeId>& candidates,
    std::uint32_t degree_bound, double alpha, std::uint32_t max_candidates,
    const VectorAccessor& vectors, const DistanceFunction& distance);

LogicalGraph merge_pair(
    const LogicalGraph& source, const LogicalGraph& target,
    std::uint32_t lambda, std::uint32_t degree_bound, double alpha,
    std::uint32_t max_candidates, const VectorAccessor& vectors,
    const DistanceFunction& distance, PairwiseMergeDecision* decision);

MultiShardMergeResult merge_large_first(
    std::vector<LogicalGraph> graphs, std::uint32_t degree_bound,
    double alpha, std::uint32_t max_candidates, const VectorAccessor& vectors,
    const DistanceFunction& distance, CancellationToken* cancellation,
    MetricsSink* metrics);

}  // namespace diskann_pipeline::hnsw_merger_vamana
