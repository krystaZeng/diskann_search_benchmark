#include "papers/xn_graph/xn_descent/xn_descent_algorithm.hpp"

#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <unordered_set>

namespace diskann_pipeline::xn_descent {
namespace {

void require_node(LocalNodeId node, NodeCount count, const char* role) {
  if (!is_valid_local_node_id(node, count)) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        std::string("XN-Descent ") + role +
                            " LocalNodeId is out of range");
  }
}

double squared_distance(LocalNodeId lhs, LocalNodeId rhs,
                        const std::vector<std::vector<float>>& vectors,
                        const DistanceFunction& distance) {
  require_node(lhs, vectors.size(), "distance lhs");
  require_node(rhs, vectors.size(), "distance rhs");
  if (vectors[lhs].size() != vectors[rhs].size()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent vector dimensions differ");
  }
  const auto result = distance.evaluate(vectors[lhs], vectors[rhs]);
  if (!std::isfinite(result) || result < 0.0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent squared distance is not finite/non-negative");
  }
  return result;
}

bool neighbor_less(const NeighborRecord& lhs, const NeighborRecord& rhs) {
  if (lhs.squared_distance_to_owner != rhs.squared_distance_to_owner) {
    return lhs.squared_distance_to_owner < rhs.squared_distance_to_owner;
  }
  return lhs.id < rhs.id;
}

std::uint64_t membership_delta(const NeighborList& lhs,
                               const NeighborList& rhs) {
  std::set<LocalNodeId> left;
  std::set<LocalNodeId> right;
  for (const auto& value : lhs) left.insert(value.id);
  for (const auto& value : rhs) right.insert(value.id);
  std::uint64_t result = 0;
  for (const auto value : left) result += right.count(value) == 0;
  for (const auto value : right) result += left.count(value) == 0;
  return result;
}

std::uint64_t membership_delta(const ReverseList& lhs,
                               const ReverseList& rhs) {
  std::uint64_t result = 0;
  for (const auto value : lhs) {
    result += !std::binary_search(rhs.begin(), rhs.end(), value);
  }
  for (const auto value : rhs) {
    result += !std::binary_search(lhs.begin(), lhs.end(), value);
  }
  return result;
}

ConvergenceEvidence compare_states(const NormalizedRoundState& before,
                                   const NormalizedRoundState& after,
                                   std::uint32_t round) {
  if (before.c.size() != after.c.size() || before.r.size() != after.r.size() ||
      before.g.size() != after.g.size()) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent round state shapes differ");
  }
  ConvergenceEvidence evidence;
  evidence.round_index = round;
  evidence.direct_equality_checked = true;
  for (std::size_t node = 0; node < before.c.size(); ++node) {
    if (before.c[node] != after.c[node]) ++evidence.changed_c_nodes;
    if (before.r[node] != after.r[node]) ++evidence.changed_r_nodes;
    if (before.g[node] != after.g[node]) ++evidence.changed_g_nodes;
    evidence.changed_c_edges += membership_delta(before.c[node], after.c[node]);
    evidence.changed_r_edges += membership_delta(before.r[node], after.r[node]);
    evidence.changed_g_edges += membership_delta(before.g[node], after.g[node]);
  }
  evidence.fixed_point = before == after;
  return evidence;
}

std::uint64_t unbiased_bounded(std::mt19937_64& generator,
                               std::uint64_t bound) {
  if (bound == 0) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent sampling bound is zero");
  }
  const auto threshold = (std::uint64_t{0} - bound) % bound;
  for (;;) {
    const auto value = generator();
    if (value >= threshold) return value % bound;
  }
}

}  // namespace

bool NeighborRecord::operator==(const NeighborRecord& other) const noexcept {
  return id == other.id &&
         squared_distance_to_owner == other.squared_distance_to_owner;
}

bool NormalizedRoundState::operator==(
    const NormalizedRoundState& other) const noexcept {
  return c == other.c && r == other.r && g == other.g;
}

std::uint64_t splitmix64_node_seed(std::uint64_t seed, LocalNodeId node) {
  auto value = seed + 0x9E3779B97F4A7C15ULL *
                          (static_cast<std::uint64_t>(node) + 1ULL);
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

std::vector<LocalNodeId> initialize_neighbor_ids(
    LocalNodeId owner, NodeCount local_node_count, std::uint32_t omega,
    std::uint64_t seed) {
  require_node(owner, local_node_count, "initialization owner");
  if (omega == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent omega must be positive");
  }
  const auto available = local_node_count - 1;
  const auto wanted = std::min<std::uint64_t>(omega, available);
  std::vector<LocalNodeId> result;
  result.reserve(static_cast<std::size_t>(wanted));
  if (wanted == available) {
    for (NodeCount candidate = 0; candidate < local_node_count; ++candidate) {
      if (candidate != owner) result.push_back(static_cast<LocalNodeId>(candidate));
    }
    return result;
  }

  std::mt19937_64 generator(splitmix64_node_seed(seed, owner));
  std::unordered_set<LocalNodeId> selected;
  while (result.size() < wanted) {
    auto mapped = unbiased_bounded(generator, available);
    if (mapped >= owner) ++mapped;
    const auto candidate = static_cast<LocalNodeId>(mapped);
    if (selected.insert(candidate).second) result.push_back(candidate);
  }
  return result;
}

NeighborList normalize_neighbors(LocalNodeId owner, NodeCount local_node_count,
                                 NeighborList values,
                                 std::uint32_t capacity) {
  require_node(owner, local_node_count, "normalization owner");
  if (capacity == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent neighbor capacity must be positive");
  }
  for (const auto& value : values) {
    require_node(value.id, local_node_count, "neighbor");
    if (value.id == owner) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "XN-Descent neighbor cannot be self");
    }
    if (!std::isfinite(value.squared_distance_to_owner) ||
        value.squared_distance_to_owner < 0.0) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent neighbor distance is invalid");
    }
  }
  std::sort(values.begin(), values.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.id != rhs.id) return lhs.id < rhs.id;
    return lhs.squared_distance_to_owner < rhs.squared_distance_to_owner;
  });
  values.erase(std::unique(values.begin(), values.end(),
                           [](const auto& lhs, const auto& rhs) {
                             return lhs.id == rhs.id;
                           }),
               values.end());
  std::sort(values.begin(), values.end(), neighbor_less);
  if (values.size() > capacity) values.resize(capacity);
  return values;
}

ReverseList normalize_reverse(LocalNodeId owner, NodeCount local_node_count,
                              ReverseList values) {
  require_node(owner, local_node_count, "reverse owner");
  for (const auto value : values) {
    require_node(value, local_node_count, "reverse neighbor");
    if (value == owner) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "XN-Descent reverse neighbor cannot be self");
    }
  }
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

bool scaled_alpha_squared_strict_less(double alpha, double squared_distance,
                                      double right_hand_side) {
  if (!std::isfinite(alpha) || alpha < 1.0 ||
      !std::isfinite(squared_distance) || squared_distance < 0.0 ||
      !std::isfinite(right_hand_side) || right_hand_side < 0.0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent Equation (2) input is invalid");
  }
  if (squared_distance == 0.0) return right_hand_side > 0.0;
  if (right_hand_side == 0.0) return false;

  int alpha_exponent = 0;
  int distance_exponent = 0;
  int right_exponent = 0;
  const auto alpha_mantissa = std::frexp(alpha, &alpha_exponent);
  const auto distance_mantissa =
      std::frexp(squared_distance, &distance_exponent);
  const auto right_mantissa =
      std::frexp(right_hand_side, &right_exponent);
  int product_adjustment = 0;
  const long double product_mantissa = std::frexp(
      static_cast<long double>(alpha_mantissa) * alpha_mantissa *
          distance_mantissa,
      &product_adjustment);
  const std::int64_t product_exponent =
      2LL * alpha_exponent + distance_exponent + product_adjustment;
  if (product_exponent != right_exponent) {
    return product_exponent < right_exponent;
  }
  return product_mantissa < static_cast<long double>(right_mantissa);
}

bool equation_two_occludes(LocalNodeId owner, LocalNodeId candidate,
                           LocalNodeId occluder, double alpha,
                           const std::vector<std::vector<float>>& vectors,
                           const DistanceFunction& distance) {
  if (candidate == owner || occluder == owner || candidate == occluder) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "XN-Descent Equation (2) nodes must be distinct");
  }
  const auto owner_occluder =
      squared_distance(owner, occluder, vectors, distance);
  const auto owner_candidate =
      squared_distance(owner, candidate, vectors, distance);
  if (!(owner_occluder < owner_candidate)) return false;
  const auto between = squared_distance(occluder, candidate, vectors, distance);
  return scaled_alpha_squared_strict_less(alpha, between, owner_candidate);
}

void try_insert(LocalNodeId owner, LocalNodeId candidate,
                std::uint32_t capacity,
                const std::vector<std::vector<float>>& vectors,
                const DistanceFunction& distance, NeighborList& target) {
  require_node(owner, vectors.size(), "TryInsert target center");
  require_node(candidate, vectors.size(), "TryInsert candidate");
  if (owner == candidate) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "XN-Descent TryInsert rejects self");
  }
  if (capacity == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent TryInsert capacity must be positive");
  }
  target.push_back(
      {candidate, squared_distance(owner, candidate, vectors, distance)});
  target = normalize_neighbors(owner, vectors.size(), std::move(target),
                               capacity);
}

NormalizedRoundState initialize_state(
    const std::vector<std::vector<float>>& vectors,
    const XnDescentParameters& parameters,
    const DistanceFunction& distance) {
  if (vectors.empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent Partition must not be empty");
  }
  NormalizedRoundState state;
  state.c.resize(vectors.size());
  state.r.resize(vectors.size());
  state.g.resize(vectors.size());
  for (LocalNodeId owner = 0; owner < vectors.size(); ++owner) {
    for (const auto candidate : initialize_neighbor_ids(
             owner, vectors.size(), parameters.omega, parameters.seed)) {
      try_insert(owner, candidate, parameters.omega, vectors, distance,
                 state.c[owner]);
    }
  }
  return state;
}

RoundResult execute_round(const RoundSnapshot& snapshot,
                          const std::vector<std::vector<float>>& vectors,
                          const XnDescentParameters& parameters,
                          const DistanceFunction& distance) {
  const auto count = vectors.size();
  if (count == 0 || snapshot.state.c.size() != count ||
      snapshot.state.r.size() != count || snapshot.state.g.size() != count) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent round snapshot shape is invalid");
  }

  RoundResult result;
  result.state = snapshot.state;
  WorkingGState working{snapshot.state.g};
  for (LocalNodeId owner = 0; owner < count; ++owner) {
    std::vector<LocalNodeId> candidate_ids;
    for (const auto& value : snapshot.state.c[owner]) {
      candidate_ids.push_back(value.id);
    }
    candidate_ids.insert(candidate_ids.end(), snapshot.state.r[owner].begin(),
                         snapshot.state.r[owner].end());
    std::sort(candidate_ids.begin(), candidate_ids.end());
    candidate_ids.erase(std::unique(candidate_ids.begin(), candidate_ids.end()),
                        candidate_ids.end());
    NeighborList candidates;
    for (const auto candidate : candidate_ids) {
      require_node(candidate, count, "candidate");
      if (candidate == owner) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "XN-Descent candidate cannot be self");
      }
      candidates.push_back(
          {candidate, squared_distance(owner, candidate, vectors, distance)});
    }
    std::sort(candidates.begin(), candidates.end(), neighbor_less);

    for (const auto& candidate_record : candidates) {
      const auto candidate = candidate_record.id;
      result.proposals.reverse_r.push_back({candidate, owner});
      bool not_occluded = true;
      for (const auto& current : working.nodes[owner]) {
        if (current.id == candidate) continue;
        result.proposals.cross_c.push_back({current.id, candidate});
        result.proposals.cross_c.push_back({candidate, current.id});
        if (equation_two_occludes(owner, candidate, current.id,
                                  parameters.alpha, vectors, distance)) {
          not_occluded = false;
        }
      }
      if (!not_occluded) continue;
      auto& graph = working.nodes[owner];
      graph.erase(std::remove_if(graph.begin(), graph.end(),
                                 [&](const auto& current) {
                                   if (current.id == candidate) return false;
                                   return equation_two_occludes(
                                       owner, current.id, candidate,
                                       parameters.alpha, vectors, distance);
                                 }),
                  graph.end());
      try_insert(owner, candidate, parameters.lambda, vectors, distance, graph);
    }
  }

  std::sort(result.proposals.cross_c.begin(), result.proposals.cross_c.end(),
            [](const auto& lhs, const auto& rhs) {
              if (lhs.target_owner != rhs.target_owner) {
                return lhs.target_owner < rhs.target_owner;
              }
              return lhs.proposed_neighbor < rhs.proposed_neighbor;
            });
  for (const auto& proposal : result.proposals.cross_c) {
    try_insert(proposal.target_owner, proposal.proposed_neighbor,
               parameters.omega, vectors, distance,
               result.state.c[proposal.target_owner]);
  }
  for (auto& values : result.state.r) values.clear();
  for (const auto& proposal : result.proposals.reverse_r) {
    result.state.r[proposal.target_owner].push_back(
        proposal.proposed_reverse_neighbor);
  }
  for (LocalNodeId owner = 0; owner < count; ++owner) {
    result.state.c[owner] = normalize_neighbors(
        owner, count, std::move(result.state.c[owner]), parameters.omega);
    result.state.r[owner] =
        normalize_reverse(owner, count, std::move(result.state.r[owner]));
    result.state.g[owner] = normalize_neighbors(
        owner, count, std::move(working.nodes[owner]), parameters.lambda);
  }
  result.convergence = compare_states(snapshot.state, result.state,
                                      snapshot.round_index + 1);
  return result;
}

AlgorithmResult run_algorithm(const std::vector<std::vector<float>>& vectors,
                              const XnDescentParameters& parameters,
                              const DistanceFunction& distance,
                              CancellationToken* cancellation) {
  AlgorithmResult result;
  result.state = initialize_state(vectors, parameters, distance);
  for (std::uint32_t round = 1; round <= parameters.iteration_cap; ++round) {
    if (cancellation != nullptr && cancellation->is_cancelled()) {
      result.termination = TerminationKind::kCancelled;
      result.failure_reason = "cancelled";
      return result;
    }
    RoundSnapshot snapshot{round - 1, result.state};
    auto next = execute_round(snapshot, vectors, parameters, distance);
    result.state = std::move(next.state);
    result.convergence = next.convergence;
    result.completed_rounds = round;
    if (result.convergence.fixed_point) {
      result.termination = TerminationKind::kConverged;
      return result;
    }
  }
  result.termination = TerminationKind::kIterationCapNonconverged;
  result.failure_reason = "iteration_cap_nonconverged";
  return result;
}

}  // namespace diskann_pipeline::xn_descent
