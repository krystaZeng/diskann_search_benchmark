#include "search/diskann/post_graph_materializer.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/validation.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "search/diskann/native_search_formats.hpp"
#include "search/diskann/fixed_descriptors.hpp"
#include "search/diskann/pqflash_instrumented_runner.hpp"

#if DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE
#include <immintrin.h>
#include "linux_aligned_file_reader.h"
#include "pq_flash_index.h"
#include "percentile_stats.h"
#include <omp.h>
#endif

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace dp = diskann_pipeline;
namespace ds = diskann_pipeline::search;
using Json = nlohmann::json;

namespace {

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time);
#else
  gmtime_r(&time, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

Json read_json(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                            "request cannot be read");
  }
  return Json::parse(input);
}

dp::ArtifactRef artifact_ref(const Json& value) {
  return dp::parse_artifact_ref(value.dump());
}

std::string on_off(bool value) { return value ? "ON" : "OFF"; }

dp::SourceIdentity configured_project_source() {
  dp::SourceIdentity source{DISKANN_SEARCH_PROJECT_REPOSITORY,
                            DISKANN_SEARCH_PROJECT_BRANCH,
                            DISKANN_SEARCH_PROJECT_COMMIT,
                            DISKANN_SEARCH_PROJECT_DIRTY != 0, std::nullopt};
  if (source.dirty_worktree) {
    source.dirty_source_checksum = DISKANN_SEARCH_PROJECT_DIRTY_CHECKSUM;
  }
  return source;
}

dp::SourceIdentity configured_diskann_source() {
  return {"https://github.com/microsoft/DiskANN.git", "cpp_main",
          "78256bbab4685e1774e78d331e081a153be26823", false,
          std::nullopt};
}

dp::ToolchainIdentity configured_toolchain() {
  return {DISKANN_SEARCH_SYSTEM_NAME,
          DISKANN_SEARCH_CXX_COMPILER,
          DISKANN_SEARCH_CXX_COMPILER_VERSION,
          DISKANN_SEARCH_CMAKE_VERSION,
          DISKANN_SEARCH_CMAKE_GENERATOR,
          DISKANN_SEARCH_BUILD_TYPE,
          {std::string("CMAKE_BUILD_TYPE=") + DISKANN_SEARCH_BUILD_TYPE,
           std::string("BUILD_TESTING=") +
               on_off(DISKANN_SEARCH_BUILD_TESTING != 0),
           std::string("DISKANN_CPP_MAIN_BASELINE=") +
               on_off(DISKANN_SEARCH_BASELINE_ENABLED != 0),
           std::string("DISKANN_SEARCH_EVALUATION=") +
               on_off(DISKANN_SEARCH_FEATURE_ENABLED != 0),
           std::string("CMAKE_EXPORT_COMPILE_COMMANDS=") +
               on_off(DISKANN_SEARCH_EXPORT_COMPILE_COMMANDS != 0),
           std::string("DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE=") +
               on_off(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE != 0)},
          {std::string(DISKANN_SEARCH_CXX_COMPILER) + " -std=c++17 " +
           DISKANN_SEARCH_EFFECTIVE_CXX_FLAGS +
           " [target=diskann_search_evaluation_runner]"},
          {std::string(DISKANN_SEARCH_CXX_COMPILER) +
           " [target=diskann_search_evaluation_runner; links="
           "diskann_pipeline::core,nlohmann_json::nlohmann_json,"
           "diskann_cpp_main_upstream,OpenMP::OpenMP_CXX]"}};
}

const dp::PayloadRef& payload(const ds::SearchArtifactAdmission& artifact,
                              const std::string& role) {
  for (const auto& candidate : artifact.payloads) {
    if (candidate.role == role) {
      return candidate;
    }
  }
  throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                          "load-probe payload is missing: " + role);
}

std::filesystem::path payload_path(const dp::WorkspaceRef& workspace,
                                   const ds::SearchArtifactAdmission& artifact,
                                   const dp::PayloadRef& value) {
  static_cast<void>(workspace);
  return dp::resolve_confined_path(artifact.metadata_file.parent_path(),
                                   value.path);
}

ds::SearchArtifactAdmission admitted_from_validated(
    const dp::WorkspaceRef& workspace, const dp::ValidatedArtifact& artifact) {
  return {artifact.reference(),
          artifact.dataset(),
          artifact.id_space(),
          artifact.source_artifacts(),
          artifact.payloads(),
          dp::resolve_normal_artifact_metadata(
              workspace, artifact.reference().metadata_path)};
}

struct StagedNativeIndex {
  std::filesystem::path stage_root;
  std::filesystem::path prefix;
  std::filesystem::path disk;
  std::filesystem::path pivots;
  std::filesystem::path codes;

  StagedNativeIndex(std::filesystem::path root,
                    std::filesystem::path prefix_value,
                    std::filesystem::path disk_value,
                    std::filesystem::path pivots_value,
                    std::filesystem::path codes_value)
      : stage_root(std::move(root)),
        prefix(std::move(prefix_value)),
        disk(std::move(disk_value)),
        pivots(std::move(pivots_value)),
        codes(std::move(codes_value)) {}
  StagedNativeIndex(const StagedNativeIndex&) = delete;
  StagedNativeIndex& operator=(const StagedNativeIndex&) = delete;
  StagedNativeIndex(StagedNativeIndex&&) noexcept = default;
  StagedNativeIndex& operator=(StagedNativeIndex&&) noexcept = default;
  ~StagedNativeIndex() {
    if (!stage_root.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(stage_root, ignored);
    }
  }
};

StagedNativeIndex stage_native_index(
    const dp::WorkspaceRef& workspace, const std::string& invocation_id,
    const ds::SearchArtifactAdmission& pq,
    const ds::SearchArtifactAdmission& layout) {
  const auto temporary =
      dp::create_temporary_storage(workspace, invocation_id + "-native-index");
  const auto prefix = temporary.stage_root / "index";
  const auto disk = std::filesystem::path(prefix.string() + "_disk.index");
  const auto pivots = std::filesystem::path(prefix.string() + "_pq_pivots.bin");
  const auto codes =
      std::filesystem::path(prefix.string() + "_pq_compressed.bin");
  const auto medoids =
      std::filesystem::path(disk.string() + "_medoids.bin");
  static_cast<void>(ds::stage_native_payload_alias(
      payload_path(workspace, layout, payload(layout, "disk_index")), disk));
  static_cast<void>(ds::stage_native_payload_alias(
      payload_path(workspace, layout, payload(layout, "medoids")), medoids));
  static_cast<void>(ds::stage_native_payload_alias(
      payload_path(workspace, pq, payload(pq, "pq_pivots")), pivots));
  static_cast<void>(ds::stage_native_payload_alias(
      payload_path(workspace, pq, payload(pq, "pq_compressed_vectors")),
      codes));
  return {temporary.stage_root, prefix, disk, pivots, codes};
}

class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};

int materialize(const std::filesystem::path& request_path,
                const std::filesystem::path& executable_path) {
  const auto json = read_json(request_path);
  ds::MaterializationRequest request;
  ds::configure_materialization_request(request, json.dump());
  const auto workspace = dp::create_workspace(
      json.at("workspace").at("root").get<std::string>());
  const auto runner_binary = std::filesystem::weakly_canonical(executable_path);
  if (!std::filesystem::is_regular_file(runner_binary)) {
    throw dp::PipelineError(dp::ErrorCategory::kPayloadMissing,
                            "materialization runner binary is missing");
  }
  request.run_id = json.at("run_id").get<std::string>();
  request.dataset = artifact_ref(json.at("dataset"));
  if (json.contains("global_graph")) {
    request.global_graph = artifact_ref(json.at("global_graph"));
  }
  request.source = configured_project_source();
  request.diskann_source = configured_diskann_source();
  request.toolchain = configured_toolchain();
  request.resource_budget = {
      json.at("resource_budget").at("memory_budget_bytes").get<std::uint64_t>(),
      json.at("resource_budget").at("thread_budget").get<std::uint32_t>()};
  request.runner_binary_path = runner_binary;
  request.runner_binary_sha256 = dp::sha256_file(runner_binary);
  request.started_at = now_utc();
  NeverCancelled cancellation;
  ds::PostGraphMaterializer operation(workspace,
                                      ds::fixed_native_post_graph_calls(),
                                      &cancellation);
  const auto result = json.at("operation") == "pq"
                          ? operation.materialize_pq(request)
                          : operation.materialize_disk_layout(request);
  Json output{{"status", dp::to_string(result.status)},
              {"build_manifest_path", result.build_manifest_path.string()}};
  if (result.artifact) {
    output["artifact"] = Json::parse(dp::serialize_artifact_ref(*result.artifact));
  }
  if (!result.validation_evidence_path.empty()) {
    output["validation_evidence_path"] =
        result.validation_evidence_path.string();
  }
  if (result.quarantine_id) {
    output["quarantine_id"] = *result.quarantine_id;
  }
  if (result.failure) {
    output["failure"] = result.failure->reason;
  }
  std::cout << output.dump() << '\n';
  return result.status == dp::StageStatus::kSucceeded ? 0 : 2;
}

int load_probe(const std::filesystem::path& workspace_root,
               const std::filesystem::path& pq_metadata,
               const std::filesystem::path& layout_metadata) {
  const auto workspace = dp::create_workspace(workspace_root);
  const auto pq_json = read_json(
      dp::resolve_confined_path(workspace.normal_root, pq_metadata));
  const auto layout_json = read_json(
      dp::resolve_confined_path(workspace.normal_root, layout_metadata));
  const dp::ArtifactRef pq_ref{
      pq_metadata, dp::artifact_type_from_string(
                       pq_json.at("artifact_type").get<std::string>()),
      pq_json.at("format_version").get<std::string>(),
      pq_json.at("aggregate_checksum").get<std::string>()};
  const dp::ArtifactRef layout_ref{
      layout_metadata, dp::artifact_type_from_string(
                           layout_json.at("artifact_type").get<std::string>()),
      layout_json.at("format_version").get<std::string>(),
      layout_json.at("aggregate_checksum").get<std::string>()};
  const auto pq = dp::Validator(workspace).validate_with_lineage(
      pq_ref);
  const auto layout = dp::Validator(workspace).validate_with_lineage(
      layout_ref);
  if (!pq.valid() || !layout.valid()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "load-probe inputs failed fresh validation");
  }
#if DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE
  const auto& pq_grant = *pq.validated_artifact;
  const auto& layout_grant = *layout.validated_artifact;
  const auto pq_admission = admitted_from_validated(workspace, pq_grant);
  const auto layout_admission = admitted_from_validated(workspace, layout_grant);
  const auto staged = stage_native_index(
      workspace,
      "load-probe-" + pq_ref.aggregate_checksum.substr(0, 12) + "-" +
          layout_ref.aggregate_checksum.substr(0, 12),
      pq_admission, layout_admission);
  std::shared_ptr<AlignedFileReader> reader(new LinuxAlignedFileReader());
  diskann::PQFlashIndex<float> index(reader, diskann::Metric::L2);
  const auto status = index.load_from_separate_paths(
      1, staged.disk.c_str(), staged.pivots.c_str(), staged.codes.c_str());
  if (status != 0 || index.get_num_points() != pq_grant.dataset().global_node_count ||
      index.get_data_dim() != pq_grant.dataset().vector_dimension) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "PQFlashIndex load probe failed");
  }
  std::cout << "{\"status\":\"SUCCEEDED\"}\n";
  return 0;
#else
  throw dp::PipelineError(dp::ErrorCategory::kCapabilityUnsatisfied,
                          "PQFlashIndex load probe is Linux-only pending");
#endif
}

#if DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE
std::vector<float> read_query_matrix(const std::filesystem::path& path,
                                     std::uint32_t& count,
                                     std::uint32_t& dimension) {
  std::ifstream input(path, std::ios::binary);
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
  if (!input || count == 0 || dimension == 0) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "native QuerySet header is invalid");
  }
  std::vector<float> values(static_cast<std::size_t>(count) * dimension);
  input.read(reinterpret_cast<char*>(values.data()),
             static_cast<std::streamsize>(values.size() * sizeof(float)));
  if (!input || input.peek() != std::ifstream::traits_type::eof()) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "native QuerySet payload is invalid");
  }
  return values;
}

Json artifact_json(const dp::ArtifactRef& value) {
  return Json::parse(dp::serialize_artifact_ref(value));
}

ds::SearchAdmissionEvidenceRef admission_evidence_ref(const Json& value) {
  return {value.at("path").get<std::string>(),
          value.at("sha256").get<std::string>(),
          artifact_ref(value.at("expected_artifact"))};
}

void validate_protocol_checksums(const Json& protocol) {
  const auto& execution = protocol.at("search_execution");
  const auto execution_checksum = dp::sha256_bytes(
      dp::canonicalize_restricted_json(execution.dump()));
  if (execution_checksum !=
      protocol.at("search_execution_checksum").get<std::string>()) {
    throw dp::PipelineError(dp::ErrorCategory::kChecksumMismatch,
                            "SearchProtocol execution checksum mismatch");
  }
  auto projection = protocol;
  projection.erase("protocol_checksum");
  const auto protocol_checksum = dp::sha256_bytes(
      dp::canonicalize_restricted_json(projection.dump()));
  if (protocol_checksum != protocol.at("protocol_checksum").get<std::string>()) {
    throw dp::PipelineError(dp::ErrorCategory::kChecksumMismatch,
                            "SearchProtocol checksum mismatch");
  }
}

int search_protocol(const std::filesystem::path& workspace_root,
                    const std::filesystem::path& protocol_path,
                    const std::filesystem::path& executable_path) {
  const auto workspace = dp::create_workspace(workspace_root);
  const auto protocol_absolute = protocol_path.is_absolute()
                                     ? protocol_path
                                     : dp::resolve_confined_path(workspace.root,
                                                                 protocol_path);
  const auto protocol = read_json(protocol_absolute);
  validate_protocol_checksums(protocol);
  const auto& execution = protocol.at("search_execution");
  if (execution.at("K") != 100 || execution.at("beamwidth") != 2 ||
      execution.at("thread_count") != 6 ||
      execution.at("repetition_count") != 3 ||
      execution.at("schedule") != "repetition_major_then_L_order" ||
      execution.at("warmup_policy") != "none") {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "SearchProtocol controls drifted");
  }
  const auto dataset_ref = artifact_ref(execution.at("dataset_artifact"));
  const auto global_ref = artifact_ref(execution.at("global_graph_artifact"));
  const auto pq_ref = artifact_ref(execution.at("pq_artifact"));
  const auto layout_ref = artifact_ref(execution.at("disk_layout_artifact"));
  const auto& implementation = execution.at("implementation");
  const auto& runner_build = execution.at("runner_build");
  ds::validate_search_runner_execution_identity(
      workspace, std::filesystem::current_path(), executable_path,
      {runner_build.at("binary_path").get<std::string>(),
       runner_build.at("binary_sha256").get<std::string>(),
       runner_build.at("build_manifest_path").get<std::string>(),
       runner_build.at("build_manifest_sha256").get<std::string>(),
       {runner_build.at("project_repository").get<std::string>(),
        runner_build.at("project_branch").get<std::string>(),
        runner_build.at("project_commit").get<std::string>(),
        runner_build.at("project_dirty").get<bool>(), std::nullopt},
       implementation.at("implementation_id").get<std::string>(),
       implementation.at("version").get<std::string>(),
       implementation.at("reproduction_mode").get<std::string>(),
       execution.at("diskann_source_commit").get<std::string>()});

  ds::SearchArtifactAdmission dataset_admission;
  ds::SearchArtifactAdmission global_admission;
  ds::SearchArtifactAdmission pq_admission;
  ds::SearchArtifactAdmission layout_admission;
  if (execution.contains("admission_evidence")) {
    dataset_admission =
        ds::admit_search_artifact_metadata(workspace, dataset_ref);
    global_admission =
        ds::admit_search_artifact_metadata(workspace, global_ref);
    const auto& evidence = execution.at("admission_evidence");
    pq_admission = ds::admit_prevalidated_search_artifact(
        workspace, pq_ref,
        admission_evidence_ref(evidence.at("pq_artifact")));
    layout_admission = ds::admit_prevalidated_search_artifact(
        workspace, layout_ref,
        admission_evidence_ref(evidence.at("disk_layout_artifact")));
  } else {
    const auto dataset =
        dp::Validator(workspace).validate_with_lineage(dataset_ref);
    const auto global =
        dp::Validator(workspace).validate_with_lineage(global_ref);
    const auto pq = dp::Validator(workspace).validate_with_lineage(pq_ref);
    const auto layout =
        dp::Validator(workspace).validate_with_lineage(layout_ref);
    if (!dataset.valid() || !global.valid() || !pq.valid() || !layout.valid()) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "legacy search input failed fresh validation");
    }
    dataset_admission =
        admitted_from_validated(workspace, *dataset.validated_artifact);
    global_admission =
        admitted_from_validated(workspace, *global.validated_artifact);
    pq_admission = admitted_from_validated(workspace, *pq.validated_artifact);
    layout_admission =
        admitted_from_validated(workspace, *layout.validated_artifact);
  }
  ds::validate_search_artifact_lineage(dataset_admission, global_admission,
                                       pq_admission, layout_admission);
  const auto& query_ref = execution.at("query_set");
  const auto query_path = dp::resolve_confined_path(
      workspace.root,
      query_ref.at("native_payload").at("path").get<std::string>());
  if (!std::filesystem::is_regular_file(query_path) ||
      std::filesystem::file_size(query_path) !=
          query_ref.at("native_payload").at("byte_length").get<std::uint64_t>() ||
      dp::sha256_file(query_path) !=
      query_ref.at("native_payload").at("sha256").get<std::string>()) {
    throw dp::PipelineError(dp::ErrorCategory::kChecksumMismatch,
                            "QuerySet payload checksum mismatch");
  }
  const auto mapping_path = dp::resolve_confined_path(
      workspace.root, query_ref.at("row_mapping").at("path").get<std::string>());
  if (!std::filesystem::is_regular_file(mapping_path) ||
      std::filesystem::file_size(mapping_path) !=
          query_ref.at("row_mapping").at("byte_length").get<std::uint64_t>() ||
      dp::sha256_file(mapping_path) !=
          query_ref.at("row_mapping").at("sha256").get<std::string>()) {
    throw dp::PipelineError(dp::ErrorCategory::kChecksumMismatch,
                            "QuerySet row mapping checksum mismatch");
  }
  const auto mapping = read_json(mapping_path);
  std::uint32_t query_count = 0;
  std::uint32_t query_dimension = 0;
  auto queries = read_query_matrix(query_path, query_count, query_dimension);
  if (query_count != query_ref.at("query_count").get<std::uint32_t>() ||
      query_dimension != pq_admission.dataset.vector_dimension ||
      !mapping.is_array() || mapping.size() != query_count) {
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "QuerySet identity/row mapping mismatch");
  }
  for (std::uint32_t row = 0; row < query_count; ++row) {
    if (mapping.at(row).at("query_row_index").get<std::uint32_t>() != row ||
        !mapping.at(row).at("source_query_id").is_string()) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "QuerySet row mapping order is invalid");
    }
  }
  dp::BuildManifest manifest;
  manifest.run_id = protocol.at("protocol_id").get<std::string>() + "-search";
  manifest.stage = "search";
  const auto& identity = ds::fixed_instrumented_search_identity();
  manifest.implementation = {identity.implementation_id,
                             identity.implementation_version};
  manifest.source = {"https://github.com/microsoft/DiskANN.git", "cpp_main",
                     "78256bbab4685e1774e78d331e081a153be26823", false,
                     std::nullopt};
  manifest.toolchain = configured_toolchain();
  manifest.dataset = pq_admission.dataset;
  Json observation_references = Json::array();
  const auto refresh_manifest_parameters = [&]() {
    manifest.parameters_json =
        Json{{"protocol_id", protocol.at("protocol_id")},
             {"protocol_checksum", protocol.at("protocol_checksum")},
             {"search_execution_checksum",
              protocol.at("search_execution_checksum")},
             {"search_execution", execution},
             {"observation_references", observation_references}}
            .dump();
  };
  refresh_manifest_parameters();
  manifest.resource_budget = {1, 6};
  manifest.input_artifacts = {dataset_ref, global_ref, pq_ref, layout_ref};
  manifest.started_at = now_utc();
  try {
    const auto staged = stage_native_index(
        workspace, protocol.at("protocol_id").get<std::string>() + "-search",
        pq_admission, layout_admission);
    std::shared_ptr<AlignedFileReader> reader(new LinuxAlignedFileReader());
    diskann::PQFlashIndex<float> index(reader, diskann::Metric::L2);
    if (index.load_from_separate_paths(6, staged.disk.c_str(),
                                       staged.pivots.c_str(),
                                       staged.codes.c_str()) != 0) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "PQFlashIndex load failed");
    }
    const auto requested_nodes =
        execution.at("cache").at("requested_nodes").get<std::uint64_t>();
    std::vector<std::uint32_t> cache_nodes;
    index.cache_bfs_levels(requested_nodes, cache_nodes, false);
    index.load_cache_list(cache_nodes);
    const auto effective_nodes = cache_nodes.size();
    const auto primary_bytes = ds::derived_primary_cache_bytes(
        effective_nodes, query_dimension, sizeof(float), index.get_max_degree());

    const auto checked_add = [](std::uint64_t lhs, std::uint64_t rhs) {
      if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        throw dp::PipelineError(dp::ErrorCategory::kCapabilityUnsatisfied,
                                "search working-set size overflow");
      }
      return lhs + rhs;
    };
    const auto query_bytes =
        static_cast<std::uint64_t>(queries.size()) * sizeof(float);
    const auto result_bytes =
        static_cast<std::uint64_t>(query_count) * 100U *
        (sizeof(std::uint64_t) + sizeof(float));
    const auto cache_list_bytes =
        static_cast<std::uint64_t>(cache_nodes.size()) * sizeof(std::uint32_t);
    manifest.resource_budget.memory_budget_bytes = checked_add(
        checked_add(query_bytes, result_bytes),
        checked_add(primary_bytes, cache_list_bytes));

    const auto environment_path = dp::resolve_confined_path(
        workspace.report_root,
        std::filesystem::path("environment") /
            (protocol.at("protocol_id").get<std::string>() + ".json"));
    Json environment{{"os", DISKANN_SEARCH_SYSTEM_NAME},
                     {"performance_classification", "runtime_observed"}};
    dp::write_immutable_text(environment_path, environment.dump());
    const auto environment_relative =
        std::filesystem::relative(environment_path, workspace.root);

    omp_set_num_threads(6);
    for (std::uint32_t repetition = 0; repetition < 3; ++repetition) {
      for (const auto& l_value : execution.at("L_search")) {
        const auto l = l_value.get<std::uint32_t>();
        if (l < 100) {
          throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                                  "L_search is below K");
        }
        std::vector<ds::PerQueryObservation> rows(query_count);
        const auto start = std::chrono::steady_clock::now();
#pragma omp parallel for schedule(dynamic, 1)
        for (std::int64_t row = 0; row < query_count; ++row) {
          auto& result = rows[static_cast<std::size_t>(row)];
          result.query_row_index = static_cast<std::uint32_t>(row);
          result.source_query_id =
              mapping.at(static_cast<std::size_t>(row))
                  .at("source_query_id")
                  .get<std::string>();
          result.result_ids.resize(100);
          result.result_distances.resize(100);
          diskann::QueryStats stats;
          index.cached_beam_search(
              queries.data() + static_cast<std::size_t>(row) * query_dimension,
              100, l, result.result_ids.data(), result.result_distances.data(),
              2, std::numeric_limits<std::uint32_t>::max(), false, &stats);
          result.total_us = stats.total_us;
          result.n_ios = stats.n_ios;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);
        Json per_query = Json::array();
        for (const auto& row : rows) {
          per_query.push_back(
              {{"query_row_index", row.query_row_index},
               {"source_query_id", row.source_query_id},
               {"result_ids", row.result_ids},
               {"result_distances", row.result_distances},
               {"total_us", row.total_us},
               {"n_ios", row.n_ios}});
        }
        const auto point_id =
            protocol.at("protocol_id").get<std::string>() + "-L" +
            std::to_string(l) + "-r" + std::to_string(repetition);
        Json observation{
            {"format_version", "search-observation/v1"},
            {"observation_id", point_id},
            {"run_id", manifest.run_id},
            {"generated_at", now_utc()},
            {"implementation",
             {{"implementation_id", identity.implementation_id},
              {"version", identity.implementation_version},
              {"reproduction_mode", identity.reproduction_mode}}},
            {"source_build",
             {{"diskann_repository",
               "https://github.com/microsoft/DiskANN.git"},
              {"diskann_branch", "cpp_main"},
              {"diskann_commit",
               "78256bbab4685e1774e78d331e081a153be26823"},
              {"binary_path", execution.at("runner_build").at("binary_path")},
              {"binary_sha256",
               execution.at("runner_build").at("binary_sha256")},
              {"build_manifest_path",
               execution.at("runner_build").at("build_manifest_path")},
              {"build_manifest_sha256",
               execution.at("runner_build").at("build_manifest_sha256")}}},
            {"protocol",
             {{"path", std::filesystem::relative(protocol_absolute,
                                                   workspace.root)
                           .generic_string()},
              {"protocol_id", protocol.at("protocol_id")},
              {"protocol_checksum", protocol.at("protocol_checksum")}}},
            {"search_execution",
             {{"execution_id", execution.at("execution_id")},
              {"canonical_checksum",
               protocol.at("search_execution_checksum")}}},
            {"dataset_artifact", artifact_json(dataset_ref)},
            {"global_graph_artifact", artifact_json(global_ref)},
            {"pq_artifact", artifact_json(pq_ref)},
            {"disk_layout_artifact", artifact_json(layout_ref)},
            {"query_set_checksum", query_ref.at("canonical_checksum")},
            {"raw_search_point",
             {{"point_id", point_id},
              {"L_search", l},
              {"K", 100},
              {"beamwidth", 2},
              {"cache",
               {{"mode", execution.at("cache").at("mode")},
                {"requested_nodes", requested_nodes},
                {"effective_nodes", effective_nodes},
                {"primary_payload_bytes", primary_bytes}}},
              {"repetition_index", repetition},
              {"query_count", query_count},
              {"completed_query_count", query_count},
              {"batch_wall_clock_us",
               std::max<std::int64_t>(1, elapsed.count())},
              {"status", "SUCCEEDED"}}},
            {"environment",
             {{"fingerprint_path", environment_relative.generic_string()},
              {"fingerprint_sha256", dp::sha256_file(environment_path)}}},
            {"per_query", per_query}};
        observation["observation_checksum"] = dp::sha256_bytes(observation.dump());
        const auto target = dp::resolve_confined_path(
            workspace.report_root,
            std::filesystem::path("search-observations") / (point_id + ".json"));
        dp::write_immutable_text(target, observation.dump());
        observation_references.push_back(
            {{"path", std::filesystem::relative(target, workspace.root)
                          .generic_string()},
             {"sha256", dp::sha256_file(target)}});
        refresh_manifest_parameters();
      }
    }
    manifest.status = dp::StageStatus::kSucceeded;
    manifest.ended_at = now_utc();
    const auto manifest_path = dp::persist_terminal_build_manifest(workspace, manifest);
    std::cout << Json{{"status", "SUCCEEDED"},
                     {"build_manifest_path", manifest_path.string()}}
                     .dump()
              << '\n';
    return 0;
  } catch (const dp::PipelineError& error) {
    manifest.status = dp::StageStatus::kFailed;
    manifest.failure = {error.category(), error.what(), std::nullopt};
    manifest.ended_at = now_utc();
    static_cast<void>(dp::persist_terminal_build_manifest(workspace, manifest));
    throw;
  } catch (const std::exception& error) {
    manifest.status = dp::StageStatus::kFailed;
    manifest.failure = {dp::ErrorCategory::kInternal, error.what(), std::nullopt};
    manifest.ended_at = now_utc();
    static_cast<void>(dp::persist_terminal_build_manifest(workspace, manifest));
    throw;
  }
}
#else
int search_protocol(const std::filesystem::path&,
                    const std::filesystem::path&,
                    const std::filesystem::path&) {
  throw dp::PipelineError(dp::ErrorCategory::kCapabilityUnsatisfied,
                          "instrumented PQFlash search is Linux-only pending");
}
#endif

std::string argument(int argc, char** argv, const std::string& name) {
  for (int index = 2; index + 1 < argc; ++index) {
    if (argv[index] == name) {
      return argv[index + 1];
    }
  }
  throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                          "missing argument: " + name);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                              "operation is required");
    }
    const std::string operation = argv[1];
    if (operation == "materialize") {
      return materialize(argument(argc, argv, "--request"), argv[0]);
    }
    if (operation == "load-probe") {
      return load_probe(argument(argc, argv, "--workspace"),
                        argument(argc, argv, "--pq-artifact"),
                        argument(argc, argv, "--disk-layout-artifact"));
    }
    if (operation == "search") {
      return search_protocol(argument(argc, argv, "--workspace"),
                             argument(argc, argv, "--protocol"), argv[0]);
    }
    throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid,
                            "unsupported Feature operation");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
