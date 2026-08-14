#include "papers/hnsw_merger/vamana/stage_binding.hpp"

#include "papers/hnsw_merger/vamana/m1_algorithm.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/workspace.hpp"

#include "in_mem_graph_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace diskann_pipeline::hnsw_merger_vamana {
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
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setw(6) << std::setfill('0') << micros.count() << 'Z';
  return output.str();
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "M1 parameter or metadata document is missing");
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

DatasetIdentity partition_dataset(const WorkspaceRef& workspace,
                                  const ArtifactRef& partition) {
  const auto path =
      resolve_normal_artifact_metadata(workspace, partition.metadata_path);
  const auto json = detail::parse_json_strict(read_text(path));
  const auto& value = json.at("dataset");
  return {value.at("dataset_id").get<std::string>(),
          value.at("dataset_checksum").get<std::string>(),
          value.at("global_node_count").get<NodeCount>(),
          value.at("vector_dimension").get<std::uint32_t>(),
          value.at("scalar_type").get<std::string>(),
          value.at("distance_metric").get<std::string>()};
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

Json source_identity_json(const SourceIdentity& source) {
  Json value{{"repository", source.repository},
             {"branch", source.branch},
             {"commit", source.commit},
             {"dirty_worktree", source.dirty_worktree}};
  if (source.dirty_source_checksum) {
    value["dirty_source_checksum"] = *source.dirty_source_checksum;
  }
  return value;
}

Json payload_json(const std::string& role, const std::string& relative,
                  const std::string& media,
                  const std::filesystem::path& path) {
  return {{"role", role},
          {"path", relative},
          {"media_type", media},
          {"byte_length", std::filesystem::file_size(path)},
          {"sha256", sha256_file(path)}};
}

std::string aggregate_for(const Json& metadata) {
  AggregateIdentityProjection projection;
  projection.artifact_type = ArtifactType::kGlobalGraph;
  projection.format_version = "global-graph-artifact/v1";
  projection.dataset_checksum =
      metadata.at("dataset").at("dataset_checksum").get<std::string>();
  projection.id_space = IdSpace::kGlobal;
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

void write_medoids(const std::filesystem::path& path,
                   const std::vector<GlobalNodeId>& medoids) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  const auto write_u32 = [&](std::uint32_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8U) & 0xffU),
        static_cast<unsigned char>((value >> 16U) & 0xffU),
        static_cast<unsigned char>((value >> 24U) & 0xffU)};
    output.write(reinterpret_cast<const char*>(bytes), 4);
  };
  write_u32(static_cast<std::uint32_t>(medoids.size()));
  write_u32(1);
  for (const auto medoid : medoids) write_u32(medoid);
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "M1 cannot write medoids payload");
  }
}

Json decision_json(const PairwiseMergeDecision& value) {
  return {{"step", value.step},
          {"left_lineage", value.left_lineage},
          {"right_lineage", value.right_lineage},
          {"left_size", value.left_size},
          {"right_size", value.right_size},
          {"source_lineage", value.source_lineage},
          {"target_lineage", value.target_lineage},
          {"epoch_N0", value.epoch_n0},
          {"lambda_policy", value.lambda_policy},
          {"effective_lambda", value.effective_lambda},
          {"shared_node_count", value.shared_node_count},
          {"output_lineage", value.output_lineage},
          {"output_size", value.output_size}};
}

SourceIdentity project_source() {
  SourceIdentity source{HNSW_MERGER_PROJECT_REPOSITORY,
                        HNSW_MERGER_PROJECT_BRANCH,
                        HNSW_MERGER_PROJECT_COMMIT,
                        HNSW_MERGER_PROJECT_DIRTY != 0,
                        std::nullopt};
  if (source.dirty_worktree) {
    source.dirty_source_checksum = HNSW_MERGER_PROJECT_DIRTY_CHECKSUM;
  }
  return source;
}

ToolchainIdentity actual_toolchain() {
  return {HNSW_MERGER_SYSTEM_NAME,
          HNSW_MERGER_CXX_COMPILER,
          HNSW_MERGER_CXX_COMPILER_VERSION,
          HNSW_MERGER_CMAKE_VERSION,
          HNSW_MERGER_CMAKE_GENERATOR,
          HNSW_MERGER_BUILD_TYPE,
          {"DISKANN_CPP_MAIN_BASELINE=ON", "DISKANN_HNSW_MERGER=ON"},
          {std::string("cmake --build ") + HNSW_MERGER_BINARY_DIR +
           " --target diskann_hnsw_merger_vamana"},
          {"diskann_hnsw_merger_vamana -> diskann_s"}};
}

std::filesystem::path run_id_from(const std::filesystem::path& parameter_path) {
  if (parameter_path.filename() != "merge.json" ||
      parameter_path.parent_path().filename() != "parameters") {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "M1 parameter path does not bind a Merge run");
  }
  return parameter_path.parent_path().parent_path().filename();
}

BuildManifest base_execution_manifest(
    const std::string& run_id, const DatasetIdentity& dataset,
    const std::vector<ArtifactRef>& inputs, const StageContext& context,
    const std::string& started_at) {
  BuildManifest manifest;
  manifest.run_id = run_id;
  manifest.stage = "merge";
  manifest.implementation = {kImplementationId, kImplementationVersion};
  manifest.source = project_source();
  manifest.toolchain = actual_toolchain();
  manifest.dataset = dataset;
  manifest.resource_budget = {context.memory_budget_bytes,
                              context.thread_budget};
  manifest.input_artifacts = inputs;
  manifest.status = StageStatus::kRunning;
  manifest.started_at = started_at;
  manifest.metrics_json = "{}";
  return manifest;
}

Json provenance_parameters(const M1Parameters& parameters,
                           const std::vector<PairwiseMergeDecision>& decisions) {
  Json decision_values = Json::array();
  for (const auto& decision : decisions) {
    decision_values.push_back(decision_json(decision));
  }
  const SourceIdentity paper{kPaperRepository, kPaperBranch, kPaperCommit,
                             false, std::nullopt};
  const SourceIdentity diskann{
      "https://github.com/microsoft/DiskANN.git", "cpp_main",
      "78256bbab4685e1774e78d331e081a153be26823", false, std::nullopt};
  return {{"paper_source_identity", source_identity_json(paper)},
          {"diskann_source_identity", source_identity_json(diskann)},
          {"paper_doi", kPaperDoi},
          {"paper_pdf_sha256", kPaperPdfSha256},
          {"integration_mode", "source_mapping_only"},
          {"official_binary_linked", false},
          {"evidence_classification",
           {{"merge_mechanism", "paper_verified"},
            {"official_hnsw_behavior", "source_verified"},
            {"vamana_mapping", "Vamana/DiskANN_faithful_adaptation"},
            {"replica_normalization", "DiskANN_compatibility_adaptation"}}},
          {"effective_policy",
           {{"R", parameters.r},
            {"alpha", parameters.alpha},
            {"max_candidate_size", parameters.max_candidate_size},
            {"metric", "l2"},
            {"multi_shard_strategy", "large_first_pairwise_v1"},
            {"lambda_policy", "paper_adaptive_log_v1"}}},
          {"pairwise_merge_decisions", std::move(decision_values)}};
}

std::filesystem::path persist_execution_manifest(
    const WorkspaceRef& workspace, BuildManifest manifest) {
  manifest.ended_at = now_utc();
  const auto path = resolve_confined_path(
      workspace.report_root,
      std::filesystem::path("hnsw-merger") / manifest.run_id /
          "merge-execution.json");
  write_immutable_text(path, serialize_build_manifest(manifest));
  return path;
}

class HnswMergerVamanaMerge final : public GraphMerger {
 public:
  StageResult merge(const ArtifactRef& partition,
                    const std::vector<ArtifactRef>& locals,
                    const std::filesystem::path& parameter_path,
                    StageContext& context) override {
    const auto started_at = now_utc();
    const auto run_id = run_id_from(parameter_path).string();
    std::vector<ArtifactRef> inputs{partition};
    inputs.insert(inputs.end(), locals.begin(), locals.end());
    const auto dataset = partition_dataset(context.workspace, partition);
    const auto parameters = parse_parameters(read_text(parameter_path));
    auto manifest = base_execution_manifest(run_id, dataset, inputs, context,
                                            started_at);
    try {
      if (context.cancellation_token->is_cancelled()) {
        manifest.status = StageStatus::kCancelled;
        manifest.parameters_json =
            provenance_parameters(parameters, {}).dump();
        return {StageStatus::kCancelled, std::nullopt,
                persist_execution_manifest(context.workspace,
                                           std::move(manifest))};
      }
      auto loaded = load_merge_inputs(context.workspace, partition, locals);
      if (context.cancellation_token->is_cancelled()) {
        manifest.status = StageStatus::kCancelled;
        manifest.parameters_json =
            provenance_parameters(parameters, {}).dump();
        return {StageStatus::kCancelled, std::nullopt,
                persist_execution_manifest(context.workspace,
                                           std::move(manifest))};
      }
      auto merged = merge_large_first(
          std::move(loaded.graphs), parameters.r, parameters.alpha,
          parameters.max_candidate_size, *context.vector_accessor,
          *context.distance_function, context.cancellation_token,
          context.metrics_sink);
      if (context.cancellation_token->is_cancelled()) {
        manifest.status = StageStatus::kCancelled;
        manifest.parameters_json =
            provenance_parameters(parameters, merged.decisions).dump();
        return {StageStatus::kCancelled, std::nullopt,
                persist_execution_manifest(context.workspace,
                                           std::move(manifest))};
      }
      if (merged.graph.adjacency.size() != dataset.global_node_count) {
        throw PipelineError(ErrorCategory::kMappingIncomplete,
                            "M1 final logical coverage is incomplete");
      }
      const auto native = context.temporary_storage.stage_root / "native";
      std::filesystem::create_directories(native);
      const auto graph_path = native / "global.index";
      const auto medoids_path = native / "medoids.bin";
      diskann::InMemGraphStore output(dataset.global_node_count, parameters.r);
      for (GlobalNodeId node = 0; node < dataset.global_node_count; ++node) {
        const auto found = merged.graph.adjacency.find(node);
        if (found == merged.graph.adjacency.end()) {
          throw PipelineError(ErrorCategory::kMappingIncomplete,
                              "M1 final graph lacks a Dataset node");
        }
        auto neighbors = found->second;
        output.set_neighbours(node, neighbors);
      }
      output.store(graph_path.string(), dataset.global_node_count, 0,
                   merged.graph.entry);
      write_medoids(medoids_path, loaded.medoids);
      const auto graph_facts = cpp_main::validate_merged_global_graph(
          graph_path, dataset.global_node_count, parameters.r);
      cpp_main::validate_medoids(medoids_path, loaded.medoids.size(),
                                 dataset.global_node_count);

      const auto candidate =
          context.temporary_storage.stage_root / "candidate";
      std::filesystem::create_directories(candidate);
      const auto candidate_graph = candidate / "global.index";
      const auto candidate_medoids = candidate / "medoids.bin";
      std::filesystem::copy_file(graph_path, candidate_graph);
      std::filesystem::copy_file(medoids_path, candidate_medoids);
      Json source_locals = Json::array();
      Json sources = Json::array({source_json(partition)});
      for (const auto& local : locals) {
        source_locals.push_back(source_json(local));
        sources.push_back(source_json(local));
      }
      Json metadata{
          {"schema_version", "1.0.0"},
          {"canonicalization", "diskann-artifact-c14n-v1"},
          {"artifact_type", "global_graph"},
          {"format_version", "global-graph-artifact/v1"},
          {"dataset", dataset_json(dataset)},
          {"id_space", "global"},
          {"structural",
           {{"global_node_count", dataset.global_node_count},
            {"declared_max_degree", parameters.r},
            {"observed_max_degree", graph_facts.observed_max_degree},
            {"source_partition_artifact", source_json(partition)},
            {"source_local_graph_artifacts", source_locals}}},
          {"capabilities",
           Json::array({"graph.global_adjacency/v1", "graph.medoids/v1"})},
          {"source_artifacts", sources},
          {"payloads",
           Json::array({payload_json("global_graph", "global.index",
                                     cpp_main::kGlobalGraphMediaType,
                                     candidate_graph),
                        payload_json("medoids", "medoids.bin",
                                     cpp_main::kMedoidsMediaType,
                                     candidate_medoids)})}};
      metadata["aggregate_checksum"] = aggregate_for(metadata);
      std::ofstream(candidate / "artifact.json", std::ios::binary)
          << metadata.dump();
      if (context.cancellation_token->is_cancelled()) {
        manifest.status = StageStatus::kCancelled;
        manifest.parameters_json =
            provenance_parameters(parameters, merged.decisions).dump();
        return {StageStatus::kCancelled, std::nullopt,
                persist_execution_manifest(context.workspace,
                                           std::move(manifest))};
      }
      manifest.status = StageStatus::kSucceeded;
      manifest.parameters_json =
          provenance_parameters(parameters, merged.decisions).dump();
      const auto manifest_path =
          persist_execution_manifest(context.workspace, std::move(manifest));
      return {StageStatus::kSucceeded,
              ArtifactRef{"candidate/artifact.json",
                          ArtifactType::kGlobalGraph,
                          "global-graph-artifact/v1",
                          metadata.at("aggregate_checksum").get<std::string>()},
              manifest_path};
    } catch (const PipelineError& error) {
      manifest.status = error.category() == ErrorCategory::kCancelled
                            ? StageStatus::kCancelled
                            : StageStatus::kFailed;
      manifest.parameters_json = provenance_parameters(parameters, {}).dump();
      if (manifest.status == StageStatus::kFailed) {
        manifest.failure =
            FailureInfo{error.category(), error.what(), std::nullopt};
      }
      return {manifest.status, std::nullopt,
              persist_execution_manifest(context.workspace,
                                         std::move(manifest))};
    } catch (const std::exception& error) {
      manifest.status = StageStatus::kFailed;
      manifest.failure =
          FailureInfo{ErrorCategory::kInternal, error.what(), std::nullopt};
      manifest.parameters_json = provenance_parameters(parameters, {}).dump();
      return {StageStatus::kFailed, std::nullopt,
              persist_execution_manifest(context.workspace,
                                         std::move(manifest))};
    }
  }
};

}  // namespace

std::unique_ptr<GraphMerger> make_m1_merge() {
  return std::make_unique<HnswMergerVamanaMerge>();
}

}  // namespace diskann_pipeline::hnsw_merger_vamana
