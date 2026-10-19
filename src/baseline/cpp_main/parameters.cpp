#include "baseline/cpp_main/stage_bindings.hpp"

#include "core/json_codec.hpp"
#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace diskann_pipeline::cpp_main {
namespace {
using Json = nlohmann::json;

[[noreturn]] void reject(const std::string& reason) {
  throw PipelineError(ErrorCategory::kSchemaInvalid, reason);
}

std::uint32_t integer(const Json& value, const char* name, std::uint32_t minimum) {
  if (!value.is_number_integer() ||
      (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) {
    reject(std::string(name) + " must be an integer in uint32 range");
  }
  const auto wide = value.get<std::uint64_t>();
  if (wide < minimum || wide > std::numeric_limits<std::uint32_t>::max()) {
    reject(std::string(name) + " is outside its native range");
  }
  return static_cast<std::uint32_t>(wide);
}

std::uint32_t local_degree(std::uint32_t r) {
  // Widen before multiplication, including for R=UINT32_MAX.
  const auto degree = 2ULL * r / 3ULL;
  if (!degree || degree > std::numeric_limits<std::uint32_t>::max()) {
    reject("floor(2R/3) must be nonzero and fit native uint32");
  }
  return static_cast<std::uint32_t>(degree);
}

double number(const Json& value, const char* name) {
  if (!value.is_number()) reject(std::string(name) + " must be numeric");
  const auto result = value.get<double>();
  if (!std::isfinite(result)) reject(std::string(name) + " must be finite");
  return result;
}

void require_version(const std::string& version) {
  if (version != "1.0.0" && version != "1.1.0") reject("unsupported P0/B0 implementation version");
}
}  // namespace

P0Parameters parse_p0_parameters(const std::string& text, NodeCount nodes,
                                 const std::string& version) {
  require_version(version);
  const auto json = detail::parse_json_strict(text);
  if (version == "1.0.0") {
    validate_p0_parameters(text, nodes);
    return {16, 10, json.at("build_DRAM_budget_gib").get<double>(), 2,
            json.at("sample_probability").get<double>()};
  }
  detail::require_object_shape(json,
      {"R", "build_DRAM_budget_gib", "replication_factor", "sample_probability"});
  const auto r = integer(json.at("R"), "R", 2);
  const auto budget = number(json.at("build_DRAM_budget_gib"), "build_DRAM_budget_gib");
  const auto replication = integer(json.at("replication_factor"), "replication_factor", 1);
  const auto probability = number(json.at("sample_probability"), "sample_probability");
  if (!nodes || budget != 0.6 || replication != 2 ||
      probability <= 0 || probability > 1 ||
      std::abs(probability - std::min(1.0, 256000.0 / static_cast<double>(nodes))) > 1e-12) {
    reject("P0 parameters differ from fixed budget, replication or sample derivation");
  }
  return {r, local_degree(r), budget, replication, probability};
}

B0Parameters parse_b0_parameters(const std::string& text, const std::string& version) {
  require_version(version);
  if (version == "1.0.0") {
    validate_b0_parameters(text);
    return {16, 32, 10};
  }
  const auto json = detail::parse_json_strict(text);
  detail::require_object_shape(json,
      {"L", "R", "alpha", "build_pq_bytes", "max_occlusion_size",
       "num_threads", "saturation", "use_opq"}, {"partition_id"});
  const auto r = integer(json.at("R"), "R", 2);
  const auto l = integer(json.at("L"), "L", 1);
  if (number(json.at("alpha"), "alpha") != 1.2 ||
      integer(json.at("build_pq_bytes"), "build_pq_bytes", 0) != 0 ||
      integer(json.at("max_occlusion_size"), "max_occlusion_size", 1) != 750 ||
      integer(json.at("num_threads"), "num_threads", 1) != 6 ||
      !json.at("saturation").is_boolean() || json.at("saturation") != false ||
      !json.at("use_opq").is_boolean() || json.at("use_opq") != false) {
    reject("B0 parameters differ from fixed native rules");
  }
  if (json.contains("partition_id")) integer(json.at("partition_id"), "partition_id", 0);
  return {r, l, local_degree(r)};
}
}  // namespace diskann_pipeline::cpp_main
