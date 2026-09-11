#include "papers/xn_graph/xn_descent/parameters.hpp"

#include "diskann_pipeline/errors.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string>

namespace diskann_pipeline::xn_descent {
namespace {

using Json = nlohmann::json;

[[noreturn]] void invalid(const std::string& reason) {
  throw PipelineError(ErrorCategory::kSchemaInvalid,
                      "XN-Descent parameters: " + reason);
}

void require_exact_string(const Json& json, const char* key,
                          const char* expected) {
  if (!json.contains(key) || !json.at(key).is_string() ||
      json.at(key).get<std::string>() != expected) {
    invalid(std::string(key) + " must equal " + expected);
  }
}

std::uint32_t positive_u32(const Json& json, const char* key) {
  if (!json.contains(key) || !json.at(key).is_number_unsigned()) {
    invalid(std::string(key) + " must be a positive uint32");
  }
  const auto value = json.at(key).get<std::uint64_t>();
  if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
    invalid(std::string(key) + " must be a positive uint32");
  }
  return static_cast<std::uint32_t>(value);
}

}  // namespace

std::string to_string(ValidationProfile profile) {
  switch (profile) {
    case ValidationProfile::kSemanticSmall:
      return "semantic-small";
    case ValidationProfile::kPipelineCompatible:
      return "pipeline-compatible";
    case ValidationProfile::kPaperStyle:
      return "paper-style";
  }
  throw PipelineError(ErrorCategory::kInternal,
                      "unknown XN-Descent validation profile");
}

XnDescentParameters parse_parameters(const std::string& json_text) {
  Json json;
  try {
    json = Json::parse(json_text);
  } catch (const std::exception& error) {
    invalid(std::string("invalid JSON: ") + error.what());
  }
  if (!json.is_object()) invalid("document must be an object");

  static const std::set<std::string> allowed{
      "schema_version",        "validation_profile", "omega",
      "lambda",                "alpha",              "seed",
      "iteration_cap",         "prng_policy",        "sampling_policy",
      "tie_break_policy",      "round_model",        "convergence_rule",
      "entry_point_rule",      "distance_implementation",
      "reproduction_mode",     "partition_id"};
  for (const auto& item : json.items()) {
    if (allowed.count(item.key()) == 0) {
      invalid("unexpected field: " + item.key());
    }
  }

  require_exact_string(json, "schema_version", kParameterSchemaVersion);
  require_exact_string(json, "prng_policy", kPrngPolicy);
  require_exact_string(json, "sampling_policy", kSamplingPolicy);
  require_exact_string(json, "tie_break_policy", kTieBreakPolicy);
  require_exact_string(json, "round_model", kRoundModel);
  require_exact_string(json, "convergence_rule", kConvergenceRule);
  require_exact_string(json, "entry_point_rule", kEntryPointRule);
  require_exact_string(json, "distance_implementation",
                       kDistanceImplementation);
  require_exact_string(json, "reproduction_mode", kReproductionMode);

  if (!json.contains("validation_profile") ||
      !json.at("validation_profile").is_string()) {
    invalid("validation_profile is required");
  }
  XnDescentParameters result;
  const auto profile = json.at("validation_profile").get<std::string>();
  if (profile == "semantic-small") {
    result.validation_profile = ValidationProfile::kSemanticSmall;
  } else if (profile == "pipeline-compatible") {
    result.validation_profile = ValidationProfile::kPipelineCompatible;
  } else if (profile == "paper-style") {
    result.validation_profile = ValidationProfile::kPaperStyle;
  } else {
    invalid("validation_profile is unsupported");
  }

  result.omega = positive_u32(json, "omega");
  result.lambda = positive_u32(json, "lambda");
  result.iteration_cap = positive_u32(json, "iteration_cap");
  if (!json.contains("alpha") || !json.at("alpha").is_number()) {
    invalid("alpha must be a finite number >= 1");
  }
  result.alpha = json.at("alpha").get<double>();
  if (!std::isfinite(result.alpha) || result.alpha < 1.0) {
    invalid("alpha must be a finite number >= 1");
  }
  if (json.contains("seed")) {
    if (!json.at("seed").is_number_unsigned()) {
      invalid("seed must be a uint64");
    }
    result.seed = json.at("seed").get<std::uint64_t>();
  }
  if (json.contains("partition_id")) {
    if (!json.at("partition_id").is_number_unsigned() ||
        json.at("partition_id").get<std::uint64_t>() >
            std::numeric_limits<std::uint32_t>::max()) {
      invalid("partition_id must be a uint32 when present");
    }
  }

  return result;
}

std::string canonical_parameters_json(
    const XnDescentParameters& parameters) {
  const auto validated = parse_parameters(
      Json{{"schema_version", kParameterSchemaVersion},
           {"validation_profile", to_string(parameters.validation_profile)},
           {"omega", parameters.omega},
           {"lambda", parameters.lambda},
           {"alpha", parameters.alpha},
           {"seed", parameters.seed},
           {"iteration_cap", parameters.iteration_cap},
           {"prng_policy", kPrngPolicy},
           {"sampling_policy", kSamplingPolicy},
           {"tie_break_policy", kTieBreakPolicy},
           {"round_model", kRoundModel},
           {"convergence_rule", kConvergenceRule},
           {"entry_point_rule", kEntryPointRule},
           {"distance_implementation", kDistanceImplementation},
           {"reproduction_mode", kReproductionMode}}
          .dump());
  return Json{{"schema_version", kParameterSchemaVersion},
              {"validation_profile", to_string(validated.validation_profile)},
              {"omega", validated.omega},
              {"lambda", validated.lambda},
              {"alpha", validated.alpha},
              {"seed", validated.seed},
              {"iteration_cap", validated.iteration_cap},
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

}  // namespace diskann_pipeline::xn_descent
