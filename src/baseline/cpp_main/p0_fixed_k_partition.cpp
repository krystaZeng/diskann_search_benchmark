#include "baseline/cpp_main/p0_fixed_k_partition.hpp"

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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>

namespace diskann_pipeline::cpp_main::fixed_k {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaxIterations = 10;
constexpr std::size_t kReplication = 2;
constexpr std::size_t kMaxTrainingSamples = 8388608;
std::string now_utc();
BuildManifest build_manifest(const SourceBuildEvidence&, const std::string&);

void require_version(const std::string& version) {
  if (version != kImplementationVersion)
    throw PipelineError(ErrorCategory::kUnsupportedVersion, "unsupported fixed K P0 implementation version");
}

std::size_t checked_float_elements(std::uint64_t rows, std::uint64_t columns) {
  const auto limit = std::min<std::uint64_t>(
      std::numeric_limits<std::size_t>::max() / sizeof(float),
      std::numeric_limits<std::ptrdiff_t>::max() / sizeof(float));
  if (!rows || !columns || rows > limit / columns)
    throw PipelineError(ErrorCategory::kPayloadLengthMismatch, "native matrix allocation exceeds addressable range");
  return static_cast<std::size_t>(rows * columns);
}

void check_native_sizes(NodeCount nodes, std::uint32_t dimension, std::uint32_t k) {
  if (!dimension || dimension > static_cast<std::uint64_t>(std::numeric_limits<MKL_INT>::max()))
    throw PipelineError(ErrorCategory::kSchemaInvalid, "native dimension exceeds positive MKL_INT range");
  // The native sampler first collects rows and then allocates S*d. N is a safe
  // upper bound before S is known; this is an overflow check, not a RAM limit.
  checked_float_elements(nodes, dimension);
  checked_float_elements(k, dimension);
  const auto batch = std::min<NodeCount>(nodes, 5000000);
  checked_float_elements(batch, k); // Native distance matrix.
  checked_float_elements(batch, kReplication); // uint32 nearest-center IDs.
}

void require_new_path(const std::filesystem::path& path) {
  if (std::filesystem::exists(path) || std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
    throw PipelineError(ErrorCategory::kPathEscape, "fixed K P0 output already occupied: " + path.string());
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw PipelineError(ErrorCategory::kPayloadMissing,
                                  "missing fixed K P0 input/build evidence: " + path.string());
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
    throw PipelineError(ErrorCategory::kPathEscape, "fixed K P0 candidate already exists");
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
    throw PipelineError(ErrorCategory::kCancelled, "fixed K P0 Partition cancelled between native calls");
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
    // The existing executor writes manifests/<run_id>/parameters/partition.json.
    const auto relative = std::filesystem::relative(parameter_path, context.workspace.manifest_root);
    static_cast<void>(resolve_confined_path(context.workspace.manifest_root, relative));
    const auto run_id = relative.begin()->string();
    const auto path = context.temporary_storage.stage_root / "p0-fixed-k-build-manifest.json";
    require_new_path(path);
    auto manifest = build_manifest(evidence_, run_id);
    cpp_main::CppMainVectorAccessor vectors(context.workspace, dataset);
    manifest.dataset = vectors.dataset_identity();
    manifest.input_artifacts = {dataset};
    manifest.resource_budget = {context.memory_budget_bytes, context.thread_budget};
    auto parameters = Json::parse(manifest.parameters_json);
    parameters["dataset_node_count"] = manifest.dataset.global_node_count;
    parameters["vector_dimension"] = manifest.dataset.vector_dimension;
    parameters["actual_training_sample_count"] = nullptr;
    const auto start = std::chrono::steady_clock::now();
    std::optional<ArtifactRef> candidate;
    try {
      if (context.thread_budget > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw PipelineError(ErrorCategory::kSchemaInvalid, "native thread budget exceeds int range");
      parameters["requested_parameters"] = detail::parse_json_strict(read_text(parameter_path));
      const auto config = parse_parameters(parameters.at("requested_parameters").dump(),
                                            manifest.dataset.global_node_count);
      validate_native_shape(manifest.dataset.global_node_count, manifest.dataset.vector_dimension, config.k);
      const double probability = sample_probability(manifest.dataset.global_node_count);
      parameters["effective_parameters"] = {{"K", config.k},
          {"max_k_means_reps", kMaxIterations}, {"k_base", kReplication},
          {"sample_probability", probability}, {"expected_training_samples_target", 256000},
          {"randomness", "native_random_device_no_seed_override"}};
      check_cancelled(context);
      const auto directory = context.temporary_storage.stage_root / "native";
      require_new_path(directory);
      if (!std::filesystem::create_directory(directory)) {
        throw PipelineError(ErrorCategory::kPathEscape, "native fixed K P0 output already exists");
      }
      const auto prefix = directory / "p0-fixed-k";
      ThreadScope threads(context.thread_budget);
      float* sampled = nullptr;
      std::size_t samples = 0, dimension = 0;
      context.metrics_sink->event("cpp_main.p0_fixed_k.gen_random_slice");
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
      checked_float_elements(samples, config.k);
      std::vector<float> pivots(checked_float_elements(config.k, dimension));
      context.metrics_sink->event("cpp_main.p0_fixed_k.kmeanspp_selecting_pivots");
      kmeans::kmeanspp_selecting_pivots(training.get(), samples, dimension, pivots.data(), config.k);
      check_cancelled(context);
      context.metrics_sink->event("cpp_main.p0_fixed_k.run_lloyds");
      const float residual = kmeans::run_lloyds(training.get(), samples, dimension,
          pivots.data(), config.k, kMaxIterations, nullptr, nullptr);
      if (!std::isfinite(residual)) {
        throw PipelineError(ErrorCategory::kSchemaInvalid, "native Lloyd residual is non-finite");
      }
      training.reset();
      check_cancelled(context);
      diskann::save_bin<float>(prefix.string() + "_centroids.bin", pivots.data(), config.k, dimension);
      cpp_main::validate_centroids(prefix.string() + "_centroids.bin", config.k, dimension);
      context.metrics_sink->event("cpp_main.p0_fixed_k.shard_data_into_clusters_only_ids");
      const auto status = shard_data_into_clusters_only_ids<float>(
          vectors.native_payload_path().string(), pivots.data(), config.k, dimension, kReplication, prefix.string());
      if (status != 0) throw PipelineError(ErrorCategory::kInternal, "native ID-only sharder failed");
      check_cancelled(context);
      context.metrics_sink->event("cpp_main.p0_fixed_k.validate_output");
      const auto counts = validate_native_output(prefix, manifest.dataset, config.k);
      parameters["actual_partition_count"] = counts.size();
      parameters["partition_memberships"] = counts;
      parameters["total_memberships"] = 2ULL * manifest.dataset.global_node_count;
      candidate = materialize(context.temporary_storage.stage_root, prefix,
                               manifest.dataset, dataset, counts);
      auto staging = context.workspace;
      staging.normal_root = context.temporary_storage.stage_root;
      const auto validation = Validator(staging).validate(*candidate, {vectors.validated_dataset()});
      if (!validation.valid()) {
        throw PipelineError(ErrorCategory::kSchemaInvalid, "fixed K P0 candidate failed PartitionArtifact/v1 validation");
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
    write_immutable_text(path, serialize_build_manifest(manifest));
    return {manifest.status, candidate, path};
  }
 private:
  cpp_main::SourceBuildEvidence evidence_;
};

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
  manifest.toolchain.cmake_definitions.clear();
  std::istringstream cache(read_text(std::filesystem::path(FIXED_K_BUILD_DIR) / "CMakeCache.txt"));
  std::string line;
  while (std::getline(cache, line)) {
    if (line.rfind("DISKANN_", 0) == 0 || line.rfind("CMAKE_CXX_FLAGS", 0) == 0)
      manifest.toolchain.cmake_definitions.push_back(line);
  }
  const std::vector<std::pair<std::string, std::string>> sources{
      {"src/baseline/cpp_main/p0_fixed_k_partition.cpp", FIXED_K_SOURCE_SHA},
      {"src/baseline/cpp_main/p0_fixed_k_partition.hpp", FIXED_K_HEADER_SHA},
      {"src/partition/diskann_kmeans/native_partition.cpp", FIXED_K_BRIDGE_SHA},
      {"contracts/partition/cpp-main-p0-fixed-k-v1/parameters.schema.json", FIXED_K_SCHEMA_SHA}};
  Json source_checksums = Json::object();
  for (const auto& source : sources) {
    if (sha256_file(std::filesystem::path(FIXED_K_PROJECT_ROOT) / source.first) != source.second)
      throw PipelineError(ErrorCategory::kSourceMismatch, "fixed K source changed since configuration: " + source.first);
    source_checksums[source.first] = source.second;
  }
  manifest.toolchain.compile_commands.clear();
  const auto build_dir = std::filesystem::path(FIXED_K_BUILD_DIR);
  for (const auto& unit : Json::parse(read_text(build_dir / "compile_commands.json"))) {
    if (unit.at("file").get<std::string>().find("/tests/") == std::string::npos)
      manifest.toolchain.compile_commands.push_back(unit.at("command").get<std::string>());
  }
  const auto executable = std::filesystem::read_symlink("/proc/self/exe");
  manifest.toolchain.link_commands = {read_text(executable.parent_path() / "CMakeFiles" /
      (executable.filename().string() + ".dir") / "link.txt")};
  manifest.parameters_json = Json{
      {"native_library_sha256", evidence.upstream_library_sha256},
      {"adapter_library_sha256", sha256_file(FIXED_K_LIBRARY)},
      {"native_bridge_library_sha256", sha256_file(FIXED_K_BRIDGE_LIBRARY)},
      {"source_files_sha256", source_checksums},
      {"native_partition_source_sha256", sha256_file(evidence.upstream_source_dir / "src/partition.cpp")},
      {"runner_binary_sha256", sha256_file("/proc/self/exe")},
      {"wrapper_source", {{"repository", FIXED_K_PROJECT_ROOT}, {"branch", FIXED_K_PROJECT_BRANCH},
                          {"commit", FIXED_K_PROJECT_COMMIT}, {"dirty_worktree", bool(FIXED_K_PROJECT_DIRTY)},
                          {"dirty_source_checksum", FIXED_K_PROJECT_DIRTY_SHA}}},
      {"assignment", "shard_data_into_clusters_only_ids<float>"},
      {"native_template_binding", "out_of_tree_explicit_instantiation_of_unmodified_partition.cpp"}
  }.dump();
  return manifest;
}

}  // namespace

ImplementationDescriptor descriptor() {
  return {kImplementationId, kImplementationVersion, StageKind::kPartition,
          {{{ArtifactType::kDataset, {"diskann-float32-dataset/v1"}, IdSpace::kNone}},
           {"dataset.diskann_float32_bin/v1"}, {"cancellation_token/v1", "metrics_sink/v1"}},
          {ArtifactType::kPartition, "partition-artifact/v1", IdSpace::kLocal,
           {"partition.centroids/v1", "partition.complete_local_to_global_map/v1",
            "partition.local_to_global_ascending/v1", "partition.overlap/v1"}},
          "contracts/partition/cpp-main-p0-fixed-k-v1/parameters.schema.json", false};
}

Parameters parse_parameters(const std::string& text, NodeCount nodes, const std::string& version) {
  require_version(version);
  const auto json = detail::parse_json_strict(text);
  detail::require_object_shape(json, {"K"});
  const auto& value = json.at("K");
  if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 2))
    throw PipelineError(ErrorCategory::kSchemaInvalid, "K must be an integer >= 2");
  const auto k = value.get<std::uint64_t>();
  if (!nodes || nodes > std::numeric_limits<std::uint32_t>::max() || k < 2 || k > nodes ||
      k > std::numeric_limits<std::uint32_t>::max())
    throw PipelineError(ErrorCategory::kSchemaInvalid, "K must be <= validated Dataset N and fit native uint32");
  return {static_cast<std::uint32_t>(k)};
}

double sample_probability(NodeCount nodes) {
  if (!nodes) throw PipelineError(ErrorCategory::kNodeCountMismatch, "empty Dataset");
  return std::min(1.0, 256000.0 / static_cast<double>(nodes));
}

void validate_native_shape(NodeCount nodes, std::uint32_t dimension, std::uint32_t k) {
  static_cast<void>(parse_parameters(Json{{"K", k}}.dump(), nodes));
  check_native_sizes(nodes, dimension, k);
}

void validate_training_sample_count(std::size_t samples, std::uint32_t k) {
  if (k < 2 || samples < k || samples > kMaxTrainingSamples)
    throw PipelineError(ErrorCategory::kNodeCountMismatch,
        "actual training sample count must be >= K and <= 8388608; no resampling");
}

std::vector<NodeCount> validate_native_output(const std::filesystem::path& prefix,
                                             const DatasetIdentity& dataset, std::uint32_t k) {
  static_cast<void>(parse_parameters(Json{{"K", k}}.dump(), dataset.global_node_count));
  validate_centroids(prefix.string() + "_centroids.bin", k, dataset.vector_dimension);
  std::set<std::string> expected{prefix.filename().string() + "_centroids.bin"};
  for (std::uint32_t i = 0; i < k; ++i)
    expected.insert(prefix.filename().string() + "_subshard-" + std::to_string(i) + "_ids_uint32.bin");
  for (const auto& file : std::filesystem::directory_iterator(prefix.parent_path())) {
    if (!file.is_regular_file() || file.is_symlink() || !expected.erase(file.path().filename().string()))
      throw PipelineError(ErrorCategory::kSchemaInvalid, "unexpected native fixed K output");
  }
  if (!expected.empty()) throw PipelineError(ErrorCategory::kPayloadMissing, "missing native fixed K output");
  std::vector<std::uint8_t> ownership(static_cast<std::size_t>(dataset.global_node_count), 0);
  std::vector<NodeCount> counts;
  std::uint64_t total = 0;
  for (std::uint32_t i = 0; i < k; ++i) {
    const auto path = prefix.string() + "_subshard-" + std::to_string(i) + "_ids_uint32.bin";
    std::ifstream input(path, std::ios::binary);
    std::uint32_t rows = 0, dimension = 0;
    input.read(reinterpret_cast<char*>(&rows), sizeof(rows));
    input.read(reinterpret_cast<char*>(&dimension), sizeof(dimension));
    if (!input || !rows || dimension != 1)
      throw PipelineError(ErrorCategory::kMappingIncomplete, "missing, empty or malformed fixed K shard " + std::to_string(i));
    const auto ids = validate_partition_id_map(path, rows, dataset.global_node_count);
    if (!std::is_sorted(ids.begin(), ids.end()))
      throw PipelineError(ErrorCategory::kMappingIncomplete, "fixed K map is not ascending");
    for (auto id : ids) {
      if (ownership[id] == kReplication)
        throw PipelineError(ErrorCategory::kMappingIncomplete, "fixed K node has more than two memberships");
      ++ownership[id];
    }
    // N and each map count fit uint32; the sum is bounded by K*N < 2^64.
    total += static_cast<std::uint64_t>(rows);
    counts.push_back(rows);
  }
  if (total != 2ULL * dataset.global_node_count ||
      std::any_of(ownership.begin(), ownership.end(), [](std::uint8_t n) { return n != kReplication; }))
    throw PipelineError(ErrorCategory::kMappingIncomplete, "fixed K must assign every node to exactly two different blocks");
  return counts;
}

std::unique_ptr<PartitionStrategy> make_partition(SourceBuildEvidence evidence, const std::string& version) {
  require_version(version);
  return std::make_unique<Partition>(std::move(evidence));
}
}  // namespace diskann_pipeline::cpp_main::fixed_k
