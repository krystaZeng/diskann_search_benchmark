#include "xn_descent_test_support.hpp"

#include "baseline/cpp_main/native_formats.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/validation.hpp"

#include "diskann_pipeline/compatibility.hpp"
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <set>
#include <atomic>
#include <cmath>

namespace dp = diskann_pipeline;
namespace xn = diskann_pipeline::xn_descent;
namespace native = diskann_pipeline::cpp_main;
namespace fixture = diskann_pipeline::xn_descent::test;

namespace {

constexpr const char* kAscending = "partition.local_to_global_ascending/v1";

class EuclideanDistance final : public dp::DistanceFunction {
 public:
  explicit EuclideanDistance(bool known = true) : known_(known) {}
  dp::DistanceSemantics semantics() const noexcept override {
    return known_ ? dp::DistanceSemantics::kEuclideanL2
                  : dp::DistanceSemantics::kUnknown;
  }
  double evaluate(const std::vector<float>& a,
                  const std::vector<float>& b) const override {
    ++calls;
    return std::sqrt(squared_.evaluate(a, b));
  }
  mutable std::atomic<std::size_t> calls{0};
 private:
  fixture::baseline_fixture::TestDistance squared_;
  bool known_;
};

// This XN fixture deliberately preserves local IDs in non-ascending global
// order. Do not change the shared Baseline fixture or infer/repair capabilities
// in the validator: only this positive fixture omits the ascending promise.
dp::ArtifactRef materialize_unordered_partition(
    const dp::WorkspaceRef& workspace,
    const fixture::baseline_fixture::NativeDatasetFixture& dataset,
    const std::vector<dp::GlobalNodeId>& map) {
  auto reference = fixture::baseline_fixture::materialize_partition(
      workspace, dataset, {map});
  const auto path = workspace.normal_root / reference.metadata_path;
  auto metadata = fixture::baseline_fixture::read_json(path);
  auto capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  capabilities.erase(
      std::remove(capabilities.begin(), capabilities.end(), kAscending),
      capabilities.end());
  metadata["capabilities"] = capabilities;
  reference.aggregate_checksum =
      fixture::baseline_fixture::aggregate_for(metadata);
  metadata["aggregate_checksum"] = reference.aggregate_checksum;
  fixture::baseline_fixture::write_text(path, metadata.dump());
  return reference;
}

void test_false_ascending_claim() {
  fixture::baseline_fixture::TemporaryDirectory temporary("xn-false-ascending");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture dataset(workspace, 3, 1);
  const std::vector<dp::GlobalNodeId> map{2, 1, 0};
  // Independent negative fixture: retain the shared fixture's false claim.
  const auto partition = fixture::baseline_fixture::materialize_partition(
      workspace, dataset, {map});
  const auto metadata_path = workspace.normal_root / partition.metadata_path;
  const auto metadata = fixture::baseline_fixture::read_json(metadata_path);
  const auto capabilities =
      metadata.at("capabilities").get<std::vector<std::string>>();
  DP_EXPECT_TRUE(std::find(capabilities.begin(), capabilities.end(), kAscending) !=
                 capabilities.end());
  DP_EXPECT_EQ(partition.aggregate_checksum,
               fixture::baseline_fixture::aggregate_for(metadata));
  const auto map_path = workspace.normal_root / "partition/map-0.bin";
  DP_EXPECT_EQ(native::validate_partition_id_map(map_path, 3, 3), map);
  const auto metadata_before = dp::sha256_file(metadata_path);
  const auto map_before = fixture::read_bytes(map_path);

  const auto dataset_validation =
      dp::Validator(workspace).validate(dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  if (!dataset_validation.valid()) return;
  const auto validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(!validation.valid());
  DP_EXPECT_TRUE(!validation.validated_artifact.has_value());
  DP_EXPECT_EQ(validation.evidence.result, dp::ValidationResult::kInvalid);
  DP_EXPECT_TRUE(validation.evidence.failure_category.has_value());
  if (validation.evidence.failure_category) {
    DP_EXPECT_EQ(*validation.evidence.failure_category,
                 dp::ErrorCategory::kMappingIncomplete);
  }
  DP_EXPECT_TRUE(std::any_of(
      validation.evidence.checks.begin(), validation.evidence.checks.end(),
      [](const auto& check) {
        return !check.passed &&
               check.message.find("ascending capability") != std::string::npos;
      }));

  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::baseline_fixture::TestContext environment(workspace,
                                                     "xn-false-ascending");
  environment.context.vector_accessor = &vectors;
  const auto parameters = fixture::baseline_fixture::write_parameters(
      environment.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
  const auto result = xn::make_xn_descent_local_build()->build(
      partition, 0, {parameters}, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!result.output_candidate.has_value());
  DP_EXPECT_TRUE(!environment.metrics.saw("xn_descent.initialized"));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      environment.context.temporary_storage.stage_root /
      "candidate/artifact.json"));
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.build_manifest_path));
  if (!std::filesystem::is_regular_file(result.build_manifest_path)) return;
  const auto manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(result.build_manifest_path).dump());
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(manifest.output_artifacts.empty());
  DP_EXPECT_TRUE(manifest.failure.has_value());
  if (manifest.failure) {
    DP_EXPECT_EQ(manifest.failure->category, dp::ErrorCategory::kSourceMismatch);
    DP_EXPECT_TRUE(
        manifest.failure->reason.find("PartitionArtifact failed validation") !=
        std::string::npos);
  }
  DP_EXPECT_EQ(dp::sha256_file(metadata_path), metadata_before);
  DP_EXPECT_EQ(fixture::read_bytes(map_path), map_before);
  std::cout << "XN_MAP_FALSE_ASCENDING validator_valid=" << validation.valid()
            << " b1=" << dp::to_string(result.status)
            << " candidate=" << result.output_candidate.has_value() << '\n';
}


void test_input_admission() {
  namespace base = fixture::baseline_fixture;
  for (const std::string kind : {"native", "accessor", "json-map"}) {
    base::TemporaryDirectory temporary("xn-admission-" + kind);
    auto workspace = dp::create_workspace(temporary.path / "workspace");
    base::NativeDatasetFixture dataset(workspace, 8, 4);
    const std::vector<dp::GlobalNodeId> map{0,1,2,3,4,5,6,7};
    auto partition = base::materialize_partition(workspace, dataset, {map});
    if (kind == "json-map") partition = base::json_payload(workspace, partition,
        "partition/0/local_to_global_map", {{"local_to_global", map}});
    auto vd = dp::Validator(workspace).validate(dataset.reference);
    auto vp = dp::Validator(workspace).validate(partition, {*vd.validated_artifact});
    DP_EXPECT_TRUE(vp.valid());
    if (!vp.valid()) throw std::runtime_error("invalid admission fixture");
    native::CppMainVectorAccessor vectors(workspace, dataset.reference);
    base::DelegatingVectors generic(vectors);
    base::TestContext environment(workspace, "xn-admission");
    environment.context.vector_accessor = kind == "accessor" ? static_cast<dp::VectorAccessor*>(&generic) : &vectors;
    const auto parameter_path = base::write_parameters(environment.context.temporary_storage,
        fixture::effective_parameters_json("semantic-small", 4, 1, 1.0, 100));
    std::size_t factories = 0, stages = 0;
    dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kProduction);
    registry.register_local_build(xn::descriptor(), [&] {
      ++factories;
      return std::make_unique<base::ObservedLocal>(xn::make_xn_descent_local_build(), stages);
    }, xn::input_admission);
    auto compatible = dp::CompatibilityChecker{}.check(xn::descriptor(), {
      {dp::ArtifactType::kDataset, dataset.reference.format_version, dp::IdSpace::kNone, vd.validated_artifact->capabilities()},
      {dp::ArtifactType::kPartition, partition.format_version, dp::IdSpace::kLocal, vp.validated_artifact->capabilities()}},
      dp::runtime_service_capabilities(environment.context, vd.validated_artifact->dataset()));
    if (compatible.compatible) compatible = registry.check_known_inputs(
        xn::descriptor(), {*vd.validated_artifact, *vp.validated_artifact}, environment.context);
    dp::StageResult result{dp::StageStatus::kFailed, std::nullopt, {}};
    if (compatible.compatible) result = registry.resolve_local_build_factory(xn::kImplementationId, "1.0.0")()->build(partition, 0, {parameter_path}, environment.context);
    std::cout << "ADMISSION XN " << kind << " valid=1 preflight=" << compatible.compatible
              << " factories=" << factories << " stages=" << stages
              << " algorithm=" << environment.metrics.saw("xn_descent.initialized")
              << " status=" << dp::to_string(result.status) << std::endl;
    DP_EXPECT_EQ(result.status, kind == "native" ? dp::StageStatus::kSucceeded : dp::StageStatus::kFailed);
    DP_EXPECT_EQ(factories, kind == "native" ? std::size_t{1} : std::size_t{0});
    DP_EXPECT_EQ(stages, factories);
    DP_EXPECT_EQ(registry.check_known_inputs(xn::descriptor(), {*vd.validated_artifact}, environment.context).compatible,
                 kind != "accessor");
    if (kind != "native") {
      base::TestContext direct(workspace, "direct-admission-defense");
      direct.context.vector_accessor = environment.context.vector_accessor;
      const auto defended = xn::make_xn_descent_local_build()->build(
          partition, 0, {parameter_path}, direct.context);
      DP_EXPECT_EQ(defended.status, dp::StageStatus::kFailed);
      DP_EXPECT_TRUE(!direct.metrics.saw("xn_descent.initialized"));
    }
  }
}

}  // namespace

int main() {
  test_input_admission();
  if (std::getenv("DISKANN_ADMISSION_ONLY")) return dp::test::finish();
  test_false_ascending_claim();
  fixture::baseline_fixture::TemporaryDirectory temporary("xn-artifact");
  const auto workspace = dp::create_workspace(temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture dataset(workspace, 3,
                                                                 1);
  const std::vector<dp::GlobalNodeId> map{2, 1, 0};
  const auto partition = materialize_unordered_partition(workspace, dataset, map);
  const auto dataset_validation = dp::Validator(workspace).validate(
      dataset.reference);
  DP_EXPECT_TRUE(dataset_validation.valid());
  if (!dataset_validation.valid()) return dp::test::finish();
  const auto partition_validation = dp::Validator(workspace).validate(
      partition, {*dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(partition_validation.valid());
  if (!partition_validation.valid()) return dp::test::finish();
  DP_EXPECT_TRUE(
      !partition_validation.validated_artifact->has_capability(kAscending));
  DP_EXPECT_TRUE(partition_validation.validated_artifact->has_capability(
      "partition.complete_local_to_global_map/v1"));
  const auto partition_metadata = fixture::baseline_fixture::read_json(
      workspace.normal_root / partition.metadata_path);
  DP_EXPECT_EQ(partition.aggregate_checksum,
               fixture::baseline_fixture::aggregate_for(partition_metadata));
  DP_EXPECT_EQ(partition_metadata.at("aggregate_checksum"),
               partition.aggregate_checksum);
  native::CppMainVectorAccessor vectors(workspace, dataset.reference);
  fixture::baseline_fixture::TestContext environment(workspace,
                                                      "xn-direct-us1");
  environment.context.vector_accessor = &vectors;
  DP_EXPECT_TRUE(xn::input_admission(
      {*dataset_validation.validated_artifact, *partition_validation.validated_artifact},
      environment.context).compatible);
  for (const bool known : {true, false}) {
    const std::string label = known ? "euclidean" : "unknown";
    fixture::baseline_fixture::TestContext rejected_environment(
        workspace, "xn-service-" + label);
    EuclideanDistance distance(known);
    rejected_environment.context.vector_accessor = &vectors;
    rejected_environment.context.distance_function = &distance;
    const auto parameters = fixture::baseline_fixture::write_parameters(
        rejected_environment.context.temporary_storage,
        fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
    const auto rejected = xn::make_xn_descent_local_build()->build(
        partition, 0, {parameters}, rejected_environment.context);
    std::cout << "XN_SERVICE " << label << " stage=1 status="
              << dp::to_string(rejected.status)
              << " distance_calls=" << distance.calls.load() << '\n';
    DP_EXPECT_EQ(rejected.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(!rejected.output_candidate.has_value());
    DP_EXPECT_EQ(distance.calls.load(), std::size_t{0});
    DP_EXPECT_TRUE(!rejected_environment.metrics.saw("xn_descent.initialized"));
    const auto rejected_manifest = dp::parse_build_manifest(
        fixture::baseline_fixture::read_json(rejected.build_manifest_path).dump());
    DP_EXPECT_TRUE(rejected_manifest.output_artifacts.empty());
    DP_EXPECT_TRUE(rejected_manifest.failure.has_value());
    if (rejected_manifest.failure) {
      DP_EXPECT_EQ(rejected_manifest.failure->category,
                   dp::ErrorCategory::kCapabilityUnsatisfied);
      DP_EXPECT_TRUE(rejected_manifest.failure->reason.find("squared-l2-v1") !=
                     std::string::npos);
    }
  }
  environment.context.vector_accessor = &vectors;
  const auto parameter_path = fixture::baseline_fixture::write_parameters(
      environment.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
  const auto result = xn::make_xn_descent_local_build()->build(
      partition, 0, {parameter_path}, environment.context);
  DP_EXPECT_EQ(result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(result.output_candidate.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(result.build_manifest_path));
  if (result.status != dp::StageStatus::kSucceeded || !result.output_candidate ||
      !std::filesystem::is_regular_file(result.build_manifest_path)) {
    return dp::test::finish();
  }
  const auto candidate_workspace =
      fixture::baseline_fixture::candidate_workspace(environment.context);
  const auto validation = dp::Validator(candidate_workspace).validate(
      *result.output_candidate, {*partition_validation.validated_artifact});
  DP_EXPECT_TRUE(validation.valid());
  if (!validation.valid()) return dp::test::finish();

  const auto candidate = environment.context.temporary_storage.stage_root /
                         "candidate";
  const auto metadata =
      fixture::baseline_fixture::read_json(candidate / "artifact.json");
  DP_EXPECT_EQ(metadata.at("id_space"), "local");
  DP_EXPECT_EQ(metadata.at("structural").at("local_node_count"), map.size());
  DP_EXPECT_EQ(metadata.at("structural").at("declared_max_degree"), 3);
  DP_EXPECT_EQ(metadata.at("structural").at("source_partition_checksum"),
               partition.aggregate_checksum);

  const auto partition_map = workspace.normal_root / "partition/map-0.bin";
  DP_EXPECT_EQ(native::validate_partition_id_map(partition_map, 3, 3), map);
  DP_EXPECT_EQ(native::validate_partition_id_map(candidate / "map.bin", 3, 3), map);
  DP_EXPECT_EQ(dp::sha256_file(partition_map),
               dp::sha256_file(candidate / "map.bin"));
  DP_EXPECT_EQ(fixture::read_bytes(partition_map),
               fixture::read_bytes(candidate / "map.bin"));

  const auto graph_path = candidate / "graph.index";
  const auto graph_bytes = fixture::read_bytes(graph_path);
  const auto actual = fixture::read_native_adjacency(graph_path, map.size());
  const auto native_header_degree = fixture::read_u32(graph_bytes, 8);
  const auto native_entry = fixture::read_u32(graph_bytes, 12);
  DP_EXPECT_EQ(native_entry, dp::LocalNodeId{1});
  std::uint32_t observed = 0;
  for (std::size_t owner = 0; owner < actual.size(); ++owner) {
    observed = std::max(observed,
                        static_cast<std::uint32_t>(actual[owner].size()));
    const std::set<dp::LocalNodeId> unique(actual[owner].begin(),
                                           actual[owner].end());
    DP_EXPECT_EQ(unique.size(), actual[owner].size());
    DP_EXPECT_TRUE(unique.count(static_cast<dp::LocalNodeId>(owner)) == 0);
    DP_EXPECT_TRUE(std::all_of(actual[owner].begin(), actual[owner].end(),
                               [&](auto id) { return id < map.size(); }));
  }
  DP_EXPECT_EQ(native_header_degree, observed);
  DP_EXPECT_EQ(metadata.at("structural").at("observed_max_degree"), observed);
  DP_EXPECT_TRUE(observed <= 3);

  const auto manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(result.build_manifest_path).dump());
  DP_EXPECT_EQ(manifest.implementation.implementation_id,
               xn::kImplementationId);
  DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_EQ(manifest.resource_budget.thread_budget,
               environment.context.thread_budget);
  DP_EXPECT_EQ(manifest.resource_budget.memory_budget_bytes,
               environment.context.memory_budget_bytes);
  DP_EXPECT_EQ(manifest.output_artifacts.size(), std::size_t{1});
  if (!manifest.output_artifacts.empty()) {
    DP_EXPECT_EQ(manifest.output_artifacts.front().aggregate_checksum,
                 result.output_candidate->aggregate_checksum);
  }
  const auto manifest_parameters =
      fixture::Json::parse(manifest.parameters_json);
  DP_EXPECT_EQ(manifest_parameters.at("termination"), "converged");
  DP_EXPECT_TRUE(manifest_parameters.at("completed_rounds").get<int>() > 0);
  DP_EXPECT_TRUE(
      manifest_parameters.at("convergence").at("direct_equality_checked"));
  DP_EXPECT_TRUE(manifest_parameters.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(manifest_parameters.at("convergence")
                   .at("convergence_condition"),
               "full_state_fixed_point");
  DP_EXPECT_EQ(
      manifest_parameters.at("diskann_native_layout_source").at("commit"),
      "78256bbab4685e1774e78d331e081a153be26823");
  DP_EXPECT_EQ(manifest.metrics_json, "{}");
  DP_EXPECT_TRUE(!metadata.contains("recall"));
  DP_EXPECT_TRUE(!metadata.contains("qps"));
  DP_EXPECT_TRUE(!metadata.contains("latency"));

  fixture::baseline_fixture::TestContext repeated(workspace,
                                                   "xn-direct-us1-repeat");
  repeated.context.vector_accessor = &vectors;
  const auto repeated_parameters = fixture::baseline_fixture::write_parameters(
      repeated.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 16));
  const auto repeated_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {repeated_parameters}, repeated.context);
  DP_EXPECT_EQ(repeated_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(repeated_result.output_candidate.has_value());
  if (repeated_result.status != dp::StageStatus::kSucceeded ||
      !repeated_result.output_candidate) return dp::test::finish();
  DP_EXPECT_EQ(
      fixture::read_bytes(graph_path),
      fixture::read_bytes(repeated.context.temporary_storage.stage_root /
                          "candidate/graph.index"));

  fixture::baseline_fixture::TestContext undersized(
      workspace, "xn-direct-us1-undersized-partition");
  undersized.context.vector_accessor = &vectors;
  undersized.context.memory_budget_bytes = 4096;
  const auto undersized_parameters =
      fixture::baseline_fixture::write_parameters(
          undersized.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1000, 3, 1.2,
                                             16));
  const auto undersized_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {undersized_parameters}, undersized.context);
  DP_EXPECT_EQ(undersized_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(undersized_result.output_candidate.has_value());

  fixture::baseline_fixture::TestContext capped(workspace,
                                                 "xn-direct-us1-cap");
  capped.context.vector_accessor = &vectors;
  const auto capped_parameters = fixture::baseline_fixture::write_parameters(
      capped.context.temporary_storage,
      fixture::effective_parameters_json("semantic-small", 5, 3, 1.2, 1));
  const auto capped_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {capped_parameters}, capped.context);
  DP_EXPECT_EQ(capped_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!capped_result.output_candidate.has_value());
  DP_EXPECT_TRUE(
      std::filesystem::is_regular_file(capped_result.build_manifest_path));
  DP_EXPECT_TRUE(!std::filesystem::exists(
      capped.context.temporary_storage.stage_root / "candidate/artifact.json"));
  if (!std::filesystem::is_regular_file(capped_result.build_manifest_path)) {
    return dp::test::finish();
  }
  const auto capped_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(capped_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(capped_manifest.failure.has_value());
  if (!capped_manifest.failure) return dp::test::finish();
  DP_EXPECT_TRUE(capped_manifest.failure->reason.find(
                     "iteration_cap_nonconverged") != std::string::npos);
  DP_EXPECT_TRUE(capped_manifest.output_artifacts.empty());
  const auto capped_evidence =
      fixture::Json::parse(capped_manifest.parameters_json);
  DP_EXPECT_EQ(capped_evidence.at("termination"),
               "iteration_cap_nonconverged");
  DP_EXPECT_EQ(capped_evidence.at("completed_rounds"), 1);
  DP_EXPECT_TRUE(
      capped_evidence.at("convergence").at("direct_equality_checked"));
  DP_EXPECT_TRUE(!capped_evidence.at("convergence").at("fixed_point"));

  fixture::baseline_fixture::TestContext constrained(
      workspace, "xn-direct-us1-budget");
  constrained.context.vector_accessor = &vectors;
  constrained.context.memory_budget_bytes = 1;
  const auto constrained_parameters =
      fixture::baseline_fixture::write_parameters(
          constrained.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 5, 3, 1.2,
                                             16));
  const auto constrained_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {constrained_parameters}, constrained.context);
  DP_EXPECT_EQ(constrained_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!constrained_result.output_candidate.has_value());
  DP_EXPECT_TRUE(
      std::filesystem::is_regular_file(constrained_result.build_manifest_path));
  if (!std::filesystem::is_regular_file(constrained_result.build_manifest_path)) {
    return dp::test::finish();
  }
  const auto constrained_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          constrained_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(constrained_manifest.failure.has_value());
  if (!constrained_manifest.failure) return dp::test::finish();
  DP_EXPECT_TRUE(constrained_manifest.failure->reason.find("memory budget") !=
                 std::string::npos);
  DP_EXPECT_TRUE(constrained_manifest.output_artifacts.empty());
  const auto constrained_evidence =
      fixture::Json::parse(constrained_manifest.parameters_json);
  DP_EXPECT_EQ(constrained_evidence.at("termination"),
               "memory_budget_preflight_exceeded");
  DP_EXPECT_EQ(constrained_evidence.at("completed_rounds"), 0);
  DP_EXPECT_TRUE(
      !constrained_evidence.at("convergence").at("fixed_point"));

  fixture::baseline_fixture::TestContext unsupported(
      workspace, "xn-direct-us1-unsupported-accessor");
  fixture::MatrixVectorAccessor generic_vectors(fixture::line_vectors({0.0F}));
  unsupported.context.vector_accessor = &generic_vectors;
  const auto unsupported_parameters =
      fixture::baseline_fixture::write_parameters(
          unsupported.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.0,
                                             4));
  const auto unsupported_result = xn::make_xn_descent_local_build()->build(
      partition, 0, {unsupported_parameters}, unsupported.context);
  DP_EXPECT_EQ(unsupported_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!unsupported_result.output_candidate.has_value());
  DP_EXPECT_TRUE(unsupported_result.build_manifest_path.empty());

  fixture::baseline_fixture::TemporaryDirectory cycle_temporary(
      "xn-artifact-r-cycle");
  const auto cycle_workspace =
      dp::create_workspace(cycle_temporary.path / "workspace");
  const fixture::baseline_fixture::NativeDatasetFixture cycle_dataset(
      cycle_workspace, 16, 1);
  const auto cycle_partition =
      fixture::baseline_fixture::materialize_partition(
          cycle_workspace, cycle_dataset, {{0, 1, 2, 15}});
  const auto cycle_dataset_validation = dp::Validator(cycle_workspace).validate(
      cycle_dataset.reference);
  DP_EXPECT_TRUE(cycle_dataset_validation.valid());
  if (!cycle_dataset_validation.valid()) return dp::test::finish();
  const auto cycle_partition_validation =
      dp::Validator(cycle_workspace).validate(
          cycle_partition, {*cycle_dataset_validation.validated_artifact});
  DP_EXPECT_TRUE(cycle_partition_validation.valid());
  if (!cycle_partition_validation.valid()) return dp::test::finish();
  DP_EXPECT_TRUE(
      cycle_partition_validation.validated_artifact->has_capability(kAscending));
  native::CppMainVectorAccessor cycle_vectors(cycle_workspace,
                                               cycle_dataset.reference);

  fixture::baseline_fixture::TestContext cycle_success(
      cycle_workspace, "xn-direct-us1-r-cycle-success");
  cycle_success.context.vector_accessor = &cycle_vectors;
  const auto cycle_success_parameters =
      fixture::baseline_fixture::write_parameters(
          cycle_success.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.2,
                                             8, 20));
  const auto cycle_success_result = xn::make_xn_descent_local_build()->build(
      cycle_partition, 0, {cycle_success_parameters}, cycle_success.context);
  DP_EXPECT_EQ(cycle_success_result.status, dp::StageStatus::kSucceeded);
  DP_EXPECT_TRUE(cycle_success_result.output_candidate.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_regular_file(
      cycle_success_result.build_manifest_path));
  if (cycle_success_result.status != dp::StageStatus::kSucceeded ||
      !cycle_success_result.output_candidate ||
      !std::filesystem::is_regular_file(
          cycle_success_result.build_manifest_path)) {
    return dp::test::finish();
  }
  const auto cycle_success_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          cycle_success_result.build_manifest_path)
          .dump());
  const auto cycle_success_evidence =
      fixture::Json::parse(cycle_success_manifest.parameters_json);
  DP_EXPECT_EQ(cycle_success_evidence.at("termination"), "converged");
  DP_EXPECT_EQ(cycle_success_evidence.at("completed_rounds"), 8);
  DP_EXPECT_TRUE(
      !cycle_success_evidence.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(cycle_success_evidence.at("convergence")
                   .at("stable_c_g_rounds"),
               3);
  DP_EXPECT_EQ(cycle_success_evidence.at("convergence")
                   .at("convergence_condition"),
               "three_round_c_g_stability");

  fixture::baseline_fixture::TestContext cycle_capped(
      cycle_workspace, "xn-direct-us1-r-cycle-cap");
  cycle_capped.context.vector_accessor = &cycle_vectors;
  const auto cycle_capped_parameters =
      fixture::baseline_fixture::write_parameters(
          cycle_capped.context.temporary_storage,
          fixture::effective_parameters_json("semantic-small", 1, 1, 1.2,
                                             7, 20));
  const auto cycle_capped_result = xn::make_xn_descent_local_build()->build(
      cycle_partition, 0, {cycle_capped_parameters}, cycle_capped.context);
  DP_EXPECT_EQ(cycle_capped_result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(!cycle_capped_result.output_candidate.has_value());
  DP_EXPECT_TRUE(!std::filesystem::exists(
      cycle_capped.context.temporary_storage.stage_root /
      "candidate/artifact.json"));
  DP_EXPECT_TRUE(
      std::filesystem::is_regular_file(cycle_capped_result.build_manifest_path));
  if (!std::filesystem::is_regular_file(cycle_capped_result.build_manifest_path)) {
    return dp::test::finish();
  }
  const auto cycle_capped_manifest = dp::parse_build_manifest(
      fixture::baseline_fixture::read_json(
          cycle_capped_result.build_manifest_path)
          .dump());
  DP_EXPECT_TRUE(cycle_capped_manifest.output_artifacts.empty());
  const auto cycle_capped_evidence =
      fixture::Json::parse(cycle_capped_manifest.parameters_json);
  DP_EXPECT_EQ(cycle_capped_evidence.at("termination"),
               "iteration_cap_nonconverged");
  DP_EXPECT_EQ(cycle_capped_evidence.at("completed_rounds"), 7);
  DP_EXPECT_TRUE(
      !cycle_capped_evidence.at("convergence").at("fixed_point"));
  DP_EXPECT_EQ(cycle_capped_evidence.at("convergence")
                   .at("convergence_condition"),
               "none");
  std::cout << "XN_MAP_UNORDERED map=2,1,0 ascending=false partition_valid="
            << partition_validation.valid()
            << " b1=" << dp::to_string(result.status)
            << " bytes_preserved="
            << (fixture::read_bytes(partition_map) ==
                fixture::read_bytes(candidate / "map.bin"))
            << " ascending_control="
            << dp::to_string(cycle_success_result.status) << '\n';
  return dp::test::finish();
}
