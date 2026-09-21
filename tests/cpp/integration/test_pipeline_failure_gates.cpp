#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/runner_protocol.hpp"
#include "support/contract_artifact_fixture.hpp"
#include "support/test_assert.hpp"
#include "baseline/cpp_main_stage_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace dp = diskann_pipeline;

namespace {

using Counts = std::array<std::size_t, 3>;

// This generic in-memory accessor owns a copy of the validation grant for the
// exact payload it loads. It is not a caller-supplied Dataset label.
class BoundVectors final : public dp::VectorAccessor {
 public:
  BoundVectors(const dp::WorkspaceRef& workspace, const dp::ArtifactRef& reference,
               bool known = true, std::vector<std::vector<float>> native_rows = {})
      : known_(known) {
    auto checked = dp::Validator(workspace).validate(reference);
    if (!checked.valid()) throw std::runtime_error("invalid service test Dataset");
    grant_ = checked.validated_artifact;
    const auto path = workspace.normal_root / reference.metadata_path;
    if (reference.format_version == "diskann-float32-dataset/v1") {
      std::vector<float> values;
      for (const auto& row : native_rows) values.insert(values.end(), row.begin(), row.end());
      const auto bytes = dp::cpp_main::test::float_matrix(
          static_cast<std::uint32_t>(native_rows.size()), dimension(), values);
      if (dp::sha256_bytes(std::string(bytes.begin(), bytes.end())) != grant_->dataset().dataset_checksum)
        throw std::runtime_error("in-memory service rows do not match validated payload");
      rows_ = std::move(native_rows);
    } else {
      rows_ = dp::test::read_json(path.parent_path() / grant_->payloads().at(0).path)
                  .at("vectors").get<std::vector<std::vector<float>>>();
    }
  }
  const dp::ValidatedArtifact* bound_dataset() const noexcept override {
    return known_ ? &*grant_ : nullptr;
  }
  std::size_t dimension() const noexcept override { return grant_->dataset().vector_dimension; }
  void read(dp::GlobalNodeId id, std::vector<float>& row) const override {
    ++reads;
    row = rows_.at(id);
  }
  mutable std::size_t reads{0};
 private:
  bool known_;
  std::optional<dp::ValidatedArtifact> grant_;
  std::vector<std::vector<float>> rows_;
};

class ServiceDistance final : public dp::DistanceFunction {
 public:
  explicit ServiceDistance(dp::DistanceSemantics semantics) : semantics_(semantics) {}
  dp::DistanceSemantics semantics() const noexcept override { return semantics_; }
  double evaluate(const std::vector<float>& a, const std::vector<float>& b) const override {
    ++calls;
    const auto squared = dp::test::FakeL2Distance{}.evaluate(a, b);
    return semantics_ == dp::DistanceSemantics::kEuclideanL2 ? std::sqrt(squared) : squared;
  }
  mutable std::size_t calls{0};
 private:
  dp::DistanceSemantics semantics_;
};

class PartitionAfter final : public dp::PartitionStrategy {
 public:
  explicit PartitionAfter(std::function<void(dp::StageContext&)> after) : after_(std::move(after)) {}
  dp::StageResult run(const dp::ArtifactRef& dataset, const std::filesystem::path& parameters,
                      dp::StageContext& context) override {
    auto result = dp::test::make_fake_partition()->run(dataset, parameters, context);
    after_(context);
    return result;
  }
 private:
  std::function<void(dp::StageContext&)> after_;
};

class LocalAfter final : public dp::LocalGraphBuilder {
 public:
  LocalAfter(dp::PartitionId after_id, std::function<void(dp::StageContext&)> after)
      : after_id_(after_id), after_(std::move(after)) {}
  dp::StageResult build(const dp::ArtifactRef& partition, dp::PartitionId id,
                        const dp::LocalBuildConfig& config, dp::StageContext& context) override {
    auto result = dp::test::make_fake_local_graph()->build(partition, id, config, context);
    if (id == after_id_) after_(context);
    return result;
  }
 private:
  dp::PartitionId after_id_;
  std::function<void(dp::StageContext&)> after_;
};

void register_counted(dp::ImplementationRegistry& registry, Counts& factories,
                      const dp::ImplementationDescriptor& partition,
                      const dp::ImplementationDescriptor& local,
                      const dp::ImplementationDescriptor& merge) {
  registry.register_partition(partition, [&factories] {
    ++factories[0];
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local, [&factories] {
    ++factories[1];
    return dp::test::make_fake_local_graph();
  });
  registry.register_merge(merge, [&factories] {
    ++factories[2];
    return dp::test::make_fake_merge();
  });
}

void expect_counts(const std::string& name, const Counts& factories,
                   dp::test::FakeStageEnvironment& environment,
                   const dp::PipelineExecutionResult& result,
                   const Counts& expected) {
  // These events are emitted at the actual Fake stage method entry, independently
  // of both factory creation and the executor's bookkeeping.
  const auto& events = environment.metrics().events();
  const Counts stages{
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "partition.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "local_build.start")),
      static_cast<std::size_t>(std::count(events.begin(), events.end(), "merge.start"))};
  const Counts reported{result.invocations.partition, result.invocations.local_build,
                        result.invocations.merge};
  std::cout << name << " status=" << dp::to_string(result.status)
            << " factories=" << factories[0] << ',' << factories[1] << ',' << factories[2]
            << " stages=" << stages[0] << ',' << stages[1] << ',' << stages[2]
            << " executor=" << reported[0] << ',' << reported[1] << ',' << reported[2] << '\n';
  DP_EXPECT_EQ(factories, expected);
  DP_EXPECT_EQ(stages, expected);
  DP_EXPECT_EQ(reported, expected);
}

void runtime_admission(const std::string& path, const std::string& service) {
  const std::string name = "runtime-" + path + "-" + service;
  dp::test::FakeStageEnvironment environment(name);
  const auto& workspace = environment.workspace();
  const auto dataset = dp::test::materialize_fake_dataset(workspace);
  const auto checked = dp::Validator(workspace).validate(dataset);
  DP_EXPECT_TRUE(checked.valid());
  if (!checked.valid()) return;
  BoundVectors correct(workspace, dataset), unknown(workspace, dataset, false);
  dp::test::ContractArtifactFixture other;
  // fake-dataset-json/v1 intentionally fixes one payload checksum. Use the
  // existing native format for a genuinely different, independently VALID Dataset.
  const auto native_bytes = dp::cpp_main::test::float_matrix(6, 2, {9,0,1,0,2,0,0,1,1,1,2,1});
  const std::string other_bytes(native_bytes.begin(), native_bytes.end());
  dp::test::replace_payload(other.workspace, other.dataset, "vectors", other_bytes);
  dp::test::mutate_metadata(other.workspace, other.dataset, [&](auto& metadata) {
    metadata["dataset"]["dataset_checksum"] = dp::sha256_bytes(other_bytes);
    metadata["format_version"] = "diskann-float32-dataset/v1";
    metadata["capabilities"] = dp::test::Json::array({"dataset.diskann_float32_bin/v1"});
    metadata["payloads"][0]["media_type"] = dp::cpp_main::kFloat32DatasetMediaType;
  });
  other.dataset.format_version = "diskann-float32-dataset/v1";
  BoundVectors wrong(other.workspace, other.dataset, true, {{9,0},{1,0},{2,0},{0,1},{1,1},{2,1}});
  DP_EXPECT_EQ(wrong.bound_dataset()->dataset().dataset_id, checked.validated_artifact->dataset().dataset_id);
  DP_EXPECT_EQ(wrong.dimension(), correct.dimension());
  DP_EXPECT_EQ(wrong.bound_dataset()->dataset().global_node_count,
               checked.validated_artifact->dataset().global_node_count);
  ServiceDistance squared(dp::DistanceSemantics::kSquaredL2);
  ServiceDistance euclidean(dp::DistanceSemantics::kEuclideanL2);
  ServiceDistance unknown_distance(dp::DistanceSemantics::kUnknown);
  const bool late = path == "after-partition" || path == "after-local" || path == "before-merge";
  const bool success = service == "correct";
  const bool reuse = path == "reuse" || path == "cli-reuse";
  const bool merge_only = path == "merge-only";
  const bool cli = path.rfind("cli-", 0) == 0;
  const auto corrupt = [&](dp::StageContext& context) {
    if (service == "wrong-dataset") context.vector_accessor = &wrong;
    else if (service == "unknown-binding") context.vector_accessor = &unknown;
    else if (service == "euclidean") context.distance_function = &euclidean;
    else if (service == "unknown-distance") context.distance_function = &unknown_distance;
  };
  auto context = environment.make_context("executor");
  context.vector_accessor = &correct;
  context.distance_function = &squared;
  if (!late) corrupt(context);
  auto request = dp::test::canonical_execution_request(name, dataset);
  if (reuse || merge_only) {
    auto partition = dp::test::materialize_partition_candidate(workspace.normal_root / "seed", dataset);
    partition.metadata_path = std::filesystem::path("seed") / partition.metadata_path;
    DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(partition).valid());
    if (reuse) request.reused_partition = partition;
    else {
      std::vector<dp::ArtifactRef> locals;
      for (dp::PartitionId id : {0u, 1u}) {
        const auto prefix = "seed-local-" + std::to_string(id);
        auto local = dp::test::materialize_local_candidate(workspace.normal_root / prefix,
                                                          workspace, partition, id, false);
        local.metadata_path = std::filesystem::path(prefix) / local.metadata_path;
        DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(local).valid());
        locals.push_back(local);
      }
      request.merge_only_inputs = dp::MergeOnlyInputSet{partition, locals};
    }
  }
  std::map<std::filesystem::path, std::string> inputs;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(workspace.normal_root))
    if (entry.is_regular_file()) inputs.emplace(entry.path(), dp::sha256_file(entry.path()));
  auto partition_descriptor = dp::test::fake_partition_descriptor();
  auto local_descriptor = dp::test::fake_local_graph_descriptor();
  auto merge_descriptor = dp::test::fake_merge_descriptor();
  local_descriptor.requires.required_services.push_back("distance_function/squared-l2-v1");
  merge_descriptor.requires.required_services.push_back("vector_accessor/dataset-bound-v1");
  merge_descriptor.requires.required_services.push_back("distance_function/squared-l2-v1");
  // An unexecuted producer must not impose its service requirement on reuse.
  if (reuse) partition_descriptor.requires.required_services.push_back("checkpoint_store/v1");
  Counts factories{};
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(partition_descriptor, [&]() -> std::unique_ptr<dp::PartitionStrategy> {
    ++factories[0];
    if (path == "after-partition") return std::make_unique<PartitionAfter>(corrupt);
    return dp::test::make_fake_partition();
  });
  registry.register_local_build(local_descriptor, [&]() -> std::unique_ptr<dp::LocalGraphBuilder> {
    ++factories[1];
    if (path == "after-local" || path == "before-merge")
      return std::make_unique<LocalAfter>(path == "after-local" ? 0u : 1u, corrupt);
    return dp::test::make_fake_local_graph();
  });
  registry.register_merge(merge_descriptor, [&] {
    ++factories[2]; return dp::test::make_fake_merge();
  });
  const auto expected_category = service == "wrong-dataset" ? dp::ErrorCategory::kSourceMismatch
                                                             : dp::ErrorCategory::kCapabilityUnsatisfied;
  if (cli) {
    using Json = dp::test::Json;
    const std::string operation = path == "cli-preflight" ? "preflight" : "execute";
    Json document{{"request_version", "1.0.0"}, {"request_id", name}, {"operation", operation},
      {"study_class", "infrastructure_validation"},
      {"workspace", {{"root", workspace.root.string()}, {"normal_root", workspace.normal_root.string()},
        {"temporary_root", workspace.temporary_root.string()}, {"manifest_root", workspace.manifest_root.string()},
        {"report_root", workspace.report_root.string()}, {"quarantine_root", workspace.quarantine_root.string()},
        {"memory_budget_bytes", 1048576}, {"thread_budget", 1}}},
      {"dataset", Json::parse(dp::serialize_artifact_ref(dataset))},
      {"implementations", {{"partition", request.partition_implementation_id},
        {"local_build", request.local_implementation_id}, {"merge", request.merge_implementation_id}}},
      {"stage_parameters", {{"partition", Json::parse(request.partition_parameters_json)},
        {"local_build", Json::parse(request.local_parameters_json)}, {"merge", Json::parse(request.merge_parameters_json)}}},
      {"repetition_policy", {{"count", 2}, {"aggregation", "retain_all"}}}};
    if (reuse) document["reuse_artifact"] = Json::parse(dp::serialize_artifact_ref(*request.reused_partition));
    const auto file = workspace.temporary_root / "request.json";
    dp::write_immutable_text(file, document.dump());
    const auto dispatched = dp::dispatch_runner_request(file, operation, registry, context);
    const auto result = Json::parse(dispatched.result_json);
    DP_EXPECT_EQ(dispatched.exit_code, success ? 0 : 3);
    const Counts expected = success && operation == "execute" ? Counts{reuse ? 0u : 1u, 2, 1} : Counts{};
    dp::PipelineExecutionResult counts_result;
    counts_result.status = success ? dp::StageStatus::kSucceeded : dp::StageStatus::kFailed;
    counts_result.invocations = {result.at("stage_invocations").at("partition").get<std::size_t>(),
                                result.at("stage_invocations").at("local_build").get<std::size_t>(),
                                result.at("stage_invocations").at("merge").get<std::size_t>()};
    expect_counts(name, factories, environment, counts_result, expected);
    if (!success) {
      DP_EXPECT_EQ(result.at("outcome"), "REJECTED");
      DP_EXPECT_EQ(result.at("failure").at("category"), std::string(dp::to_string(expected_category)));
      DP_EXPECT_TRUE(!result.contains("artifact"));
    }
  } else {
    const auto result = dp::PipelineExecutor(registry, workspace).execute(request, context);
    Counts expected{};
    if (success) expected = merge_only ? Counts{0, 0, 1} : Counts{reuse ? 0u : 1u, 2, 1};
    else if (late) expected = {1, path == "after-partition" ? 0u : path == "after-local" ? 1u : 2u, 0};
    expect_counts(name, factories, environment, result, expected);
    DP_EXPECT_EQ(result.status, success ? dp::StageStatus::kSucceeded : dp::StageStatus::kFailed);
    DP_EXPECT_EQ(result.artifact.has_value(), success);
    DP_EXPECT_TRUE(!result.quarantine_id.has_value());
    if (!success) {
      DP_EXPECT_TRUE(result.failure.has_value());
      if (result.failure) {
        DP_EXPECT_EQ(result.failure->category, expected_category);
        DP_EXPECT_TRUE(result.failure->reason.find("runtime service") != std::string::npos);
      }
      DP_EXPECT_TRUE(!result.evaluation_report_path.has_value());
    }
    if (reuse || merge_only || late) DP_EXPECT_TRUE(!result.validation_evidence_paths.empty());
    for (const auto& evidence : result.validation_evidence_paths) {
      const auto document = dp::test::read_json(evidence);
      DP_EXPECT_EQ(document.at("result"), "VALID");
      DP_EXPECT_TRUE(!document.contains("failure_category"));
    }
    for (const auto& manifest_path : result.build_manifest_paths) {
      const auto manifest = dp::parse_build_manifest(dp::test::read_json(manifest_path).dump());
      DP_EXPECT_TRUE(manifest.status != dp::StageStatus::kInvalidOutput);
      if (late) {
        DP_EXPECT_EQ(manifest.status, dp::StageStatus::kSucceeded);
        for (const auto& artifact : manifest.output_artifacts)
          DP_EXPECT_TRUE(dp::Validator(workspace).validate_with_lineage(artifact).valid());
      }
    }
    if (late) DP_EXPECT_EQ(result.build_manifest_paths.size(), expected[0] + expected[1]);
  }
  // Fake stages need no numeric work; admission itself must never read/evaluate.
  DP_EXPECT_EQ(correct.reads + wrong.reads + unknown.reads, std::size_t{0});
  DP_EXPECT_EQ(squared.calls + euclidean.calls + unknown_distance.calls, std::size_t{0});
  DP_EXPECT_TRUE(std::filesystem::is_empty(workspace.quarantine_root));
  for (const auto& item : inputs) DP_EXPECT_EQ(dp::sha256_file(item.first), item.second);
  DP_EXPECT_TRUE(dp::Validator(workspace).validate(dataset).valid());
  std::cout << name << " reads=0 distance_calls=0 inputs=VALID quarantine=0\n";
}

void runtime_services_matrix() {
  dp::test::FakeStageEnvironment environment("runtime-identity-fields");
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  BoundVectors vectors(environment.workspace(), dataset);
  auto context = environment.make_context("identity-fields");
  // Existing implementations compile unchanged but their missing evidence is
  // genuinely unknown, even when evaluate happens to calculate squared L2.
  DP_EXPECT_TRUE(context.vector_accessor->bound_dataset() == nullptr);
  DP_EXPECT_EQ(context.distance_function->semantics(), dp::DistanceSemantics::kUnknown);
  context.vector_accessor = &vectors;
  dp::ContractRequirement binding_only{{}, {}, {"vector_accessor/dataset-bound-v1"}};
  const auto original = vectors.bound_dataset()->dataset();
  DP_EXPECT_TRUE(dp::check_runtime_services(binding_only, context, original).compatible);
  for (int field = 0; field != 6; ++field) {
    auto expected = original;
    switch (field) {
      case 0: expected.dataset_id += "-different"; break;
      case 1: expected.dataset_checksum = std::string(64, '0'); break;
      case 2: ++expected.global_node_count; break;
      case 3: ++expected.vector_dimension; break;
      case 4: expected.scalar_type = "int8"; break;
      case 5: expected.distance_metric = "cosine"; break;
    }
    const auto result = dp::check_runtime_services(binding_only, context, expected);
    DP_EXPECT_TRUE(!result.compatible);
    if (result.failure) DP_EXPECT_EQ(result.failure->category, dp::ErrorCategory::kSourceMismatch);
  }
  ServiceDistance squared(dp::DistanceSemantics::kSquaredL2);
  ServiceDistance euclidean(dp::DistanceSemantics::kEuclideanL2);
  DP_EXPECT_EQ(squared.evaluate({0, 0}, {3, 4}), 25.0);
  DP_EXPECT_EQ(euclidean.evaluate({0, 0}, {3, 4}), 5.0);
  const auto advertised = dp::runtime_service_capabilities(context, original);
  DP_EXPECT_TRUE(std::find(advertised.available_services.begin(), advertised.available_services.end(),
                           "distance_function/squared-l2-v1") == advertised.available_services.end());
  for (const std::string path : {"generated", "reuse", "merge-only", "cli-preflight", "cli-execute", "cli-reuse"})
    for (const std::string service : {"correct", "wrong-dataset", "unknown-binding", "euclidean", "unknown-distance"})
      runtime_admission(path, service);
  for (const std::string path : {"after-partition", "after-local", "before-merge"})
    for (const std::string service : {"wrong-dataset", "euclidean"})
      runtime_admission(path, service);
}

enum class ReuseCase { kMissingLocal, kMissingMerge, kChecksum, kStructure, kLineage };

void reject_reuse(const std::string& name, ReuseCase scenario,
                  bool producer_claims_capability = false) {
  dp::test::FakeStageEnvironment environment(name);
  const auto& workspace = environment.workspace();
  const auto dataset = dp::test::materialize_fake_dataset(workspace);
  auto cached = dp::test::materialize_partition_candidate(workspace.normal_root / "seed", dataset);
  cached.metadata_path = std::filesystem::path("seed") / cached.metadata_path;
  const dp::Validator validator(workspace);
  const auto checked_dataset = validator.validate(dataset);
  DP_EXPECT_TRUE(checked_dataset.valid());
  if (!checked_dataset.valid()) return;
  const auto sources = std::vector<dp::ValidatedArtifact>{*checked_dataset.validated_artifact};
  DP_EXPECT_TRUE(validator.validate(cached, sources).valid());

  const bool valid_input = scenario == ReuseCase::kMissingLocal ||
                           scenario == ReuseCase::kMissingMerge;
  auto expected_category = dp::ErrorCategory::kCapabilityUnsatisfied;
  if (scenario == ReuseCase::kChecksum) {
    cached.aggregate_checksum = std::string(64, '0');
    expected_category = dp::ErrorCategory::kChecksumMismatch;
  } else if (scenario == ReuseCase::kStructure) {
    dp::test::mutate_metadata(workspace, cached, [](auto& metadata) {
      metadata["structural"]["partition_count"] = 3;
    });
    expected_category = dp::ErrorCategory::kNodeCountMismatch;
  } else if (scenario == ReuseCase::kLineage) {
    dp::test::mutate_metadata(workspace, cached, [](auto& metadata) {
      metadata["source_artifacts"][0]["aggregate_checksum"] = std::string(64, '0');
    });
    expected_category = dp::ErrorCategory::kSourceMismatch;
  }
  const auto independently_checked = validator.validate(cached, sources);
  DP_EXPECT_EQ(independently_checked.valid(), valid_input);
  if (!valid_input) {
    DP_EXPECT_EQ(independently_checked.evidence.failure_category,
                 std::optional<dp::ErrorCategory>{expected_category});
  }
  // Freeze every input byte after fixture preparation, not merely the reference.
  std::map<std::filesystem::path, std::string> input_hashes;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(workspace.normal_root)) {
    if (entry.is_regular_file()) input_hashes.emplace(entry.path(), dp::sha256_file(entry.path()));
  }

  auto partition = dp::test::fake_partition_descriptor();
  auto local = dp::test::fake_local_graph_descriptor();
  auto merge = dp::test::fake_merge_descriptor();
  const std::string capability = "partition.local_to_global_ascending/v1";
  if (producer_claims_capability) partition.provides.capabilities.push_back(capability);
  if (scenario == ReuseCase::kMissingLocal) local.requires.required_capabilities.push_back(capability);
  if (scenario == ReuseCase::kMissingMerge) merge.requires.required_capabilities.push_back(capability);
  Counts factories{};
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  register_counted(registry, factories, partition, local, merge);
  auto request = dp::test::canonical_execution_request(name, dataset);
  request.reused_partition = cached;
  auto context = environment.make_context("executor");
  const auto result = dp::PipelineExecutor(registry, workspace).execute(request, context);
  expect_counts(name, factories, environment, result, {0, 0, 0});
  DP_EXPECT_EQ(result.status, dp::StageStatus::kFailed);
  DP_EXPECT_TRUE(result.failure.has_value());
  if (result.failure) {
    DP_EXPECT_EQ(result.failure->category, expected_category);
    if (valid_input) DP_EXPECT_TRUE(result.failure->reason.find(capability) != std::string::npos);
  }
  DP_EXPECT_TRUE(result.stage_order.empty());
  DP_EXPECT_TRUE(result.local_partition_ids.empty());
  DP_EXPECT_TRUE(!result.artifact.has_value());
  DP_EXPECT_TRUE(!result.partition_artifact.has_value());
  DP_EXPECT_TRUE(!result.evaluation_report_path.has_value());
  DP_EXPECT_TRUE(!result.quarantine_id.has_value());
  DP_EXPECT_TRUE(std::filesystem::is_empty(workspace.quarantine_root));
  DP_EXPECT_EQ(result.validation_evidence_paths.size(), std::size_t{1});
  if (result.validation_evidence_paths.size() == 1) {
    // A consumer mismatch must not rewrite an independently VALID contract result.
    DP_EXPECT_EQ(dp::test::read_json(result.validation_evidence_paths.front()),
                 dp::test::Json::parse(dp::serialize_validation_evidence(independently_checked.evidence)));
  }
  DP_EXPECT_EQ(result.build_manifest_paths.size(), std::size_t{1});
  if (result.build_manifest_paths.size() == 1) {
    const auto manifest = dp::parse_build_manifest(
        dp::test::read_json(result.build_manifest_paths.front()).dump());
    DP_EXPECT_EQ(manifest.status, dp::StageStatus::kFailed);
    DP_EXPECT_TRUE(manifest.output_artifacts.empty());
    DP_EXPECT_TRUE(manifest.failure.has_value());
    if (manifest.failure) DP_EXPECT_EQ(manifest.failure->category, expected_category);
    if (valid_input) DP_EXPECT_TRUE(manifest.reuse.has_value());
    if (manifest.reuse) {
      DP_EXPECT_TRUE(!manifest.reuse->reused);
      DP_EXPECT_TRUE(manifest.reuse->source_artifact.has_value());
      if (manifest.reuse->source_artifact) {
        DP_EXPECT_EQ(dp::serialize_artifact_ref(*manifest.reuse->source_artifact),
                     dp::serialize_artifact_ref(cached));
      }
    }
    DP_EXPECT_TRUE(manifest.validation_evidence_path.has_value());
    if (manifest.validation_evidence_path && !result.validation_evidence_paths.empty()) {
      DP_EXPECT_EQ(workspace.root / *manifest.validation_evidence_path,
                   result.validation_evidence_paths.front());
    }
  }
  for (const auto& input : input_hashes) DP_EXPECT_EQ(dp::sha256_file(input.first), input.second);
  DP_EXPECT_EQ(validator.validate(cached, sources).valid(), valid_input);
}

dp::PipelineExecutionResult run_with_cancel(std::size_t observation,
                                            const std::string& identity) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  environment.cancellation().cancel_on_observation(observation);
  auto context = environment.make_context("executor");
  return dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(identity, dataset), context);
}

}  // namespace

int main() {
  runtime_services_matrix();
  reject_reuse("reuse-missing-local-claimed", ReuseCase::kMissingLocal, true);
  reject_reuse("reuse-missing-merge-claimed", ReuseCase::kMissingMerge, true);
  reject_reuse("reuse-missing-local-unclaimed", ReuseCase::kMissingLocal);
  reject_reuse("reuse-missing-merge-unclaimed", ReuseCase::kMissingMerge);
  reject_reuse("reuse-invalid-checksum", ReuseCase::kChecksum);
  reject_reuse("reuse-invalid-structure", ReuseCase::kStructure);
  reject_reuse("reuse-invalid-lineage", ReuseCase::kLineage);
  const auto partition_cancelled = run_with_cancel(1, "cancel-partition");
  DP_EXPECT_EQ(partition_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(partition_cancelled.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(partition_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partition_cancelled.artifact.has_value());

  const auto local_cancelled = run_with_cancel(4, "cancel-local");
  DP_EXPECT_EQ(local_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(local_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!local_cancelled.artifact.has_value());

  const auto merge_cancelled = run_with_cancel(10, "cancel-merge");
  DP_EXPECT_EQ(merge_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(merge_cancelled.invocations.merge, std::size_t{1});
  DP_EXPECT_TRUE(!merge_cancelled.artifact.has_value());

  dp::test::FakeStageEnvironment invalid_environment("invalid-local");
  const auto dataset =
      dp::test::materialize_fake_dataset(invalid_environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  registry.register_partition(dp::test::fake_partition_descriptor(),
                              dp::test::make_fake_partition);
  registry.register_local_build(dp::test::fake_local_graph_descriptor(),
                                dp::test::make_invalid_fake_local_graph);
  registry.register_merge(dp::test::fake_merge_descriptor(),
                          dp::test::make_fake_merge);
  const auto invalid_request =
      dp::test::canonical_execution_request("invalid-local", dataset);
  auto context = invalid_environment.make_context("executor");
  const auto invalid =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(invalid_request, context);
  DP_EXPECT_EQ(invalid.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_EQ(invalid.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(invalid.quarantine_id.has_value());
  DP_EXPECT_TRUE(!invalid.artifact.has_value());

  auto incompatible_request =
      dp::test::canonical_execution_request("incompatible", dataset);
  incompatible_request.merge_implementation_id = "missing_merge";
  const auto incompatible =
      dp::PipelineExecutor(registry, invalid_environment.workspace())
          .execute(incompatible_request, context);
  DP_EXPECT_EQ(incompatible.status, dp::StageStatus::kFailed);
  DP_EXPECT_EQ(incompatible.invocations.partition, std::size_t{0});

  dp::test::FakeStageEnvironment handoff_environment("handoff-mismatch");
  const auto handoff_dataset =
      dp::test::materialize_fake_dataset(handoff_environment.workspace());
  dp::ImplementationRegistry handoff_registry(dp::RegistryAdmissionPolicy::kTest);
  auto partition_descriptor = dp::test::fake_partition_descriptor();
  partition_descriptor.provides.capabilities.push_back(
      "partition.unobserved_claim/v1");
  Counts handoff_factories{};
  register_counted(handoff_registry, handoff_factories, partition_descriptor,
                   dp::test::fake_local_graph_descriptor(),
                   dp::test::fake_merge_descriptor());
  auto handoff_context = handoff_environment.make_context("handoff-executor");
  const auto handoff =
      dp::PipelineExecutor(handoff_registry, handoff_environment.workspace())
          .execute(dp::test::canonical_execution_request(
                       "handoff-mismatch", handoff_dataset),
                   handoff_context);
  DP_EXPECT_EQ(handoff.status, dp::StageStatus::kInvalidOutput);
  DP_EXPECT_TRUE(handoff.failure.has_value());
  DP_EXPECT_EQ(handoff.failure->category,
               dp::ErrorCategory::kCapabilityUnsatisfied);
  DP_EXPECT_EQ(handoff.invocations.partition, std::size_t{1});
  DP_EXPECT_EQ(handoff.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(handoff.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(handoff.quarantine_id.has_value());
  expect_counts("generated-producer-output-mismatch", handoff_factories,
                 handoff_environment, handoff, {1, 0, 0});

  return dp::test::finish();
}
