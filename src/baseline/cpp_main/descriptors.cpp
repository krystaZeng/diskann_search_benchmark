#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/native_formats.hpp"

namespace diskann_pipeline::cpp_main {
namespace {

constexpr const char* kVersion = "1.0.0";

}  // namespace

ImplementationDescriptor p0_descriptor() {
  return {kP0ImplementationId,
          kVersion,
          StageKind::kPartition,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
             IdSpace::kNone}},
           {"dataset.diskann_float32_bin/v1"},
           {"cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kPartition,
           "partition-artifact/v1",
           IdSpace::kLocal,
           {"partition.centroids/v1",
            "partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1",
            "partition.overlap/v1"}},
          "contracts/baseline-v1/schemas/cpp-main-p0-parameters.schema.json",
          false};
}

ImplementationDescriptor b0_descriptor() {
  return {kB0ImplementationId,
          kVersion,
          StageKind::kLocalBuild,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"},
             IdSpace::kNone},
            {ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal}},
           {"dataset.diskann_float32_bin/v1",
            "partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1"},
           {"vector_accessor/v1", "cancellation_token/v1",
            "metrics_sink/v1"}},
          {ArtifactType::kLocalGraph,
           "local-graph-artifact/v1",
           IdSpace::kLocal,
           {"graph.local_adjacency/v1"}},
          "contracts/baseline-v1/schemas/cpp-main-b0-parameters.schema.json",
          false};
}

ImplementationDescriptor m0_descriptor() {
  return {kM0ImplementationId,
          kVersion,
          StageKind::kMerge,
          {{{ArtifactType::kPartition, {"partition-artifact/v1"},
             IdSpace::kLocal},
            {ArtifactType::kLocalGraph, {"local-graph-artifact/v1"},
             IdSpace::kLocal}},
           {"partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1",
            "graph.local_adjacency/v1"},
           {"cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kGlobalGraph,
           "global-graph-artifact/v1",
           IdSpace::kGlobal,
           {"graph.global_adjacency/v1", "graph.medoids/v1"}},
          "contracts/baseline-v1/schemas/"
          "cpp-main-m0-parameters-v1.1.0.schema.json",
          false};
}

CompatibilityResult b0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext& context) {
  if (!dynamic_cast<const CppMainVectorAccessor*>(context.vector_accessor)) {
    return {false, FailureInfo{ErrorCategory::kCapabilityUnsatisfied,
        "B0 requires the CppMainVectorAccessor runtime service", std::nullopt}};
  }
  return check_native_partition_maps(known_inputs);
}

ImplementationDescriptor p0_descriptor_v1_1() {
  auto descriptor = p0_descriptor();
  descriptor.implementation_version = "1.1.0";
  descriptor.parameter_schema_path =
      "contracts/baseline-v1/schemas/cpp-main-p0-parameters-v1.1.0.schema.json";
  return descriptor;
}

ImplementationDescriptor b0_descriptor_v1_1() {
  auto descriptor = b0_descriptor();
  descriptor.implementation_version = "1.1.0";
  descriptor.parameter_schema_path =
      "contracts/baseline-v1/schemas/cpp-main-b0-parameters-v1.1.0.schema.json";
  return descriptor;
}

CompatibilityResult m0_input_admission(
    const std::vector<ValidatedArtifact>& known_inputs, const StageContext&) {
  return check_native_merge_inputs(known_inputs);
}

void register_cpp_main_stage_bindings(ImplementationRegistry& registry) {
  registry.register_partition(p0_descriptor(), make_p0_partition);
  registry.register_local_build(b0_descriptor(), make_b0_local_build, b0_input_admission);
  registry.register_merge(m0_descriptor(), make_m0_merge, m0_input_admission);
}

}  // namespace diskann_pipeline::cpp_main
