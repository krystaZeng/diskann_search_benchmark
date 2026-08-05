#include "validation/validation_internal.hpp"

#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>

namespace diskann_pipeline::validation_detail {

std::uint32_t validate_adjacency_graph(
    const std::filesystem::path& payload_path, NodeCount expected_count,
    NodeCount id_limit, std::uint32_t declared_max_degree) {
  std::ifstream input(payload_path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "graph payload is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  try {
    const auto json = detail::parse_json_strict(bytes.str());
    detail::require_object_shape(json, {"adjacency"});
    const auto& adjacency = json.at("adjacency");
    if (!adjacency.is_array() || adjacency.size() != expected_count) {
      throw PipelineError(ErrorCategory::kNodeCountMismatch,
                          "graph adjacency count does not match node count");
    }
    std::uint32_t observed = 0;
    for (const auto& neighbors : adjacency) {
      if (!neighbors.is_array()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "graph adjacency entry is not an array");
      }
      if (neighbors.size() > declared_max_degree) {
        throw PipelineError(ErrorCategory::kDegreeExceeded,
                            "graph degree exceeds declared maximum");
      }
      observed = std::max(observed,
                          static_cast<std::uint32_t>(neighbors.size()));
      for (const auto& neighbor : neighbors) {
        if ((!neighbor.is_number_unsigned() && !neighbor.is_number_integer()) ||
            (neighbor.is_number_integer() && neighbor.get<std::int64_t>() < 0)) {
          throw PipelineError(ErrorCategory::kIdRangeInvalid,
                              "graph contains a non-integer or negative neighbor");
        }
        const auto id = neighbor.get<NodeCount>();
        if (id >= id_limit || id > std::numeric_limits<GlobalNodeId>::max()) {
          throw PipelineError(ErrorCategory::kIdRangeInvalid,
                              "graph contains an out-of-range neighbor");
        }
      }
    }
    return observed;
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("invalid adjacency graph: ") + error.what());
  }
}

}  // namespace diskann_pipeline::validation_detail
