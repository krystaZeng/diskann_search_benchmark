#include "papers/gp_ann/ogp/stage_binding.hpp"

#include "diskann_pipeline/errors.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace diskann_pipeline::gp_ann_ogp {

ImplementationDescriptor descriptor() {
  return {kImplementationId,
          kImplementationVersion,
          StageKind::kPartition,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
             IdSpace::kNone}},
           {"dataset.diskann_float32_bin/v1"},
           {"cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kPartition,
           "partition-artifact/v1",
           IdSpace::kLocal,
           {"partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1",
            "partition.overlap/v1"}},
          "contracts/papers/gp-ann-ogp-v1/schemas/ogp-parameters.schema.json",
          false};
}

OgpParameters parse_parameters(const std::string& json_text,
                               NodeCount global_node_count) {
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(json_text);
  } catch (const std::exception&) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP parameters are not valid JSON");
  }
  if (!json.is_object() || json.size() != 3 ||
      !json.contains("requested_num_clusters") ||
      !json.contains("requested_overlap") ||
      !json.contains("partitioning_mode") ||
      !json.at("requested_num_clusters").is_number_integer() ||
      !json.at("requested_overlap").is_number() ||
      !json.at("partitioning_mode").is_string()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP parameters do not match the official CLI surface");
  }

  const auto requested = json.at("requested_num_clusters").get<std::int64_t>();
  const auto overlap = json.at("requested_overlap").get<double>();
  const auto mode = json.at("partitioning_mode").get<std::string>();
  if (requested < 1 ||
      requested > static_cast<std::int64_t>(
                      std::numeric_limits<std::int32_t>::max()) ||
      static_cast<NodeCount>(requested) > global_node_count ||
      !std::isfinite(overlap) || overlap < 0.0 ||
      (mode != "default" && mode != "strong")) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP parameter value is outside the pinned CLI contract");
  }
  const long double assignments =
      static_cast<long double>(overlap) * global_node_count;
  if (!std::isfinite(assignments) ||
      assignments > static_cast<long double>(
                        std::numeric_limits<std::size_t>::max() -
                        global_node_count)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "OGP overlap assignment arithmetic overflows");
  }
  return {static_cast<std::uint32_t>(requested), overlap, mode};
}

}  // namespace diskann_pipeline::gp_ann_ogp
