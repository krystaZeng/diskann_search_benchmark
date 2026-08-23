#include "search/diskann/pqflash_instrumented_runner.hpp"

#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>

namespace diskann_pipeline::search {

std::uint64_t derived_primary_cache_bytes(
    std::uint64_t requested_nodes, std::uint32_t dimension,
    std::uint32_t scalar_bytes, std::uint32_t declared_max_degree) {
  if (dimension == 0 || scalar_bytes == 0 || declared_max_degree == 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "cache projection inputs must be positive");
  }
  const auto vector_bytes = static_cast<std::uint64_t>(dimension) * scalar_bytes;
  const auto neighbor_bytes =
      (static_cast<std::uint64_t>(declared_max_degree) + 1) *
      sizeof(std::uint32_t);
  if (vector_bytes > std::numeric_limits<std::uint64_t>::max() - neighbor_bytes ||
      requested_nodes >
          std::numeric_limits<std::uint64_t>::max() /
              (vector_bytes + neighbor_bytes)) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "cache primary-payload size overflow");
  }
  return requested_nodes * (vector_bytes + neighbor_bytes);
}

bool validate_complete_raw_search_point(const RawSearchPoint& point,
                                        std::uint64_t global_node_count) {
  if (point.status != StageStatus::kSucceeded || point.k != 100 ||
      point.l_search < point.k || point.beamwidth != 2 ||
      point.repetition_index > 2 || point.query_count == 0 ||
      point.completed_query_count != point.query_count ||
      point.per_query.size() != point.query_count ||
      point.batch_wall_clock_us == 0 || global_node_count == 0) {
    return false;
  }
  for (std::size_t row = 0; row < point.per_query.size(); ++row) {
    const auto& query = point.per_query[row];
    if (query.query_row_index != row || query.result_ids.size() != 100 ||
        query.result_distances.size() != 100 || !std::isfinite(query.total_us) ||
        query.total_us < 0) {
      return false;
    }
    std::set<std::uint64_t> unique;
    for (std::size_t index = 0; index < 100; ++index) {
      if (query.result_ids[index] >= global_node_count ||
          !std::isfinite(query.result_distances[index]) ||
          query.result_distances[index] < 0 ||
          !unique.insert(query.result_ids[index]).second ||
          (index != 0 && query.result_distances[index - 1] >
                             query.result_distances[index])) {
        return false;
      }
    }
  }
  return true;
}

RawSearchPoint capture_raw_search_point(RawSearchPoint declaration,
                                        const SearchOne& search_one) {
  if (declaration.k != 100 || declaration.l_search < 100 ||
      declaration.beamwidth != 2 || declaration.query_count == 0 ||
      !search_one) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "raw search point declaration is invalid");
  }
  declaration.per_query.clear();
  declaration.per_query.resize(declaration.query_count);
  declaration.completed_query_count = 0;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t row = 0; row < declaration.query_count; ++row) {
    auto& query = declaration.per_query[row];
    query.query_row_index = static_cast<std::uint32_t>(row);
    query.result_ids.resize(declaration.k);
    query.result_distances.resize(declaration.k);
    search_one(row, query.result_ids, query.result_distances, query.total_us,
               query.n_ios);
    ++declaration.completed_query_count;
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);
  declaration.batch_wall_clock_us =
      std::max<std::uint64_t>(1, static_cast<std::uint64_t>(elapsed.count()));
  declaration.status = StageStatus::kSucceeded;
  return declaration;
}

bool search_runner_semantics_equal(const ParitySearchPoint& oracle,
                                   const ParitySearchPoint& instrumented,
                                   double absolute_tolerance,
                                   double relative_tolerance) {
  if (oracle.result_ids != instrumented.result_ids ||
      oracle.result_distances.size() != instrumented.result_distances.size() ||
      oracle.recall_at_10 != instrumented.recall_at_10 ||
      oracle.recall_at_100 != instrumented.recall_at_100 ||
      oracle.mean_ios != instrumented.mean_ios ||
      oracle.controls_checksum != instrumented.controls_checksum ||
      oracle.timing_boundary != instrumented.timing_boundary ||
      oracle.timing_boundary != "cpp_main_search_disk_index_query_batch_v1" ||
      !oracle.persistence_outside_timing ||
      !instrumented.persistence_outside_timing) {
    return false;
  }
  for (std::size_t index = 0; index < oracle.result_distances.size(); ++index) {
    const auto lhs = static_cast<double>(oracle.result_distances[index]);
    const auto rhs = static_cast<double>(instrumented.result_distances[index]);
    if (std::abs(lhs - rhs) >
        absolute_tolerance + relative_tolerance * std::abs(lhs)) {
      return false;
    }
  }
  return true;
}

}  // namespace diskann_pipeline::search
