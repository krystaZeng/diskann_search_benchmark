#include "search/diskann/pqflash_instrumented_runner.hpp"

#include "support/test_assert.hpp"

namespace ds = diskann_pipeline::search;

namespace {
ds::ParitySearchPoint point() {
  ds::ParitySearchPoint value;
  value.result_ids = {1, 2, 3};
  value.result_distances = {0.1F, 0.2F, 0.3F};
  value.recall_at_10 = 100.0;
  value.recall_at_100 = 100.0;
  value.mean_ios = 3.0;
  value.controls_checksum = std::string(64, 'a');
  value.timing_boundary = "cpp_main_search_disk_index_query_batch_v1";
  value.persistence_outside_timing = true;
  return value;
}
}  // namespace

int main() {
  const auto oracle = point();
  auto instrumented = point();
  DP_EXPECT_TRUE(ds::search_runner_semantics_equal(oracle, instrumented));
  instrumented.result_ids[1] = 4;
  DP_EXPECT_TRUE(!ds::search_runner_semantics_equal(oracle, instrumented));
  instrumented = point();
  instrumented.result_distances[1] += 1.0e-6F;
  DP_EXPECT_TRUE(ds::search_runner_semantics_equal(oracle, instrumented));
  instrumented.timing_boundary = "serialization-inside-timer";
  DP_EXPECT_TRUE(!ds::search_runner_semantics_equal(oracle, instrumented));
  return diskann_pipeline::test::finish();
}
