#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#include "papers/hnsw_merger/vamana/m1_algorithm.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/registry.hpp"
#include "support/test_assert.hpp"
#include "validation/parameter_validator.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace dp = diskann_pipeline;
namespace m1 = diskann_pipeline::hnsw_merger_vamana;
namespace native = diskann_pipeline::cpp_main;

namespace {

class Vectors final : public dp::VectorAccessor {
 public:
  explicit Vectors(std::vector<float> values) : values_(std::move(values)) {}
  std::size_t dimension() const noexcept override { return 1; }
  void read(dp::GlobalNodeId id,
            std::vector<float>& destination) const override {
    destination = {values_.at(id)};
  }
 private:
  std::vector<float> values_;
};

class CountingVectors final : public dp::VectorAccessor {
 public:
  explicit CountingVectors(std::vector<float> values)
      : values_(std::move(values)), reads_(values_.size(), 0) {}
  std::size_t dimension() const noexcept override { return 1; }
  void read(dp::GlobalNodeId id,
            std::vector<float>& destination) const override {
    ++reads_.at(id);
    destination = {values_.at(id)};
  }
  std::size_t reads(dp::GlobalNodeId id) const { return reads_.at(id); }

 private:
  std::vector<float> values_;
  mutable std::vector<std::size_t> reads_;
};

class L2 final : public dp::DistanceFunction {
 public:
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    const auto delta = lhs.at(0) - rhs.at(0);
    return delta * delta;
  }
};

class CancelAfterRound final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return cancelled; }
  bool cancelled{false};
};

class CancellingMetrics final : public dp::MetricsSink {
 public:
  explicit CancellingMetrics(CancelAfterRound& token) : token_(token) {}
  void event(const std::string&) override {}
  void increment(const std::string& name, std::uint64_t) override {
    if (name == "hnsw_merger.m1.pairwise_rounds") token_.cancelled = true;
  }
 private:
  CancelAfterRound& token_;
};

m1::LogicalGraph graph(std::initializer_list<dp::PartitionId> lineage,
                       dp::GlobalNodeId entry,
                       std::map<dp::GlobalNodeId,
                                std::vector<dp::GlobalNodeId>> adjacency) {
  m1::LogicalGraph value;
  value.lineage.assign(lineage);
  value.entry = entry;
  value.adjacency = std::move(adjacency);
  return value;
}

void test_bounded_algorithm_one_search() {
  L2 distance;
  CountingVectors vectors({0, 10, 9, 1, 2, 8, 7, 6});
  const auto target = graph(
      {0}, 1,
      {{1, {2, 3}}, {2, {5}}, {3, {4}}, {4, {}},
       {5, {6}},    {6, {7}}, {7, {}}});
  const auto original_adjacency = target.adjacency;
  const auto original_entry = target.entry;

  const auto hits = m1::hnsw_merger_single_layer_search(
      target, 0, 2, vectors, distance);

  DP_EXPECT_EQ(hits, std::vector<dp::GlobalNodeId>({3, 4}));
  DP_EXPECT_TRUE(hits.size() <= std::size_t{2});
  DP_EXPECT_EQ(std::set<dp::GlobalNodeId>(hits.begin(), hits.end()).size(),
               hits.size());
  DP_EXPECT_TRUE(vectors.reads(target.entry) > 0);
  DP_EXPECT_EQ(vectors.reads(5), std::size_t{0});
  DP_EXPECT_EQ(vectors.reads(6), std::size_t{0});
  DP_EXPECT_EQ(vectors.reads(7), std::size_t{0});
  DP_EXPECT_EQ(target.entry, original_entry);
  DP_EXPECT_EQ(target.adjacency, original_adjacency);

  CountingVectors tied_vectors({0, 10, -1, 1});
  const auto tied_target =
      graph({1}, 1, {{1, {3, 2}}, {2, {}}, {3, {}}});
  const auto tied_hits = m1::hnsw_merger_single_layer_search(
      tied_target, 0, 1, tied_vectors, distance);
  DP_EXPECT_EQ(tied_hits, std::vector<dp::GlobalNodeId>({2}));
}

void test_algorithm_semantics() {
  Vectors vectors({0, 1, 2, 3, 4, 5, 6, 7});
  L2 distance;
  const auto target = graph({0}, 4, {{2, {3}}, {3, {2, 4}}, {4, {3}}});
  const auto hits = m1::hnsw_merger_single_layer_search(
      target, 0, 2, vectors, distance);
  DP_EXPECT_EQ(hits, std::vector<dp::GlobalNodeId>({2, 3}));

  const auto source = graph({1}, 0, {{0, {1}}, {1, {0}}});
  m1::PairwiseMergeDecision decision;
  const auto merged = m1::merge_pair(source, target, 2, 16, 1.2, 750,
                                     vectors, distance, &decision);
  DP_EXPECT_TRUE(std::find(merged.adjacency.at(0).begin(),
                           merged.adjacency.at(0).end(), 2) !=
                 merged.adjacency.at(0).end());
  DP_EXPECT_TRUE(std::find(merged.adjacency.at(2).begin(),
                           merged.adjacency.at(2).end(), 0) !=
                 merged.adjacency.at(2).end());
  DP_EXPECT_EQ(merged.entry, dp::GlobalNodeId{4});

  const auto overlap_source = graph({2}, 3, {{3, {5}}, {5, {3}}});
  const auto overlap_target = graph({3}, 3, {{3, {4}}, {4, {3}}});
  const auto overlap = m1::merge_pair(overlap_source, overlap_target, 2, 16,
                                      1.2, 750, vectors, distance, nullptr);
  DP_EXPECT_EQ(overlap.adjacency.at(3),
               std::vector<dp::GlobalNodeId>({4, 5}));

  const auto duplicate_source =
      graph({4}, 0, {{0, {1, 1}}, {1, {0, 0}}});
  const auto duplicate_target =
      graph({5}, 2, {{2, {3, 3}}, {3, {2, 2}}});
  const auto duplicate_merged = m1::merge_pair(
      duplicate_source, duplicate_target, 2, 2, 1.2, 750, vectors,
      distance, nullptr);
  for (const auto& item : duplicate_merged.adjacency) {
    DP_EXPECT_TRUE(item.second.size() <= std::size_t{2});
    DP_EXPECT_TRUE(std::find(item.second.begin(), item.second.end(),
                             item.first) == item.second.end());
    DP_EXPECT_EQ(std::set<dp::GlobalNodeId>(item.second.begin(),
                                            item.second.end()).size(),
                 item.second.size());
  }
  const auto bounded_prune = m1::hnsw_merger_vamana_prune(
      0, {1, 2, 3, 4, 5}, 2, 1.2, 750, vectors, distance);
  DP_EXPECT_TRUE(bounded_prune.size() <= std::size_t{2});
  DP_EXPECT_THROW(m1::hnsw_merger_vamana_prune(
      0, {0, 1}, 2, 1.2, 750, vectors, distance));
  DP_EXPECT_THROW(m1::hnsw_merger_single_layer_search(
      graph({6}, 2, {{2, {99}}}), 0, 1, vectors, distance));

  const auto multi = m1::merge_large_first(
      {graph({0}, 0, {{0, {1}}, {1, {0}}, {2, {1}}, {3, {2}}}),
       graph({1}, 4, {{4, {5}}, {5, {4}}, {6, {5}}}),
       graph({2}, 7, {{7, {6}}, {6, {7}}})},
      16, 1.2, 750, vectors, distance, nullptr, nullptr);
  DP_EXPECT_EQ(multi.decisions.size(), std::size_t{2});
  DP_EXPECT_EQ(multi.decisions.front().target_lineage,
               std::vector<dp::PartitionId>({0}));
  DP_EXPECT_EQ(multi.graph.adjacency.size(), std::size_t{8});

  CancelAfterRound cancellation;
  CancellingMetrics metrics(cancellation);
  try {
    static_cast<void>(m1::merge_large_first(
        {graph({0}, 0, {{0, {1}}, {1, {0}}}),
         graph({1}, 2, {{2, {3}}, {3, {2}}}),
         graph({2}, 4, {{4, {5}}, {5, {4}}})},
        16, 1.2, 750, vectors, distance, &cancellation, &metrics));
    DP_EXPECT_TRUE(false);
  } catch (const dp::PipelineError& error) {
    DP_EXPECT_EQ(error.category(), dp::ErrorCategory::kCancelled);
  }
}

}  // namespace

int main() {
  const auto descriptor = m1::descriptor();
  DP_EXPECT_EQ(descriptor.implementation_id,
               std::string("hnsw_merger_vamana_merge_v1"));
  DP_EXPECT_EQ(descriptor.implementation_version, std::string("1.0.0"));
  DP_EXPECT_EQ(descriptor.stage, dp::StageKind::kMerge);
  DP_EXPECT_EQ(descriptor.requires.input_artifacts.size(), std::size_t{2});
  DP_EXPECT_EQ(descriptor.provides.artifact_type,
               dp::ArtifactType::kGlobalGraph);
  DP_EXPECT_EQ(descriptor.provides.output_id_space, dp::IdSpace::kGlobal);

  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
  registry.register_merge(native::m0_descriptor(), [] {
    return std::unique_ptr<dp::GraphMerger>{};
  });
  registry.register_merge(descriptor, [] {
    return std::unique_ptr<dp::GraphMerger>{};
  });
  registry.seal();
  DP_EXPECT_EQ(registry.size(), std::size_t{2});

  const auto repository_root =
      std::filesystem::path(HNSW_MERGER_TEST_PROJECT_ROOT);
  constexpr auto valid =
      R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})";
  dp::detail::validate_implementation_parameters(descriptor, valid,
                                                 repository_root);
  const auto parameters = m1::parse_parameters(valid);
  DP_EXPECT_EQ(parameters.r, std::uint32_t{16});
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      descriptor, R"({"R":16})", repository_root));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      descriptor,
      R"({"R":16,"alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1","lambda":4})",
      repository_root));
  DP_EXPECT_THROW(dp::detail::validate_implementation_parameters(
      descriptor,
      R"({"R":"16","alpha":1.2,"max_candidate_size":750,"metric":"l2","use_filters":false,"multi_shard_strategy":"large_first_pairwise_v1","lambda_policy":"paper_adaptive_log_v1"})",
      repository_root));
  DP_EXPECT_EQ(
      dp::sha256_file(repository_root /
                      "specs/004-hnsw-merger-diskann-merge-adaptation/"
                      "contracts/hnsw-merger-vamana-parameters.schema.json"),
      dp::sha256_file(repository_root /
                      "contracts/papers/hnsw-merger-v1/schemas/"
                      "hnsw-merger-vamana-parameters.schema.json"));
  test_bounded_algorithm_one_search();
  test_algorithm_semantics();
  return dp::test::finish();
}
