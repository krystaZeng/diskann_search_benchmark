#pragma once

#include "papers/xn_graph/xn_descent/parameters.hpp"
#include "papers/xn_graph/xn_descent/stage_binding.hpp"
#include "papers/xn_graph/xn_descent/xn_descent_algorithm.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"
#include "support/test_assert.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace diskann_pipeline::xn_descent::test {

namespace baseline_fixture = diskann_pipeline::cpp_main::test;
using Json = nlohmann::json;

inline std::vector<std::vector<float>> line_vectors(
    std::initializer_list<float> coordinates) {
  std::vector<std::vector<float>> result;
  for (const auto value : coordinates) result.push_back({value});
  return result;
}

class MatrixVectorAccessor final : public VectorAccessor {
 public:
  explicit MatrixVectorAccessor(std::vector<std::vector<float>> rows)
      : rows_(std::move(rows)) {}

  std::size_t dimension() const noexcept override {
    return rows_.empty() ? 0 : rows_.front().size();
  }

  void read(GlobalNodeId id, std::vector<float>& destination) const override {
    if (id >= rows_.size()) throw std::out_of_range("fixture vector id");
    destination = rows_[id];
  }

 private:
  std::vector<std::vector<float>> rows_;
};

class AllocationFailureVectorAccessor final : public VectorAccessor {
 public:
  AllocationFailureVectorAccessor(std::size_t dimension,
                                  std::size_t successful_reads)
      : dimension_(dimension), successful_reads_(successful_reads) {}

  std::size_t dimension() const noexcept override { return dimension_; }

  void read(GlobalNodeId, std::vector<float>& destination) const override {
    if (reads_++ >= successful_reads_) throw std::bad_alloc();
    destination.assign(dimension_, 0.0F);
  }

 private:
  std::size_t dimension_;
  std::size_t successful_reads_;
  mutable std::size_t reads_{0};
};

class StepCancellation final : public CancellationToken {
 public:
  explicit StepCancellation(std::uint64_t cancel_after)
      : cancel_after_(cancel_after) {}
  bool is_cancelled() const noexcept override {
    return polls_++ >= cancel_after_;
  }

 private:
  std::uint64_t cancel_after_;
  mutable std::uint64_t polls_{0};
};

struct SchedulingFixture {
  std::vector<LocalNodeId> owner_order;
  std::uint32_t worker_count{1};
};

struct StageCallCounts {
  std::uint64_t partition{0};
  std::uint64_t local_build{0};
  std::uint64_t merge{0};
};

inline std::string effective_parameters_json(
    const std::string& profile = "semantic-small", std::uint32_t omega = 3,
    std::uint32_t lambda = 3, double alpha = 1.2,
    std::uint32_t iteration_cap = 32, std::uint64_t seed = 42) {
  return Json{{"schema_version", kParameterSchemaVersion},
              {"validation_profile", profile},
              {"omega", omega},
              {"lambda", lambda},
              {"alpha", alpha},
              {"seed", seed},
              {"iteration_cap", iteration_cap},
              {"prng_policy", kPrngPolicy},
              {"sampling_policy", kSamplingPolicy},
              {"tie_break_policy", kTieBreakPolicy},
              {"round_model", kRoundModel},
              {"convergence_rule", kConvergenceRule},
              {"entry_point_rule", kEntryPointRule},
              {"distance_implementation", kDistanceImplementation},
              {"reproduction_mode", kReproductionMode}}
      .dump();
}

inline std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                              std::size_t offset) {
  return static_cast<std::uint32_t>(bytes.at(offset)) |
         (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8U) |
         (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16U) |
         (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24U);
}

inline std::uint64_t read_u64(const std::vector<std::uint8_t>& bytes,
                              std::size_t offset) {
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(bytes.at(offset + index))
             << (8U * index);
  }
  return value;
}

inline std::vector<std::uint8_t> read_bytes(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to read XN fixture bytes");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

inline std::vector<std::vector<LocalNodeId>> read_native_adjacency(
    const std::filesystem::path& path, NodeCount count) {
  const auto bytes = read_bytes(path);
  std::size_t offset = 24;
  std::vector<std::vector<LocalNodeId>> result(count);
  for (NodeCount node = 0; node < count; ++node) {
    const auto degree = read_u32(bytes, offset);
    offset += 4;
    for (std::uint32_t index = 0; index < degree; ++index) {
      result[node].push_back(read_u32(bytes, offset));
      offset += 4;
    }
  }
  if (offset != bytes.size()) throw std::runtime_error("trailing graph bytes");
  return result;
}

}  // namespace diskann_pipeline::xn_descent::test
