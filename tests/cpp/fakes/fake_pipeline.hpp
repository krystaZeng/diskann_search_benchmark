#pragma once

#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline::test {

inline constexpr const char* kDatasetAggregateChecksum =
    "4cfb7bdb7ce50e3b9f07abe72b918a2666d35ef07211d675c708c0e1d0ecf12d";
inline constexpr const char* kPartitionAggregateChecksum =
    "a640037c2d07b2dcdff98cd22b82b5c4f5da7e6323fc8da2a61538c1cbbb2984";
inline constexpr const char* kLocal0AggregateChecksum =
    "31d93424d2d0a826891c5d2cc6d3b5b7da6f646a9c6f878603d532ddfae0c9c1";
inline constexpr const char* kLocal1AggregateChecksum =
    "ca02d15d4709d62aea8f404de57d2143be1205f6fa517b4d2d550fe2dab265f6";
inline constexpr const char* kGlobalAggregateChecksum =
    "443e732732f559249248d749c1fac19af9f1986fc44dbdf25f3c2ed7de59a0fe";

class FakeVectorAccessor final : public VectorAccessor {
 public:
  std::size_t dimension() const noexcept override;
  void read(GlobalNodeId id, std::vector<float>& destination) const override;
};

class FakeL2Distance final : public DistanceFunction {
 public:
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override;
};

class FakeMetricsSink final : public MetricsSink {
 public:
  void event(const std::string& name) override;
  void increment(const std::string& counter, std::uint64_t amount) override;
  const std::vector<std::string>& events() const noexcept;
  std::uint64_t counter(const std::string& name) const;

 private:
  std::vector<std::string> events_;
  std::map<std::string, std::uint64_t> counters_;
};

class FakeCancellationToken final : public CancellationToken {
 public:
  bool is_cancelled() const noexcept override;
  void cancel_on_observation(std::size_t observation) noexcept;
  void cancel_now() noexcept;
  void reset() noexcept;
  std::size_t observations() const noexcept;

 private:
  mutable std::size_t observations_{0};
  std::optional<std::size_t> cancel_on_;
  bool cancelled_{false};
};

class FakeStageEnvironment {
 public:
  explicit FakeStageEnvironment(const std::string& identity);
  FakeStageEnvironment(const std::string& identity,
                       const std::filesystem::path& workspace_root);
  ~FakeStageEnvironment();
  FakeStageEnvironment(const FakeStageEnvironment&) = delete;
  FakeStageEnvironment& operator=(const FakeStageEnvironment&) = delete;

  const WorkspaceRef& workspace() const noexcept;
  StageContext make_context(const std::string& stage_identity);
  FakeMetricsSink& metrics() noexcept;
  FakeCancellationToken& cancellation() noexcept;

 private:
  std::filesystem::path root_;
  WorkspaceRef workspace_;
  bool owns_root_{true};
  FakeVectorAccessor vectors_;
  FakeL2Distance distance_;
  FakeMetricsSink metrics_;
  FakeCancellationToken cancellation_;
  UnavailableCheckpointStore checkpoints_;
};

ArtifactRef materialize_fake_dataset(const WorkspaceRef& workspace);
ImplementationDescriptor fake_partition_descriptor();
ImplementationDescriptor fake_local_graph_descriptor();
ImplementationDescriptor fake_merge_descriptor();
void register_fake_implementations(ImplementationRegistry& registry);

PipelineExecutionRequest canonical_execution_request(
    const std::string& run_id, const ArtifactRef& dataset);

std::vector<std::vector<GlobalNodeId>> merge_local_graphs(
    const std::vector<std::vector<GlobalNodeId>>& maps,
    const std::vector<std::vector<std::vector<LocalNodeId>>>& local_graphs,
    std::uint32_t max_degree);

ArtifactRef materialize_partition_candidate(
    const std::filesystem::path& root, const ArtifactRef& dataset);
ArtifactRef materialize_local_candidate(
    const std::filesystem::path& root, const WorkspaceRef& workspace,
    const ArtifactRef& partition, PartitionId partition_id, bool corrupt);
ArtifactRef materialize_global_candidate(
    const std::filesystem::path& root, const WorkspaceRef& workspace,
    const ArtifactRef& partition, const std::vector<ArtifactRef>& locals,
    bool corrupt);

}  // namespace diskann_pipeline::test
