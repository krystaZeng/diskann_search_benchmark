#include "papers/xn_graph/xn_descent/stage_binding.hpp"

#include "papers/xn_graph/xn_descent/parameters.hpp"
#include "papers/xn_graph/xn_descent/xn_descent_algorithm.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/provenance.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace diskann_pipeline::xn_descent {
namespace {

using Json = nlohmann::json;

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch()) %
                      std::chrono::seconds(1);
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(6)
         << std::setfill('0') << micros.count() << 'Z';
  return output.str();
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "XN-Descent parameter or metadata file is missing");
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Json read_json(const std::filesystem::path& path) {
  return detail::parse_json_strict(read_text(path));
}

void write_bytes(const std::filesystem::path& path,
                 const std::vector<std::uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent cannot write native payload");
  }
}

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent cannot write candidate metadata");
  }
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

std::uint64_t checked_add(std::uint64_t lhs, std::uint64_t rhs) {
  if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent memory estimate overflow");
  }
  return lhs + rhs;
}

std::uint64_t checked_multiply(std::uint64_t lhs, std::uint64_t rhs) {
  if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent memory estimate overflow");
  }
  return lhs * rhs;
}

std::uint64_t mandatory_memory(NodeCount count, std::uint32_t dimension,
                               const XnDescentParameters& parameters) {
  const auto available = count - 1;
  const auto c_capacity =
      std::min<std::uint64_t>(parameters.omega, available);
  const auto g_capacity =
      std::min<std::uint64_t>(parameters.lambda, available);
  const auto vector_bytes = checked_multiply(
      checked_multiply(count, dimension), sizeof(float));
  const auto c_records = checked_multiply(count, c_capacity);
  const auto g_records = checked_multiply(count, g_capacity);
  auto total = vector_bytes;
  total = checked_add(total, checked_multiply(count, sizeof(GlobalNodeId)));
  total = checked_add(total,
                      checked_multiply(checked_multiply(c_records, 2),
                                       sizeof(NeighborRecord)));
  total = checked_add(total,
                      checked_multiply(checked_multiply(g_records, 2),
                                       sizeof(NeighborRecord)));
  total = checked_add(total,
                      checked_multiply(count, sizeof(ReverseList) * 2ULL));
  total = checked_add(total,
                      checked_multiply(dimension, sizeof(long double)));
  const auto native_records = checked_multiply(
      count, checked_add(sizeof(std::uint32_t),
                         checked_multiply(g_capacity,
                                          sizeof(std::uint32_t))));
  return checked_add(total, checked_add(24, native_records));
}

Json dataset_json(const DatasetIdentity& dataset) {
  return {{"dataset_id", dataset.dataset_id},
          {"dataset_checksum", dataset.dataset_checksum},
          {"global_node_count", dataset.global_node_count},
          {"vector_dimension", dataset.vector_dimension},
          {"scalar_type", dataset.scalar_type},
          {"distance_metric", dataset.distance_metric}};
}

Json source_json(const ArtifactRef& reference) {
  return {{"artifact_type", to_string(reference.artifact_type)},
          {"format_version", reference.format_version},
          {"aggregate_checksum", reference.aggregate_checksum}};
}

Json payload_json(const std::string& role, const std::string& relative,
                  const std::string& media_type,
                  const std::filesystem::path& path) {
  return {{"role", role},
          {"path", relative},
          {"media_type", media_type},
          {"byte_length", std::filesystem::file_size(path)},
          {"sha256", sha256_file(path)}};
}

std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection;
  projection.artifact_type = ArtifactType::kLocalGraph;
  projection.format_version = "local-graph-artifact/v1";
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space = IdSpace::kLocal;
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
        {artifact_type_from_string(source.at("artifact_type").get<std::string>()),
         source.at("format_version").get<std::string>(),
         source.at("aggregate_checksum").get<std::string>()});
  }
  projection.structural_json = metadata.at("structural").dump();
  return aggregate_checksum(projection);
}

std::string run_id_from(const std::filesystem::path& parameter_path,
                        const TemporaryStorageRef& temporary) {
  if (parameter_path.parent_path().filename() == "parameters" &&
      !parameter_path.parent_path().parent_path().filename().empty()) {
    return parameter_path.parent_path().parent_path().filename().string();
  }
  return temporary.stage_root.filename().string();
}

SourceIdentity project_source() {
  SourceIdentity result{XN_DESCENT_PROJECT_REPOSITORY,
                        XN_DESCENT_PROJECT_BRANCH,
                        XN_DESCENT_PROJECT_COMMIT,
                        XN_DESCENT_PROJECT_DIRTY != 0,
                        std::nullopt};
  if (result.dirty_worktree) {
    result.dirty_source_checksum = XN_DESCENT_PROJECT_DIRTY_CHECKSUM;
  }
  return result;
}

ToolchainIdentity actual_toolchain() {
  return {XN_DESCENT_SYSTEM_NAME,
          XN_DESCENT_CXX_COMPILER,
          XN_DESCENT_CXX_COMPILER_VERSION,
          XN_DESCENT_CMAKE_VERSION,
          XN_DESCENT_CMAKE_GENERATOR,
          XN_DESCENT_BUILD_TYPE,
          {std::string("DISKANN_CPP_MAIN_BASELINE=") +
               (XN_DESCENT_CPP_MAIN_BASELINE_ENABLED ? "ON" : "OFF"),
           "DISKANN_XN_DESCENT=ON"},
          {std::string("cmake --build ") + XN_DESCENT_BINARY_DIR +
           " --target diskann_xn_descent"},
          {"diskann_xn_descent -> diskann_pipeline_core"}};
}

BuildManifest base_manifest(const std::string& run_id,
                            const DatasetIdentity& dataset,
                            const ArtifactRef& dataset_reference,
                            const ArtifactRef& partition,
                            const StageContext& context,
                            const std::string& started_at) {
  BuildManifest manifest;
  manifest.run_id = run_id;
  manifest.stage = "local_build";
  manifest.implementation = {kImplementationId, kImplementationVersion};
  manifest.source = project_source();
  manifest.toolchain = actual_toolchain();
  manifest.dataset = dataset;
  manifest.resource_budget = {context.memory_budget_bytes,
                              context.thread_budget};
  manifest.input_artifacts = {dataset_reference, partition};
  manifest.status = StageStatus::kRunning;
  manifest.started_at = started_at;
  manifest.metrics_json = "{}";
  return manifest;
}

std::filesystem::path persist_manifest(const WorkspaceRef& workspace,
                                       PartitionId partition_id,
                                       BuildManifest manifest) {
  manifest.ended_at = now_utc();
  const auto path = resolve_confined_path(
      workspace.report_root,
      std::filesystem::path("xn-descent") / manifest.run_id /
          ("local-" + std::to_string(partition_id) + "-execution.json"));
  write_immutable_text(path, serialize_build_manifest(manifest));
  return path;
}

LocalNodeId local_medoid(const std::vector<std::vector<float>>& vectors,
                         const DistanceFunction& distance) {
  if (vectors.empty() || vectors.front().empty()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent medoid requires a non-empty Partition");
  }
  std::vector<long double> sum(vectors.front().size(), 0.0L);
  for (const auto& vector : vectors) {
    if (vector.size() != sum.size()) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent vector dimension mismatch");
    }
    for (std::size_t column = 0; column < vector.size(); ++column) {
      if (!std::isfinite(vector[column])) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "XN-Descent vector is not finite");
      }
      sum[column] += vector[column];
    }
  }
  std::vector<float> centroid(sum.size());
  for (std::size_t column = 0; column < sum.size(); ++column) {
    centroid[column] =
        static_cast<float>(sum[column] / static_cast<long double>(vectors.size()));
    if (!std::isfinite(centroid[column])) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent centroid is not finite");
    }
  }
  LocalNodeId best = 0;
  auto best_distance = distance.evaluate(vectors[0], centroid);
  if (!std::isfinite(best_distance) || best_distance < 0.0) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "XN-Descent medoid distance is invalid");
  }
  for (LocalNodeId node = 1; node < vectors.size(); ++node) {
    const auto candidate = distance.evaluate(vectors[node], centroid);
    if (!std::isfinite(candidate) || candidate < 0.0) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "XN-Descent medoid distance is invalid");
    }
    if (candidate < best_distance) {
      best = node;
      best_distance = candidate;
    }
  }
  return best;
}

std::vector<std::uint8_t> native_graph_bytes(
    const std::vector<NeighborList>& graph, LocalNodeId entry,
    std::uint32_t* observed_degree) {
  std::uint32_t observed = 0;
  std::uint64_t record_size = 0;
  for (LocalNodeId owner = 0; owner < graph.size(); ++owner) {
    std::set<LocalNodeId> unique;
    record_size = checked_add(record_size, sizeof(std::uint32_t));
    record_size = checked_add(
        record_size,
        checked_multiply(graph[owner].size(), sizeof(std::uint32_t)));
    observed = std::max(observed,
                        static_cast<std::uint32_t>(graph[owner].size()));
    for (const auto& neighbor : graph[owner]) {
      if (!is_valid_local_node_id(neighbor.id, graph.size()) ||
          neighbor.id == owner || !unique.insert(neighbor.id).second) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "XN-Descent final graph violates local-ID invariants");
      }
    }
  }
  const auto file_size = checked_add(24, record_size);
  if (file_size > std::numeric_limits<std::size_t>::max()) {
    throw PipelineError(ErrorCategory::kInternal,
                        "XN-Descent native graph exceeds addressable size");
  }
  std::vector<std::uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(file_size));
  append_u64(bytes, file_size);
  append_u32(bytes, observed);
  append_u32(bytes, entry);
  append_u64(bytes, 0);
  for (const auto& row : graph) {
    append_u32(bytes, static_cast<std::uint32_t>(row.size()));
    for (const auto& neighbor : row) append_u32(bytes, neighbor.id);
  }
  *observed_degree = observed;
  return bytes;
}

Json execution_parameters(const XnDescentParameters& parameters,
                          PartitionId partition_id,
                          const AlgorithmResult& result) {
  auto json = Json::parse(canonical_parameters_json(parameters));
  json["partition_id"] = partition_id;
  json["effective_worker_count"] = 1;
  json["completed_rounds"] = result.completed_rounds;
  json["termination"] =
      result.termination == TerminationKind::kConverged
          ? "converged"
          : result.failure_reason;
  json["convergence"] =
      {{"round_index", result.convergence.round_index},
       {"changed_c_nodes", result.convergence.changed_c_nodes},
       {"changed_r_nodes", result.convergence.changed_r_nodes},
       {"changed_g_nodes", result.convergence.changed_g_nodes},
       {"changed_c_edges", result.convergence.changed_c_edges},
       {"changed_r_edges", result.convergence.changed_r_edges},
       {"changed_g_edges", result.convergence.changed_g_edges},
       {"direct_equality_checked",
        result.convergence.direct_equality_checked},
       {"fixed_point", result.convergence.fixed_point},
       {"stable_c_g_rounds", result.convergence.stable_c_g_rounds},
       {"convergence_condition",
        to_string(result.convergence.convergence_condition)}};
  json["diskann_native_layout_source"] =
      {{"repository", "https://github.com/microsoft/DiskANN.git"},
       {"branch", "cpp_main"},
       {"commit", "78256bbab4685e1774e78d331e081a153be26823"}};
  return json;
}

class XnDescentLocalBuild final : public LocalGraphBuilder {
 public:
  StageResult build(const ArtifactRef& partition_artifact,
                    PartitionId partition_id,
                    const LocalBuildConfig& config,
                    StageContext& context) override {
    const auto started_at = now_utc();
    std::optional<BuildManifest> manifest;
    XnDescentParameters parameters;
    AlgorithmResult algorithm;
    const auto run_id = run_id_from(config.stage_manifest_path,
                                    context.temporary_storage);
    try {
      validate_stage_context(context);
      const auto* accessor =
          dynamic_cast<const cpp_main::CppMainVectorAccessor*>(
              context.vector_accessor);
      if (accessor == nullptr) {
        throw PipelineError(
            ErrorCategory::kCapabilityUnsatisfied,
            "XN-Descent requires the validated native Dataset VectorAccessor");
      }
      manifest = base_manifest(run_id, accessor->dataset_identity(),
                               accessor->dataset_reference(),
                               partition_artifact, context, started_at);
      if (context.cancellation_token->is_cancelled()) {
        manifest->status = StageStatus::kCancelled;
        manifest->parameters_json = "{}";
        return {StageStatus::kCancelled, std::nullopt,
                persist_manifest(context.workspace, partition_id,
                                 std::move(*manifest))};
      }

      const auto partition_validation = Validator(context.workspace).validate(
          partition_artifact, {accessor->validated_dataset()});
      if (!partition_validation.valid()) {
        throw PipelineError(ErrorCategory::kSourceMismatch,
                            "XN-Descent PartitionArtifact failed validation");
      }
      const auto& validated_partition =
          *partition_validation.validated_artifact;
      require_partition_vector_source(validated_partition, partition_id,
                                      context.vector_accessor);

      const auto parameter_json = read_json(config.stage_manifest_path);
      parameters = parse_parameters(parameter_json.dump());
      if (parameter_json.contains("partition_id") &&
          parameter_json.at("partition_id").get<PartitionId>() !=
              partition_id) {
        throw PipelineError(
            ErrorCategory::kSourceMismatch,
            "XN-Descent parameter PartitionId differs from interface input");
      }

      const auto metadata_path = resolve_normal_artifact_metadata(
          context.workspace, partition_artifact.metadata_path);
      const auto metadata = read_json(metadata_path);
      const auto& records = metadata.at("structural").at("partitions");
      const auto record = std::find_if(
          records.begin(), records.end(), [&](const auto& value) {
            return value.at("partition_id").template get<PartitionId>() ==
                   partition_id;
          });
      if (record == records.end()) {
        throw PipelineError(ErrorCategory::kIdRangeInvalid,
                            "XN-Descent selected Partition does not exist");
      }
      const auto local_count =
          record->at("local_node_count").get<NodeCount>();
      if (!is_valid_count(local_count)) {
        throw PipelineError(ErrorCategory::kNodeCountMismatch,
                            "XN-Descent local node count is invalid");
      }
      const auto map_role =
          record->at("local_to_global_map_role").get<std::string>();
      const auto payload = std::find_if(
          metadata.at("payloads").begin(), metadata.at("payloads").end(),
          [&](const auto& value) { return value.at("role") == map_role; });
      if (payload == metadata.at("payloads").end() ||
          payload->at("media_type") != cpp_main::kPartitionIdMapMediaType) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "XN-Descent selected native map is absent");
      }
      const auto map_path = resolve_confined_path(
          metadata_path.parent_path(), payload->at("path").get<std::string>());
      const auto map = cpp_main::validate_partition_id_map(
          map_path, local_count,
          accessor->dataset_identity().global_node_count);

      const auto required = mandatory_memory(
          local_count, accessor->dataset_identity().vector_dimension,
          parameters);
      if (required > context.memory_budget_bytes) {
        algorithm.termination = TerminationKind::kResourceExhausted;
        algorithm.failure_reason = "memory_budget_preflight_exceeded";
        throw PipelineError(
            ErrorCategory::kInternal,
            "XN-Descent mandatory preallocation exceeds memory budget");
      }
      if (context.thread_budget == 0) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "XN-Descent thread budget must be positive");
      }

      std::vector<std::vector<float>> vectors(local_count);
      for (LocalNodeId local = 0; local < local_count; ++local) {
        context.vector_accessor->read(map[local], vectors[local]);
        if (vectors[local].size() !=
            accessor->dataset_identity().vector_dimension) {
          throw PipelineError(ErrorCategory::kSourceMismatch,
                              "XN-Descent vector dimension is inconsistent");
        }
      }
      context.metrics_sink->event("xn_descent.initialized");
      algorithm = run_algorithm(vectors, parameters,
                                *context.distance_function,
                                context.cancellation_token);
      manifest->parameters_json =
          execution_parameters(parameters, partition_id, algorithm).dump();
      if (algorithm.termination == TerminationKind::kCancelled) {
        manifest->status = StageStatus::kCancelled;
        return {StageStatus::kCancelled, std::nullopt,
                persist_manifest(context.workspace, partition_id,
                                 std::move(*manifest))};
      }
      if (algorithm.termination != TerminationKind::kConverged) {
        throw PipelineError(ErrorCategory::kInternal,
                            algorithm.failure_reason.empty()
                                ? "XN-Descent algorithm did not converge"
                                : algorithm.failure_reason);
      }

      const auto entry = local_medoid(vectors, *context.distance_function);
      const auto candidate =
          context.temporary_storage.stage_root / "candidate";
      std::filesystem::create_directories(candidate);
      const auto candidate_map = candidate / "map.bin";
      const auto candidate_graph = candidate / "graph.index";
      std::filesystem::copy_file(map_path, candidate_map);
      std::uint32_t observed = 0;
      write_bytes(candidate_graph,
                  native_graph_bytes(algorithm.state.g, entry, &observed));
      const auto facts = cpp_main::validate_local_graph(
          candidate_graph, local_count, parameters.lambda);
      if (facts.header_max_degree != observed ||
          facts.observed_max_degree != observed || facts.entry_point != entry) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "XN-Descent native graph self-check failed");
      }

      Json artifact{
          {"schema_version", "1.0.0"},
          {"canonicalization", "diskann-artifact-c14n-v1"},
          {"artifact_type", "local_graph"},
          {"format_version", "local-graph-artifact/v1"},
          {"dataset", dataset_json(accessor->dataset_identity())},
          {"id_space", "local"},
          {"structural",
           {{"partition_id", partition_id},
            {"local_node_count", local_count},
            {"declared_max_degree", parameters.lambda},
            {"observed_max_degree", observed},
            {"source_partition_checksum",
             partition_artifact.aggregate_checksum}}},
          {"capabilities", Json::array({"graph.local_adjacency/v1"})},
          {"source_artifacts", Json::array({source_json(partition_artifact)})},
          {"payloads",
           Json::array(
               {payload_json("local_to_global_map", "map.bin",
                             cpp_main::kPartitionIdMapMediaType, candidate_map),
                payload_json("local_graph", "graph.index",
                             cpp_main::kLocalGraphMediaType,
                             candidate_graph)})}};
      artifact["aggregate_checksum"] = aggregate_for(artifact);
      write_text(candidate / "artifact.json", artifact.dump());
      ArtifactRef reference{
          "candidate/artifact.json", ArtifactType::kLocalGraph,
          "local-graph-artifact/v1",
          artifact.at("aggregate_checksum").get<std::string>()};

      auto candidate_workspace = context.workspace;
      candidate_workspace.normal_root = context.temporary_storage.stage_root;
      const auto candidate_validation = Validator(candidate_workspace).validate(
          reference, {validated_partition});
      if (!candidate_validation.valid()) {
        manifest->status = StageStatus::kInvalidOutput;
        manifest->failure =
            FailureInfo{ErrorCategory::kSchemaInvalid,
                        "XN-Descent candidate failed LocalGraphArtifact/v1 "
                        "validation",
                        std::nullopt};
        return {StageStatus::kInvalidOutput, std::nullopt,
                persist_manifest(context.workspace, partition_id,
                                 std::move(*manifest))};
      }
      manifest->status = StageStatus::kSucceeded;
      manifest->output_artifacts = {reference};
      context.metrics_sink->event("xn_descent.converged");
      return {StageStatus::kSucceeded, reference,
              persist_manifest(context.workspace, partition_id,
                               std::move(*manifest))};
    } catch (const PipelineError& error) {
      if (!manifest) return {StageStatus::kFailed, std::nullopt, {}};
      manifest->status = error.category() == ErrorCategory::kCancelled
                             ? StageStatus::kCancelled
                             : StageStatus::kFailed;
      if (manifest->parameters_json == "{}") {
        try {
          manifest->parameters_json =
              execution_parameters(parameters, partition_id, algorithm).dump();
        } catch (...) {
          manifest->parameters_json = "{}";
        }
      }
      if (manifest->status == StageStatus::kFailed) {
        manifest->failure =
            FailureInfo{error.category(), error.what(), std::nullopt};
      }
      return {manifest->status, std::nullopt,
              persist_manifest(context.workspace, partition_id,
                               std::move(*manifest))};
    } catch (const std::bad_alloc&) {
      if (!manifest) return {StageStatus::kFailed, std::nullopt, {}};
      manifest->status = StageStatus::kFailed;
      manifest->failure =
          FailureInfo{ErrorCategory::kInternal,
                      "XN-Descent allocation failed", std::nullopt};
      return {StageStatus::kFailed, std::nullopt,
              persist_manifest(context.workspace, partition_id,
                               std::move(*manifest))};
    } catch (const std::exception& error) {
      if (!manifest) return {StageStatus::kFailed, std::nullopt, {}};
      manifest->status = StageStatus::kFailed;
      manifest->failure =
          FailureInfo{ErrorCategory::kInternal, error.what(), std::nullopt};
      return {StageStatus::kFailed, std::nullopt,
              persist_manifest(context.workspace, partition_id,
                               std::move(*manifest))};
    }
  }
};

}  // namespace

std::unique_ptr<LocalGraphBuilder> make_xn_descent_local_build() {
  return std::make_unique<XnDescentLocalBuild>();
}

}  // namespace diskann_pipeline::xn_descent
