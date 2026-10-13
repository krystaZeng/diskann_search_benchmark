#include "partition/diskann_kmeans/stage_binding.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "baseline/cpp_main/stage_bindings.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <immintrin.h>
#include "partition.h"
#include "math_utils.h"
#include <mkl.h>
#include <omp.h>
#include <sys/resource.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <nlohmann/json.hpp>

namespace diskann_pipeline::diskann_kmeans {
namespace {
using Json = nlohmann::json;

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw PipelineError(ErrorCategory::kPayloadMissing,
                                  "missing KMeans input/build evidence: " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

Json payload(const std::string& role, const std::string& name,
             const char* media, const std::filesystem::path& path) {
  return {{"role", role}, {"path", name}, {"media_type", media},
          {"byte_length", std::filesystem::file_size(path)}, {"sha256", sha256_file(path)}};
}

ArtifactRef materialize(const std::filesystem::path& root,
                        const std::filesystem::path& prefix,
                        const DatasetIdentity& identity, const ArtifactRef& dataset,
                        const std::vector<NodeCount>& counts) {
  const auto directory = root / "candidate";
  if (!std::filesystem::create_directory(directory)) {
    throw PipelineError(ErrorCategory::kPathEscape, "KMeans candidate already exists");
  }
  Json records = Json::array(), payloads = Json::array();
  for (std::size_t index = 0; index < counts.size(); ++index) {
    const auto number = std::to_string(index);
    const auto filename = "partition-" + number + "-map.bin";
    const auto role = "partition/" + number + "/local_to_global_map";
    std::filesystem::copy_file(prefix.string() + "_subshard-" + number + "_ids_uint32.bin",
                               directory / filename);
    records.push_back({{"partition_id", index}, {"local_node_count", counts[index]},
                       {"local_to_global_map_role", role}});
    payloads.push_back(payload(role, filename, cpp_main::kPartitionIdMapMediaType,
                               directory / filename));
  }
  std::filesystem::copy_file(prefix.string() + "_centroids.bin", directory / "centroids.bin");
  payloads.push_back(payload(kPartitionCentroidsPayloadRole, "centroids.bin",
                             cpp_main::kCentroidsMediaType, directory / "centroids.bin"));
  Json metadata{
      {"schema_version", "1.0.0"}, {"canonicalization", "diskann-artifact-c14n-v1"},
      {"artifact_type", "partition"}, {"format_version", "partition-artifact/v1"},
      {"dataset", {{"dataset_id", identity.dataset_id},
                   {"dataset_checksum", identity.dataset_checksum},
                   {"global_node_count", identity.global_node_count},
                   {"vector_dimension", identity.vector_dimension},
                   {"scalar_type", identity.scalar_type}, {"distance_metric", identity.distance_metric}}},
      {"id_space", "local"},
      {"structural", {{"partition_count", counts.size()}, {"partitions", records}}},
      {"capabilities", descriptor().provides.capabilities},
      {"source_artifacts", Json::array({{{"artifact_type", "dataset"},
                                         {"format_version", dataset.format_version},
                                         {"aggregate_checksum", dataset.aggregate_checksum}}})},
      {"payloads", payloads}};
  AggregateIdentityProjection projection{
      ArtifactType::kPartition, "partition-artifact/v1", identity.dataset_checksum,
      IdSpace::kLocal, descriptor().provides.capabilities, {},
      {{ArtifactType::kDataset, dataset.format_version, dataset.aggregate_checksum}},
      metadata.at("structural").dump()};
  for (const auto& item : payloads) {
    projection.payloads.push_back({item.at("role"), item.at("byte_length"), item.at("sha256")});
  }
  const auto checksum = aggregate_checksum(projection);
  metadata["aggregate_checksum"] = checksum;
  write_immutable_text(directory / "artifact.json", metadata.dump());
  return {"candidate/artifact.json", ArtifactType::kPartition, "partition-artifact/v1", checksum};
}

void check_cancelled(const StageContext& context) {
  if (context.cancellation_token->is_cancelled()) {
    throw PipelineError(ErrorCategory::kCancelled, "KMeans Partition cancelled between native calls");
  }
}

class ThreadScope {
 public:
  explicit ThreadScope(std::uint32_t count)
      : omp_previous_(omp_get_max_threads()), mkl_previous_(mkl_set_num_threads_local(count)) {
    omp_set_num_threads(count);
  }
  ~ThreadScope() { omp_set_num_threads(omp_previous_); mkl_set_num_threads_local(mkl_previous_); }
 private:
  int omp_previous_, mkl_previous_;
};

class Partition final : public PartitionStrategy {
 public:
  explicit Partition(cpp_main::SourceBuildEvidence evidence) : evidence_(std::move(evidence)) {}

  StageResult run(const ArtifactRef& dataset, const std::filesystem::path& parameter_path,
                  StageContext& context) override {
    validate_stage_context(context);
    if (context.thread_budget > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      throw PipelineError(ErrorCategory::kSchemaInvalid, "native thread budget exceeds int range");
    }
    // The existing executor writes manifests/<run_id>/parameters/partition.json.
    const auto relative = std::filesystem::relative(parameter_path, context.workspace.manifest_root);
    static_cast<void>(resolve_confined_path(context.workspace.manifest_root, relative));
    const auto run_id = relative.begin()->string();
    auto manifest = build_manifest(evidence_, run_id);
    cpp_main::CppMainVectorAccessor vectors(context.workspace, dataset);
    manifest.dataset = vectors.dataset_identity();
    manifest.input_artifacts = {dataset};
    manifest.resource_budget = {context.memory_budget_bytes, context.thread_budget};
    auto parameters = Json::parse(manifest.parameters_json);
    parameters["requested_parameters"] = detail::parse_json_strict(read_text(parameter_path));
    parameters["actual_training_sample_count"] = nullptr;
    const auto start = std::chrono::steady_clock::now();
    std::optional<ArtifactRef> candidate;
    try {
      const auto config = parse_parameters(parameters.at("requested_parameters").dump(),
                                            manifest.dataset.global_node_count);
      const double probability = sample_probability(manifest.dataset.global_node_count);
      parameters["effective_parameters"] = {{"K", config.k},
          {"max_k_means_reps", config.max_k_means_reps}, {"k_base", 1},
          {"sample_probability", probability}, {"expected_training_samples_target", 256000},
          {"randomness", "native_random_device_no_seed_override"}};
      check_cancelled(context);
      const auto directory = context.temporary_storage.stage_root / "native";
      if (!std::filesystem::create_directory(directory)) {
        throw PipelineError(ErrorCategory::kPathEscape, "native KMeans output already exists");
      }
      const auto prefix = directory / "kmeans";
      ThreadScope threads(context.thread_budget);
      float* sampled = nullptr;
      std::size_t samples = 0, dimension = 0;
      context.metrics_sink->event("diskann_kmeans.gen_random_slice");
      gen_random_slice<float>(vectors.native_payload_path().string(), probability,
                               sampled, samples, dimension);
      std::unique_ptr<float[]> training(sampled);
      parameters["actual_training_sample_count"] = samples;
      // Must precede both pivot allocation and native initialization.
      validate_training_sample_count(samples, config.k);
      if (dimension != manifest.dataset.vector_dimension) {
        throw PipelineError(ErrorCategory::kNodeCountMismatch, "sample dimension differs from Dataset");
      }
      check_cancelled(context);
      std::vector<float> pivots(static_cast<std::size_t>(config.k) * dimension);
      context.metrics_sink->event("diskann_kmeans.kmeanspp_selecting_pivots");
      kmeans::kmeanspp_selecting_pivots(training.get(), samples, dimension, pivots.data(), config.k);
      check_cancelled(context);
      context.metrics_sink->event("diskann_kmeans.run_lloyds");
      const float residual = kmeans::run_lloyds(training.get(), samples, dimension,
          pivots.data(), config.k, config.max_k_means_reps, nullptr, nullptr);
      if (!std::isfinite(residual)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid, "native Lloyd residual is non-finite");
      }
      training.reset();
      check_cancelled(context);
      diskann::save_bin<float>(prefix.string() + "_centroids.bin", pivots.data(), config.k, dimension);
      cpp_main::validate_centroids(prefix.string() + "_centroids.bin", config.k, dimension);
      context.metrics_sink->event("diskann_kmeans.shard_data_into_clusters_only_ids");
      const auto status = shard_data_into_clusters_only_ids<float>(
          vectors.native_payload_path().string(), pivots.data(), config.k, dimension, 1, prefix.string());
      if (status != 0) throw PipelineError(ErrorCategory::kInternal, "native ID-only sharder failed");
      check_cancelled(context);
      const auto counts = validate_native_output(prefix, manifest.dataset, config.k);
      candidate = materialize(context.temporary_storage.stage_root, prefix,
                               manifest.dataset, dataset, counts);
      auto staging = context.workspace;
      staging.normal_root = context.temporary_storage.stage_root;
      const auto validation = Validator(staging).validate(*candidate, {vectors.validated_dataset()});
      if (!validation.valid()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid, "KMeans candidate failed PartitionArtifact/v1 validation");
      }
      const auto evidence_path = context.temporary_storage.stage_root / "validation.json";
      write_immutable_text(evidence_path, serialize_validation_evidence(validation.evidence));
      manifest.validation_evidence_path = std::filesystem::relative(evidence_path, context.workspace.root);
      check_cancelled(context);
      manifest.status = StageStatus::kSucceeded;
      auto output = *candidate;
      output.metadata_path = std::filesystem::relative(staging.normal_root / output.metadata_path,
                                                      context.workspace.root);
      manifest.output_artifacts = {output};
    } catch (const PipelineError& error) {
      manifest.status = error.category() == ErrorCategory::kCancelled ? StageStatus::kCancelled : StageStatus::kFailed;
      manifest.failure = FailureInfo{error.category(), error.what(), std::nullopt};
      candidate.reset();
    } catch (const std::exception& error) {
      manifest.status = StageStatus::kFailed;
      manifest.failure = FailureInfo{ErrorCategory::kInternal, error.what(), std::nullopt};
      candidate.reset();
    }
    manifest.parameters_json = parameters.dump();
    manifest.ended_at = now_utc();
    Json metrics{{"elapsed_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()},
                 {"timing_scope", "parameter_validation_sampling_training_centroid_and_id_io_artifact_validation"}};
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
      metrics["process_peak_rss_bytes"] = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
      metrics["rss_scope"] = "process_lifetime_high_water_mark";
    }
    manifest.metrics_json = metrics.dump();
    const auto path = context.temporary_storage.stage_root / "kmeans-build-manifest.json";
    write_immutable_text(path, serialize_build_manifest(manifest));
    return {manifest.status, candidate, path};
  }
 private:
  cpp_main::SourceBuildEvidence evidence_;
};
}  // namespace

std::string now_utc() {
  const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

BuildManifest build_manifest(const cpp_main::SourceBuildEvidence& evidence,
                             const std::string& run_id) {
  auto manifest = cpp_main::make_build_manifest(evidence, run_id, now_utc(), now_utc());
  manifest.stage = "partition";
  manifest.implementation = {kImplementationId, kImplementationVersion};
  // Use actual build files; the frozen Baseline's illustrative build commands
  // name a different build directory and cannot describe this integration.
  manifest.toolchain.cmake_definitions.push_back("DISKANN_KMEANS_PARTITION=ON");
  manifest.toolchain.compile_commands.clear();
  const auto build_dir = std::filesystem::path(KMEANS_BUILD_DIR);
  for (const auto& unit : Json::parse(read_text(build_dir / "compile_commands.json"))) {
    if (unit.at("file").get<std::string>().find("/tests/") == std::string::npos)
      manifest.toolchain.compile_commands.push_back(unit.at("command").get<std::string>());
  }
  const auto executable = std::filesystem::read_symlink("/proc/self/exe");
  manifest.toolchain.link_commands = {read_text(executable.parent_path() / "CMakeFiles" /
      (executable.filename().string() + ".dir") / "link.txt")};
  manifest.parameters_json = Json{
      {"native_library_sha256", evidence.upstream_library_sha256},
      {"native_partition_source_sha256", sha256_file(evidence.upstream_source_dir / "src/partition.cpp")},
      {"runner_binary_sha256", sha256_file("/proc/self/exe")},
      {"wrapper_source", {{"repository", KMEANS_PROJECT_ROOT}, {"branch", KMEANS_PROJECT_BRANCH},
                          {"commit", KMEANS_PROJECT_COMMIT}, {"dirty_worktree", bool(KMEANS_PROJECT_DIRTY)},
                          {"dirty_source_checksum", KMEANS_PROJECT_DIRTY_SHA}}},
      {"assignment", "shard_data_into_clusters_only_ids<float>"},
      {"native_template_binding", "out_of_tree_explicit_instantiation_of_unmodified_partition.cpp"}
  }.dump();
  return manifest;
}

std::unique_ptr<PartitionStrategy> make_partition(cpp_main::SourceBuildEvidence evidence) {
  return std::make_unique<Partition>(std::move(evidence));
}
}  // namespace diskann_pipeline::diskann_kmeans
