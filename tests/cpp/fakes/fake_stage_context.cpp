#include "fakes/fake_pipeline.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>

namespace diskann_pipeline::test {

std::size_t FakeVectorAccessor::dimension() const noexcept { return 2; }

void FakeVectorAccessor::read(GlobalNodeId id,
                              std::vector<float>& destination) const {
  static const std::array<std::array<float, 2>, 6> vectors{{
      {{0.0F, 0.0F}}, {{1.0F, 0.0F}}, {{2.0F, 0.0F}},
      {{0.0F, 1.0F}}, {{1.0F, 1.0F}}, {{2.0F, 1.0F}}}};
  if (id >= vectors.size()) {
    throw PipelineError(ErrorCategory::kIdRangeInvalid,
                        "Fake vector ID is out of range");
  }
  destination.assign(vectors[id].begin(), vectors[id].end());
}

double FakeL2Distance::evaluate(const std::vector<float>& lhs,
                                const std::vector<float>& rhs) const {
  if (lhs.size() != rhs.size()) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "Fake L2 vector dimensions differ");
  }
  double total = 0.0;
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto difference = static_cast<double>(lhs[index] - rhs[index]);
    total += difference * difference;
  }
  return total;
}

void FakeMetricsSink::event(const std::string& name) { events_.push_back(name); }
void FakeMetricsSink::increment(const std::string& counter,
                                std::uint64_t amount) {
  counters_[counter] += amount;
}
const std::vector<std::string>& FakeMetricsSink::events() const noexcept {
  return events_;
}
std::uint64_t FakeMetricsSink::counter(const std::string& name) const {
  const auto found = counters_.find(name);
  return found == counters_.end() ? 0 : found->second;
}

bool FakeCancellationToken::is_cancelled() const noexcept {
  ++observations_;
  return cancelled_ || (cancel_on_ && observations_ >= *cancel_on_);
}
void FakeCancellationToken::cancel_on_observation(
    std::size_t observation) noexcept {
  cancel_on_ = observation;
}
void FakeCancellationToken::cancel_now() noexcept { cancelled_ = true; }
void FakeCancellationToken::reset() noexcept {
  observations_ = 0;
  cancel_on_.reset();
  cancelled_ = false;
}
std::size_t FakeCancellationToken::observations() const noexcept {
  return observations_;
}

FakeStageEnvironment::FakeStageEnvironment(const std::string& identity) {
  static std::atomic<unsigned long long> sequence{0};
  const auto token = std::to_string(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count()) +
                     "-" + std::to_string(sequence.fetch_add(1));
  root_ = std::filesystem::temp_directory_path() /
          ("diskann-fake-" + identity + "-" + token);
  workspace_ = create_workspace(root_);
}

FakeStageEnvironment::FakeStageEnvironment(
    const std::string&, const std::filesystem::path& workspace_root)
    : root_(workspace_root), workspace_(create_workspace(workspace_root)),
      owns_root_(false) {}

FakeStageEnvironment::~FakeStageEnvironment() {
  if (!owns_root_) {
    return;
  }
  std::error_code ignored;
  std::filesystem::remove_all(root_, ignored);
}

const WorkspaceRef& FakeStageEnvironment::workspace() const noexcept {
  return workspace_;
}

StageContext FakeStageEnvironment::make_context(
    const std::string& stage_identity) {
  return {workspace_,
          1024 * 1024,
          1,
          create_temporary_storage(workspace_, stage_identity),
          &vectors_,
          &distance_,
          &checkpoints_,
          &metrics_,
          &cancellation_};
}

FakeMetricsSink& FakeStageEnvironment::metrics() noexcept { return metrics_; }
FakeCancellationToken& FakeStageEnvironment::cancellation() noexcept {
  return cancellation_;
}

PipelineExecutionRequest canonical_execution_request(
    const std::string& run_id, const ArtifactRef& dataset) {
  PipelineExecutionRequest request;
  request.run_id = run_id;
  request.dataset = dataset;
  request.partition_implementation_id = "fake_partition_v1";
  request.local_implementation_id = "fake_local_graph_v1";
  request.merge_implementation_id = "fake_merge_v1";
  request.partition_parameters_json =
      "{\"local_to_global_maps\":[[0,1,2,3],[2,3,4,5]],\"partition_count\":2}";
  request.local_parameters_json =
      "{\"adjacency\":[[1,2],[0,2],[1,3],[]],\"declared_max_degree\":2}";
  request.merge_parameters_json =
      "{\"candidate_order\":\"first_occurrence\",\"declared_max_degree\":3,"
      "\"deduplicate\":\"exact_global_node_id\",\"partition_order\":"
      "\"ascending_partition_id\",\"truncate\":\"after_deduplication\"}";
  request.source = {"https://example.invalid/diskann-methods.git", "master",
                    "aad560e23b6edde9c7b1d4493930c7b463b48475", false,
                    std::nullopt};
  request.toolchain = {"test-platform", "c++", "C++17", "3.20+", "CMake",
                       "Debug", {"CMAKE_CXX_STANDARD=17"},
                       {"c++ -std=c++17"}, {"c++ link"}};
  request.started_at = "2026-08-05T00:00:00Z";
  request.ended_at = "2026-08-05T00:00:01Z";
  return request;
}

}  // namespace diskann_pipeline::test
