#include "validation/validation_internal.hpp"

#include "diskann_pipeline/errors.hpp"

#include <fstream>
#include <sstream>

namespace diskann_pipeline::validation_detail {

std::vector<GlobalNodeId> validate_local_to_global_map(
    const std::filesystem::path& payload_path, NodeCount expected_count,
    NodeCount global_node_count) {
  std::ifstream input(payload_path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "LocalToGlobal map payload is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  try {
    const auto json = detail::parse_json_strict(bytes.str());
    if (!json.is_object() || !json.contains("local_to_global") ||
        json.size() != 1) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "LocalToGlobal map field is absent or malformed");
    }
    const auto& values = json.at("local_to_global");
    if (!values.is_array() || values.size() != expected_count) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "LocalToGlobal map length does not match local_node_count");
    }
    std::vector<GlobalNodeId> result;
    result.reserve(values.size());
    for (const auto& value : values) {
      if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "LocalToGlobal map contains a non-integer ID");
      }
      const auto id = value.get<NodeCount>();
      if (id >= global_node_count ||
          id > std::numeric_limits<GlobalNodeId>::max()) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "LocalToGlobal map contains an out-of-range ID");
      }
      result.push_back(static_cast<GlobalNodeId>(id));
    }
    return result;
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kMappingIncomplete,
                        std::string("invalid LocalToGlobal map: ") + error.what());
  }
}

}  // namespace diskann_pipeline::validation_detail
