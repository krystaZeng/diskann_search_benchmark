#include "partition/diskann_kmeans/stage_binding.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace diskann_pipeline::diskann_kmeans {

ImplementationDescriptor descriptor() {
  return {kImplementationId, kImplementationVersion, StageKind::kPartition,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"}, IdSpace::kNone}},
           {"dataset.diskann_float32_bin/v1"},
           {"cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kPartition, "partition-artifact/v1", IdSpace::kLocal,
           {"partition.centroids/v1", "partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1"}},
          "contracts/partition/diskann-kmeans-v1/parameters.schema.json", false};
}

Parameters parse_parameters(const std::string& text, NodeCount nodes) {
  const auto json = detail::parse_json_strict(text);
  detail::require_object_shape(json, {"K"}, {"max_k_means_reps"});
  const auto positive = [](const nlohmann::json& value, const char* name) {
    if (!value.is_number_integer() ||
        (!value.is_number_unsigned() && value.get<std::int64_t>() <= 0) ||
        (value.is_number_unsigned() && value.get<std::uint64_t>() == 0)) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          std::string(name) + " must be a positive integer");
    }
    return value.get<std::uint64_t>();
  };
  const auto k = positive(json.at("K"), "K");
  const auto reps = positive(json.value("max_k_means_reps", nlohmann::json(15)),
                             "max_k_means_reps");
  if (!nodes || k > nodes || k > std::numeric_limits<std::uint32_t>::max() ||
      reps > std::numeric_limits<std::size_t>::max()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "K exceeds Dataset node count/native ID range or iteration count overflows");
  }
  return {static_cast<std::uint32_t>(k), static_cast<std::size_t>(reps)};
}

double sample_probability(NodeCount nodes) {
  if (!nodes) throw PipelineError(ErrorCategory::kNodeCountMismatch, "empty Dataset");
  return std::min(1.0, 256000.0 / static_cast<double>(nodes));
}

void validate_training_sample_count(std::size_t samples, std::uint32_t k) {
  if (!samples || samples < k || samples > 8388608) {
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
                        "actual training sample count must be nonzero, >= K and <= 8388608; no resampling");
  }
}

std::vector<NodeCount> validate_native_output(
    const std::filesystem::path& prefix, const DatasetIdentity& dataset,
    std::uint32_t k) {
  cpp_main::validate_centroids(prefix.string() + "_centroids.bin", k,
                               dataset.vector_dimension);
  std::vector<bool> seen(dataset.global_node_count, false);
  std::vector<NodeCount> counts;
  for (std::uint32_t partition = 0; partition < k; ++partition) {
    const auto path = prefix.string() + "_subshard-" + std::to_string(partition) +
                      "_ids_uint32.bin";
    std::ifstream input(path, std::ios::binary);
    std::uint32_t rows = 0, dim = 0;
    input.read(reinterpret_cast<char*>(&rows), sizeof(rows));
    input.read(reinterpret_cast<char*>(&dim), sizeof(dim));
    if (!input || !rows || dim != 1) {
      throw PipelineError(ErrorCategory::kMappingIncomplete,
                          "missing, empty or malformed native shard " + std::to_string(partition));
    }
    const auto ids = cpp_main::validate_partition_id_map(path, rows, dataset.global_node_count);
    if (!std::is_sorted(ids.begin(), ids.end())) {
      throw PipelineError(ErrorCategory::kMappingIncomplete, "native map is not ascending");
    }
    for (const auto id : ids) {
      if (seen[id]) throw PipelineError(ErrorCategory::kMappingIncomplete,
                                        "global ID occurs more than once in hard Partition");
      seen[id] = true;
    }
    counts.push_back(rows);
  }
  if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
    throw PipelineError(ErrorCategory::kMappingIncomplete, "hard Partition does not cover Dataset");
  }
  return counts;
}

}  // namespace diskann_pipeline::diskann_kmeans
