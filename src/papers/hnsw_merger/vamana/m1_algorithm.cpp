#include "papers/hnsw_merger/vamana/m1_algorithm.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include "in_mem_graph_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <queue>
#include <set>
#include <tuple>

namespace diskann_pipeline::hnsw_merger_vamana {
namespace {

using Json = nlohmann::json;

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "M1 input metadata is missing");
  }
  return detail::parse_json_strict(
      std::string(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>()));
}

DatasetIdentity dataset_from(const Json& value) {
  detail::require_object_shape(
      value, {"dataset_id", "dataset_checksum", "global_node_count",
              "vector_dimension", "scalar_type", "distance_metric"});
  return {value.at("dataset_id").get<std::string>(),
          value.at("dataset_checksum").get<std::string>(),
          value.at("global_node_count").get<NodeCount>(),
          value.at("vector_dimension").get<std::uint32_t>(),
          value.at("scalar_type").get<std::string>(),
          value.at("distance_metric").get<std::string>()};
}

bool same_dataset(const DatasetIdentity& lhs, const DatasetIdentity& rhs) {
  return lhs.dataset_id == rhs.dataset_id &&
         lhs.dataset_checksum == rhs.dataset_checksum &&
         lhs.global_node_count == rhs.global_node_count &&
         lhs.vector_dimension == rhs.vector_dimension &&
         lhs.scalar_type == rhs.scalar_type &&
         lhs.distance_metric == rhs.distance_metric;
}

Json source_json(const ArtifactRef& source) {
  return {{"artifact_type", to_string(source.artifact_type)},
          {"format_version", source.format_version},
          {"aggregate_checksum", source.aggregate_checksum}};
}

std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection;
  projection.artifact_type = artifact_type_from_string(
      metadata.at("artifact_type").get<std::string>());
  projection.format_version = metadata.at("format_version").get<std::string>();
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space =
      id_space_from_string(metadata.at("id_space").get<std::string>());
  projection.capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  for (const auto& payload : metadata.at("payloads")) {
    projection.payloads.push_back(
        {payload.at("role").get<std::string>(),
         payload.at("byte_length").get<std::uint64_t>(),
         payload.at("sha256").get<std::string>()});
  }
  for (const auto& source : metadata.at("source_artifacts")) {
    projection.source_artifacts.push_back(
        {artifact_type_from_string(source.at("artifact_type").get<std::string>()),
         source.at("format_version").get<std::string>(),
         source.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return aggregate_checksum(projection);
}

void verify_reference(const ArtifactRef& reference, const Json& metadata,
                      ArtifactType type, const char* version,
                      const char* id_space) {
  if (reference.artifact_type != type || reference.format_version != version ||
      metadata.at("artifact_type") != to_string(type) ||
      metadata.at("format_version") != version ||
      metadata.at("id_space") != id_space ||
      metadata.at("aggregate_checksum") != reference.aggregate_checksum ||
      aggregate_for(metadata) != reference.aggregate_checksum) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "M1 Artifact reference identity is inconsistent");
  }
}

const Json& payload_for(const Json& metadata, const std::string& role) {
  const auto found = std::find_if(
      metadata.at("payloads").begin(), metadata.at("payloads").end(),
      [&](const auto& value) { return value.at("role") == role; });
  if (found == metadata.at("payloads").end()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "M1 required payload is missing: " + role);
  }
  return *found;
}

std::filesystem::path checked_payload(
    const std::filesystem::path& metadata_path, const Json& payload) {
  const auto path = resolve_confined_path(
      metadata_path.parent_path(), payload.at("path").get<std::string>());
  if (!std::filesystem::is_regular_file(path) ||
      std::filesystem::file_size(path) !=
          payload.at("byte_length").get<std::uint64_t>() ||
      sha256_file(path) != payload.at("sha256").get<std::string>()) {
    throw PipelineError(ErrorCategory::kChecksumMismatch,
                        "M1 payload identity is inconsistent");
  }
  return path;
}

struct PartitionData {
  DatasetIdentity dataset;
  std::vector<std::vector<GlobalNodeId>> maps;
  std::vector<std::string> map_checksums;
};

PartitionData load_partition(const WorkspaceRef& workspace,
                             const ArtifactRef& reference) {
  const auto metadata_path =
      resolve_normal_artifact_metadata(workspace, reference.metadata_path);
  const auto metadata = read_json(metadata_path);
  verify_reference(reference, metadata, ArtifactType::kPartition,
                   "partition-artifact/v1", "local");
  PartitionData result;
  result.dataset = dataset_from(metadata.at("dataset"));
  const auto count =
      metadata.at("structural").at("partition_count").get<NodeCount>();
  const auto& records = metadata.at("structural").at("partitions");
  if (count == 0 || records.size() != count) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "M1 Partition record set is incomplete");
  }
  for (std::size_t index = 0; index < records.size(); ++index) {
    const auto& record = records.at(index);
    if (record.at("partition_id").get<PartitionId>() != index) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "M1 Partition ownership is not contiguous");
    }
    const auto& payload = payload_for(
        metadata,
        record.at("local_to_global_map_role").get<std::string>());
    if (payload.at("media_type") != cpp_main::kPartitionIdMapMediaType) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "M1 requires reviewed native uint32 maps");
    }
    const auto path = checked_payload(metadata_path, payload);
    result.maps.push_back(cpp_main::validate_partition_id_map(
        path, record.at("local_node_count").get<NodeCount>(),
        result.dataset.global_node_count));
    result.map_checksums.push_back(payload.at("sha256").get<std::string>());
  }
  return result;
}

double node_distance(GlobalNodeId lhs, GlobalNodeId rhs,
                     const VectorAccessor& vectors,
                     const DistanceFunction& distance) {
  std::vector<float> left;
  std::vector<float> right;
  vectors.read(lhs, left);
  vectors.read(rhs, right);
  return distance.evaluate(left, right);
}

bool lineage_less(const LogicalGraph& lhs, const LogicalGraph& rhs) {
  return std::lexicographical_compare(
      lhs.lineage.begin(), lhs.lineage.end(), rhs.lineage.begin(),
      rhs.lineage.end());
}

std::uint64_t intersection_count(const LogicalGraph& lhs,
                                 const LogicalGraph& rhs) {
  std::uint64_t count = 0;
  auto left = lhs.adjacency.begin();
  auto right = rhs.adjacency.begin();
  while (left != lhs.adjacency.end() && right != rhs.adjacency.end()) {
    if (left->first < right->first) {
      ++left;
    } else if (right->first < left->first) {
      ++right;
    } else {
      ++count;
      ++left;
      ++right;
    }
  }
  return count;
}

std::vector<GlobalNodeId> normalize_candidates(
    GlobalNodeId node, std::vector<GlobalNodeId> candidates,
    NodeCount global_count) {
  for (const auto candidate : candidates) {
    if (!is_valid_global_node_id(candidate, global_count)) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "M1 candidate is out of GlobalNodeId range");
    }
    if (candidate == node) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "M1 candidate contains a self edge");
    }
  }
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

}  // namespace

LoadedMergeInput load_merge_inputs(
    const WorkspaceRef& workspace, const ArtifactRef& partition_reference,
    const std::vector<ArtifactRef>& local_graphs) {
  const auto partition = load_partition(workspace, partition_reference);
  if (local_graphs.size() != partition.maps.size()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "M1 LocalGraph set does not match Partition count");
  }
  LoadedMergeInput result;
  result.dataset = partition.dataset;
  std::vector<bool> coverage(
      static_cast<std::size_t>(partition.dataset.global_node_count), false);
  for (std::size_t index = 0; index < local_graphs.size(); ++index) {
    const auto& reference = local_graphs[index];
    const auto metadata_path =
        resolve_normal_artifact_metadata(workspace, reference.metadata_path);
    const auto metadata = read_json(metadata_path);
    verify_reference(reference, metadata, ArtifactType::kLocalGraph,
                     "local-graph-artifact/v1", "local");
    const auto id =
        metadata.at("structural").at("partition_id").get<PartitionId>();
    if (id != index || id >= partition.maps.size() ||
        !same_dataset(dataset_from(metadata.at("dataset")),
                      partition.dataset) ||
        metadata.at("source_artifacts") !=
            Json::array({source_json(partition_reference)}) ||
        metadata.at("structural").at("source_partition_checksum") !=
            partition_reference.aggregate_checksum) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "M1 LocalGraph lineage/ownership is inconsistent");
    }
    const auto& map_payload = payload_for(metadata, "local_to_global_map");
    const auto& graph_payload = payload_for(metadata, "local_graph");
    if (map_payload.at("media_type") != cpp_main::kPartitionIdMapMediaType ||
        graph_payload.at("media_type") != cpp_main::kLocalGraphMediaType ||
        map_payload.at("sha256") != partition.map_checksums[id]) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "M1 LocalGraph map binding is inconsistent");
    }
    checked_payload(metadata_path, map_payload);
    const auto graph_path = checked_payload(metadata_path, graph_payload);
    const auto& map = partition.maps[id];
    const auto declared = metadata.at("structural")
                              .at("declared_max_degree")
                              .get<std::uint32_t>();
    const auto facts = cpp_main::validate_local_graph(
        graph_path, static_cast<NodeCount>(map.size()), declared);
    diskann::InMemGraphStore store(map.size(), declared);
    const auto loaded = store.load(graph_path.string(), map.size());
    if (std::get<0>(loaded) != map.size() ||
        std::get<1>(loaded) != facts.entry_point || std::get<2>(loaded) != 0) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "M1 native graph facts are inconsistent");
    }
    LogicalGraph logical;
    logical.lineage = {id};
    logical.source_local_graphs = {reference};
    logical.entry = map[facts.entry_point];
    result.medoids.push_back(logical.entry);
    for (std::size_t local_id = 0; local_id < map.size(); ++local_id) {
      const auto global = map[local_id];
      coverage[global] = true;
      std::vector<GlobalNodeId> neighbors;
      for (const auto local_neighbor : store.get_neighbours(local_id)) {
        if (!is_valid_local_node_id(local_neighbor, map.size())) {
          throw PipelineError(ErrorCategory::kIdRangeInvalid,
                              "M1 local neighbor is out of range");
        }
        neighbors.push_back(map[local_neighbor]);
      }
      logical.adjacency.emplace(
          global, normalize_candidates(global, std::move(neighbors),
                                       partition.dataset.global_node_count));
    }
    result.graphs.push_back(std::move(logical));
  }
  if (std::find(coverage.begin(), coverage.end(), false) != coverage.end()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        "M1 combined LocalGraph coverage is incomplete");
  }
  return result;
}

std::vector<GlobalNodeId> hnsw_merger_single_layer_search(
    const LogicalGraph& immutable_target, GlobalNodeId query,
    std::uint32_t result_count, const VectorAccessor& vectors,
    const DistanceFunction& distance) {
  using Ranked = std::pair<double, GlobalNodeId>;
  if (immutable_target.adjacency.empty() || result_count == 0 ||
      immutable_target.adjacency.count(immutable_target.entry) == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "M1 target graph has no valid entry");
  }
  auto compare = [](const Ranked& lhs, const Ranked& rhs) {
    return lhs > rhs;
  };
  std::priority_queue<Ranked, std::vector<Ranked>, decltype(compare)> queue(
      compare);
  std::set<GlobalNodeId> visited;
  std::vector<Ranked> ranked;
  const auto push = [&](GlobalNodeId id, auto& pending, auto& seen) {
    if (seen.insert(id).second) {
      pending.push({node_distance(query, id, vectors, distance), id});
    }
  };
  push(immutable_target.entry, queue, visited);
  while (!queue.empty()) {
    const auto current = queue.top();
    queue.pop();
    ranked.push_back(current);
    const auto& neighbors = immutable_target.adjacency.at(current.second);
    for (const auto neighbor : neighbors) {
      if (immutable_target.adjacency.count(neighbor) == 0) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "M1 target adjacency escapes target graph");
      }
      push(neighbor, queue, visited);
    }
  }
  std::sort(ranked.begin(), ranked.end());
  if (ranked.size() > result_count) ranked.resize(result_count);
  std::vector<GlobalNodeId> result;
  for (const auto& item : ranked) result.push_back(item.second);
  return result;
}

std::vector<GlobalNodeId> hnsw_merger_vamana_prune(
    GlobalNodeId node, const std::vector<GlobalNodeId>& candidates,
    std::uint32_t degree_bound, double alpha, std::uint32_t max_candidates,
    const VectorAccessor& vectors, const DistanceFunction& distance) {
  using Ranked = std::pair<double, GlobalNodeId>;
  std::vector<Ranked> pool;
  pool.reserve(candidates.size());
  for (const auto candidate : candidates) {
    if (candidate == node) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "M1 pruning input contains a self edge");
    }
    pool.push_back({node_distance(node, candidate, vectors, distance),
                    candidate});
  }
  std::sort(pool.begin(), pool.end());
  if (pool.size() > max_candidates) pool.resize(max_candidates);
  std::vector<double> occlusion(pool.size(), 0.0);
  std::vector<GlobalNodeId> result;
  double current_alpha = 1.0;
  while (current_alpha <= alpha + 1e-9 && result.size() < degree_bound) {
    for (std::size_t index = 0;
         index < pool.size() && result.size() < degree_bound; ++index) {
      if (occlusion[index] > current_alpha) continue;
      occlusion[index] = std::numeric_limits<double>::max();
      result.push_back(pool[index].second);
      for (std::size_t later = index + 1; later < pool.size(); ++later) {
        if (occlusion[later] > alpha) continue;
        const auto pair_distance = node_distance(
            pool[later].second, pool[index].second, vectors, distance);
        const auto factor = pair_distance == 0.0
                                ? std::numeric_limits<double>::max()
                                : pool[later].first / pair_distance;
        occlusion[later] = std::max(occlusion[later], factor);
      }
    }
    current_alpha *= 1.2;
  }
  return result;
}

LogicalGraph merge_pair(
    const LogicalGraph& source, const LogicalGraph& target,
    std::uint32_t lambda, std::uint32_t degree_bound, double alpha,
    std::uint32_t max_candidates, const VectorAccessor& vectors,
    const DistanceFunction& distance, PairwiseMergeDecision* decision) {
  std::map<GlobalNodeId, std::vector<GlobalNodeId>> forward;
  std::map<GlobalNodeId, std::vector<GlobalNodeId>> backward;
  for (const auto& node : source.adjacency) {
    if (target.adjacency.count(node.first) != 0) continue;
    auto hits = hnsw_merger_single_layer_search(
        target, node.first, lambda, vectors, distance);
    for (const auto hit : hits) backward[hit].push_back(node.first);
    forward.emplace(node.first, std::move(hits));
  }

  LogicalGraph output;
  output.lineage = source.lineage;
  output.lineage.insert(output.lineage.end(), target.lineage.begin(),
                        target.lineage.end());
  std::sort(output.lineage.begin(), output.lineage.end());
  output.lineage.erase(std::unique(output.lineage.begin(), output.lineage.end()),
                       output.lineage.end());
  output.source_local_graphs = source.source_local_graphs;
  output.source_local_graphs.insert(output.source_local_graphs.end(),
                                    target.source_local_graphs.begin(),
                                    target.source_local_graphs.end());
  std::sort(output.source_local_graphs.begin(), output.source_local_graphs.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.metadata_path.generic_string() <
                     rhs.metadata_path.generic_string();
            });
  output.entry = target.entry;
  std::set<GlobalNodeId> nodes;
  for (const auto& item : source.adjacency) nodes.insert(item.first);
  for (const auto& item : target.adjacency) nodes.insert(item.first);
  const NodeCount global_count =
      nodes.empty() ? 0 : static_cast<NodeCount>(*nodes.rbegin()) + 1;
  for (const auto node : nodes) {
    std::vector<GlobalNodeId> candidates;
    const auto in_source = source.adjacency.find(node);
    const auto in_target = target.adjacency.find(node);
    if (in_source != source.adjacency.end()) {
      candidates.insert(candidates.end(), in_source->second.begin(),
                        in_source->second.end());
    }
    if (in_target != target.adjacency.end()) {
      candidates.insert(candidates.end(), in_target->second.begin(),
                        in_target->second.end());
    }
    if (in_source != source.adjacency.end() &&
        in_target == target.adjacency.end()) {
      const auto found = forward.find(node);
      if (found != forward.end()) {
        candidates.insert(candidates.end(), found->second.begin(),
                          found->second.end());
      }
    }
    const auto reverse = backward.find(node);
    if (reverse != backward.end()) {
      candidates.insert(candidates.end(), reverse->second.begin(),
                        reverse->second.end());
    }
    candidates = normalize_candidates(node, std::move(candidates), global_count);
    std::sort(candidates.begin(), candidates.end(), [&](auto lhs, auto rhs) {
      return std::make_pair(node_distance(node, lhs, vectors, distance), lhs) <
             std::make_pair(node_distance(node, rhs, vectors, distance), rhs);
    });
    if (candidates.size() > degree_bound) {
      candidates = hnsw_merger_vamana_prune(
          node, candidates, degree_bound, alpha, max_candidates, vectors,
          distance);
    }
    output.adjacency.emplace(node, std::move(candidates));
  }
  if (decision) {
    decision->shared_node_count = intersection_count(source, target);
    decision->output_lineage = output.lineage;
    decision->output_size = output.adjacency.size();
  }
  return output;
}

MultiShardMergeResult merge_large_first(
    std::vector<LogicalGraph> graphs, std::uint32_t degree_bound,
    double alpha, std::uint32_t max_candidates, const VectorAccessor& vectors,
    const DistanceFunction& distance, CancellationToken* cancellation,
    MetricsSink* metrics) {
  if (graphs.empty()) {
    throw PipelineError(ErrorCategory::kSourceMismatch,
                        "M1 requires at least one LocalGraph");
  }
  MultiShardMergeResult result;
  std::uint64_t epoch_n0 = 0;
  bool reset_epoch = true;
  while (graphs.size() > 1) {
    if (cancellation && cancellation->is_cancelled()) {
      throw PipelineError(ErrorCategory::kCancelled, "M1 was cancelled");
    }
    std::sort(graphs.begin(), graphs.end(), [](const auto& lhs, const auto& rhs) {
      if (lhs.adjacency.size() != rhs.adjacency.size()) {
        return lhs.adjacency.size() > rhs.adjacency.size();
      }
      return lineage_less(lhs, rhs);
    });
    LogicalGraph left = std::move(graphs[0]);
    LogicalGraph right = std::move(graphs[1]);
    graphs.erase(graphs.begin(), graphs.begin() + 2);
    const LogicalGraph* target = &left;
    const LogicalGraph* source = &right;
    if (left.adjacency.size() < right.adjacency.size() ||
        (left.adjacency.size() == right.adjacency.size() &&
         lineage_less(right, left))) {
      target = &right;
      source = &left;
    }
    if (reset_epoch) {
      epoch_n0 = target->adjacency.size();
      reset_epoch = false;
    }
    const auto n = static_cast<double>(target->adjacency.size());
    const auto n0 = static_cast<double>(epoch_n0);
    const auto real = 4.0 + (static_cast<double>(degree_bound) - 4.0) *
                                std::log(n / n0) /
                                std::log(static_cast<double>(degree_bound));
    const auto policy = static_cast<std::uint32_t>(std::max(
        4.0, std::min(static_cast<double>(degree_bound), std::floor(real))));
    const auto effective = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(policy, target->adjacency.size()));
    PairwiseMergeDecision decision;
    decision.step = result.decisions.size();
    decision.left_lineage = left.lineage;
    decision.right_lineage = right.lineage;
    decision.left_size = left.adjacency.size();
    decision.right_size = right.adjacency.size();
    decision.source_lineage = source->lineage;
    decision.target_lineage = target->lineage;
    decision.epoch_n0 = epoch_n0;
    decision.lambda_policy = policy;
    decision.effective_lambda = effective;
    auto merged = merge_pair(*source, *target, effective, degree_bound, alpha,
                             max_candidates, vectors, distance, &decision);
    if (metrics) metrics->increment("hnsw_merger.m1.pairwise_rounds", 1);
    result.decisions.push_back(std::move(decision));
    graphs.push_back(std::move(merged));
    if (policy >= degree_bound) reset_epoch = true;
  }
  result.graph = std::move(graphs.front());
  return result;
}

}  // namespace diskann_pipeline::hnsw_merger_vamana
