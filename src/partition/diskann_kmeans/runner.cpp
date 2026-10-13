#include "partition/diskann_kmeans/stage_binding.hpp"

#include "baseline/cpp_main/stage_bindings.hpp"
#include "core/json_codec.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/pipeline.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>

namespace dp = diskann_pipeline;
namespace km = dp::diskann_kmeans;
namespace native = dp::cpp_main;
using Json = nlohmann::json;

namespace {
class Metrics final : public dp::MetricsSink {
 public:
  void event(const std::string&) override {}
  void increment(const std::string&, std::uint64_t) override {}
};
class NeverCancelled final : public dp::CancellationToken {
 public:
  bool is_cancelled() const noexcept override { return false; }
};
class L2 final : public dp::DistanceFunction {
 public:
  dp::DistanceSemantics semantics() const noexcept override { return dp::DistanceSemantics::kSquaredL2; }
  double evaluate(const std::vector<float>& a, const std::vector<float>& b) const override {
    if (a.size() != b.size()) throw std::runtime_error("L2 dimensions differ");
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
      const double delta = static_cast<double>(a[i]) - b[i];
      sum += delta * delta;
    }
    return sum;
  }
};
Json artifact(const dp::ArtifactRef& value) { return Json::parse(dp::serialize_artifact_ref(value)); }
}  // namespace

int main(int argc, char** argv) {
  if (argc != 6 || std::string(argv[1]) != "execute" ||
      std::string(argv[2]) != "--request" || std::string(argv[4]) != "--result") {
    std::cerr << "usage: kmeans_partition_runner execute --request CONFIG.json --result FILE\n";
    return 2;
  }
  try {
    std::ifstream input(argv[3]);
    if (!input) throw std::runtime_error("KMeans config is missing");
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto config = dp::detail::parse_json_strict(bytes);
    dp::detail::require_object_shape(config, {"config_version", "run_id", "workspace", "dataset", "partition"});
    dp::detail::require_object_shape(config.at("workspace"), {"root", "memory_budget_bytes", "thread_budget"});
    if (config.at("config_version") != "diskann-kmeans-run/v1") throw std::runtime_error("unsupported KMeans config");
    const auto run_id = config.at("run_id").get<std::string>();
    if (run_id.empty() || run_id == "." || run_id == ".." ||
        run_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
      throw std::runtime_error("run_id must be a single nonempty path component");
    const auto& budget = config.at("workspace").at("memory_budget_bytes");
    const auto& threads = config.at("workspace").at("thread_budget");
    if (!budget.is_number_integer() || budget.get<std::int64_t>() <= 0 ||
        !threads.is_number_integer() || threads.get<std::int64_t>() != 6)
      throw std::runtime_error("positive memory budget and frozen B0 thread budget 6 are required");
    const auto root = std::filesystem::absolute(config.at("workspace").at("root").get<std::string>());
    const auto result_path = std::filesystem::absolute(argv[5]);
    const auto admission = native::configured_source_build_admission();
    dp::reject_upstream_write_target(root, admission.repository_root);
    dp::reject_upstream_write_target(result_path, admission.repository_root);
    static_cast<void>(dp::resolve_confined_path(root, std::filesystem::relative(result_path, root)));
    if (std::filesystem::exists(result_path) || std::filesystem::exists(root / "manifests" / run_id))
      throw std::runtime_error("result/run already exists; use a new run_id and result path");
    const auto evidence = native::admit_source_and_build(admission);
    const auto workspace = dp::create_workspace(root);
    const auto dataset = dp::parse_artifact_ref(config.at("dataset").dump());
    native::CppMainVectorAccessor vectors(workspace, dataset);
    // Experiment admission policy, NOT a KMeans algorithm/data format limit.
    // Larger runs need a separately reviewed human approval path (AGENTS.md).
    if (vectors.dataset_identity().global_node_count > 1000000)
      throw std::runtime_error("AGENTS.md approval gate: this entry does not authorize datasets above SIFT1M");
    const auto parameters = km::parse_parameters(config.at("partition").dump(),
                                                  vectors.dataset_identity().global_node_count);
    auto build = km::build_manifest(evidence, run_id);
    Metrics metrics;
    NeverCancelled cancellation;
    L2 distance;
    dp::UnavailableCheckpointStore checkpoints;
    auto bootstrap = dp::create_temporary_storage(workspace, run_id + "-bootstrap");
    dp::StageContext context{workspace, budget.get<std::uint64_t>(), 6, bootstrap,
                             &vectors, &distance, &checkpoints, &metrics, &cancellation};
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    registry.register_partition(km::descriptor(), [evidence] { return km::make_partition(evidence); });
    registry.register_local_build(native::b0_descriptor(), native::make_b0_local_build, native::b0_input_admission);
    registry.register_merge(native::m0_descriptor(), native::make_m0_merge, native::m0_input_admission);
    registry.seal();
    dp::PipelineExecutionRequest request;
    request.run_id = run_id;
    request.dataset = dataset;
    request.partition_implementation_id = km::kImplementationId;
    request.local_implementation_id = native::kB0ImplementationId;
    request.merge_implementation_id = native::kM0ImplementationId;
    request.partition_parameters_json = Json{{"K", parameters.k},
                                             {"max_k_means_reps", parameters.max_k_means_reps}}.dump();
    request.local_parameters_json = R"({"L":32,"R":16,"alpha":1.2,"build_pq_bytes":0,"max_occlusion_size":750,"num_threads":6,"saturation":false,"use_opq":false})";
    request.merge_parameters_json = R"({"R":16,"use_filters":false})";
    request.source_config_json = Json{{"configuration_sha256", dp::sha256_bytes(bytes)},
                                      {"normalized_json_sha256", dp::sha256_bytes(config.dump())}}.dump();
    request.source = evidence.source;
    request.toolchain = build.toolchain;
    request.started_at = km::now_utc();
    request.ended_at = request.started_at;
    const auto execution = dp::PipelineExecutor(registry, workspace).execute(request, context);
    static_cast<void>(native::admit_source_and_build(admission));
    Json result{{"result_version", "diskann-kmeans-run/v1"}, {"run_id", run_id},
                {"outcome", dp::to_string(execution.status)},
                {"partition_implementation", km::kImplementationId},
                {"stage_invocations", {{"partition", execution.invocations.partition},
                                       {"local_build", execution.invocations.local_build},
                                       {"merge", execution.invocations.merge}}},
                {"local_graphs", Json::array()}, {"build_manifest_paths", Json::array()},
                {"validation_evidence_paths", Json::array()}};
    if (execution.partition_artifact) result["partition"] = artifact(*execution.partition_artifact);
    if (execution.artifact) result["global_graph"] = artifact(*execution.artifact);
    for (const auto& local : execution.local_artifacts) result["local_graphs"].push_back(artifact(local));
    for (const auto& path : execution.build_manifest_paths)
      result["build_manifest_paths"].push_back(std::filesystem::relative(path, root).generic_string());
    for (const auto& path : execution.validation_evidence_paths)
      result["validation_evidence_paths"].push_back(std::filesystem::relative(path, root).generic_string());
    if (execution.evaluation_report_path)
      result["evaluation_report_path"] = std::filesystem::relative(*execution.evaluation_report_path, root).generic_string();
    if (execution.failure) result["failure"] = {{"category", dp::to_string(execution.failure->category)},
                                                {"reason", execution.failure->reason}};
    dp::write_immutable_text(result_path, result.dump(2));
    return execution.status == dp::StageStatus::kSucceeded ? 0 : 4;
  } catch (const std::exception& error) {
    std::cerr << "KMeans runner rejected/failed: " << error.what() << '\n';
    return 3;
  }
}
