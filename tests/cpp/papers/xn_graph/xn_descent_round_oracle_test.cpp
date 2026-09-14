#include "xn_descent_test_support.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <vector>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace fixture = diskann_pipeline::xn_descent::test;

namespace {

bool has_c_proposal(const xn::RoundProposals& proposals,
                    dp::LocalNodeId target, dp::LocalNodeId neighbor) {
  return std::any_of(proposals.cross_c.begin(), proposals.cross_c.end(),
                     [&](const auto& value) {
                       return value.target_owner == target &&
                              value.proposed_neighbor == neighbor;
                     });
}

xn::NormalizedRoundState empty_state(std::size_t node_count) {
  xn::NormalizedRoundState state;
  state.c.resize(node_count);
  state.r.resize(node_count);
  state.g.resize(node_count);
  return state;
}

}  // namespace

int main() {
  fixture::baseline_fixture::TestDistance distance;
  const auto initialized = xn::initialize_neighbor_ids(2, 5, 3, 42);
  DP_EXPECT_EQ(initialized.size(), std::size_t{3});
  DP_EXPECT_EQ(std::set<dp::LocalNodeId>(initialized.begin(), initialized.end())
                   .size(),
               initialized.size());
  DP_EXPECT_TRUE(std::find(initialized.begin(), initialized.end(), 2) ==
                 initialized.end());
  DP_EXPECT_EQ(initialized, xn::initialize_neighbor_ids(2, 5, 3, 42));
  const auto all = xn::initialize_neighbor_ids(1, 3, 9, 42);
  DP_EXPECT_EQ(all, std::vector<dp::LocalNodeId>({0, 2}));

  const auto vectors = fixture::line_vectors({0.0F, 10.0F, 1.0F, 3.0F,
                                               -10.0F});
  xn::XnDescentParameters parameters;
  parameters.validation_profile = xn::ValidationProfile::kSemanticSmall;
  parameters.omega = 4;
  parameters.lambda = 3;
  parameters.alpha = 1.0;
  parameters.seed = 42;
  parameters.iteration_cap = 8;

  const auto initial_state =
      xn::initialize_state(vectors, parameters, distance);
  for (std::size_t owner = 0; owner < vectors.size(); ++owner) {
    DP_EXPECT_TRUE(initial_state.r[owner].empty());
    DP_EXPECT_TRUE(initial_state.g[owner].empty());
    DP_EXPECT_EQ(initial_state.c[owner].size(), std::size_t{4});
    DP_EXPECT_TRUE(std::none_of(initial_state.c[owner].begin(),
                               initial_state.c[owner].end(),
                               [owner](const auto& value) {
                                 return value.id == owner;
                               }));
  }

  xn::RoundSnapshot snapshot;
  snapshot.state = empty_state(vectors.size());
  xn::try_insert(0, 2, 4, vectors, distance, snapshot.state.c[0]);
  xn::try_insert(0, 3, 4, vectors, distance, snapshot.state.c[0]);
  xn::try_insert(0, 1, 3, vectors, distance, snapshot.state.g[0]);
  xn::try_insert(0, 4, 3, vectors, distance, snapshot.state.g[0]);

  const auto round = xn::execute_round(snapshot, vectors, parameters, distance);
  DP_EXPECT_TRUE(std::any_of(round.proposals.reverse_r.begin(),
                            round.proposals.reverse_r.end(), [](const auto& p) {
                              return p.target_owner == 3 &&
                                     p.proposed_reverse_neighbor == 0;
                            }));
  DP_EXPECT_TRUE(has_c_proposal(round.proposals, 2, 3));
  DP_EXPECT_TRUE(has_c_proposal(round.proposals, 3, 2));
  DP_EXPECT_TRUE(has_c_proposal(round.proposals, 4, 3));
  DP_EXPECT_TRUE(has_c_proposal(round.proposals, 3, 4));
  DP_EXPECT_EQ(round.proposals.cross_c.size(), std::size_t{8});
  DP_EXPECT_EQ(round.proposals.reverse_r.size(), std::size_t{2});

  auto expected_round = empty_state(vectors.size());
  expected_round.c[0] = {{2, 1.0}, {3, 9.0}};
  expected_round.c[1] = {{2, 81.0}};
  expected_round.c[2] = {{3, 4.0}, {1, 81.0}, {4, 121.0}};
  expected_round.c[3] = {{2, 4.0}, {4, 169.0}};
  expected_round.c[4] = {{2, 121.0}, {3, 169.0}};
  expected_round.r[2] = {0};
  expected_round.r[3] = {0};
  expected_round.g[0] = {{2, 1.0}, {4, 100.0}};
  DP_EXPECT_EQ(round.state, expected_round);
  DP_EXPECT_TRUE(round.convergence.direct_equality_checked);
  DP_EXPECT_TRUE(!round.convergence.fixed_point);
  DP_EXPECT_EQ(round.convergence.changed_c_nodes, std::uint64_t{4});
  DP_EXPECT_EQ(round.convergence.changed_r_nodes, std::uint64_t{2});
  DP_EXPECT_EQ(round.convergence.changed_g_nodes, std::uint64_t{1});
  DP_EXPECT_EQ(round.convergence.changed_c_edges, std::uint64_t{8});
  DP_EXPECT_EQ(round.convergence.changed_r_edges, std::uint64_t{2});
  DP_EXPECT_EQ(round.convergence.changed_g_edges, std::uint64_t{2});
  for (std::size_t node = 0; node < vectors.size(); ++node) {
    DP_EXPECT_TRUE(round.state.c[node].size() <= parameters.omega);
    DP_EXPECT_TRUE(round.state.g[node].size() <= parameters.lambda);
  }

  fixture::CountingDistance counted_distance;
  xn::AlgorithmExecutionOptions counted_options;
  counted_options.thread_budget = 1;
  const auto counted_round = xn::execute_round(
      snapshot, vectors, parameters, counted_distance, counted_options);
  DP_EXPECT_EQ(counted_round.state, expected_round);
  DP_EXPECT_EQ(counted_round.proposals.cross_c_submitted,
               round.proposals.cross_c_submitted);
  DP_EXPECT_EQ(counted_round.proposals.cross_c.size(),
               round.proposals.cross_c.size());
  DP_EXPECT_EQ(counted_round.proposals.reverse_r.size(),
               round.proposals.reverse_r.size());
  DP_EXPECT_EQ(counted_round.convergence.changed_c_nodes,
               round.convergence.changed_c_nodes);
  DP_EXPECT_EQ(counted_round.convergence.changed_r_nodes,
               round.convergence.changed_r_nodes);
  DP_EXPECT_EQ(counted_round.convergence.changed_g_nodes,
               round.convergence.changed_g_nodes);
  DP_EXPECT_EQ(counted_round.convergence.changed_c_edges,
               round.convergence.changed_c_edges);
  DP_EXPECT_EQ(counted_round.convergence.changed_r_edges,
               round.convergence.changed_r_edges);
  DP_EXPECT_EQ(counted_round.convergence.changed_g_edges,
               round.convergence.changed_g_edges);
  DP_EXPECT_EQ(counted_round.convergence.fixed_point,
               round.convergence.fixed_point);
  DP_EXPECT_EQ(counted_round.convergence.convergence_condition,
               round.convergence.convergence_condition);
  DP_EXPECT_EQ(counted_round.convergence.before_state_digest,
               round.convergence.before_state_digest);
  DP_EXPECT_EQ(counted_round.convergence.after_state_digest,
               round.convergence.after_state_digest);
  std::cout << "XN_PERF_DISTANCE_CALLS optimized_expected=6 actual="
            << counted_distance.evaluations() << '\n';
  DP_EXPECT_EQ(counted_distance.evaluations(), std::uint64_t{6});

  auto reverse_parameters = parameters;
  reverse_parameters.omega = 2;
  xn::RoundSnapshot reverse_snapshot;
  reverse_snapshot.state = empty_state(vectors.size());
  for (dp::LocalNodeId owner = 0; owner < 4; ++owner) {
    if (owner != 4) reverse_snapshot.state.r[owner].push_back(4);
  }
  const auto reverse_round =
      xn::execute_round(reverse_snapshot, vectors, reverse_parameters,
                        distance);
  DP_EXPECT_EQ(reverse_round.state.r[4],
               std::vector<dp::LocalNodeId>({0, 1, 2, 3}));
  DP_EXPECT_TRUE(reverse_round.state.r[4].size() > reverse_parameters.omega);

  auto capacity_parameters = parameters;
  capacity_parameters.alpha = 2.0;
  capacity_parameters.lambda = 1;
  xn::RoundSnapshot capacity_snapshot;
  capacity_snapshot.state = empty_state(vectors.size());
  xn::try_insert(0, 3, 4, vectors, distance,
                 capacity_snapshot.state.c[0]);
  xn::try_insert(0, 2, 1, vectors, distance,
                 capacity_snapshot.state.g[0]);
  const auto capacity_round = xn::execute_round(
      capacity_snapshot, vectors, capacity_parameters, distance);
  auto expected_capacity = empty_state(vectors.size());
  expected_capacity.c[0] = {{3, 9.0}};
  expected_capacity.c[2] = {{3, 4.0}};
  expected_capacity.c[3] = {{2, 4.0}};
  expected_capacity.r[3] = {0};
  expected_capacity.g[0] = {{2, 1.0}};
  DP_EXPECT_EQ(capacity_round.state, expected_capacity);
  DP_EXPECT_TRUE(has_c_proposal(capacity_round.proposals, 2, 3));
  DP_EXPECT_TRUE(has_c_proposal(capacity_round.proposals, 3, 2));
  DP_EXPECT_EQ(capacity_round.convergence.changed_g_nodes, std::uint64_t{0});
  DP_EXPECT_TRUE(!capacity_round.convergence.fixed_point);

  const auto fixed_point_vectors = fixture::line_vectors({0.0F, 1.0F});
  auto fixed_point_parameters = parameters;
  fixed_point_parameters.omega = 1;
  fixed_point_parameters.lambda = 1;
  const auto result = xn::run_algorithm(
      fixed_point_vectors, fixed_point_parameters, distance, nullptr);
  DP_EXPECT_EQ(result.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(result.completed_rounds, std::uint32_t{2});
  DP_EXPECT_TRUE(result.convergence.fixed_point);
  DP_EXPECT_EQ(result.convergence.convergence_condition,
               xn::ConvergenceCondition::kFullStateFixedPoint);
  DP_EXPECT_TRUE(result.convergence.direct_equality_checked);
  DP_EXPECT_EQ(result.convergence.changed_c_nodes, std::uint64_t{0});
  DP_EXPECT_EQ(result.convergence.changed_r_nodes, std::uint64_t{0});
  DP_EXPECT_EQ(result.convergence.changed_g_nodes, std::uint64_t{0});
  auto expected_fixed_point = empty_state(fixed_point_vectors.size());
  expected_fixed_point.c[0] = {{1, 1.0}};
  expected_fixed_point.c[1] = {{0, 1.0}};
  expected_fixed_point.r[0] = {1};
  expected_fixed_point.r[1] = {0};
  expected_fixed_point.g[0] = {{1, 1.0}};
  expected_fixed_point.g[1] = {{0, 1.0}};
  DP_EXPECT_EQ(result.state, expected_fixed_point);

  const auto cycle_vectors =
      fixture::line_vectors({0.0F, 7.0F, 14.0F, 4.0F});
  auto cycle_parameters = parameters;
  cycle_parameters.omega = 1;
  cycle_parameters.lambda = 1;
  cycle_parameters.alpha = 1.2;
  cycle_parameters.seed = 20;
  cycle_parameters.iteration_cap = 7;
  const auto capped_cycle = xn::run_algorithm(
      cycle_vectors, cycle_parameters, distance, nullptr);
  DP_EXPECT_EQ(capped_cycle.termination,
               xn::TerminationKind::kIterationCapNonconverged);
  DP_EXPECT_EQ(capped_cycle.completed_rounds, std::uint32_t{7});
  DP_EXPECT_TRUE(!capped_cycle.convergence.fixed_point);
  DP_EXPECT_EQ(capped_cycle.convergence.convergence_condition,
               xn::ConvergenceCondition::kNone);

  cycle_parameters.iteration_cap = 8;
  const auto converged_cycle = xn::run_algorithm(
      cycle_vectors, cycle_parameters, distance, nullptr);
  DP_EXPECT_EQ(converged_cycle.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(converged_cycle.completed_rounds, std::uint32_t{8});
  DP_EXPECT_TRUE(!converged_cycle.convergence.fixed_point);
  DP_EXPECT_EQ(converged_cycle.convergence.stable_c_g_rounds,
               std::uint32_t{3});
  DP_EXPECT_EQ(converged_cycle.convergence.convergence_condition,
               xn::ConvergenceCondition::kThreeRoundCgStability);

  std::vector<xn::NormalizedRoundState> cycle_states;
  cycle_states.push_back(
      xn::initialize_state(cycle_vectors, cycle_parameters, distance));
  std::uint32_t stable_c_g_rounds = 0;
  for (std::uint32_t round_index = 1; round_index <= 9; ++round_index) {
    const xn::RoundSnapshot cycle_snapshot{
        round_index - 1, cycle_states.back(), stable_c_g_rounds};
    const auto cycle_round = xn::execute_round(
        cycle_snapshot, cycle_vectors, cycle_parameters, distance);
    stable_c_g_rounds = cycle_round.convergence.stable_c_g_rounds;
    cycle_states.push_back(cycle_round.state);
  }
  DP_EXPECT_EQ(cycle_states[8], cycle_states[6]);
  DP_EXPECT_EQ(cycle_states[8].c, cycle_states[7].c);
  DP_EXPECT_EQ(cycle_states[8].g, cycle_states[7].g);
  DP_EXPECT_TRUE(cycle_states[8].r != cycle_states[7].r);
  DP_EXPECT_EQ(cycle_states[9].c, cycle_states[8].c);
  DP_EXPECT_EQ(cycle_states[9].g, cycle_states[8].g);
  DP_EXPECT_EQ(cycle_states[9].r, cycle_states[7].r);
  return dp::test::finish();
}
