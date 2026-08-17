#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#include "papers/hnsw_merger/vamana/m1_algorithm.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/registry.hpp"
#include "support/test_assert.hpp"
#include "validation/parameter_validator.hpp"

#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <utility>
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

class CountingL2 final : public dp::DistanceFunction {
 public:
  double evaluate(const std::vector<float>& lhs,
                  const std::vector<float>& rhs) const override {
    const auto left = static_cast<dp::GlobalNodeId>(lhs.at(0));
    const auto right = static_cast<dp::GlobalNodeId>(rhs.at(0));
    ++counts_[{left, right}];
    const auto delta = lhs.at(0) - rhs.at(0);
    return delta * delta;
  }

  std::size_t count(dp::GlobalNodeId lhs, dp::GlobalNodeId rhs) const {
    const auto found = counts_.find({lhs, rhs});
    return found == counts_.end() ? 0 : found->second;
  }

 private:
  mutable std::map<std::pair<dp::GlobalNodeId, dp::GlobalNodeId>,
                   std::size_t>
      counts_;
};

class CancelAfterRound final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return cancelled; }
  bool cancelled{false};
};

class CancelOnFirstReadVectors final : public dp::VectorAccessor {
 public:
  CancelOnFirstReadVectors(std::vector<float> values,
                           CancelAfterRound& cancellation)
      : values_(std::move(values)), cancellation_(cancellation),
        reads_(values_.size(), 0) {}

  std::size_t dimension() const noexcept override { return 1; }

  void read(dp::GlobalNodeId id,
            std::vector<float>& destination) const override {
    ++total_reads_;
    ++reads_.at(id);
    destination = {values_.at(id)};
    if (total_reads_ == 1) cancellation_.cancelled = true;
  }

  std::size_t total_reads() const { return total_reads_; }
  std::size_t reads(dp::GlobalNodeId id) const { return reads_.at(id); }

 private:
  std::vector<float> values_;
  CancelAfterRound& cancellation_;
  mutable std::size_t total_reads_{0};
  mutable std::vector<std::size_t> reads_;
};

class HugeDimensionVectors final : public dp::VectorAccessor {
 public:
  std::size_t dimension() const noexcept override {
    return std::numeric_limits<std::size_t>::max();
  }
  void read(dp::GlobalNodeId,
            std::vector<float>&) const override {
    throw std::runtime_error("overflow test must not read vectors");
  }
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
  m1::M1VectorCache cache(vectors, 8, 1024 * 1024, nullptr);
  const auto target = graph(
      {0}, 1,
      {{1, {2, 3}}, {2, {5}}, {3, {4}}, {4, {}},
       {5, {6}},    {6, {7}}, {7, {}}});
  const auto original_adjacency = target.adjacency;
  const auto original_entry = target.entry;

  const auto hits = m1::hnsw_merger_single_layer_search(
      target, 0, 2, cache, distance);

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
  m1::M1VectorCache tied_cache(tied_vectors, 4, 1024 * 1024, nullptr);
  const auto tied_target =
      graph({1}, 1, {{1, {3, 2}}, {2, {}}, {3, {}}});
  const auto tied_hits = m1::hnsw_merger_single_layer_search(
      tied_target, 0, 1, tied_cache, distance);
  DP_EXPECT_EQ(tied_hits, std::vector<dp::GlobalNodeId>({2}));
}

void test_vector_cache_and_candidate_decoration() {
  L2 distance;
  CountingVectors vectors({0, 1, 2, 3, 4, 5, 6, 7});
  constexpr std::uint64_t budget = 1024 * 1024;
  DP_EXPECT_EQ(m1::m1_vector_cache_required_bytes(1000000, 128),
               std::uint64_t{513001024});
  const auto required = m1::m1_vector_cache_required_bytes(8, 1);
  DP_EXPECT_THROW(m1::M1VectorCache(vectors, 8, required - 1, nullptr));
  DP_EXPECT_THROW(m1::M1VectorCache(
      HugeDimensionVectors{}, dp::kMaximumNodeCount,
      std::numeric_limits<std::uint64_t>::max(), nullptr));

  m1::M1VectorCache cache(vectors, 8, budget, nullptr);
  DP_EXPECT_EQ(vectors.reads(0), std::size_t{0});
  DP_EXPECT_EQ(cache.distance(0, 1, distance), 1.0);
  DP_EXPECT_EQ(cache.distance(0, 1, distance), 1.0);
  const auto target =
      graph({0}, 2, {{2, {3, 4}}, {3, {2}}, {4, {2, 5}}, {5, {4}}});
  static_cast<void>(m1::hnsw_merger_single_layer_search(
      target, 0, 2, cache, distance));
  static_cast<void>(m1::hnsw_merger_vamana_prune(
      0, {1, 2, 3, 4, 5}, 2, 1.2, 750, cache, distance));
  for (dp::GlobalNodeId id = 0; id < 8; ++id) {
    DP_EXPECT_TRUE(vectors.reads(id) <= std::size_t{1});
  }

  CountingVectors ordered_vectors({0, 1, 2});
  CountingL2 counting_distance;
  m1::M1VectorCache ordered_cache(ordered_vectors, 3, budget, nullptr);
  const auto shared_source =
      graph({1}, 0, {{0, {2, 1}}, {1, {0}}, {2, {0}}});
  const auto shared_target =
      graph({2}, 0, {{0, {1, 2}}, {1, {0}}, {2, {0}}});
  const auto ordered = m1::merge_pair(
      shared_source, shared_target, 2, 16, 1.2, 750, ordered_cache,
      counting_distance, nullptr);
  DP_EXPECT_EQ(ordered.adjacency.at(0),
               std::vector<dp::GlobalNodeId>({1, 2}));
  DP_EXPECT_EQ(counting_distance.count(0, 1), std::size_t{1});
  DP_EXPECT_EQ(counting_distance.count(0, 2), std::size_t{1});

  CancelAfterRound cancellation;
  CancelOnFirstReadVectors cancelling_vectors({0, 1, 2}, cancellation);
  m1::M1VectorCache cancelling_cache(cancelling_vectors, 3, budget,
                                     &cancellation);
  DP_EXPECT_THROW(cancelling_cache.distance(0, 1, distance));
  DP_EXPECT_EQ(cancelling_vectors.total_reads(), std::size_t{1});
  DP_EXPECT_EQ(cancelling_vectors.reads(1), std::size_t{0});
}

void test_algorithm_semantics() {
  Vectors vectors({0, 1, 2, 3, 4, 5, 6, 7});
  L2 distance;
  m1::M1VectorCache cache(vectors, 8, 1024 * 1024, nullptr);
  const auto target = graph({0}, 4, {{2, {3}}, {3, {2, 4}}, {4, {3}}});
  const auto hits = m1::hnsw_merger_single_layer_search(
      target, 0, 2, cache, distance);
  DP_EXPECT_EQ(hits, std::vector<dp::GlobalNodeId>({2, 3}));

  const auto source = graph({1}, 0, {{0, {1}}, {1, {0}}});
  m1::PairwiseMergeDecision decision;
  const auto merged = m1::merge_pair(source, target, 2, 16, 1.2, 750,
                                     cache, distance, &decision);
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
                                      1.2, 750, cache, distance, nullptr);
  DP_EXPECT_EQ(overlap.adjacency.at(3),
               std::vector<dp::GlobalNodeId>({4, 5}));

  const auto duplicate_source =
      graph({4}, 0, {{0, {1, 1}}, {1, {0, 0}}});
  const auto duplicate_target =
      graph({5}, 2, {{2, {3, 3}}, {3, {2, 2}}});
  const auto duplicate_merged = m1::merge_pair(
      duplicate_source, duplicate_target, 2, 2, 1.2, 750, cache,
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
      0, {1, 2, 3, 4, 5}, 2, 1.2, 750, cache, distance);
  DP_EXPECT_TRUE(bounded_prune.size() <= std::size_t{2});
  DP_EXPECT_THROW(m1::hnsw_merger_vamana_prune(
      0, {0, 1}, 2, 1.2, 750, cache, distance));
  DP_EXPECT_THROW(m1::hnsw_merger_single_layer_search(
      graph({6}, 2, {{2, {99}}}), 0, 1, cache, distance));

  const auto multi = m1::merge_large_first(
      {graph({0}, 0, {{0, {1}}, {1, {0}}, {2, {1}}, {3, {2}}}),
       graph({1}, 4, {{4, {5}}, {5, {4}}, {6, {5}}}),
       graph({2}, 7, {{7, {6}}, {6, {7}}})},
      16, 1.2, 750, vectors, distance, 8, 1024 * 1024, nullptr, nullptr);
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
        16, 1.2, 750, vectors, distance, 8, 1024 * 1024,
        &cancellation, &metrics));
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
  test_vector_cache_and_candidate_decoration();
  test_algorithm_semantics();
  return dp::test::finish();
}
