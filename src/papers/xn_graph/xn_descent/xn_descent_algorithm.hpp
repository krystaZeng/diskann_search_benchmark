#pragma once

#include "papers/xn_graph/xn_descent/parameters.hpp"
#include "papers/xn_graph/xn_descent/stage_binding.hpp"

#include "diskann_pipeline/identifiers.hpp"
#include "diskann_pipeline/stage_context.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace diskann_pipeline::xn_descent {

struct NeighborRecord {
  LocalNodeId id{0};
  double squared_distance_to_owner{0.0};

  bool operator==(const NeighborRecord& other) const noexcept;
};

using NeighborList = std::vector<NeighborRecord>;
using ReverseList = std::vector<LocalNodeId>;

struct NormalizedRoundState {
  std::vector<NeighborList> c;
  std::vector<ReverseList> r;
  std::vector<NeighborList> g;

  bool operator==(const NormalizedRoundState& other) const noexcept;
};

struct StateDigests {
  std::string c;
  std::string r;
  std::string g;
  std::string full;

  bool operator==(const StateDigests& other) const noexcept;
};

struct RoundSnapshot {
  std::uint32_t round_index{0};
  NormalizedRoundState state;
  std::uint32_t stable_c_g_rounds{0};
};

struct CProposal {
  LocalNodeId target_owner{0};
  LocalNodeId proposed_neighbor{0};
};

struct RProposal {
  LocalNodeId target_owner{0};
  LocalNodeId proposed_reverse_neighbor{0};
};

struct RoundProposals {
  std::vector<CProposal> cross_c;
  std::vector<RProposal> reverse_r;
  std::uint64_t cross_c_submitted{0};
};

struct WorkingGState {
  std::vector<NeighborList> nodes;
};

enum class ConvergenceCondition {
  kNone,
  kFullStateFixedPoint,
  kThreeRoundCgStability,
};

struct ConvergenceEvidence {
  std::uint32_t round_index{0};
  std::uint64_t changed_c_nodes{0};
  std::uint64_t changed_r_nodes{0};
  std::uint64_t changed_g_nodes{0};
  std::uint64_t changed_c_edges{0};
  std::uint64_t changed_r_edges{0};
  std::uint64_t changed_g_edges{0};
  bool direct_equality_checked{false};
  bool fixed_point{false};
  std::uint32_t stable_c_g_rounds{0};
  ConvergenceCondition convergence_condition{ConvergenceCondition::kNone};
  std::string before_state_digest;
  std::string after_state_digest;
};

struct AlgorithmResourceEvidence {
  std::uint32_t effective_worker_count{0};
  std::uint64_t preflight_memory_bytes{0};
  std::uint64_t peak_estimated_memory_bytes{0};
  std::uint64_t current_r_records{0};
  std::uint64_t next_r_records{0};
  std::uint64_t peak_current_r_records{0};
  std::uint64_t peak_next_r_records{0};
  bool r_truncated{false};
};

struct AlgorithmExecutionOptions {
  std::uint32_t thread_budget{1};
  std::uint64_t memory_budget_bytes{
      std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t preflight_memory_bytes{0};
  std::vector<LocalNodeId> owner_order;
};

struct RoundResult {
  NormalizedRoundState state;
  RoundProposals proposals;
  ConvergenceEvidence convergence;
  AlgorithmResourceEvidence resource;
};

struct AlgorithmResult {
  TerminationKind termination{TerminationKind::kInternalFailure};
  std::uint32_t completed_rounds{0};
  NormalizedRoundState state;
  ConvergenceEvidence convergence;
  StateDigests final_state_digests;
  AlgorithmResourceEvidence resource;
  std::uint32_t effective_worker_count{0};
  std::string failure_reason;
};

const char* to_string(ConvergenceCondition condition) noexcept;
StateDigests state_digests(const NormalizedRoundState& state);
std::uint32_t effective_worker_count(std::uint32_t thread_budget,
                                     NodeCount local_node_count);
std::uint64_t estimate_preflight_memory_bytes(
    NodeCount local_node_count, std::uint32_t dimension,
    const XnDescentParameters& parameters, std::uint32_t worker_count);

std::uint64_t splitmix64_node_seed(std::uint64_t seed, LocalNodeId node);
std::vector<LocalNodeId> initialize_neighbor_ids(
    LocalNodeId owner, NodeCount local_node_count, std::uint32_t omega,
    std::uint64_t seed);
NeighborList normalize_neighbors(LocalNodeId owner, NodeCount local_node_count,
                                 NeighborList values, std::uint32_t capacity);
ReverseList normalize_reverse(LocalNodeId owner, NodeCount local_node_count,
                              ReverseList values);
bool scaled_alpha_squared_strict_less(double alpha, double squared_distance,
                                      double right_hand_side);
bool equation_two_occludes(LocalNodeId owner, LocalNodeId candidate,
                           LocalNodeId occluder, double alpha,
                           const std::vector<std::vector<float>>& vectors,
                           const DistanceFunction& distance);
void try_insert(LocalNodeId owner, LocalNodeId candidate,
                std::uint32_t capacity,
                const std::vector<std::vector<float>>& vectors,
                const DistanceFunction& distance, NeighborList& target);
NormalizedRoundState initialize_state(
    const std::vector<std::vector<float>>& vectors,
    const XnDescentParameters& parameters,
    const DistanceFunction& distance);
RoundResult execute_round(const RoundSnapshot& snapshot,
                          const std::vector<std::vector<float>>& vectors,
                          const XnDescentParameters& parameters,
                          const DistanceFunction& distance,
                          const AlgorithmExecutionOptions& options = {});
AlgorithmResult run_algorithm(const std::vector<std::vector<float>>& vectors,
                              const XnDescentParameters& parameters,
                              const DistanceFunction& distance,
                              CancellationToken* cancellation,
                              const AlgorithmExecutionOptions& options = {});

}  // namespace diskann_pipeline::xn_descent
