#include "xn_descent_test_support.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace fixture = diskann_pipeline::xn_descent::test;
namespace native = diskann_pipeline::cpp_main;

namespace {

using Json = fixture::Json;

std::string bytes_sha256(const std::vector<std::uint8_t>& bytes) {
  return dp::sha256_bytes(std::string_view(
      reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

xn::XnDescentParameters algorithm_parameters(std::uint32_t cap = 128,
                                             std::uint64_t seed = 42) {
  xn::XnDescentParameters result;
  result.validation_profile = xn::ValidationProfile::kSemanticSmall;
  result.omega = 4;
  result.lambda = 4;
  result.alpha = 1.2;
  result.seed = seed;
  result.iteration_cap = cap;
  return result;
}

std::vector<std::vector<float>> replay_vectors(std::size_t count) {
  std::vector<std::vector<float>> result;
  result.reserve(count);
  for (std::size_t node = 0; node < count; ++node) {
    result.push_back(
        {static_cast<float>((node * 7U) % 19U),
         static_cast<float>((node * node + 3U * node) % 23U),
         static_cast<float>((node * 11U + 5U) % 29U)});
  }
  return result;
}

std::vector<dp::LocalNodeId> ascending_order(std::size_t count) {
  std::vector<dp::LocalNodeId> result(count);
  std::iota(result.begin(), result.end(), dp::LocalNodeId{0});
  return result;
}

std::vector<dp::LocalNodeId> reverse_order(std::size_t count) {
  auto result = ascending_order(count);
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<dp::LocalNodeId> even_odd_order(std::size_t count) {
  std::vector<dp::LocalNodeId> result;
  result.reserve(count);
  for (std::size_t node = 0; node < count; node += 2) {
    result.push_back(static_cast<dp::LocalNodeId>(node));
  }
  for (std::size_t node = 1; node < count; node += 2) {
    result.push_back(static_cast<dp::LocalNodeId>(node));
  }
  return result;
}

xn::AlgorithmExecutionOptions execution_options(
    std::uint32_t threads, std::vector<dp::LocalNodeId> order = {}) {
  xn::AlgorithmExecutionOptions result;
  result.thread_budget = threads;
  result.owner_order = std::move(order);
  return result;
}

struct ProgressCapture {
  std::thread::id coordinator{std::this_thread::get_id()};
  bool wrong_thread{false};
  bool throw_on_output{false};
  std::vector<xn::RoundProgress> events;

  static void record(const xn::RoundProgress& event, void* context) {
    auto& self = *static_cast<ProgressCapture*>(context);
    self.wrong_thread |= std::this_thread::get_id() != self.coordinator;
    self.events.push_back(event);
    if (self.throw_on_output) throw std::runtime_error("diagnostic failure");
  }
};

void attach_progress(xn::AlgorithmExecutionOptions& options,
                     ProgressCapture& capture) {
  options.progress_callback = &ProgressCapture::record;
  options.progress_context = &capture;
}

void test_round_progress() {
  fixture::baseline_fixture::TestDistance distance;
  const auto vectors = replay_vectors(24);
  const auto parameters = algorithm_parameters();
  const auto reference = xn::run_algorithm(
      vectors, parameters, distance, nullptr, execution_options(1));
  DP_EXPECT_EQ(reference.termination, xn::TerminationKind::kConverged);

  std::vector<xn::RoundResult> rounds;
  xn::RoundSnapshot snapshot;
  snapshot.state = xn::initialize_state(vectors, parameters, distance);
  for (std::uint32_t round = 0; round < reference.completed_rounds; ++round) {
    rounds.push_back(xn::execute_round(snapshot, vectors, parameters, distance));
    snapshot = {round + 1, rounds.back().state,
                rounds.back().convergence.stable_c_g_rounds};
  }
  DP_EXPECT_EQ(snapshot.state, reference.state);
  for (const auto threads : {1U, 2U, 33U}) {
    for (const auto& order : {ascending_order(vectors.size()),
                             reverse_order(vectors.size()),
                             even_odd_order(vectors.size())}) {
      auto options = execution_options(threads, order);
      ProgressCapture capture;
      attach_progress(options, capture);
      const auto replay = xn::run_algorithm(
          vectors, parameters, distance, nullptr, options);
      DP_EXPECT_EQ(replay.termination, reference.termination);
      DP_EXPECT_EQ(replay.state, reference.state);
      DP_EXPECT_EQ(replay.final_state_digests, reference.final_state_digests);
      DP_EXPECT_EQ(replay.completed_rounds, reference.completed_rounds);
      DP_EXPECT_EQ(replay.convergence.convergence_condition,
                   reference.convergence.convergence_condition);
      DP_EXPECT_TRUE(!capture.wrong_thread);
      DP_EXPECT_EQ(capture.events.size(), 2 * rounds.size());
      if (capture.events.size() != 2 * rounds.size()) continue;
      auto state = xn::initialize_state(vectors, parameters, distance);
      std::uint32_t stable = 0;
      for (std::size_t index = 0; index < rounds.size(); ++index) {
        const auto& expected = rounds[index];
        const auto round = xn::execute_round(
            {static_cast<std::uint32_t>(index), state, stable},
            vectors, parameters, distance, options);
        DP_EXPECT_EQ(round.proposals.cross_c_submitted,
                     expected.proposals.cross_c_submitted);
        DP_EXPECT_EQ(round.state, expected.state);
        DP_EXPECT_EQ(xn::state_digests(round.state),
                     xn::state_digests(expected.state));
        state = round.state;
        stable = round.convergence.stable_c_g_rounds;
        const auto& start = capture.events[2 * index];
        const auto& end = capture.events[2 * index + 1];
        DP_EXPECT_TRUE(!start.completed);
        DP_EXPECT_TRUE(end.completed);
        DP_EXPECT_EQ(start.round_index, index + 1);
        DP_EXPECT_EQ(end.round_index, index + 1);
        DP_EXPECT_EQ(end.effective_worker_count,
                     xn::effective_worker_count(threads, vectors.size()));
        DP_EXPECT_TRUE(std::isfinite(end.elapsed_seconds));
        DP_EXPECT_TRUE(end.elapsed_seconds >= 0.0);
        DP_EXPECT_EQ(end.current_r_records, expected.resource.current_r_records);
        DP_EXPECT_EQ(end.next_r_records, expected.resource.next_r_records);
        DP_EXPECT_EQ(end.cross_c_submitted, expected.proposals.cross_c_submitted);
        DP_EXPECT_EQ(end.changed_c_nodes, expected.convergence.changed_c_nodes);
        DP_EXPECT_EQ(end.changed_g_nodes, expected.convergence.changed_g_nodes);
        DP_EXPECT_EQ(end.changed_c_edges, expected.convergence.changed_c_edges);
        DP_EXPECT_EQ(end.changed_g_edges, expected.convergence.changed_g_edges);
        DP_EXPECT_EQ(end.stable_c_g_rounds,
                     expected.convergence.stable_c_g_rounds);
      }
    }
  }

  ProgressCapture failing_sink;
  failing_sink.throw_on_output = true;
  auto options = execution_options(2);
  attach_progress(options, failing_sink);
  const auto replay = xn::run_algorithm(
      vectors, parameters, distance, nullptr, options);
  DP_EXPECT_EQ(failing_sink.events.size(), 2 * rounds.size());
  DP_EXPECT_EQ(replay.termination, reference.termination);
  DP_EXPECT_EQ(replay.state, reference.state);
  DP_EXPECT_EQ(replay.final_state_digests, reference.final_state_digests);
  DP_EXPECT_EQ(replay.completed_rounds, reference.completed_rounds);
  DP_EXPECT_EQ(replay.convergence.convergence_condition,
               reference.convergence.convergence_condition);
  std::cout << "XN_ROUND_PROGRESS tested_schedules=9"
            << " rounds=" << reference.completed_rounds << '\n';
}

void test_progress_failure() {
  const auto vectors = replay_vectors(24);
  const auto parameters = algorithm_parameters();
  auto options = execution_options(1);
  ProgressCapture capture;
  capture.throw_on_output = true;
  attach_progress(options, capture);
  // Initialization consumes n * omega calls; fail in the real worker path.
  fixture::AllocationFailureDistance distance(vectors.size() * parameters.omega);
  const auto failed = xn::run_algorithm(
      vectors, parameters, distance, nullptr, options);
  DP_EXPECT_EQ(failed.failure_reason, "allocation_failed");
  DP_EXPECT_EQ(failed.termination, xn::TerminationKind::kResourceExhausted);
  DP_EXPECT_EQ(failed.completed_rounds, std::uint32_t{0});
  DP_EXPECT_EQ(capture.events.size(), std::size_t{1});
  if (!capture.events.empty()) DP_EXPECT_TRUE(!capture.events.front().completed);
  DP_EXPECT_TRUE(!capture.wrong_thread);

  // A worker exception that the algorithm normally rethrows must remain the
  // original exception even when the start diagnostic throws as well.
  struct WorkerFailureDistance final : dp::DistanceFunction {
    std::thread::id coordinator{std::this_thread::get_id()};
    double evaluate(const std::vector<float>& left,
                    const std::vector<float>& right) const override {
      if (std::this_thread::get_id() != coordinator) {
        throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                                "original worker exception");
      }
      return fixture::baseline_fixture::TestDistance{}.evaluate(left, right);
    }
  } worker_failure;
  capture.events.clear();
  options.thread_budget = 3;
  bool original_exception = false;
  try {
    (void)xn::run_algorithm(vectors, parameters, worker_failure, nullptr, options);
  } catch (const dp::PipelineError& error) {
    original_exception = std::string(error.what()) == "original worker exception";
  }
  DP_EXPECT_TRUE(original_exception);
  DP_EXPECT_EQ(capture.events.size(), std::size_t{1});
  if (!capture.events.empty()) DP_EXPECT_TRUE(!capture.events.front().completed);

  fixture::baseline_fixture::TestDistance normal_distance;
  capture.events.clear();
  fixture::StepCancellation cancellation(1);
  const auto cancelled = xn::run_algorithm(
      vectors, parameters, normal_distance, &cancellation, options);
  DP_EXPECT_EQ(cancelled.termination, xn::TerminationKind::kCancelled);
  DP_EXPECT_TRUE(!cancellation.worker_poll_observed());
  DP_EXPECT_EQ(capture.events.size(), 2 * cancelled.completed_rounds);
  for (std::size_t index = 0; index < capture.events.size(); ++index) {
    DP_EXPECT_EQ(capture.events[index].completed, index % 2 == 1);
  }
}

struct StageObservation {
  dp::StageStatus status{dp::StageStatus::kPlanned};
  bool has_artifact{false};
  std::vector<std::uint8_t> graph_bytes;
  dp::BuildManifest manifest;
  Json parameters{Json::object()};
};

StageObservation run_stage(
    const dp::WorkspaceRef& workspace,
    const dp::ArtifactRef& partition, native::CppMainVectorAccessor& vectors,
    const std::string& label, std::uint32_t threads, std::uint64_t seed,
    std::uint32_t cap = 128,
    std::uint64_t memory_budget = 1024ULL * 1024ULL * 1024ULL,
    dp::DistanceFunction* distance = nullptr,
    dp::CancellationToken* cancellation = nullptr) {
  fixture::baseline_fixture::TestContext environment(workspace, label);
  environment.context.vector_accessor = &vectors;
  environment.context.thread_budget = threads;
  environment.context.memory_budget_bytes = memory_budget;
  if (distance != nullptr) environment.context.distance_function = distance;
  if (cancellation != nullptr) {
    environment.context.cancellation_token = cancellation;
  }
  const auto parameters = fixture::baseline_fixture::write_parameters(
      environment.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 4, 4, 1.2, cap,
                                         seed));
  const auto result = xn::make_xn_descent_local_build()->build(
      partition, 0, {parameters}, environment.context);

  StageObservation observation;
  observation.status = result.status;
  observation.has_artifact = result.output_candidate.has_value();
  if (result.output_candidate) {
    observation.graph_bytes = fixture::read_bytes(
        environment.context.temporary_storage.stage_root /
        "candidate/graph.index");
  }
  if (!result.build_manifest_path.empty()) {
    observation.manifest = dp::parse_build_manifest(
        fixture::baseline_fixture::read_json(result.build_manifest_path)
            .dump());
    observation.parameters = Json::parse(observation.manifest.parameters_json);
  }
  return observation;
}

void expect_no_artifact(const StageObservation& value) {
  DP_EXPECT_TRUE(value.status != dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(!value.has_artifact);
  DP_EXPECT_TRUE(value.manifest.output_artifacts.empty());
}

void test_determinism() {
  fixture::baseline_fixture::TestDistance distance;
  const auto vectors = replay_vectors(24);
  const auto parameters = algorithm_parameters();

  const auto one = xn::run_algorithm(
      vectors, parameters, distance, nullptr,
      execution_options(1, ascending_order(vectors.size())));
  const auto two = xn::run_algorithm(
      vectors, parameters, distance, nullptr,
      execution_options(2, reverse_order(vectors.size())));
  const auto many = xn::run_algorithm(
      vectors, parameters, distance, nullptr,
      execution_options(static_cast<std::uint32_t>(vectors.size() + 9),
                        even_odd_order(vectors.size())));

  DP_EXPECT_EQ(one.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(two.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(many.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(one.effective_worker_count, std::uint32_t{1});
  DP_EXPECT_EQ(two.effective_worker_count, std::uint32_t{2});
  DP_EXPECT_EQ(many.effective_worker_count,
               static_cast<std::uint32_t>(vectors.size()));
  DP_EXPECT_EQ(one.state, two.state);
  DP_EXPECT_EQ(one.state, many.state);
  DP_EXPECT_EQ(one.final_state_digests, two.final_state_digests);
  DP_EXPECT_EQ(one.final_state_digests, many.final_state_digests);
  DP_EXPECT_EQ(one.completed_rounds, two.completed_rounds);
  DP_EXPECT_EQ(one.completed_rounds, many.completed_rounds);
  DP_EXPECT_EQ(one.convergence.fixed_point, two.convergence.fixed_point);
  DP_EXPECT_EQ(one.convergence.convergence_condition,
               two.convergence.convergence_condition);
  DP_EXPECT_EQ(one.convergence.convergence_condition,
               many.convergence.convergence_condition);

  const auto repeated = xn::run_algorithm(
      vectors, parameters, distance, nullptr,
      execution_options(2, reverse_order(vectors.size())));
  DP_EXPECT_EQ(repeated.state, two.state);
  DP_EXPECT_EQ(repeated.final_state_digests, two.final_state_digests);
  DP_EXPECT_EQ(repeated.completed_rounds, two.completed_rounds);
  DP_EXPECT_EQ(repeated.convergence.convergence_condition,
               two.convergence.convergence_condition);

  fixture::baseline_fixture::TemporaryDirectory temporary("xn-us3-replay");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture dataset(workspace, 24,
                                                                 4);
  std::vector<dp::GlobalNodeId> map(24);
  std::iota(map.begin(), map.end(), dp::GlobalNodeId{0});
  const auto partition = fixture::baseline_fixture::materialize_partition(
      workspace, dataset, {map});
  native::CppMainVectorAccessor accessor(workspace, dataset.reference);

  const auto stage_one = run_stage(workspace, partition, accessor,
                                   "xn-us3-thread-1", 1, 42);
  const auto stage_two = run_stage(workspace, partition, accessor,
                                   "xn-us3-thread-2", 2, 42);
  const auto stage_many = run_stage(workspace, partition, accessor,
                                    "xn-us3-thread-many", 40, 42);
  DP_EXPECT_EQ(stage_one.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(stage_two.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(stage_many.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(stage_one.graph_bytes, stage_two.graph_bytes);
  DP_EXPECT_EQ(stage_one.graph_bytes, stage_many.graph_bytes);
  DP_EXPECT_EQ(stage_one.parameters.at("state_digests"),
               stage_two.parameters.at("state_digests"));
  DP_EXPECT_EQ(stage_one.parameters.at("state_digests"),
               stage_many.parameters.at("state_digests"));
  DP_EXPECT_EQ(stage_one.parameters.at("completed_rounds"),
               stage_two.parameters.at("completed_rounds"));
  DP_EXPECT_EQ(stage_one.parameters.at("convergence"),
               stage_two.parameters.at("convergence"));
  DP_EXPECT_EQ(stage_one.parameters.at("entry_point"),
               stage_two.parameters.at("entry_point"));
  DP_EXPECT_EQ(stage_one.parameters.at("effective_worker_count"), 1);
  DP_EXPECT_EQ(stage_two.parameters.at("effective_worker_count"), 2);
  DP_EXPECT_EQ(stage_many.parameters.at("effective_worker_count"), 24);

  const auto changed_seed = run_stage(workspace, partition, accessor,
                                      "xn-us3-seed-change", 2, 43);
  DP_EXPECT_EQ(changed_seed.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(stage_two.parameters.at("seed"), 42);
  DP_EXPECT_EQ(changed_seed.parameters.at("seed"), 43);
  DP_EXPECT_TRUE(stage_two.manifest.run_id != changed_seed.manifest.run_id);
  DP_EXPECT_EQ(stage_two.manifest.input_artifacts.size(),
               changed_seed.manifest.input_artifacts.size());
  DP_EXPECT_TRUE(std::equal(
      stage_two.manifest.input_artifacts.begin(),
      stage_two.manifest.input_artifacts.end(),
      changed_seed.manifest.input_artifacts.begin(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.artifact_type == rhs.artifact_type &&
               lhs.format_version == rhs.format_version &&
               lhs.aggregate_checksum == rhs.aggregate_checksum;
      }));

  DP_EXPECT_EQ(stage_two.manifest.implementation.implementation_id,
               xn::kImplementationId);
  DP_EXPECT_EQ(stage_two.manifest.implementation.implementation_version,
               xn::kImplementationVersion);
  DP_EXPECT_TRUE(!stage_two.manifest.source.repository.empty());
  DP_EXPECT_TRUE(!stage_two.manifest.source.commit.empty());
  if (stage_two.manifest.source.dirty_worktree) {
    DP_EXPECT_TRUE(
        stage_two.manifest.source.dirty_source_checksum.has_value());
    if (stage_two.manifest.source.dirty_source_checksum) {
      const auto& checksum =
          *stage_two.manifest.source.dirty_source_checksum;
      DP_EXPECT_EQ(checksum.size(), std::size_t{64});
      DP_EXPECT_TRUE(std::all_of(checksum.begin(), checksum.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      }));
    }
  } else {
    DP_EXPECT_TRUE(
        !stage_two.manifest.source.dirty_source_checksum.has_value());
  }
  DP_EXPECT_TRUE(!stage_two.manifest.dataset.dataset_checksum.empty());
  DP_EXPECT_EQ(stage_two.manifest.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_TRUE(std::all_of(
      stage_two.manifest.input_artifacts.begin(),
      stage_two.manifest.input_artifacts.end(), [](const auto& input) {
        return !input.aggregate_checksum.empty() &&
               !input.format_version.empty();
      }));
  DP_EXPECT_EQ(stage_two.manifest.output_artifacts.size(), std::size_t{1});
  DP_EXPECT_TRUE(
      !stage_two.manifest.output_artifacts.front().aggregate_checksum.empty());
  DP_EXPECT_TRUE(!stage_two.manifest.toolchain.compiler_path.empty());
  DP_EXPECT_TRUE(!stage_two.manifest.toolchain.compiler_version.empty());
  DP_EXPECT_TRUE(!stage_two.manifest.toolchain.compile_commands.empty());
  DP_EXPECT_TRUE(!stage_two.manifest.toolchain.link_commands.empty());
  DP_EXPECT_TRUE(std::any_of(
      stage_two.manifest.toolchain.compile_commands.begin(),
      stage_two.manifest.toolchain.compile_commands.end(),
      [](const auto& command) {
        return command.find("xn_descent_algorithm.cpp") != std::string::npos &&
               command.find("-std=c++17") != std::string::npos;
      }));
  DP_EXPECT_EQ(stage_two.parameters.at("reproduction_mode"),
               "faithful_adaptation");
  DP_EXPECT_EQ(stage_two.parameters.at("omega"), 4);
  DP_EXPECT_EQ(stage_two.parameters.at("lambda"), 4);
  DP_EXPECT_EQ(stage_two.parameters.at("seed"), 42);
  DP_EXPECT_EQ(stage_two.parameters.at("requested_thread_budget"), 2);
  DP_EXPECT_EQ(stage_two.parameters.at("effective_worker_count"), 2);
  DP_EXPECT_TRUE(stage_two.parameters.contains("prng_policy"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("sampling_policy"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("tie_break_policy"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("round_model"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("convergence_rule"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("entry_point_rule"));
  DP_EXPECT_TRUE(stage_two.parameters.contains("distance_implementation"));
  DP_EXPECT_TRUE(stage_two.parameters.at("convergence")
                     .at("direct_equality_checked"));
  DP_EXPECT_TRUE(!stage_two.parameters.at("convergence")
                      .at("before_state_digest")
                      .get<std::string>()
                      .empty());
  DP_EXPECT_TRUE(!stage_two.parameters.at("state_digests")
                      .at("full")
                      .get<std::string>()
                      .empty());
  DP_EXPECT_EQ(stage_two.parameters.at("resource_evidence").at("r_truncated"),
               false);
  DP_EXPECT_TRUE(stage_two.parameters.contains("execution_environment"));
  DP_EXPECT_TRUE(stage_two.parameters.at("execution_environment")
                     .contains("architecture"));
  DP_EXPECT_TRUE(stage_two.parameters.at("execution_environment")
                     .contains("numerical_environment"));
  DP_EXPECT_EQ(stage_two.manifest.metrics_json, "{}");
  DP_EXPECT_TRUE(!stage_two.parameters.contains("recall"));
  DP_EXPECT_TRUE(!stage_two.parameters.contains("qps"));
  DP_EXPECT_TRUE(!stage_two.parameters.contains("latency"));

  std::cout << "XN_US3_DETERMINISM full_state_sha256="
            << two.final_state_digests.full
            << " native_payload_sha256=" << bytes_sha256(stage_two.graph_bytes)
            << " completed_rounds=" << two.completed_rounds
            << " convergence_condition="
            << xn::to_string(two.convergence.convergence_condition)
            << " effective_workers=1,2,24"
            << " source_dirty=" << stage_two.manifest.source.dirty_worktree;
  if (stage_two.manifest.source.dirty_source_checksum) {
    std::cout << " dirty_source_checksum="
              << *stage_two.manifest.source.dirty_source_checksum;
  }
  std::cout << '\n';
}

void test_termination() {
  fixture::baseline_fixture::TestDistance distance;
  auto fixed_parameters = algorithm_parameters(2);
  fixed_parameters.omega = 1;
  fixed_parameters.lambda = 1;
  const auto fixed = xn::run_algorithm(
      fixture::line_vectors({0.0F, 1.0F}), fixed_parameters, distance, nullptr,
      execution_options(2));
  DP_EXPECT_EQ(fixed.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(fixed.completed_rounds, std::uint32_t{2});
  DP_EXPECT_TRUE(fixed.convergence.fixed_point);
  DP_EXPECT_EQ(fixed.convergence.convergence_condition,
               xn::ConvergenceCondition::kFullStateFixedPoint);

  auto cycle_parameters = algorithm_parameters(7, 20);
  cycle_parameters.omega = 1;
  cycle_parameters.lambda = 1;
  const auto cycle_vectors =
      fixture::line_vectors({0.0F, 7.0F, 14.0F, 4.0F});
  const auto capped = xn::run_algorithm(
      cycle_vectors, cycle_parameters, distance, nullptr,
      execution_options(4, reverse_order(cycle_vectors.size())));
  DP_EXPECT_EQ(capped.termination,
               xn::TerminationKind::kIterationCapNonconverged);
  DP_EXPECT_EQ(capped.completed_rounds, std::uint32_t{7});
  DP_EXPECT_EQ(capped.convergence.convergence_condition,
               xn::ConvergenceCondition::kNone);

  cycle_parameters.iteration_cap = 8;
  const auto last_round = xn::run_algorithm(
      cycle_vectors, cycle_parameters, distance, nullptr,
      execution_options(4, even_odd_order(cycle_vectors.size())));
  DP_EXPECT_EQ(last_round.termination, xn::TerminationKind::kConverged);
  DP_EXPECT_EQ(last_round.completed_rounds, std::uint32_t{8});
  DP_EXPECT_TRUE(!last_round.convergence.fixed_point);
  DP_EXPECT_EQ(last_round.convergence.stable_c_g_rounds,
               std::uint32_t{3});
  DP_EXPECT_EQ(last_round.convergence.convergence_condition,
               xn::ConvergenceCondition::kThreeRoundCgStability);
  std::cout << "XN_US3_TERMINATION fixed_point_rounds="
            << fixed.completed_rounds
            << " fixed_point_digest=" << fixed.final_state_digests.full
            << " cycle_cap7=iteration_cap_nonconverged"
            << " cycle_cap8_digest=" << last_round.final_state_digests.full
            << " cycle_cap8_condition="
            << xn::to_string(last_round.convergence.convergence_condition)
            << '\n';
}

void test_resource_and_failure() {
  auto parameters = algorithm_parameters();
  bool overflow_observed = false;
  try {
    static_cast<void>(xn::estimate_preflight_memory_bytes(
        dp::kMaximumNodeCount, std::numeric_limits<std::uint32_t>::max(),
        parameters, std::numeric_limits<std::uint32_t>::max()));
  } catch (const dp::PipelineError& error) {
    overflow_observed =
        std::string(error.what()).find("checked_size_overflow") !=
        std::string::npos;
  }
  DP_EXPECT_TRUE(overflow_observed);

  fixture::baseline_fixture::TestDistance distance;
  const auto reverse_vectors =
      fixture::line_vectors({0.0F, 10.0F, 1.0F, 3.0F, -10.0F});
  xn::RoundSnapshot reverse_snapshot;
  reverse_snapshot.state.c.resize(reverse_vectors.size());
  reverse_snapshot.state.r.resize(reverse_vectors.size());
  reverse_snapshot.state.g.resize(reverse_vectors.size());
  for (dp::LocalNodeId owner = 0; owner < 4; ++owner) {
    reverse_snapshot.state.r[owner].push_back(4);
  }
  auto reverse_parameters = parameters;
  reverse_parameters.omega = 2;
  const auto reverse_round = xn::execute_round(
      reverse_snapshot, reverse_vectors, reverse_parameters, distance,
      execution_options(2));
  DP_EXPECT_EQ(reverse_round.state.r[4],
               std::vector<dp::LocalNodeId>({0, 1, 2, 3}));
  DP_EXPECT_TRUE(reverse_round.state.r[4].size() > reverse_parameters.omega);
  DP_EXPECT_EQ(reverse_round.resource.next_r_records, std::uint64_t{4});

  xn::RoundSnapshot bounded_snapshot;
  bounded_snapshot.state.c.resize(reverse_vectors.size());
  bounded_snapshot.state.r.resize(reverse_vectors.size());
  bounded_snapshot.state.g.resize(reverse_vectors.size());
  const std::vector<dp::LocalNodeId> bounded_candidates{0, 2, 3, 4, 1};
  for (dp::LocalNodeId owner = 1; owner < reverse_vectors.size(); ++owner) {
    xn::try_insert(owner, bounded_candidates[owner], 2, reverse_vectors,
                   distance, bounded_snapshot.state.c[owner]);
    xn::try_insert(owner, 0, 2, reverse_vectors, distance,
                   bounded_snapshot.state.g[owner]);
  }
  const auto bounded_round = xn::execute_round(
      bounded_snapshot, reverse_vectors, reverse_parameters, distance,
      execution_options(2, reverse_order(reverse_vectors.size())));
  DP_EXPECT_EQ(bounded_round.proposals.cross_c_submitted, std::uint64_t{8});
  DP_EXPECT_EQ(bounded_round.proposals.cross_c.size(), std::size_t{6});
  DP_EXPECT_EQ(bounded_round.state.c[0],
               xn::NeighborList({{2, 1.0}, {3, 9.0}}));

  fixture::baseline_fixture::TemporaryDirectory temporary("xn-us3-resource");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture dataset(workspace, 12,
                                                                 1);
  std::vector<dp::GlobalNodeId> map(12);
  std::iota(map.begin(), map.end(), dp::GlobalNodeId{0});
  const auto partition = fixture::baseline_fixture::materialize_partition(
      workspace, dataset, {map});
  native::CppMainVectorAccessor accessor(workspace, dataset.reference);

  const auto preflight_bytes = xn::estimate_preflight_memory_bytes(
      map.size(), 1, parameters, 1);
  const auto known_budget = run_stage(
      workspace, partition, accessor, "xn-us3-known-budget", 1, 42,
      128, preflight_bytes - 1);
  expect_no_artifact(known_budget);
  DP_EXPECT_EQ(known_budget.parameters.at("termination_reason"),
               "memory_budget_preflight_exceeded");
  DP_EXPECT_EQ(known_budget.parameters.at("completed_rounds"), 0);

  const auto reverse_budget = run_stage(
      workspace, partition, accessor, "xn-us3-r-budget", 1, 42,
      128, preflight_bytes);
  expect_no_artifact(reverse_budget);
  DP_EXPECT_EQ(reverse_budget.parameters.at("termination_reason"),
               "reverse_state_memory_budget_exceeded");
  DP_EXPECT_EQ(reverse_budget.parameters.at("resource_evidence")
                   .at("r_truncated"),
               false);

  const auto capped = run_stage(
      workspace, partition, accessor, "xn-us3-cap", 1, 42, 1);
  expect_no_artifact(capped);
  DP_EXPECT_EQ(capped.parameters.at("termination_status"),
               "iteration_cap_nonconverged");
  DP_EXPECT_EQ(capped.parameters.at("termination_reason"),
               "iteration_cap_nonconverged");

  fixture::AllocationFailureDistance allocation_failure(48);
  const auto allocation = run_stage(
      workspace, partition, accessor, "xn-us3-allocation", 1, 42,
      128, 1024ULL * 1024ULL * 1024ULL, &allocation_failure);
  expect_no_artifact(allocation);
  DP_EXPECT_TRUE(allocation_failure.evaluations() > 48);
  DP_EXPECT_EQ(allocation.parameters.at("termination_reason"),
               "allocation_failed");

  fixture::StepCancellation cancellation(2);
  const auto cancelled = run_stage(
      workspace, partition, accessor, "xn-us3-cancelled", 2, 42,
      128, 1024ULL * 1024ULL * 1024ULL, nullptr, &cancellation);
  expect_no_artifact(cancelled);
  DP_EXPECT_EQ(cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(cancelled.parameters.at("termination_reason"), "cancelled");
  DP_EXPECT_TRUE(cancellation.poll_count() >= 3);
  DP_EXPECT_TRUE(!cancellation.worker_poll_observed());

  DP_EXPECT_TRUE(reverse_budget.parameters.at("effective_worker_count")
                     .get<std::uint32_t>() <= 1);
  DP_EXPECT_TRUE(cancelled.parameters.at("effective_worker_count")
                     .get<std::uint32_t>() <= 2);
  std::cout << "XN_US3_RESOURCE preflight_bytes=" << preflight_bytes
            << " uncapped_r_records=" << reverse_round.state.r[4].size()
            << " bounded_cross_submitted="
            << bounded_round.proposals.cross_c_submitted
            << " bounded_cross_retained="
            << bounded_round.proposals.cross_c.size()
            << " allocation_failure=allocation_failed"
            << " cancellation=cancelled\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    throw std::runtime_error(
        "usage: xn_descent_determinism_test determinism|termination|resource");
  }
  const std::string mode = argv[1];
  if (mode == "determinism") {
    test_round_progress();
    test_determinism();
  } else if (mode == "termination") {
    test_termination();
  } else if (mode == "resource") {
    test_progress_failure();
    test_resource_and_failure();
  } else {
    throw std::runtime_error("unknown XN-Descent US3 test mode");
  }
  return dp::test::finish();
}
