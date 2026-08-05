#include "fakes/fake_pipeline.hpp"

#include "support/test_assert.hpp"

namespace dp = diskann_pipeline;

int main() {
  const std::vector<std::vector<dp::GlobalNodeId>> maps{
      {0, 1, 2, 3}, {2, 3, 4, 5}};
  const std::vector<std::vector<std::vector<dp::LocalNodeId>>> local_graphs{
      {{1, 2}, {0, 2}, {1, 3}, {}},
      {{1, 2}, {0, 2}, {1, 3}, {}}};
  const auto merged = dp::test::merge_local_graphs(maps, local_graphs, 3);
  DP_EXPECT_EQ(merged.size(), std::size_t{6});
  DP_EXPECT_EQ(merged[0], std::vector<dp::GlobalNodeId>({1, 2}));
  DP_EXPECT_EQ(merged[2], std::vector<dp::GlobalNodeId>({1, 3, 4}));
  DP_EXPECT_EQ(merged[3], std::vector<dp::GlobalNodeId>({2, 4}));
  DP_EXPECT_TRUE(merged[5].empty());
  for (const auto& neighbors : merged) {
    DP_EXPECT_TRUE(neighbors.size() <= 3);
    for (const auto neighbor : neighbors) {
      DP_EXPECT_TRUE(neighbor < 6);
    }
  }
  return dp::test::finish();
}
