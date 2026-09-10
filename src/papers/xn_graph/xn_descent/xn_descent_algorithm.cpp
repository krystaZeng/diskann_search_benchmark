#include "papers/xn_graph/xn_descent/xn_descent_algorithm.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <unordered_set>
#include <utility>

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

std::uint64_t checked_add(std::uint64_t lhs, std::uint64_t rhs) {
  if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent checked_size_overflow");
  }
  return lhs + rhs;
}

std::uint64_t checked_multiply(std::uint64_t lhs, std::uint64_t rhs) {
  if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent checked_size_overflow");
  }
  return lhs * rhs;
}

void append_u32(std::string& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
  }
}

void append_u64(std::string& bytes, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<char>((value >> shift) & 0xffU));
  }
}

std::string encode_neighbors(const char* marker,
                             const std::vector<NeighborList>& values) {
  std::string bytes(marker);
  append_u64(bytes, values.size());
  for (const auto& row : values) {
    append_u64(bytes, row.size());
    for (const auto& value : row) {
      append_u32(bytes, value.id);
      std::uint64_t bits = 0;
      static_assert(sizeof(bits) == sizeof(value.squared_distance_to_owner));
      std::memcpy(&bits, &value.squared_distance_to_owner, sizeof(bits));
      append_u64(bytes, bits);
    }
  }
  return bytes;
}

std::string encode_reverse(const std::vector<ReverseList>& values) {
  std::string bytes("xn-descent-r-state-v1");
  append_u64(bytes, values.size());
  for (const auto& row : values) {
    append_u64(bytes, row.size());
    for (const auto value : row) append_u32(bytes, value);
  }
  return bytes;
}

std::vector<LocalNodeId> candidate_ids_for(
    LocalNodeId owner, const NormalizedRoundState& state, NodeCount count) {
  std::vector<LocalNodeId> result;
  result.reserve(state.c[owner].size() + state.r[owner].size());
  for (const auto& value : state.c[owner]) result.push_back(value.id);
  result.insert(result.end(), state.r[owner].begin(), state.r[owner].end());
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  for (const auto candidate : result) {
    require_node(candidate, count, "candidate");
    if (candidate == owner) {
      throw PipelineError(ErrorCategory::kIdRangeInvalid,
                          "XN-Descent candidate cannot be self");
    }
  }
  return result;
}

std::vector<LocalNodeId> normalized_owner_order(
    NodeCount count, const std::vector<LocalNodeId>& requested) {
  std::vector<LocalNodeId> result = requested;
  if (result.empty()) {
    result.resize(static_cast<std::size_t>(count));
    for (LocalNodeId owner = 0; owner < count; ++owner) result[owner] = owner;
    return result;
  }
  if (result.size() != count) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent owner schedule must be a permutation");
  }
  auto sorted = result;
  std::sort(sorted.begin(), sorted.end());
  for (LocalNodeId owner = 0; owner < count; ++owner) {
    if (sorted[owner] != owner) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent owner schedule must be a permutation");
    }
  }
  return result;
}

class ResourceBudgetFailure final : public std::runtime_error {
 public:
  ResourceBudgetFailure(const std::string& reason,
                        std::uint64_t estimated_memory,
                        std::uint64_t current_r_records,
                        std::uint64_t next_r_records)
      : std::runtime_error(reason),
        estimated_memory(estimated_memory),
        current_r_records(current_r_records),
        next_r_records(next_r_records) {}

  std::uint64_t estimated_memory;
  std::uint64_t current_r_records;
  std::uint64_t next_r_records;
};

std::uint64_t round_memory_bytes(std::uint64_t preflight,
                                 std::uint64_t current_r_records,
                                 std::uint64_t next_r_records) {
  auto total = preflight;
  total = checked_add(
      total, checked_multiply(checked_multiply(current_r_records, 2),
                              sizeof(LocalNodeId)));
  total = checked_add(
      total, checked_multiply(next_r_records, sizeof(RProposal)));
  total = checked_add(
      total, checked_multiply(next_r_records, sizeof(LocalNodeId)));
  total = checked_add(
      total,
      checked_multiply(std::max(current_r_records, next_r_records), 16));
  return total;
}

void reduce_cross_try_insert(
    LocalNodeId owner, LocalNodeId candidate, std::uint32_t capacity,
    const std::vector<std::vector<float>>& vectors,
    const DistanceFunction& distance, NeighborList& target) {
  const NeighborRecord proposal{
      candidate, squared_distance(owner, candidate, vectors, distance)};
  const auto duplicate = std::find_if(
      target.begin(), target.end(),
      [&](const auto& current) { return current.id == candidate; });
  if (duplicate != target.end()) {
    if (proposal.squared_distance_to_owner <
        duplicate->squared_distance_to_owner) {
      *duplicate = proposal;
    }
    std::sort(target.begin(), target.end(), neighbor_less);
    return;
  }
  if (target.size() < capacity) {
    target.push_back(proposal);
    std::sort(target.begin(), target.end(), neighbor_less);
    return;
  }
  if (neighbor_less(proposal, target.back())) {
    target.back() = proposal;
    std::sort(target.begin(), target.end(), neighbor_less);
  }
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
  evidence.before_state_digest = state_digests(before).full;
  evidence.after_state_digest = state_digests(after).full;
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

bool StateDigests::operator==(const StateDigests& other) const noexcept {
  return c == other.c && r == other.r && g == other.g && full == other.full;
}

StateDigests state_digests(const NormalizedRoundState& state) {
  const auto c_bytes = encode_neighbors("xn-descent-c-state-v1", state.c);
  const auto r_bytes = encode_reverse(state.r);
  const auto g_bytes = encode_neighbors("xn-descent-g-state-v1", state.g);
  StateDigests result;
  result.c = sha256_bytes(c_bytes);
  result.r = sha256_bytes(r_bytes);
  result.g = sha256_bytes(g_bytes);
  std::string full("xn-descent-full-state-v1");
  append_u64(full, c_bytes.size());
  full.append(c_bytes);
  append_u64(full, r_bytes.size());
  full.append(r_bytes);
  append_u64(full, g_bytes.size());
  full.append(g_bytes);
  result.full = sha256_bytes(full);
  return result;
}

std::uint32_t effective_worker_count(std::uint32_t thread_budget,
                                     NodeCount local_node_count) {
  if (thread_budget == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent thread budget must be positive");
  }
  if (!is_valid_count(local_node_count)) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "XN-Descent local node count is invalid");
  }
  return static_cast<std::uint32_t>(
      std::min<std::uint64_t>(thread_budget, local_node_count));
}

std::uint64_t estimate_preflight_memory_bytes(
    NodeCount local_node_count, std::uint32_t dimension,
    const XnDescentParameters& parameters, std::uint32_t worker_count) {
  if (!is_valid_count(local_node_count) || dimension == 0) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "XN-Descent preflight dimensions are invalid");
  }
  const auto workers = effective_worker_count(worker_count, local_node_count);
  const auto available = local_node_count - 1;
  const auto omega =
      std::min<std::uint64_t>(parameters.omega, available);
  const auto lambda =
      std::min<std::uint64_t>(parameters.lambda, available);
  const auto nodes = static_cast<std::uint64_t>(local_node_count);
  const auto vector_records = checked_multiply(nodes, dimension);
  const auto c_records = checked_multiply(nodes, omega);
  const auto g_records = checked_multiply(nodes, lambda);

  std::uint64_t total = 0;
  const auto add_records = [&](std::uint64_t count, std::uint64_t size) {
    total = checked_add(total, checked_multiply(count, size));
  };
  add_records(vector_records, sizeof(float));
  add_records(nodes, sizeof(std::vector<float>));
  add_records(nodes, sizeof(GlobalNodeId));
  add_records(checked_multiply(c_records, 2), sizeof(NeighborRecord));
  add_records(checked_multiply(g_records, 2), sizeof(NeighborRecord));
  add_records(c_records, sizeof(NeighborRecord));
  add_records(c_records, sizeof(CProposal));
  add_records(checked_multiply(nodes, 5), sizeof(NeighborList));
  add_records(checked_multiply(checked_add(nodes, 1), 2),
              sizeof(std::uint64_t));
  add_records(checked_multiply(nodes, 2), sizeof(ReverseList));
  add_records(nodes, sizeof(std::mutex));

  const auto candidate_scratch = checked_multiply(
      available, checked_add(sizeof(LocalNodeId), sizeof(NeighborRecord)));
  auto worker_scratch = checked_add(
      candidate_scratch,
      checked_multiply(lambda, sizeof(NeighborRecord)));
  worker_scratch = checked_add(
      worker_scratch,
      checked_multiply(dimension, sizeof(long double)));
  add_records(workers, worker_scratch);
  add_records(workers, sizeof(std::thread));

  auto native_payload = checked_add(
      24, checked_multiply(
              nodes,
              checked_add(sizeof(std::uint32_t),
                          checked_multiply(lambda, sizeof(std::uint32_t)))));
  native_payload = checked_add(
      native_payload, checked_multiply(nodes, sizeof(GlobalNodeId)));
  total = checked_add(total, native_payload);
  const auto bounded_digest_records = checked_add(c_records, g_records);
  add_records(bounded_digest_records, 16);
  add_records(nodes, 128);
  return total;
}

const char* to_string(ConvergenceCondition condition) noexcept {
  switch (condition) {
    case ConvergenceCondition::kFullStateFixedPoint:
      return "full_state_fixed_point";
    case ConvergenceCondition::kThreeRoundCgStability:
      return "three_round_c_g_stability";
    case ConvergenceCondition::kNone:
      return "none";
  }
  return "none";
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
                          const DistanceFunction& distance,
                          const AlgorithmExecutionOptions& options) {
  const auto count = vectors.size();
  if (count == 0 || snapshot.state.c.size() != count ||
      snapshot.state.r.size() != count || snapshot.state.g.size() != count) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent round snapshot shape is invalid");
  }

  const auto workers =
      effective_worker_count(options.thread_budget, vectors.size());
  const auto owner_order = normalized_owner_order(count, options.owner_order);
  std::uint64_t current_r_records = 0;
  for (const auto& values : snapshot.state.r) {
    current_r_records = checked_add(current_r_records, values.size());
  }

  std::vector<std::uint64_t> reverse_counts(count, 0);
  std::uint64_t next_r_records = 0;
  for (LocalNodeId owner = 0; owner < count; ++owner) {
    const auto candidates = candidate_ids_for(owner, snapshot.state, count);
    next_r_records = checked_add(next_r_records, candidates.size());
    for (const auto candidate : candidates) {
      reverse_counts[candidate] = checked_add(reverse_counts[candidate], 1);
    }
  }
  const auto estimated_memory = round_memory_bytes(
      options.preflight_memory_bytes, current_r_records, next_r_records);
  if (estimated_memory > options.memory_budget_bytes) {
    throw ResourceBudgetFailure("reverse_state_memory_budget_exceeded",
                                estimated_memory, current_r_records,
                                next_r_records);
  }

  RoundResult result;
  result.state = snapshot.state;
  result.resource.effective_worker_count = workers;
  result.resource.preflight_memory_bytes = options.preflight_memory_bytes;
  result.resource.peak_estimated_memory_bytes = estimated_memory;
  result.resource.current_r_records = current_r_records;
  result.resource.next_r_records = next_r_records;
  result.resource.peak_current_r_records = current_r_records;
  result.resource.peak_next_r_records = next_r_records;
  result.proposals.reverse_r.reserve(static_cast<std::size_t>(next_r_records));
  for (LocalNodeId owner = 0; owner < count; ++owner) {
    for (const auto candidate : candidate_ids_for(owner, snapshot.state, count)) {
      result.proposals.reverse_r.push_back({candidate, owner});
    }
  }

  WorkingGState working{snapshot.state.g};
  std::vector<NeighborList> reduced_cross(count);
  std::vector<std::mutex> cross_mutex(count);
  std::atomic<std::uint64_t> cross_submitted{0};
  std::atomic<std::size_t> next_owner{0};
  std::atomic<bool> stop{false};
  std::exception_ptr first_exception;
  std::mutex exception_mutex;

  const auto submit_cross = [&](LocalNodeId target, LocalNodeId proposed) {
    cross_submitted.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(cross_mutex[target]);
    reduce_cross_try_insert(target, proposed, parameters.omega, vectors,
                            distance, reduced_cross[target]);
  };
  const auto worker_body = [&] {
    try {
      for (;;) {
        if (stop.load(std::memory_order_relaxed)) return;
        const auto schedule_index =
            next_owner.fetch_add(1, std::memory_order_relaxed);
        if (schedule_index >= owner_order.size()) return;
        const auto owner = owner_order[schedule_index];
        const auto owner_candidates =
            candidate_ids_for(owner, snapshot.state, count);
        NeighborList candidates;
        candidates.reserve(owner_candidates.size());
        for (const auto candidate : owner_candidates) {
          candidates.push_back(
              {candidate,
               squared_distance(owner, candidate, vectors, distance)});
        }
        std::sort(candidates.begin(), candidates.end(), neighbor_less);

        for (const auto& candidate_record : candidates) {
          const auto candidate = candidate_record.id;
          bool not_occluded = true;
          for (const auto& current : working.nodes[owner]) {
            if (current.id == candidate) continue;
            submit_cross(current.id, candidate);
            submit_cross(candidate, current.id);
            if (equation_two_occludes(owner, candidate, current.id,
                                      parameters.alpha, vectors, distance)) {
              not_occluded = false;
            }
          }
          if (!not_occluded) continue;
          auto& graph = working.nodes[owner];
          graph.erase(
              std::remove_if(graph.begin(), graph.end(), [&](const auto& current) {
                if (current.id == candidate) return false;
                return equation_two_occludes(owner, current.id, candidate,
                                              parameters.alpha, vectors,
                                              distance);
              }),
              graph.end());
          try_insert(owner, candidate, parameters.lambda, vectors, distance,
                     graph);
        }
      }
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(exception_mutex);
        if (!first_exception) first_exception = std::current_exception();
      }
      stop.store(true, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(workers);
  try {
    for (std::uint32_t worker = 0; worker < workers; ++worker) {
      threads.emplace_back(worker_body);
    }
  } catch (...) {
    stop.store(true, std::memory_order_relaxed);
    for (auto& thread : threads) thread.join();
    throw;
  }
  for (auto& thread : threads) thread.join();
  if (first_exception) std::rethrow_exception(first_exception);

  result.proposals.cross_c_submitted =
      cross_submitted.load(std::memory_order_relaxed);
  for (LocalNodeId target = 0; target < count; ++target) {
    for (const auto& proposal : reduced_cross[target]) {
      result.proposals.cross_c.push_back({target, proposal.id});
      try_insert(target, proposal.id, parameters.omega, vectors, distance,
                 result.state.c[target]);
    }
  }
  for (auto& values : result.state.r) values.clear();
  for (LocalNodeId target = 0; target < count; ++target) {
    result.state.r[target].reserve(
        static_cast<std::size_t>(reverse_counts[target]));
  }
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
  const bool c_g_stable = result.convergence.changed_c_nodes == 0 &&
                          result.convergence.changed_g_nodes == 0;
  result.convergence.stable_c_g_rounds =
      c_g_stable
          ? (snapshot.stable_c_g_rounds >= 3
                 ? 3
                 : snapshot.stable_c_g_rounds + 1)
          : 0;
  if (result.convergence.fixed_point) {
    result.convergence.convergence_condition =
        ConvergenceCondition::kFullStateFixedPoint;
  } else if (result.convergence.stable_c_g_rounds >= 3) {
    result.convergence.convergence_condition =
        ConvergenceCondition::kThreeRoundCgStability;
  }
  return result;
}

AlgorithmResult run_algorithm(const std::vector<std::vector<float>>& vectors,
                              const XnDescentParameters& parameters,
                              const DistanceFunction& distance,
                              CancellationToken* cancellation,
                              const AlgorithmExecutionOptions& options) {
  AlgorithmResult result;
  try {
    result.effective_worker_count =
        effective_worker_count(options.thread_budget, vectors.size());
    result.resource.effective_worker_count = result.effective_worker_count;
    result.resource.preflight_memory_bytes = options.preflight_memory_bytes;
    result.resource.peak_estimated_memory_bytes =
        options.preflight_memory_bytes;
    if (options.preflight_memory_bytes > options.memory_budget_bytes) {
      result.termination = TerminationKind::kResourceExhausted;
      result.failure_reason = "memory_budget_preflight_exceeded";
      return result;
    }

    result.state = initialize_state(vectors, parameters, distance);
    result.final_state_digests = state_digests(result.state);
    for (std::uint32_t round = 1; round <= parameters.iteration_cap; ++round) {
      if (cancellation != nullptr && cancellation->is_cancelled()) {
        result.termination = TerminationKind::kCancelled;
        result.failure_reason = "cancelled";
        return result;
      }
      RoundSnapshot snapshot{round - 1, result.state,
                             result.convergence.stable_c_g_rounds};
      auto next = execute_round(snapshot, vectors, parameters, distance,
                                options);
      result.state = std::move(next.state);
      result.convergence = next.convergence;
      result.completed_rounds = round;
      result.resource.peak_estimated_memory_bytes = std::max(
          result.resource.peak_estimated_memory_bytes,
          next.resource.peak_estimated_memory_bytes);
      result.resource.current_r_records =
          next.resource.current_r_records;
      result.resource.next_r_records = next.resource.next_r_records;
      result.resource.peak_current_r_records = std::max(
          result.resource.peak_current_r_records,
          next.resource.peak_current_r_records);
      result.resource.peak_next_r_records = std::max(
          result.resource.peak_next_r_records,
          next.resource.peak_next_r_records);
      result.final_state_digests = state_digests(result.state);
      if (cancellation != nullptr && cancellation->is_cancelled()) {
        result.termination = TerminationKind::kCancelled;
        result.failure_reason = "cancelled";
        return result;
      }
      if (result.convergence.convergence_condition !=
          ConvergenceCondition::kNone) {
        result.termination = TerminationKind::kConverged;
        return result;
      }
    }
    result.termination = TerminationKind::kIterationCapNonconverged;
    result.failure_reason = "iteration_cap_nonconverged";
    return result;
  } catch (const ResourceBudgetFailure& error) {
    result.termination = TerminationKind::kResourceExhausted;
    result.failure_reason = error.what();
    result.resource.peak_estimated_memory_bytes = error.estimated_memory;
    result.resource.current_r_records = error.current_r_records;
    result.resource.next_r_records = error.next_r_records;
    result.resource.peak_current_r_records = error.current_r_records;
    result.resource.peak_next_r_records = error.next_r_records;
    if (!result.state.c.empty()) {
      result.final_state_digests = state_digests(result.state);
    }
    return result;
  } catch (const std::bad_alloc&) {
    result.termination = TerminationKind::kResourceExhausted;
    result.failure_reason = "allocation_failed";
    if (!result.state.c.empty()) result.final_state_digests = state_digests(result.state);
    return result;
  } catch (const PipelineError& error) {
    if (std::string(error.what()).find("checked_size_overflow") ==
        std::string::npos) {
      throw;
    }
    result.termination = TerminationKind::kResourceExhausted;
    result.failure_reason = "checked_size_overflow";
    if (!result.state.c.empty()) result.final_state_digests = state_digests(result.state);
    return result;
  } catch (...) {
    result.termination = TerminationKind::kInternalFailure;
    result.failure_reason = "worker_exception";
    if (!result.state.c.empty()) result.final_state_digests = state_digests(result.state);
    try {
      throw;
    } catch (const std::exception& error) {
      result.failure_reason = error.what();
    } catch (...) {
    }
    return result;
  }
}

}  // namespace diskann_pipeline::xn_descent
