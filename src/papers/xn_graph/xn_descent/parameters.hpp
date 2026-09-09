#pragma once

#include <cstdint>
#include <string>

namespace diskann_pipeline::xn_descent {

inline constexpr const char* kParameterSchemaVersion =
    "xn-descent-parameters/v1";
inline constexpr const char* kPrngPolicy =
    "mt19937_64-splitmix64-per-node-v1";
inline constexpr const char* kSamplingPolicy =
    "unbiased-rejection-unique-nonself-v1";
inline constexpr const char* kTieBreakPolicy =
    "squared-distance-then-local-id-v1";
inline constexpr const char* kRoundModel =
    "deterministic-synchronous-round-v1";
inline constexpr const char* kConvergenceRule =
    "full-state-fixed-point-or-three-round-c-g-stability-v1";
inline constexpr const char* kEntryPointRule = "local-centroid-nearest-v1";
inline constexpr const char* kDistanceImplementation = "squared-l2/v1";
inline constexpr const char* kReproductionMode = "faithful_adaptation";

enum class ValidationProfile {
  kSemanticSmall,
  kPipelineCompatible,
  kPaperStyle,
};

struct XnDescentParameters {
  ValidationProfile validation_profile{ValidationProfile::kSemanticSmall};
  std::uint32_t omega{0};
  std::uint32_t lambda{0};
  double alpha{1.0};
  std::uint64_t seed{42};
  std::uint32_t iteration_cap{0};
};

XnDescentParameters parse_parameters(const std::string& json_text);
std::string canonical_parameters_json(const XnDescentParameters& parameters);
std::string to_string(ValidationProfile profile);

}  // namespace diskann_pipeline::xn_descent
