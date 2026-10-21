#pragma once

#include "diskann_pipeline/identifiers.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace diskann_pipeline::search {

struct PerQueryObservation {
  std::uint32_t query_row_index{0};
  std::string source_query_id;
  std::vector<std::uint64_t> result_ids;
  std::vector<float> result_distances;
  double total_us{0};
  std::uint32_t n_ios{0};
  std::optional<std::uint32_t> n_hops;
};

struct RawSearchPoint {
  std::uint32_t l_search{100};
  std::uint32_t k{100};
  std::uint32_t beamwidth{2};
  std::uint32_t repetition_index{0};
  std::uint32_t query_count{0};
  std::uint32_t completed_query_count{0};
  std::uint64_t batch_wall_clock_us{0};
  StageStatus status{StageStatus::kPlanned};
  std::vector<PerQueryObservation> per_query;
};

using SearchOne = std::function<void(
    std::size_t query_index, std::vector<std::uint64_t>& result_ids,
    std::vector<float>& result_distances, double& total_us,
    std::uint32_t& n_ios)>;

std::uint64_t derived_primary_cache_bytes(std::uint64_t requested_nodes,
                                          std::uint32_t dimension,
                                          std::uint32_t scalar_bytes,
                                          std::uint32_t declared_max_degree);
bool validate_complete_raw_search_point(const RawSearchPoint& point,
                                        std::uint64_t global_node_count);
RawSearchPoint capture_raw_search_point(RawSearchPoint declaration,
                                        const SearchOne& search_one);

struct ParitySearchPoint {
  std::vector<std::uint64_t> result_ids;
  std::vector<float> result_distances;
  double recall_at_10{0};
  double recall_at_100{0};
  double mean_ios{0};
  std::string controls_checksum;
  std::string timing_boundary;
  bool persistence_outside_timing{false};
};

bool search_runner_semantics_equal(const ParitySearchPoint& oracle,
                                   const ParitySearchPoint& instrumented,
                                   double absolute_tolerance = 1.0e-5,
                                   double relative_tolerance = 1.0e-6);

}  // namespace diskann_pipeline::search
