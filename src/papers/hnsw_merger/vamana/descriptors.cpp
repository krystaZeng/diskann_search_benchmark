#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#include "baseline/cpp_main/native_formats.hpp"

#include "diskann_pipeline/errors.hpp"

#include <nlohmann/json.hpp>

namespace diskann_pipeline::hnsw_merger_vamana {

CompatibilityResult input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext&) {
  return cpp_main::check_native_merge_inputs(known_inputs);
}

ImplementationDescriptor descriptor() {
  return {kImplementationId,
          kImplementationVersion,
          StageKind::kMerge,
          {{{ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal},
            {ArtifactType::kLocalGraph, {"local-graph-artifact/v1"},
             IdSpace::kLocal}},
           {"partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1",
            "graph.local_adjacency/v1"},
           {"vector_accessor/v1", "vector_accessor/dataset-bound-v1",
            "distance_function/squared-l2-v1",
            "cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kGlobalGraph,
           "global-graph-artifact/v1",
           IdSpace::kGlobal,
           {"graph.global_adjacency/v1", "graph.medoids/v1"}},
          "contracts/papers/hnsw-merger-v1/schemas/"
          "hnsw-merger-vamana-parameters.schema.json",
          false};
}

M1Parameters parse_parameters(const std::string& json_text) {
  try {
    const auto json = nlohmann::json::parse(json_text);
    if (!json.is_object() || json.size() != 7 || json.at("R") != 16 ||
        json.at("alpha") != 1.2 || json.at("max_candidate_size") != 750 ||
        json.at("metric") != "l2" || json.at("use_filters") != false ||
        json.at("multi_shard_strategy") != "large_first_pairwise_v1" ||
        json.at("lambda_policy") != "paper_adaptive_log_v1") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "M1 parameters differ from the frozen profile");
    }
    return {};
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception&) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "M1 parameters are not the frozen JSON document");
  }
}

}  // namespace diskann_pipeline::hnsw_merger_vamana
