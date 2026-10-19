#include "baseline/cpp_main/stage_bindings.hpp"
#include "baseline/cpp_main/source_build_identity.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "validation/parameter_validator.hpp"
#ifdef DISKANN_KMEANS_PARTITION
#include "partition/diskann_kmeans/stage_binding.hpp"
#endif
#ifdef DISKANN_GP_ANN_OGP
#include "papers/gp_ann/ogp/stage_binding.hpp"
#endif
#ifdef DISKANN_XN_DESCENT
#include "papers/xn_graph/xn_descent/stage_binding.hpp"
#include "papers/xn_graph/xn_descent/parameters.hpp"
#endif
#ifdef DISKANN_HNSW_MERGER
#include "papers/hnsw_merger/vamana/stage_binding.hpp"
#endif

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <nlohmann/json.hpp>

namespace dp = diskann_pipeline;
namespace native = dp::cpp_main;
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
constexpr const char* kVersion = "module-composition-run/v1";
class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string& name) override { events.push_back(name); }
  void increment(const std::string& name, std::uint64_t amount) override {
    if (name.find("cpp_main.p0.v1_1.") == 0 || name.find("cpp_main.b0.v1_1.") == 0)
      counters[name] = counters.value(name, std::uint64_t{0}) + amount;
  }
  std::vector<std::string> events;
  Json counters = Json::object();
};
class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};
class SquaredL2 final : public dp::DistanceFunction {
 public:
  dp::DistanceSemantics semantics() const noexcept override { return dp::DistanceSemantics::kSquaredL2; }
  double evaluate(const std::vector<float>& a, const std::vector<float>& b) const override {
    if (a.size() != b.size()) throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid, "L2 dimensions differ");
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      const double delta = static_cast<double>(a[i]) - b[i];
      sum += delta * delta;
    }
    return sum;
  }
};
[[noreturn]] void reject(const std::string& reason) {
  throw dp::PipelineError(dp::ErrorCategory::kSchemaInvalid, reason);
}
std::string read(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) reject("cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::string now() {
  const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm value{};
  gmtime_r(&time, &value);
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}
std::uint64_t positive(const Json& value, const char* name, std::uint64_t maximum) {
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0 ||
      value.get<std::uint64_t>() > maximum) reject(std::string(name) + " must be a positive integer in range");
  return value.get<std::uint64_t>();
}
dp::SourceIdentity project_source(const std::string& repository = COMPOSITION_SOURCE_REPOSITORY) {
  dp::SourceIdentity source{repository, COMPOSITION_SOURCE_BRANCH, COMPOSITION_SOURCE_COMMIT,
                            COMPOSITION_SOURCE_DIRTY != 0, std::nullopt};
  if (source.dirty_worktree) source.dirty_source_checksum = COMPOSITION_SOURCE_DIRTY_CHECKSUM;
  return source;
}
Json reference(const dp::ArtifactRef& value) { return Json::parse(dp::serialize_artifact_ref(value)); }
Json file_identity(const fs::path& value) {
  return {{"path", value.generic_string()}, {"sha256", dp::sha256_file(value)}};
}

// Explicit bindings use the existing Registry; there is no combination table.
// Admission of external source/build dependencies is lazy, for the selected P.
void register_modules(dp::ImplementationRegistry& registry, const std::string& partition_id,
                      const native::SourceBuildEvidence& native_evidence,
                      dp::SourceIdentity& partition_source) {
  native::register_cpp_main_stage_bindings(registry);
  registry.register_partition(native::p0_descriptor_v1_1(), native::make_p0_partition_v1_1);
  registry.register_local_build(native::b0_descriptor_v1_1(), native::make_b0_local_build_v1_1,
                                native::b0_input_admission);
#ifdef DISKANN_KMEANS_PARTITION
  registry.register_partition(dp::diskann_kmeans::descriptor(), [native_evidence] {
    return dp::diskann_kmeans::make_partition(native_evidence);
  });
#else
  static_cast<void>(native_evidence);
#endif
#ifdef DISKANN_GP_ANN_OGP
  // Do not require GP-ANN's executable/source when an unrelated P is selected.
  if (partition_id == dp::gp_ann_ogp::kImplementationId) {
    const auto evidence = dp::gp_ann_ogp::admit_source_and_build(
        dp::gp_ann_ogp::configured_source_build_admission());
    partition_source = evidence.source;
    registry.register_partition(dp::gp_ann_ogp::descriptor(), [evidence] {
      return dp::gp_ann_ogp::make_ogp_partition(evidence);
    });
  }
#else
  static_cast<void>(partition_id);
  static_cast<void>(partition_source);
#endif
#ifdef DISKANN_XN_DESCENT
  registry.register_local_build(dp::xn_descent::descriptor(), dp::xn_descent::make_xn_descent_local_build,
                                dp::xn_descent::input_admission);
#endif
#ifdef DISKANN_HNSW_MERGER
  registry.register_merge(dp::hnsw_merger_vamana::descriptor(), dp::hnsw_merger_vamana::make_m1_merge,
                          dp::hnsw_merger_vamana::input_admission);
#endif
  registry.seal();
}

void validate_parameters(const dp::ImplementationDescriptor& descriptor, Json& parameters,
                         dp::NodeCount count, std::uint32_t threads) {
  if (parameters.contains("partition_id")) reject("partition_id is executor-owned");
  dp::detail::validate_implementation_parameters(descriptor, parameters.dump(), COMPOSITION_ROOT);
  const auto& id = descriptor.implementation_id;
  if (id == native::kP0ImplementationId)
    static_cast<void>(native::parse_p0_parameters(parameters.dump(), count, descriptor.implementation_version));
  else if (id == native::kB0ImplementationId) {
    static_cast<void>(native::parse_b0_parameters(parameters.dump(), descriptor.implementation_version));
    if (threads != parameters.at("num_threads")) reject("B0 requires thread_budget equal to frozen num_threads=6");
  } else if (id == native::kM0ImplementationId) native::validate_m0_parameters(parameters.dump());
#ifdef DISKANN_KMEANS_PARTITION
  else if (id == dp::diskann_kmeans::kImplementationId) {
    const auto parsed = dp::diskann_kmeans::parse_parameters(parameters.dump(), count);
    parameters = {{"K", parsed.k}, {"max_k_means_reps", parsed.max_k_means_reps}};
  }
#endif
#ifdef DISKANN_GP_ANN_OGP
  else if (id == dp::gp_ann_ogp::kImplementationId) {
    static_cast<void>(dp::gp_ann_ogp::parse_parameters(parameters.dump(), count));
  }
#endif
#ifdef DISKANN_XN_DESCENT
  else if (id == dp::xn_descent::kImplementationId) {
    parameters = Json::parse(dp::xn_descent::canonical_parameters_json(
        dp::xn_descent::parse_parameters(parameters.dump())));
  }
#endif
#ifdef DISKANN_HNSW_MERGER
  else if (id == dp::hnsw_merger_vamana::kImplementationId) {
    static_cast<void>(dp::hnsw_merger_vamana::parse_parameters(parameters.dump()));
  }
#endif
  else reject("no parameter binding for implementation " + id);
}

// Derived execution parameters are provenance, not extra configuration inputs.
Json native_effective_parameters(const Json& config, dp::NodeCount nodes) {
  Json result = Json::object();
  const auto& p = config.at("partition");
  if (p.at("implementation_id") == native::kP0ImplementationId &&
      p.at("implementation_version") == "1.1.0") {
    const auto parsed = native::parse_p0_parameters(p.at("parameters").dump(), nodes, "1.1.0");
    result["partition"] = {{"graph_degree", parsed.local_degree},
        {"R", parsed.r}, {"build_DRAM_budget_gib", parsed.build_dram_budget_gib},
        {"replication_factor", parsed.replication_factor}, {"sample_probability", parsed.sample_probability}};
  }
  const auto& b = config.at("local_build");
  if (b.at("implementation_id") == native::kB0ImplementationId &&
      b.at("implementation_version") == "1.1.0") {
    const auto parsed = native::parse_b0_parameters(b.at("parameters").dump(), "1.1.0");
    result["local_build"] = {{"R", parsed.r}, {"L", parsed.l},
        {"local_degree", parsed.local_degree}, {"filter_list_size", parsed.l},
        {"num_threads", 6}, {"alpha", 1.2}, {"max_occlusion_size", 750},
        {"build_pq_bytes", 0}, {"use_opq", false}, {"saturation", false}};
  }
  return result;
}

dp::BuildManifest entry_manifest(const native::SourceBuildEvidence& evidence, const Json& config,
                                const dp::DatasetIdentity& dataset, const dp::ArtifactRef& input) {
  auto build = native::make_build_manifest(evidence, config.at("run_id"), now(), now());
  build.implementation = {"module_composition_runner_v1", "1.0.0"};
  build.source = project_source();
  build.dataset = dataset;
  build.input_artifacts = {input};
  build.resource_budget = {config.at("workspace").at("memory_budget_bytes"),
                           config.at("workspace").at("thread_budget")};
  const fs::path root(COMPOSITION_BUILD);
  const auto identity_path = root / "src/module_composition/source-identity.json";
  if (dp::sha256_file(identity_path) != COMPOSITION_IDENTITY_SHA) reject("entry source identity file differs from compiled identity");
  const auto sources = dp::detail::parse_json_strict(read(identity_path));
  for (const auto& item : sources.items()) {
    if (dp::sha256_file(fs::path(COMPOSITION_ROOT) / item.key()) != item.value())
      reject("source changed since entry configuration: " + item.key());
  }
  const auto executable = fs::canonical("/proc/self/exe");
  const auto commands = dp::detail::parse_json_strict(read(root / "compile_commands.json"));
  build.toolchain.compile_commands.clear();
  for (const auto& command : commands) {
    if (command.at("file").get<std::string>().find("/src/module_composition/") != std::string::npos)
      build.toolchain.compile_commands.push_back(command.at("command"));
  }
  if (build.toolchain.compile_commands.empty()) reject("entry compile commands missing");
  build.toolchain.link_commands = {read(executable.parent_path() / "CMakeFiles/module_composition_runner.dir/link.txt")};
  Json libraries = Json::array({file_identity(COMPOSITION_BASELINE_LIBRARY)});
#ifdef DISKANN_KMEANS_PARTITION
  if (config.at("partition").at("implementation_id") == dp::diskann_kmeans::kImplementationId)
    libraries.push_back(file_identity(DISKANN_KMEANS_PARTITION_LIBRARY));
#endif
#ifdef DISKANN_GP_ANN_OGP
  if (config.at("partition").at("implementation_id") == dp::gp_ann_ogp::kImplementationId)
    libraries.push_back(file_identity(DISKANN_GP_ANN_OGP_LIBRARY));
#endif
#ifdef DISKANN_XN_DESCENT
  if (config.at("local_build").at("implementation_id") == dp::xn_descent::kImplementationId)
    libraries.push_back(file_identity(DISKANN_XN_DESCENT_LIBRARY));
#endif
#ifdef DISKANN_HNSW_MERGER
  if (config.at("merge").at("implementation_id") == dp::hnsw_merger_vamana::kImplementationId)
    libraries.push_back(file_identity(DISKANN_HNSW_MERGER_LIBRARY));
#endif
  std::istringstream cache(read(root / "CMakeCache.txt"));
  std::string line;
  while (std::getline(cache, line)) {
    if (line.rfind("DISKANN_", 0) == 0 || line.rfind("CMAKE_CXX_FLAGS", 0) == 0)
      build.toolchain.cmake_definitions.push_back(line);
  }
  build.parameters_json = Json{{"effective_config", config}, {"runner_binary", file_identity(executable)},
                               {"source_files_sha256", sources},
                               {"native_library", file_identity(evidence.upstream_library_file)},
                               {"selected_module_libraries", libraries},
                               {"configured_compile_database", commands},
                               {"build_cache", file_identity(root / "CMakeCache.txt")}}.dump();
  const auto effective = native_effective_parameters(config, dataset.global_node_count);
  if (!effective.empty()) {
    auto parameters = Json::parse(build.parameters_json);
    parameters["native_effective_parameters"] = effective;
    build.parameters_json = parameters.dump();
  }
  // This describes the already built entry, not certification of this run.
  build.status = dp::StageStatus::kSucceeded;
  return build;
}
}  // namespace

int main(int argc, char** argv) {
  Json result{{"result_version", kVersion}, {"outcome", "FAILED"},
              {"stage_invocations", {{"partition", 0}, {"local_build", 0}, {"merge", 0}}}};
  std::optional<fs::path> safe_result;
  try {
    if (argc != 6 || std::string(argv[1]) != "execute" || std::string(argv[2]) != "--request" ||
        std::string(argv[4]) != "--result") reject("usage: module_composition_runner execute --request CONFIG.json --result FILE");
    const auto bytes = read(argv[3]);
    auto config = dp::detail::parse_json_strict(bytes);
    // Establish a safe new result destination before other request checks. An
    // unparseable request or unsafe destination can only fail on stderr.
    const fs::path root(config.at("workspace").at("root").get<std::string>());
    if (!root.is_absolute()) reject("workspace.root must be absolute");
    dp::reject_upstream_write_target(root, COMPOSITION_ROOT);
    for (const auto* directory : {"normal", "temporary", "manifests", "reports", "quarantine"}) {
      const auto path = dp::resolve_confined_path(root, directory);
      dp::reject_upstream_write_target(path, COMPOSITION_ROOT);
    }
    const auto output = fs::absolute(argv[5]);
    const auto confined = dp::resolve_confined_path(root / "reports", fs::relative(output, root / "reports"));
    dp::reject_upstream_write_target(confined, COMPOSITION_ROOT);
    if (confined.parent_path() != fs::weakly_canonical(root / "reports"))
      reject("result must be a direct child file of workspace/reports");
    if (fs::exists(confined) || fs::is_symlink(fs::symlink_status(confined))) reject("result path already exists");
    safe_result = confined;
    dp::detail::require_object_shape(config, {"config_version", "run_id", "workspace", "dataset", "partition", "local_build", "merge"});
    dp::detail::require_object_shape(config.at("workspace"), {"root", "memory_budget_bytes", "thread_budget"});
    if (config.at("config_version") != kVersion) reject("unsupported config_version");
    const auto run_id = config.at("run_id").get<std::string>();
    if (run_id.empty() || run_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
      reject("run_id must be a single nonempty alphanumeric, '-' or '_' component");
    result["run_id"] = run_id;
    if (confined.filename() == run_id + "-evaluation.json") {
      safe_result.reset();
      reject("result path is reserved for the pipeline EvaluationReport");
    }
    const auto memory = positive(config.at("workspace").at("memory_budget_bytes"), "memory_budget_bytes", std::numeric_limits<std::uint64_t>::max());
    const auto threads = static_cast<std::uint32_t>(positive(config.at("workspace").at("thread_budget"), "thread_budget", std::numeric_limits<std::uint32_t>::max()));
    const auto workspace = dp::create_workspace(root);
    for (const auto& path : {workspace.manifest_root / run_id, workspace.normal_root / run_id,
                            workspace.temporary_root / (run_id + "-bootstrap"),
                            workspace.temporary_root / (run_id + "-partition"),
                            workspace.temporary_root / (run_id + "-merge"),
                            workspace.report_root / (run_id + "-evaluation.json")})
      if (fs::exists(path) || fs::is_symlink(fs::symlink_status(path))) reject("run_id already occupied: " + run_id);
    const auto local_prefix = run_id + "-local-";
    for (const auto& item : fs::directory_iterator(workspace.temporary_root)) {
      const auto name = item.path().filename().string();
      if (name.rfind(local_prefix, 0) == 0) {
        const auto suffix = name.substr(local_prefix.size());
        if (!suffix.empty() && suffix.find_first_not_of("0123456789") == std::string::npos)
          reject("run_id already occupied: " + run_id);
      }
    }
    const auto run_root = dp::resolve_confined_path(workspace.manifest_root, run_id);
    if (!fs::create_directory(run_root)) reject("run_id already occupied: " + run_id);
    dp::write_immutable_text(run_root / "request.json", bytes);
    for (const auto* stage : {"partition", "local_build", "merge"}) {
      const auto& selected = config.at(stage);
      dp::detail::require_object_shape(selected, {"implementation_id", "implementation_version", "parameters"});
      if (!selected.at("implementation_id").is_string() || !selected.at("implementation_version").is_string() ||
          !selected.at("parameters").is_object()) reject(std::string(stage) + " identity/version must be strings and parameters an object");
    }
    const auto dataset = dp::parse_artifact_ref(config.at("dataset").dump());
    // Scale comes from the fully validated Artifact and native payload.
    // Larger experiments still require the separate AGENTS.md human approval.
    native::CppMainVectorAccessor vectors(workspace, dataset);
    const auto native_admission = native::configured_source_build_admission();
    const auto evidence = native::admit_source_and_build(native_admission);
    dp::StageSourceIdentities sources{evidence.source, evidence.source, evidence.source};
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    register_modules(registry, config.at("partition").at("implementation_id"), evidence, sources.partition);
    const std::vector<std::pair<std::string, dp::StageKind>> stages{
        {"partition", dp::StageKind::kPartition}, {"local_build", dp::StageKind::kLocalBuild}, {"merge", dp::StageKind::kMerge}};
    for (const auto& stage : stages) {
      auto& selected = config.at(stage.first);
      try {
        const auto& descriptor = registry.resolve_descriptor(selected.at("implementation_id"), selected.at("implementation_version"), stage.second);
        validate_parameters(descriptor, selected.at("parameters"), vectors.dataset_identity().global_node_count, threads);
      } catch (const dp::PipelineError& error) {
        throw dp::PipelineError(error.category(), stage.first + " [" +
            selected.at("implementation_id").get<std::string>() + "@" +
            selected.at("implementation_version").get<std::string>() + "]: " + error.what());
      }
    }
#ifdef DISKANN_XN_DESCENT
    if (config.at("local_build").at("implementation_id") == dp::xn_descent::kImplementationId) sources.local_build = project_source();
#endif
#ifdef DISKANN_HNSW_MERGER
    if (config.at("merge").at("implementation_id") == dp::hnsw_merger_vamana::kImplementationId) sources.merge = project_source(COMPOSITION_M1_REPOSITORY);
#endif
    auto build = entry_manifest(evidence, config, vectors.dataset_identity(), dataset);
    const auto build_path = run_root / "entry-build.json";
    dp::write_immutable_text(build_path, dp::serialize_build_manifest(build));
    result["entry_build_manifest"] = {{"path", fs::relative(build_path, root).generic_string()}, {"sha256", dp::sha256_file(build_path)}};
    Metrics metrics;
    NeverCancelled cancellation;
    SquaredL2 distance;
    dp::UnavailableCheckpointStore checkpoints;
    auto bootstrap = dp::create_temporary_storage(workspace, run_id + "-bootstrap");
    dp::StageContext context{workspace, memory, threads, bootstrap, &vectors, &distance, &checkpoints, &metrics, &cancellation};
    dp::PipelineExecutionRequest request;
    request.run_id = run_id;
    request.dataset = dataset;
    request.partition_implementation_id = config.at("partition").at("implementation_id");
    request.partition_implementation_version = config.at("partition").at("implementation_version");
    request.local_implementation_id = config.at("local_build").at("implementation_id");
    request.local_implementation_version = config.at("local_build").at("implementation_version");
    request.merge_implementation_id = config.at("merge").at("implementation_id");
    request.merge_implementation_version = config.at("merge").at("implementation_version");
    request.partition_parameters_json = config.at("partition").at("parameters").dump();
    request.local_parameters_json = config.at("local_build").at("parameters").dump();
    request.merge_parameters_json = config.at("merge").at("parameters").dump();
    request.source_config_json = Json{{"configuration_sha256", dp::sha256_bytes(bytes)},
                                      {"effective_config", config}, {"entry_build_manifest", result.at("entry_build_manifest")}}.dump();
    const auto native_effective = native_effective_parameters(config, vectors.dataset_identity().global_node_count);
    if (!native_effective.empty()) {
      auto provenance = Json::parse(request.source_config_json);
      provenance["native_effective_parameters"] = native_effective;
      request.source_config_json = provenance.dump();
    }
    request.source = evidence.source;
    request.stage_sources = sources;
    request.toolchain = build.toolchain;
    request.started_at = request.ended_at = now();
    const auto execution = dp::PipelineExecutor(registry, workspace).execute(request, context);
    result["outcome"] = dp::to_string(execution.status);
    result["effective_config"] = config;
    result["stage_invocations"] = {{"partition", execution.invocations.partition}, {"local_build", execution.invocations.local_build}, {"merge", execution.invocations.merge}};
    result["stage_order"] = execution.stage_order;
    result["events"] = metrics.events;
    if (!metrics.counters.empty()) result["native_parameter_observations"] = metrics.counters;
    result["local_graphs"] = Json::array();
    if (execution.partition_artifact) result["partition"] = reference(*execution.partition_artifact);
    if (execution.artifact) result["global_graph"] = reference(*execution.artifact);
    for (const auto& value : execution.local_artifacts) result["local_graphs"].push_back(reference(value));
    for (const auto& group : {std::make_pair("build_manifests", execution.build_manifest_paths),
                              std::make_pair("validation_evidence", execution.validation_evidence_paths)}) {
      result[group.first] = Json::array();
      for (const auto& path : group.second)
        result[group.first].push_back({{"path", fs::relative(path, root).generic_string()}, {"sha256", dp::sha256_file(path)}});
    }
    if (execution.evaluation_report_path)
      result["evaluation_report_path"] = fs::relative(*execution.evaluation_report_path, root).generic_string();
    if (execution.failure) result["failure"] = {{"category", dp::to_string(execution.failure->category)}, {"reason", execution.failure->reason}};
    static_cast<void>(native::admit_source_and_build(native_admission));
    dp::write_immutable_text(*safe_result, result.dump(2) + "\n");
    if (execution.status != dp::StageStatus::kSucceeded) std::cerr << result.dump() << '\n';
    return execution.status == dp::StageStatus::kSucceeded ? 0 : 4;
  } catch (const std::exception& error) {
    result["outcome"] = "FAILED";
    const auto* pipeline_error = dynamic_cast<const dp::PipelineError*>(&error);
    result["failure"] = {{"category", pipeline_error ? dp::to_string(pipeline_error->category()) : "invalid_request"}, {"reason", error.what()}};
    if (safe_result) {
      try { dp::write_immutable_text(*safe_result, result.dump(2) + "\n"); }
      catch (const std::exception& write_error) { result["result_write_failure"] = write_error.what(); }
    }
    std::cerr << result.dump() << '\n';
    return 2;
  }
}
