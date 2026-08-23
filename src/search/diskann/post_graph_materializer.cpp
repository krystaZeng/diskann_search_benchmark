#include "search/diskann/post_graph_materializer.hpp"

#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/quarantine.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "search/diskann/fixed_descriptors.hpp"
#include "search/diskann/native_search_formats.hpp"

#if DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE
#include <immintrin.h>
#include "disk_utils.h"
#include "pq.h"
#endif

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace diskann_pipeline::search {
namespace {

using detail::Json;

constexpr const char* kPinnedRepository =
    "https://github.com/microsoft/DiskANN.git";
constexpr const char* kPinnedBranch = "cpp_main";
constexpr const char* kPinnedCommit =
    "78256bbab4685e1774e78d331e081a153be26823";

Json artifact_json(const ArtifactRef& value) {
  return {{"artifact_type", to_string(value.artifact_type)},
          {"format_version", value.format_version},
          {"aggregate_checksum", value.aggregate_checksum}};
}

Json dataset_json(const DatasetIdentity& value) {
  return {{"dataset_id", value.dataset_id},
          {"dataset_checksum", value.dataset_checksum},
          {"global_node_count", value.global_node_count},
          {"vector_dimension", value.vector_dimension},
          {"scalar_type", value.scalar_type},
          {"distance_metric", value.distance_metric}};
}

const PayloadRef& require_payload(const ValidatedArtifact& artifact,
                                  const std::string& role) {
  const auto found = std::find_if(
      artifact.payloads().begin(), artifact.payloads().end(),
      [&](const PayloadRef& payload) { return payload.role == role; });
  if (found == artifact.payloads().end()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "materialization input payload is missing: " + role);
  }
  return *found;
}

std::filesystem::path payload_path(const WorkspaceRef& workspace,
                                   const ValidatedArtifact& artifact,
                                   const PayloadRef& payload) {
  const auto metadata =
      resolve_normal_artifact_metadata(workspace,
                                       artifact.reference().metadata_path);
  return resolve_confined_path(metadata.parent_path(), payload.path);
}

template <typename T>
T read_scalar(std::ifstream& input, const char* description) {
  T value{};
  input.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (!input) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("truncated GlobalGraph: ") + description);
  }
  return value;
}

void require_positive_degree_graph(const std::filesystem::path& graph,
                                   NodeCount node_count,
                                   std::uint32_t degree_bound) {
  std::ifstream input(graph, std::ios::binary);
  const auto claimed_size = read_scalar<std::uint64_t>(input, "file size");
  const auto width = read_scalar<std::uint32_t>(input, "width");
  const auto entry = read_scalar<std::uint32_t>(input, "entry");
  const auto frozen = read_scalar<std::uint64_t>(input, "frozen count");
  if (claimed_size != std::filesystem::file_size(graph) ||
      width != degree_bound || !is_valid_global_node_id(entry, node_count) ||
      frozen != 0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "GlobalGraph is incompatible with pinned post-graph layout");
  }
  for (NodeCount node = 0; node < node_count; ++node) {
    const auto degree = read_scalar<std::uint32_t>(input, "degree");
    if (degree == 0 || degree > degree_bound) {
      throw PipelineError(ErrorCategory::kDegreeExceeded,
                          "post-graph layout requires positive bounded degree");
    }
    for (std::uint32_t edge = 0; edge < degree; ++edge) {
      static_cast<void>(read_scalar<GlobalNodeId>(input, "neighbor"));
    }
  }
}

PayloadRef payload_ref(const std::string& role, const std::string& path,
                       const std::string& media_type,
                       const std::filesystem::path& absolute) {
  return {role, path, media_type, std::filesystem::file_size(absolute),
          sha256_file(absolute)};
}

Json payload_json(const PayloadRef& value) {
  return {{"role", value.role},
          {"path", value.path.generic_string()},
          {"media_type", value.media_type},
          {"byte_length", value.byte_length},
          {"sha256", value.sha256}};
}

std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection;
  projection.artifact_type = artifact_type_from_string(
      metadata.at("artifact_type").get<std::string>());
  projection.format_version = metadata.at("format_version").get<std::string>();
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space =
      id_space_from_string(metadata.at("id_space").get<std::string>());
  projection.capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  for (const auto& payload : metadata.at("payloads")) {
    projection.payloads.push_back(
        {payload.at("role").get<std::string>(),
         payload.at("byte_length").get<std::uint64_t>(),
         payload.at("sha256").get<std::string>()});
  }
  for (const auto& source : metadata.at("source_artifacts")) {
    projection.source_artifacts.push_back(
        {artifact_type_from_string(
             source.at("artifact_type").get<std::string>()),
         source.at("format_version").get<std::string>(),
         source.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return aggregate_checksum(projection);
}

void require_request(const MaterializationRequest& request, bool layout) {
  if (request.run_id.empty() || request.started_at.empty() ||
      request.ended_at.empty() || request.resource_budget.memory_budget_bytes == 0 ||
      request.resource_budget.thread_budget == 0 ||
      request.source.repository != kPinnedRepository ||
      request.source.branch != kPinnedBranch ||
      request.source.commit != kPinnedCommit || request.source.dirty_worktree ||
      request.dataset.artifact_type != ArtifactType::kDataset ||
      (layout && (!request.global_graph ||
                  request.global_graph->artifact_type !=
                      ArtifactType::kGlobalGraph)) ||
      (!layout && request.global_graph)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "materialization request is incomplete or not admitted");
  }
}

BuildManifest base_manifest(const MaterializationRequest& request,
                            const SearchAssetDescriptor& descriptor,
                            const std::string& parameters) {
  BuildManifest manifest;
  manifest.run_id = request.run_id;
  manifest.stage = "build";
  manifest.implementation = {descriptor.implementation_id,
                             descriptor.implementation_version};
  manifest.source = request.source;
  manifest.toolchain = request.toolchain;
  manifest.resource_budget = request.resource_budget;
  manifest.input_artifacts = {request.dataset};
  if (request.global_graph) {
    manifest.input_artifacts.push_back(*request.global_graph);
  }
  manifest.parameters_json = parameters;
  manifest.started_at = request.started_at;
  manifest.ended_at = request.ended_at;
  manifest.metrics_json = "{}";
  return manifest;
}

std::filesystem::path publish_candidate(
    const WorkspaceRef& workspace, const TemporaryStorageRef& temporary,
    const std::string& run_id, const SearchAssetDescriptor& descriptor,
    const ArtifactRef& candidate) {
  const auto destination_relative =
      std::filesystem::path(run_id) / descriptor.implementation_id;
  const auto destination =
      resolve_confined_path(workspace.normal_root, destination_relative);
  if (std::filesystem::exists(destination)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "materialization destination already exists");
  }
  std::filesystem::create_directories(destination.parent_path());
  std::error_code error;
  std::filesystem::rename(temporary.stage_root / "candidate", destination,
                          error);
  if (error) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to publish search Artifact: " + error.message());
  }
  return destination_relative / candidate.metadata_path.filename();
}

MaterializationResult retained_failure(const WorkspaceRef& workspace,
                                       BuildManifest manifest,
                                       StageStatus status,
                                       FailureInfo failure) {
  manifest.status = status;
  manifest.failure = failure;
  const auto path = persist_terminal_build_manifest(workspace, manifest);
  return {status, std::nullopt, path, {}, std::nullopt, std::move(failure)};
}

}  // namespace

NativePostGraphCalls fixed_native_post_graph_calls() {
  NativePostGraphCalls result;
#if DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE
  result.generate_pq = [](const auto& dataset, const auto& pivots,
                          const auto& compressed, const auto& policy) {
    diskann::generate_quantized_data<float>(
        dataset.string(), pivots.string(), compressed.string(),
        diskann::Metric::L2, policy.sample_rate, policy.pq_chunks, policy.opq,
        "");
  };
  result.create_disk_layout = [](const auto& dataset, const auto& graph,
                                 const auto& output, const auto&) {
    diskann::create_disk_layout<float>(dataset.string(), graph.string(),
                                       output.string(), "");
  };
#else
  result.generate_pq = [](const auto&, const auto&, const auto&, const auto&) {
    throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                        "native PQ generation is Linux-only pending");
  };
  result.create_disk_layout =
      [](const auto&, const auto&, const auto&, const auto&) {
        throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                            "native disk layout generation is Linux-only pending");
      };
#endif
  return result;
}

PostGraphMaterializer::PostGraphMaterializer(
    WorkspaceRef workspace, NativePostGraphCalls calls,
    const CancellationToken* cancellation)
    : workspace_(std::move(workspace)),
      calls_(std::move(calls)),
      cancellation_(cancellation) {}

MaterializationResult PostGraphMaterializer::materialize_pq(
    const MaterializationRequest& request) const {
  const auto& descriptor = fixed_pq_descriptor();
  const PqGenerationPolicy policy;
  auto manifest = base_manifest(
      request, descriptor,
      Json{{"B", policy.memory_budget_gib},
           {"QD", policy.qd},
           {"pq_chunks", policy.pq_chunks},
           {"sample_rate", policy.sample_rate},
           {"centroid_count", policy.centroid_count},
           {"opq", policy.opq},
           {"distance_metric", policy.distance_metric}}
          .dump());
  try {
    require_request(request, false);
    if (cancellation_ != nullptr && cancellation_->is_cancelled()) {
      return retained_failure(
          workspace_, std::move(manifest), StageStatus::kCancelled,
          {ErrorCategory::kCancelled, "PQ materialization cancelled", std::nullopt});
    }
    const auto dataset_validation =
        Validator(workspace_).validate_with_lineage(request.dataset);
    if (!dataset_validation.valid()) {
      throw PipelineError(
          dataset_validation.evidence.failure_category.value_or(
              ErrorCategory::kSchemaInvalid),
          "Dataset failed fresh materialization admission");
    }
    const auto& dataset = *dataset_validation.validated_artifact;
    if (dataset.reference().format_version != "diskann-float32-dataset/v1" ||
        dataset.dataset().global_node_count > 1000000 ||
        dataset.dataset().vector_dimension < policy.pq_chunks ||
        dataset.dataset().distance_metric != "l2") {
      throw PipelineError(ErrorCategory::kCapabilityUnsatisfied,
                          "Dataset is outside fixed PQ MVP admission");
    }
    manifest.dataset = dataset.dataset();
    const auto& vectors = require_payload(dataset, "vectors");
    if (vectors.media_type != cpp_main::kFloat32DatasetMediaType) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "PQ input is not native float32 Dataset payload");
    }
    const auto temporary =
        create_temporary_storage(workspace_, request.run_id + "-pq");
    const auto candidate_root = temporary.stage_root / "candidate";
    std::filesystem::create_directories(candidate_root);
    const auto pivots = candidate_root / "pivots.bin";
    const auto compressed = candidate_root / "compressed.bin";
    calls_.generate_pq(payload_path(workspace_, dataset, vectors), pivots,
                       compressed, policy);
    const auto pivots_ref = payload_ref(
        "pq_pivots", "pivots.bin", kPqPivotsMediaType, pivots);
    const auto compressed_ref = payload_ref(
        "pq_compressed_vectors", "compressed.bin", kPqCodesMediaType,
        compressed);
    Json metadata{{"schema_version", "1.0.0"},
                  {"canonicalization", "diskann-artifact-c14n-v1"},
                  {"artifact_type", "pq"},
                  {"format_version", "pq-artifact/v1"},
                  {"dataset", dataset_json(dataset.dataset())},
                  {"id_space", "none"},
                  {"structural",
                   {{"point_count", dataset.dataset().global_node_count},
                    {"dimension", dataset.dataset().vector_dimension},
                    {"scalar_type", "float32"},
                    {"distance_metric", "l2"},
                    {"centroid_count", 256},
                    {"pq_chunks", policy.pq_chunks},
                    {"opq", false},
                    {"compressed_code_scalar", "uint8"}}},
                  {"capabilities",
                   Json::array({"search.pq_in_memory/v1",
                                "search.pqflash_loadable/v1"})},
                  {"source_artifacts", Json::array({artifact_json(request.dataset)})},
                  {"payloads",
                   Json::array(
                       {payload_json(pivots_ref), payload_json(compressed_ref)})}};
    metadata["aggregate_checksum"] = aggregate_for(metadata);
    write_immutable_text(candidate_root / "artifact.json", metadata.dump());
    ArtifactRef candidate{"candidate/artifact.json", ArtifactType::kPQ,
                          "pq-artifact/v1",
                          metadata.at("aggregate_checksum").get<std::string>()};
    auto candidate_workspace = workspace_;
    candidate_workspace.normal_root = temporary.stage_root;
    const auto validation = Validator(candidate_workspace).validate(
        candidate, {dataset});
    const auto evidence_path = persist_validation_evidence(
        workspace_, request.run_id + "-pq", validation.evidence);
    if (!validation.valid()) {
      manifest.status = StageStatus::kInvalidOutput;
      manifest.failure = {validation.evidence.failure_category.value_or(
                              ErrorCategory::kSchemaInvalid),
                          "PQ candidate failed validation", std::nullopt};
      manifest.validation_evidence_path =
          std::filesystem::relative(evidence_path, workspace_.root);
      const auto manifest_path =
          persist_terminal_build_manifest(workspace_, manifest);
      const auto quarantine_id = request.run_id + "-pq-invalid";
      QuarantineManager(workspace_).publish(
          quarantine_id, candidate_root, candidate_root / "artifact.json",
          manifest_path, evidence_path, *manifest.failure, request.ended_at);
      return {StageStatus::kInvalidOutput, std::nullopt, manifest_path,
              evidence_path, quarantine_id, manifest.failure};
    }
    const auto published_relative = publish_candidate(
        workspace_, temporary, request.run_id, descriptor, candidate);
    ArtifactRef published{published_relative, ArtifactType::kPQ,
                          candidate.format_version,
                          candidate.aggregate_checksum};
    const auto final_validation =
        Validator(workspace_).validate(published, {dataset});
    if (!final_validation.valid()) {
      throw PipelineError(ErrorCategory::kInternal,
                          "published PQ failed fresh validation");
    }
    const auto final_evidence = persist_validation_evidence(
        workspace_, request.run_id + "-pq-published", final_validation.evidence);
    manifest.status = StageStatus::kSucceeded;
    manifest.output_artifacts = {published};
    manifest.validation_evidence_path =
        std::filesystem::relative(final_evidence, workspace_.root);
    const auto manifest_path =
        persist_terminal_build_manifest(workspace_, manifest);
    return {StageStatus::kSucceeded, published, manifest_path, final_evidence,
            std::nullopt, std::nullopt};
  } catch (const PipelineError& error) {
    return retained_failure(workspace_, std::move(manifest),
                            error.category() == ErrorCategory::kCancelled
                                ? StageStatus::kCancelled
                                : StageStatus::kFailed,
                            {error.category(), error.what(), std::nullopt});
  } catch (const std::exception& error) {
    return retained_failure(
        workspace_, std::move(manifest), StageStatus::kFailed,
        {ErrorCategory::kInternal, error.what(), std::nullopt});
  }
}

MaterializationResult PostGraphMaterializer::materialize_disk_layout(
    const MaterializationRequest& request) const {
  const auto& descriptor = fixed_disk_layout_descriptor();
  DiskLayoutPolicy policy;
  policy.centroid_policy = request.centroid_policy;
  auto manifest = base_manifest(
      request, descriptor,
      Json{{"scalar_type", policy.scalar_type},
           {"pq_disk_bytes", policy.pq_disk_bytes},
           {"reorder", policy.reorder},
           {"labels", policy.labels},
           {"entry_policy", policy.entry_policy},
           {"centroid_policy", policy.centroid_policy}}
          .dump());
  try {
    require_request(request, true);
    if (policy.centroid_policy != "medoid_vector_fallback") {
      throw PipelineError(
          ErrorCategory::kCapabilityUnsatisfied,
          "current GlobalGraph/v1 supports only medoid-vector fallback");
    }
    if (cancellation_ != nullptr && cancellation_->is_cancelled()) {
      return retained_failure(
          workspace_, std::move(manifest), StageStatus::kCancelled,
          {ErrorCategory::kCancelled, "DiskLayout materialization cancelled",
           std::nullopt});
    }
    const auto dataset_validation =
        Validator(workspace_).validate_with_lineage(request.dataset);
    const auto global_validation =
        Validator(workspace_).validate_with_lineage(*request.global_graph);
    if (!dataset_validation.valid() || !global_validation.valid()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "materialization input failed fresh lineage validation");
    }
    const auto& dataset = *dataset_validation.validated_artifact;
    const auto& global = *global_validation.validated_artifact;
    if (dataset.reference().format_version != "diskann-float32-dataset/v1" ||
        dataset.dataset().global_node_count > 1000000 ||
        dataset.dataset().dataset_checksum !=
            global.dataset().dataset_checksum) {
      throw PipelineError(ErrorCategory::kSourceMismatch,
                          "Dataset and GlobalGraph are not MVP-compatible");
    }
    manifest.dataset = dataset.dataset();
    const auto& vectors = require_payload(dataset, "vectors");
    const auto& graph = require_payload(global, "global_graph");
    const auto& medoids = require_payload(global, kGlobalGraphMedoidsPayloadRole);
    if (vectors.media_type != cpp_main::kFloat32DatasetMediaType ||
        graph.media_type != cpp_main::kGlobalGraphMediaType ||
        medoids.media_type != cpp_main::kMedoidsMediaType) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "post-graph inputs are not pinned native formats");
    }
    const auto global_structural =
        detail::parse_json_strict(global.structural_json());
    const auto declared_degree =
        global_structural.at("declared_max_degree").get<std::uint32_t>();
    require_positive_degree_graph(payload_path(workspace_, global, graph),
                                  dataset.dataset().global_node_count,
                                  declared_degree);
    const auto temporary =
        create_temporary_storage(workspace_, request.run_id + "-layout");
    const auto candidate_root = temporary.stage_root / "candidate";
    std::filesystem::create_directories(candidate_root);
    const auto disk_index = candidate_root / "disk.index";
    calls_.create_disk_layout(payload_path(workspace_, dataset, vectors),
                              payload_path(workspace_, global, graph),
                              disk_index, policy);
    const auto medoids_output = candidate_root / "medoids.bin";
    std::filesystem::copy_file(payload_path(workspace_, global, medoids),
                               medoids_output);
    const auto disk_ref = payload_ref("disk_index", "disk.index",
                                      kDiskIndexMediaType, disk_index);
    const auto medoids_ref = payload_ref("medoids", "medoids.bin",
                                         kUint32MatrixMediaType,
                                         medoids_output);
    std::ifstream medoid_input(medoids_output, std::ios::binary);
    std::uint32_t medoid_count = 0;
    medoid_input.read(reinterpret_cast<char*>(&medoid_count),
                      sizeof(medoid_count));
    Json metadata{{"schema_version", "1.0.0"},
                  {"canonicalization", "diskann-artifact-c14n-v1"},
                  {"artifact_type", "disk_layout"},
                  {"format_version", "disk-layout-artifact/v1"},
                  {"dataset", dataset_json(dataset.dataset())},
                  {"id_space", "global"},
                  {"structural",
                   {{"global_node_count", dataset.dataset().global_node_count},
                    {"dimension", dataset.dataset().vector_dimension},
                    {"scalar_type", "float32"},
                    {"distance_metric", "l2"},
                    {"disk_vector_encoding", "float32_full_precision"},
                    {"pq_disk_bytes", 0},
                    {"reorder", false},
                    {"declared_max_degree", declared_degree},
                    {"medoid_count", medoid_count},
                    {"entry_policy", "end_to_end"},
                    {"centroid_policy", "medoid_vector_fallback"},
                    {"frozen_point_count", 0}}},
                  {"capabilities",
                   Json::array({"graph.global_ids/v1",
                                "search.disk_layout/v1",
                                "search.disk_vectors_full_precision/v1",
                                "search.entry_end_to_end/v1",
                                "search.pqflash_loadable/v1"})},
                  {"source_artifacts",
                   Json::array({artifact_json(request.dataset),
                                artifact_json(*request.global_graph)})},
                  {"payloads",
                   Json::array(
                       {payload_json(disk_ref), payload_json(medoids_ref)})}};
    metadata["aggregate_checksum"] = aggregate_for(metadata);
    write_immutable_text(candidate_root / "artifact.json", metadata.dump());
    ArtifactRef candidate{"candidate/artifact.json",
                          ArtifactType::kDiskLayout,
                          "disk-layout-artifact/v1",
                          metadata.at("aggregate_checksum").get<std::string>()};
    auto candidate_workspace = workspace_;
    candidate_workspace.normal_root = temporary.stage_root;
    const auto validation = Validator(candidate_workspace).validate(
        candidate, {dataset, global});
    const auto evidence_path = persist_validation_evidence(
        workspace_, request.run_id + "-layout", validation.evidence);
    if (!validation.valid()) {
      manifest.status = StageStatus::kInvalidOutput;
      manifest.failure = {validation.evidence.failure_category.value_or(
                              ErrorCategory::kSchemaInvalid),
                          "DiskLayout candidate failed validation", std::nullopt};
      manifest.validation_evidence_path =
          std::filesystem::relative(evidence_path, workspace_.root);
      const auto manifest_path =
          persist_terminal_build_manifest(workspace_, manifest);
      const auto quarantine_id = request.run_id + "-layout-invalid";
      QuarantineManager(workspace_).publish(
          quarantine_id, candidate_root, candidate_root / "artifact.json",
          manifest_path, evidence_path, *manifest.failure, request.ended_at);
      return {StageStatus::kInvalidOutput, std::nullopt, manifest_path,
              evidence_path, quarantine_id, manifest.failure};
    }
    const auto published_relative = publish_candidate(
        workspace_, temporary, request.run_id, descriptor, candidate);
    ArtifactRef published{published_relative, ArtifactType::kDiskLayout,
                          candidate.format_version,
                          candidate.aggregate_checksum};
    const auto final_validation =
        Validator(workspace_).validate(published, {dataset, global});
    if (!final_validation.valid()) {
      throw PipelineError(ErrorCategory::kInternal,
                          "published DiskLayout failed fresh validation");
    }
    const auto final_evidence = persist_validation_evidence(
        workspace_, request.run_id + "-layout-published",
        final_validation.evidence);
    manifest.status = StageStatus::kSucceeded;
    manifest.output_artifacts = {published};
    manifest.validation_evidence_path =
        std::filesystem::relative(final_evidence, workspace_.root);
    const auto manifest_path =
        persist_terminal_build_manifest(workspace_, manifest);
    return {StageStatus::kSucceeded, published, manifest_path, final_evidence,
            std::nullopt, std::nullopt};
  } catch (const PipelineError& error) {
    return retained_failure(workspace_, std::move(manifest),
                            error.category() == ErrorCategory::kCancelled
                                ? StageStatus::kCancelled
                                : StageStatus::kFailed,
                            {error.category(), error.what(), std::nullopt});
  } catch (const std::exception& error) {
    return retained_failure(
        workspace_, std::move(manifest), StageStatus::kFailed,
        {ErrorCategory::kInternal, error.what(), std::nullopt});
  }
}

}  // namespace diskann_pipeline::search
