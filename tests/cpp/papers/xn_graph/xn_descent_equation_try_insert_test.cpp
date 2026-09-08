#include "xn_descent_test_support.hpp"

#include <cmath>
#include <limits>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace fixture = diskann_pipeline::xn_descent::test;

namespace {

void equation_tests() {
  fixture::baseline_fixture::TestDistance distance;
  const auto vectors = fixture::line_vectors({0.0F, 1.0F, 3.0F, 2.0F});
  DP_EXPECT_TRUE(xn::equation_two_occludes(0, 2, 1, 1.0, vectors,
                                           distance));
  DP_EXPECT_TRUE(!xn::equation_two_occludes(0, 3, 2, 1.0, vectors,
                                            distance));
  const auto first_equality = fixture::line_vectors({0.0F, 1.0F, -1.0F});
  DP_EXPECT_TRUE(!xn::equation_two_occludes(0, 1, 2, 1.0,
                                            first_equality, distance));
  const auto equality = fixture::line_vectors({0.0F, 1.0F, 2.0F});
  DP_EXPECT_TRUE(!xn::equation_two_occludes(0, 2, 1, 2.0, equality,
                                            distance));
  DP_EXPECT_TRUE(!xn::scaled_alpha_squared_strict_less(2.0, 1.0, 4.0));
  DP_EXPECT_TRUE(xn::scaled_alpha_squared_strict_less(2.0, 1.0, 4.1));
  DP_EXPECT_TRUE(xn::scaled_alpha_squared_strict_less(
      1.0e300, std::numeric_limits<double>::denorm_min(), 1.0e300));
  DP_EXPECT_TRUE(!xn::scaled_alpha_squared_strict_less(
      std::numeric_limits<double>::max(), 1.0,
      std::numeric_limits<double>::max()));
  DP_EXPECT_THROW(xn::scaled_alpha_squared_strict_less(
      std::numeric_limits<double>::infinity(), 1.0, 2.0));
  DP_EXPECT_THROW(xn::scaled_alpha_squared_strict_less(1.0, -1.0, 2.0));
}

void try_insert_tests() {
  fixture::baseline_fixture::TestDistance distance;
  const auto vectors = fixture::line_vectors({0.0F, 3.0F, -3.0F, 1.0F});
  xn::NeighborList values;
  xn::try_insert(0, 2, 2, vectors, distance, values);
  xn::try_insert(0, 1, 2, vectors, distance, values);
  DP_EXPECT_EQ(values.size(), std::size_t{2});
  DP_EXPECT_EQ(values[0].id, dp::LocalNodeId{1});
  DP_EXPECT_EQ(values[1].id, dp::LocalNodeId{2});
  xn::try_insert(0, 3, 2, vectors, distance, values);
  DP_EXPECT_EQ(values.size(), std::size_t{2});
  DP_EXPECT_EQ(values[0].id, dp::LocalNodeId{3});
  DP_EXPECT_EQ(values[1].id, dp::LocalNodeId{1});
  xn::try_insert(0, 3, 2, vectors, distance, values);
  DP_EXPECT_EQ(values.size(), std::size_t{2});

  xn::NeighborList tie_values;
  xn::try_insert(0, 2, 1, vectors, distance, tie_values);
  xn::try_insert(0, 1, 1, vectors, distance, tie_values);
  DP_EXPECT_EQ(tie_values.size(), std::size_t{1});
  DP_EXPECT_EQ(tie_values.front().id, dp::LocalNodeId{1});

  const auto center_vectors =
      fixture::line_vectors({-20.0F, 10.0F, 0.0F, 9.0F, 1.0F});
  xn::NeighborList target_center_v;
  xn::try_insert(1, 2, 1, center_vectors, distance, target_center_v);
  xn::try_insert(1, 3, 1, center_vectors, distance, target_center_v);
  DP_EXPECT_EQ(target_center_v.front().id, dp::LocalNodeId{3});
  DP_EXPECT_EQ(target_center_v.front().squared_distance_to_owner, 1.0);
  xn::NeighborList target_center_u;
  xn::try_insert(2, 1, 1, center_vectors, distance, target_center_u);
  xn::try_insert(2, 4, 1, center_vectors, distance, target_center_u);
  DP_EXPECT_EQ(target_center_u.front().id, dp::LocalNodeId{4});
  DP_EXPECT_EQ(target_center_u.front().squared_distance_to_owner, 1.0);

  DP_EXPECT_THROW(xn::try_insert(0, 0, 2, vectors, distance, values));
  DP_EXPECT_THROW(xn::try_insert(0, 4, 2, vectors, distance, values));
  DP_EXPECT_THROW(xn::try_insert(0, 1, 0, vectors, distance, values));
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::string mode = argv[1];
  if (mode == "equation") equation_tests();
  if (mode == "try_insert") try_insert_tests();
  if (mode != "equation" && mode != "try_insert") return 2;
  return dp::test::finish();
}
