#include "search/diskann/pqflash_instrumented_runner.hpp"

#include "support/test_assert.hpp"

#include <vector>

namespace ds = diskann_pipeline::search;

int main() {
  ds::RawSearchPoint point;
  point.l_search = 100;
  point.k = 100;
  point.beamwidth = 2;
  point.repetition_index = 0;
  point.query_count = 1;
  point.completed_query_count = 1;
  point.batch_wall_clock_us = 1000;
  point.status = diskann_pipeline::StageStatus::kSucceeded;
  ds::PerQueryObservation query;
  query.query_row_index = 0;
  query.source_query_id = "opaque-0042";
  query.result_ids.resize(100);
  query.result_distances.resize(100);
  for (std::size_t index = 0; index < 100; ++index) {
    query.result_ids[index] = index;
    query.result_distances[index] = static_cast<float>(index);
  }
  query.total_us = 10.0;
  query.n_ios = 2;
  point.per_query.push_back(query);
  DP_EXPECT_TRUE(ds::validate_complete_raw_search_point(point, 1000000));
  DP_EXPECT_EQ(ds::derived_primary_cache_bytes(10000, 128, 4, 16),
               std::uint64_t{5800000});
  int invocations = 0;
  const auto observed = ds::capture_raw_search_point(
      point,
      [&](std::size_t, std::vector<std::uint64_t>& ids,
          std::vector<float>& distances, double& total_us,
          std::uint32_t& n_ios) {
        ++invocations;
        for (std::size_t index = 0; index < ids.size(); ++index) {
          ids[index] = index;
          distances[index] = static_cast<float>(index);
        }
        total_us = 10.0;
        n_ios = 2;
      });
  DP_EXPECT_EQ(invocations, 1);
  DP_EXPECT_TRUE(ds::validate_complete_raw_search_point(observed, 1000000));
  return diskann_pipeline::test::finish();
}
