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
#include <cstdlib>
#include <fstream>

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

class CancelAtEvent final : public dp::MetricsSink {
 public:
  CancelAtEvent(dp::test::FakeStageEnvironment& env, std::string event)
      : env_(env), event_(std::move(event)) {}
  void event(const std::string& name) override {
    env_.metrics().event(name);
    if (name == event_) { observed = true; env_.cancellation().cancel_now(); }
  }
  void increment(const std::string& name, std::uint64_t amount) override {
    env_.metrics().increment(name, amount);
  }
  bool observed{false};
 private:
  dp::test::FakeStageEnvironment& env_;
  std::string event_;
};

dp::PipelineExecutionResult run_with_cancel(const std::string& checkpoint,
                                            const std::string& identity) {
  dp::test::FakeStageEnvironment environment(identity);
  const auto dataset = dp::test::materialize_fake_dataset(environment.workspace());
  dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
  dp::test::register_fake_implementations(registry);
  CancelAtEvent metrics(environment, checkpoint);
  auto context = environment.make_context("executor");
  context.metrics_sink = &metrics;
  auto result = dp::PipelineExecutor(registry, environment.workspace())
      .execute(dp::test::canonical_execution_request(identity, dataset), context);
  DP_EXPECT_TRUE(metrics.observed);
  return result;
}

namespace lifecycle {
namespace fs = std::filesystem;
using J = nlohmann::json;
J read(const fs::path& p) { std::ifstream f(p); return J::parse(f); }
void save(const fs::path& p, const J& j) { fs::create_directories(p.parent_path()); std::ofstream(p) << j.dump(2); }
std::string str(dp::StageStatus s) { return std::string(dp::to_string(s)); }
struct Case { std::string name, action, target, cancel_at; bool ignore_token=false; std::string route="fresh"; };
std::map<std::string,std::string> snapshot(const fs::path& root);
struct Trace;
struct Token : dp::CancellationToken {
  Trace* t; bool cancelled=false;
  explicit Token(Trace* v):t(v){}
  bool is_cancelled() const noexcept override;
};
struct Metrics : dp::MetricsSink {
  Trace* t; explicit Metrics(Trace* v):t(v){}
  void event(const std::string&) override;
  void increment(const std::string&, std::uint64_t) override {}
};
struct Trace {
  Case c; dp::PipelineExecutionRequest request; dp::DatasetIdentity dataset;
  std::array<int,3> factories{}, methods{};
  std::string active="executor", phase="initial";
  J events=J::array(), reads=J::array(), candidates=J::array();
  Token token{this}; Metrics metrics{this};
  fs::path published_marker;
  dp::WorkspaceRef workspace;
  std::map<std::string,std::string> previous_successes;
  void signal(const std::string& marker) {
    events.push_back("cancel_requested:"+marker); token.cancelled=true;
  }
};
bool Token::is_cancelled() const noexcept {
  std::error_code ec;
  if (!t->published_marker.empty() && fs::is_regular_file(t->published_marker, ec))
    const_cast<Token*>(this)->cancelled = true;
  t->reads.push_back(J{{"owner",t->active},{"checkpoint",t->phase},{"cancelled",cancelled}});
  return cancelled;
}
void Metrics::event(const std::string& name) {
  t->phase=name; t->events.push_back(t->active+":"+name);
  if (t->active==t->c.target && name==t->c.cancel_at) t->signal(t->active+":"+name);
}
J counts(const std::array<int,3>& c) {return {c[0],c[1],c[2]};}
J failure(const dp::FailureInfo& f) {
  J r={{"category",std::string(dp::to_string(f.category))},{"reason",f.reason}};
  if(f.exit_code) r["exit_code"]=*f.exit_code; return r;
}
fs::path owned_manifest(Trace& t, const std::string& label, dp::StageContext& ctx, dp::StageStatus status) {
  dp::BuildManifest m;
  m.run_id=t.request.run_id; m.stage=label=="P"?"partition":label=="M"?"merge":"local_build";
  m.implementation={label=="P"?"fake_partition_v1":label=="M"?"fake_merge_v1":"fake_local_graph_v1","1.0.0"};
  m.source=t.request.source; m.toolchain=t.request.toolchain; m.dataset=t.dataset;
  m.resource_budget={ctx.memory_budget_bytes,ctx.thread_budget}; m.status=status;
  m.started_at="2026-10-08T00:00:00Z"; m.ended_at="2026-10-08T00:00:01Z";
  m.input_artifacts={t.request.dataset};
  if(status==dp::StageStatus::kFailed) m.failure=dp::FailureInfo{dp::ErrorCategory::kSourceMismatch,"probe-original-stage-reason",37};
  fs::path p=ctx.temporary_storage.stage_root/"owned-manifest.json";
  dp::write_immutable_text(p, dp::serialize_build_manifest(m)); return p;
}
dp::StageResult method(Trace& t, const std::string& label, dp::StageContext& ctx,
                       std::function<dp::StageResult()> delegate,
                       std::function<dp::ArtifactRef()> generate) {
  int slot=label=="P"?0:label=="M"?2:1;
  ++t.methods[slot]; t.events.push_back("method:"+label); t.active=label;
  struct Scope { Trace& t; ~Scope(){t.active="executor";t.phase="between_stages";} } scope{t};
  const bool hit=label==t.c.target;
  const std::string a=hit?t.c.action:"success";
  if(a=="evidence_write_failure") {
    dp::write_immutable_text(ctx.workspace.manifest_root/t.request.run_id/"stages", "blocking fixture");
    throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,"probe-original-write-failure:"+label);
  }
  if(a=="throw_pipeline") throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,"probe-method-pipeline:"+label);
  if(a=="throw_std") throw std::runtime_error("probe-method-std:"+label);
  if(a=="throw_cancel") throw dp::PipelineError(dp::ErrorCategory::kCancelled,"probe-method-cancel:"+label);
  if(a=="throw_std_cancelled") {t.signal("ordinary-error-with-cancellation"); throw std::runtime_error("probe-method-std:"+label);}
  if(a=="owned_throw") {owned_manifest(t,label,ctx,dp::StageStatus::kFailed); throw std::runtime_error("probe-owned-then-throw:"+label);}
  if(a=="failed_cancelled") {t.signal("failed-with-token-cancelled"); return {dp::StageStatus::kFailed,std::nullopt,{}};}
  if(a=="missing_cancelled") {t.signal("missing-with-token-cancelled"); return {dp::StageStatus::kSucceeded,std::nullopt,{}};}
  if(a=="failed") return {dp::StageStatus::kFailed,std::nullopt,{}};
  if(a=="cancelled") return {dp::StageStatus::kCancelled,std::nullopt,{}};
  if(a=="owned_failed") return {dp::StageStatus::kFailed,std::nullopt,owned_manifest(t,label,ctx,dp::StageStatus::kFailed)};
  if(a=="owned_status_mismatch") return {dp::StageStatus::kFailed,std::nullopt,owned_manifest(t,label,ctx,dp::StageStatus::kSucceeded)};
  if(a=="missing") return {dp::StageStatus::kSucceeded,std::nullopt,{}};
  auto generated=[&]() {
    auto ref=generate(); auto ws=ctx.workspace; ws.normal_root=ctx.temporary_storage.stage_root;
    t.candidates.push_back(J{{"label",label},{"path",(ctx.temporary_storage.stage_root/ref.metadata_path).string()},
                             {"sha256",dp::sha256_file(ctx.temporary_storage.stage_root/ref.metadata_path)}});
    return ref;
  };
  dp::StageResult result;
  if(a=="failed_candidate" || a=="cancelled_candidate" || a=="throw_after_candidate") {
    auto ref=generated();
    if(a=="throw_after_candidate") throw std::runtime_error("probe-after-candidate:"+label);
    result={a=="failed_candidate"?dp::StageStatus::kFailed:dp::StageStatus::kCancelled,ref,{}};
  } else if(t.c.ignore_token) result={dp::StageStatus::kSucceeded,generated(),{}};
  else {
    result=delegate();
    if(result.output_candidate) t.candidates.push_back(J{{"label",label},{"path",(ctx.temporary_storage.stage_root/result.output_candidate->metadata_path).string()},
                                                        {"sha256",dp::sha256_file(ctx.temporary_storage.stage_root/result.output_candidate->metadata_path)}});
  }
  t.events.push_back("method_return:"+label+":"+str(result.status));
  if(t.c.cancel_at=="after:"+label) t.signal("after:"+label);
  return result;
}
class P : public dp::PartitionStrategy { Trace& t; public: explicit P(Trace& v):t(v){}
 dp::StageResult run(const dp::ArtifactRef& d,const fs::path& p,dp::StageContext& c) override {
 return method(t,"P",c,[&]{return dp::test::make_fake_partition()->run(d,p,c);},[&]{return dp::test::materialize_partition_candidate(c.temporary_storage.stage_root,d);}); }};
class L : public dp::LocalGraphBuilder {Trace& t;public:explicit L(Trace& v):t(v){}
 dp::StageResult build(const dp::ArtifactRef& p,dp::PartitionId id,const dp::LocalBuildConfig& cfg,dp::StageContext& c) override {
 return method(t,"L"+std::to_string(id),c,[&]{return dp::test::make_fake_local_graph()->build(p,id,cfg,c);},[&]{return dp::test::materialize_local_candidate(c.temporary_storage.stage_root,c.workspace,p,id,false);}); }};
class M : public dp::GraphMerger {Trace& t;public:explicit M(Trace& v):t(v){}
 dp::StageResult merge(const dp::ArtifactRef& p,const std::vector<dp::ArtifactRef>& l,const fs::path& cfg,dp::StageContext& c) override {
 return method(t,"M",c,[&]{return dp::test::make_fake_merge()->merge(p,l,cfg,c);},[&]{return dp::test::materialize_global_candidate(c.temporary_storage.stage_root,c.workspace,p,l,false);}); }};
void factory(Trace& t,int n,const std::string& label) {
 auto normal=snapshot(t.workspace.normal_root);
 auto manifests=snapshot(t.workspace.manifest_root);
 t.previous_successes.insert(normal.begin(),normal.end());
 t.previous_successes.insert(manifests.begin(),manifests.end());
 ++t.factories[n]; t.events.push_back("factory:"+label);
 if(t.c.target==label && t.c.action=="factory_cancel") t.signal("factory-return:"+label);
 if(t.c.target==label && t.c.action=="factory_cancel_exception") throw dp::PipelineError(dp::ErrorCategory::kCancelled,"probe-factory-cancel:"+label);
 if(t.c.target==label && t.c.action=="factory_std") throw std::runtime_error("probe-factory-std:"+label);
 if(t.c.target==label && t.c.action=="factory_pipeline") throw dp::PipelineError(dp::ErrorCategory::kSourceMismatch,"probe-factory-pipeline:"+label);
}
std::map<std::string,std::string> snapshot(const fs::path& root) {
 std::map<std::string,std::string> h;
 for(const auto& x:fs::recursive_directory_iterator(root)) if(x.is_regular_file()) h[x.path().string()]=dp::sha256_file(x.path());
 return h;
}
J run(Case c,const fs::path& root, const fs::path& cli_request={}) {
 dp::test::FakeStageEnvironment env(c.name,root); const auto& ws=env.workspace();
 auto dataset=dp::test::materialize_fake_dataset(ws); dp::Validator validator(ws);
 auto grant=validator.validate(dataset); if(!grant.valid()) throw std::runtime_error("invalid fixture Dataset");
 Trace t; t.workspace=ws; t.c=c; t.request=dp::test::canonical_execution_request(c.name,dataset); t.dataset=grant.validated_artifact->dataset();
 dp::PipelineExecutionResult seed;
 if(c.route!="fresh") {
  dp::ImplementationRegistry seed_registry(dp::RegistryAdmissionPolicy::kTest); dp::test::register_fake_implementations(seed_registry);
  auto seed_context=env.make_context("seed"); seed=dp::PipelineExecutor(seed_registry,ws).execute(dp::test::canonical_execution_request("seed",dataset),seed_context);
  if(seed.status!=dp::StageStatus::kSucceeded) throw std::runtime_error("seed fixture did not succeed");
  if(c.route=="reuse") t.request.reused_partition=seed.partition_artifact;
  else t.request.merge_only_inputs=dp::MergeOnlyInputSet{*seed.partition_artifact,seed.local_artifacts};
 }
 auto before=snapshot(ws.normal_root); auto before_manifests=snapshot(ws.manifest_root); before.insert(before_manifests.begin(),before_manifests.end());
 dp::ImplementationRegistry registry(dp::RegistryAdmissionPolicy::kTest);
 registry.register_partition(dp::test::fake_partition_descriptor(),[&]()->std::unique_ptr<dp::PartitionStrategy>{factory(t,0,"P");return std::make_unique<P>(t);});
 registry.register_local_build(dp::test::fake_local_graph_descriptor(),[&]()->std::unique_ptr<dp::LocalGraphBuilder>{factory(t,1,"L"+std::to_string(t.factories[1]));return std::make_unique<L>(t);});
 registry.register_merge(dp::test::fake_merge_descriptor(),[&]()->std::unique_ptr<dp::GraphMerger>{factory(t,2,"M");return std::make_unique<M>(t);});
 auto context=env.make_context("probe"); context.cancellation_token=&t.token; context.metrics_sink=&t.metrics;
 if(c.cancel_at.rfind("published:",0)==0)
   t.published_marker=ws.manifest_root/c.name/"stages"/(c.cancel_at.substr(10)+".json");
 if(c.cancel_at=="before") t.signal("before_executor");
 J o={{"case",c.name},{"action",c.action},{"target",c.target},{"route",c.route},{"workspace",root.string()}};
 try {
  if(cli_request.empty()) {
   auto r=dp::PipelineExecutor(registry,ws).execute(t.request,context);
   o["status"]=str(r.status); o["executor_counts"]={r.invocations.partition,r.invocations.local_build,r.invocations.merge};
   o["stage_order"]=J::array(); for(auto s:r.stage_order)o["stage_order"].push_back(std::string(dp::to_string(s)));
   o["local_partition_ids"]=r.local_partition_ids;
   o["failure"]=r.failure?failure(*r.failure):J(nullptr);
   o["result_manifest_paths"]=J::array(); for(auto p:r.build_manifest_paths)o["result_manifest_paths"].push_back(p.string());
   o["final_artifact"]=r.artifact?J::parse(dp::serialize_artifact_ref(*r.artifact)):J(nullptr);
   o["evaluation_report"]=r.evaluation_report_path?J(r.evaluation_report_path->string()):J(nullptr);
   o["quarantine_id"]=r.quarantine_id?J(*r.quarantine_id):J(nullptr);
  } else {
   J document{{"request_version", "1.0.0"}, {"request_id", c.name}, {"operation", "execute"},
      {"study_class", "infrastructure_validation"},
      {"workspace", {{"root", ws.root.string()}, {"normal_root", ws.normal_root.string()},
        {"temporary_root", ws.temporary_root.string()}, {"manifest_root", ws.manifest_root.string()},
        {"report_root", ws.report_root.string()}, {"quarantine_root", ws.quarantine_root.string()},
        {"memory_budget_bytes", 1048576}, {"thread_budget", 1}}},
      {"dataset", J::parse(dp::serialize_artifact_ref(dataset))},
      {"implementations", {{"partition", t.request.partition_implementation_id},
        {"local_build", t.request.local_implementation_id}, {"merge", t.request.merge_implementation_id}}},
      {"stage_parameters", {{"partition", J::parse(t.request.partition_parameters_json)},
        {"local_build", J::parse(t.request.local_parameters_json)}, {"merge", J::parse(t.request.merge_parameters_json)}}},
      {"repetition_policy", {{"count", 2}, {"aggregation", "retain_all"}}}};
   const auto request_path=ws.temporary_root/"cli-request.json";
   dp::write_immutable_text(request_path,document.dump());
   const auto dispatched=dp::dispatch_runner_request(request_path,"execute",registry,context);
   o["cli_result"]=J::parse(dispatched.result_json);o["status"]=o["cli_result"]["outcome"];
   o["exit_code"]=dispatched.exit_code;
  }
 } catch(const std::exception& e) {o["escaped_exception"]=e.what();} catch(...) {o["escaped_exception"]="non-std exception";}
 o["factory_counts"]=counts(t.factories); o["method_counts"]=counts(t.methods);o["events"]=t.events;o["token_reads"]=t.reads;
 before.insert(t.previous_successes.begin(),t.previous_successes.end());
 o["prior_inputs_and_successes_unchanged"]=true; for(const auto& [p,sha]:before)if(!fs::is_regular_file(p)||dp::sha256_file(p)!=sha)o["prior_inputs_and_successes_unchanged"]=false;
 o["dataset_valid_after"]=validator.validate(dataset).valid();
 o["normal_artifacts"]=J::array(); for(const auto& x:fs::recursive_directory_iterator(ws.normal_root)) if(x.path().filename()=="artifact.json"){
  auto metadata=read(x.path());
  auto ref=dp::parse_artifact_ref(J{{"metadata_path",fs::relative(x.path(),ws.normal_root).string()},{"artifact_type",metadata["artifact_type"]},{"format_version",metadata["format_version"]},{"aggregate_checksum",metadata["aggregate_checksum"]}}.dump());
  o["normal_artifacts"].push_back(J{{"path",x.path().string()},{"valid",validator.validate_with_lineage(ref).valid()}});
 }
 o["manifests_on_disk"]=J::array();
 for(const auto& x:fs::recursive_directory_iterator(ws.root))if(x.is_regular_file() && x.path().extension()==".json") {
   const auto doc=read(x.path());
   if(doc.contains("manifest_version")) {
    J item={{"path",x.path().string()},{"document",doc}};
    try {auto parsed=dp::parse_build_manifest(doc.dump()); item["cpp_parsed"]=true;}catch(const std::exception& e){item["cpp_parse_error"]=e.what();}
    o["manifests_on_disk"].push_back(item);
   }
 }
 o["candidate_tracking"]=t.candidates;for(auto& item:o["candidate_tracking"])item["original_path_exists_after"]=fs::exists(item["path"].get<std::string>());
 o["files_after"]=snapshot(ws.root);
 save(root/"observation.json",o);return o;
}

void matrix() {
 const char* retained=std::getenv("DISKANN_LIFECYCLE_TEST_OUTPUT");
 dp::test::FakeStageEnvironment owner("lifecycle-matrix");
 const fs::path base=retained?fs::path(retained):owner.workspace().root/"cases";
 struct Expected { Case c; Counts factories, methods; std::string status; int manifests; };
 std::vector<Expected> cases{
  {{"success","success","",""},{1,2,1},{1,2,1},"SUCCEEDED",4},
  {{"cancel-before","success","","before"},{0,0,0},{0,0,0},"CANCELLED",0},
  {{"cancel-after-partition","success","","published:partition"},{1,0,0},{1,0,0},"CANCELLED",1},
  {{"cancel-between-locals","success","","published:local-0"},{1,1,0},{1,1,0},"CANCELLED",2},
  {{"cancel-before-merge","success","","published:local-1"},{1,2,0},{1,2,0},"CANCELLED",3},
  {{"cancel-before-evaluation","success","","published:merge"},{1,2,1},{1,2,1},"CANCELLED",4},
 };
 for (std::string label:{"P","L1","M"}) {
  const Counts entered=label=="P"?Counts{1,0,0}:label=="L1"?Counts{1,2,0}:Counts{1,2,1};
  auto not_entered=entered; --not_entered[label=="P"?0:label=="M"?2:1];
  const int prior=label=="P"?0:label=="L1"?2:3;
  for (std::string action:{"factory_std","factory_pipeline","factory_cancel","factory_cancel_exception","throw_std","throw_pipeline","throw_cancel","throw_std_cancelled","failed","failed_cancelled","cancelled","owned_failed","owned_throw","missing","missing_cancelled","failed_candidate","cancelled_candidate","throw_after_candidate"}) {
   const bool factory=action.rfind("factory_",0)==0;
   const bool cancelled=action=="factory_cancel"||action=="factory_cancel_exception"||action=="throw_cancel"||action=="cancelled";
   const bool invalid=action=="missing"||action=="missing_cancelled"||action=="failed_candidate"||action=="cancelled_candidate";
   cases.push_back({{action+"-"+label,action,label,""},entered,factory?not_entered:entered,
                    invalid?"INVALID_OUTPUT":cancelled?"CANCELLED":"FAILED",prior+(factory?0:1)});
  }
  cases.push_back({{"cancel-before-publication-"+label,"success",label,"after:"+label},entered,entered,"CANCELLED",prior+1});
  for (std::string action:{"failed_candidate","cancelled_candidate"})
   cases.push_back({{"cancel-with-invalid-"+action+label,action,label,"after:"+label},entered,entered,"INVALID_OUTPUT",prior+1});
 }
 for (std::string route:{"reuse","merge_only"}) {
  const Counts full=route=="reuse"?Counts{0,2,1}:Counts{0,0,1};
  cases.push_back({{route+"-success","success","","",false,route},full,full,"SUCCEEDED",route=="reuse"?4:1});
  cases.push_back({{route+"-cancel-before","success","","before",false,route},{0,0,0},{0,0,0},"CANCELLED",0});
  const auto target=route=="reuse"?"L0":"M";
  const Counts first=route=="reuse"?Counts{0,1,0}:Counts{0,0,1};
  for(std::string action:{"throw_std","throw_cancel","factory_std","factory_cancel"}) {
    const bool factory=action.rfind("factory_",0)==0;
    cases.push_back({{route+"-"+action,action,target,"",false,route},first,factory?Counts{}:first,
                     action=="throw_cancel"||action=="factory_cancel"?"CANCELLED":"FAILED",
                     (route=="reuse"?1:0)+(factory?0:1)});
  }
  cases.push_back({{route+"-cancel-final","success","","published:merge",false,route},full,full,"CANCELLED",route=="reuse"?4:1});
 }
 for (const auto& e:cases) {
  const auto o=run(e.c,base/e.c.name);
  std::cout << "lifecycle " << e.c.name << " " << o.at("status") << " F=" << o.at("factory_counts") << " S=" << o.at("method_counts") << " E=" << o.at("executor_counts") << std::endl;
  DP_EXPECT_EQ(o.at("factory_counts"), J(e.factories));
  DP_EXPECT_EQ(o.at("method_counts"), J(e.methods));
  DP_EXPECT_EQ(o.at("executor_counts"), J(e.methods));
  DP_EXPECT_EQ(o.at("status"),J(e.status));
  DP_EXPECT_EQ(o.at("stage_order").size(),e.methods[0]+e.methods[1]+e.methods[2]);
  DP_EXPECT_EQ(o.at("local_partition_ids").size(),e.methods[1]);
  DP_EXPECT_EQ(o.at("result_manifest_paths").size(),static_cast<std::size_t>(e.manifests));
  DP_EXPECT_TRUE(o.at("prior_inputs_and_successes_unchanged").get<bool>());
  DP_EXPECT_TRUE(o.at("dataset_valid_after").get<bool>());
  for(const auto& a:o.at("normal_artifacts")) DP_EXPECT_TRUE(a.at("valid").get<bool>());
  for(const auto& m:o.at("manifests_on_disk")) DP_EXPECT_TRUE(m.at("cpp_parsed").get<bool>());
  if(e.status!="SUCCEEDED") DP_EXPECT_TRUE(o.at("evaluation_report").is_null());
  if(e.status!="INVALID_OUTPUT") DP_EXPECT_TRUE(o.at("quarantine_id").is_null());
  if(e.status=="CANCELLED") {
   DP_EXPECT_TRUE(o.at("failure").is_object());
   if(o.at("failure").is_object()) DP_EXPECT_EQ(o.at("failure").at("category"),J("cancelled"));
  }
  if(e.c.action.rfind("throw_",0)==0 || e.c.action.rfind("factory_",0)==0) {
   if(e.c.action!="factory_cancel") DP_EXPECT_TRUE(o.at("failure").at("reason").get<std::string>().find("probe-")!=std::string::npos);
   if(e.c.action.rfind("throw_",0)==0 && !o.at("result_manifest_paths").empty()) {
    auto terminal=read(o.at("result_manifest_paths").back().get<std::string>());
    DP_EXPECT_EQ(terminal.at("status"),J(e.status));
    DP_EXPECT_TRUE(terminal.contains("failure"));
    if(terminal.contains("failure")) {
    DP_EXPECT_EQ(terminal.at("failure").at("category"),o.at("failure").at("category"));
    DP_EXPECT_TRUE(terminal.at("failure").at("reason").get<std::string>().find("probe-")!=std::string::npos);
    }
   }
  }
  if(e.c.action=="owned_throw") {
   for(const auto& m:o.at("manifests_on_disk")) {
    if(m.at("path").get<std::string>().find("/stages/")!=std::string::npos)
      DP_EXPECT_TRUE(!m.at("document").at("parameters").contains("stage_execution_manifest"));
   }
  }
  if(e.c.name.rfind("cancel-before-publication-",0)==0) {
   DP_EXPECT_TRUE(!o.at("candidate_tracking").empty());
   DP_EXPECT_TRUE(o.at("candidate_tracking").back().at("original_path_exists_after").get<bool>());
   const auto& candidate=o.at("candidate_tracking").back();
   if(candidate.at("original_path_exists_after").get<bool>())
     DP_EXPECT_EQ(dp::sha256_file(candidate.at("path").get<std::string>()),candidate.at("sha256").get<std::string>());
   if(!o.at("result_manifest_paths").empty()) {
    const auto terminal=read(o.at("result_manifest_paths").back().get<std::string>());
    DP_EXPECT_EQ(terminal.at("status"),J("CANCELLED"));
    DP_EXPECT_TRUE(!terminal.contains("validation_evidence_path"));
   }
  }
  if(e.c.name=="cancel-before-evaluation") {
   DP_EXPECT_TRUE(!o.at("final_artifact").is_null());
   if(!o.at("result_manifest_paths").empty()) {
    const auto terminal=read(o.at("result_manifest_paths").back().get<std::string>());
    DP_EXPECT_EQ(terminal.at("status"),J("SUCCEEDED"));
   }
  }
 }
 const auto write_failure=run({"evidence-write-failure","evidence_write_failure","P",""},base/"evidence-write-failure");
 DP_EXPECT_EQ(write_failure.at("status"),J("FAILED"));
 DP_EXPECT_TRUE(write_failure.at("result_manifest_paths").empty());
 DP_EXPECT_EQ(write_failure.at("executor_counts"),J({1,0,0}));
 DP_EXPECT_EQ(write_failure.at("failure").at("category"),J("source_mismatch"));
 DP_EXPECT_TRUE(write_failure.at("failure").at("reason").get<std::string>().find("probe-original-write-failure")!=std::string::npos);
 DP_EXPECT_TRUE(write_failure.at("failure").at("reason").get<std::string>().find("terminal evidence write failed")!=std::string::npos);
 for(const auto& e:cases) {
  if(e.c.route!="fresh") continue; // merge-only is a C++ path, not a CLI request.
  auto c=e.c; c.name="cli-"+c.name;
  const auto observed=run(c,base/c.name,"dispatch");
  const auto& cli=observed.at("cli_result");
  DP_EXPECT_EQ(cli.at("outcome"),J(e.status));
  DP_EXPECT_EQ(observed.at("exit_code"),J(e.status=="SUCCEEDED"?0:e.status=="INVALID_OUTPUT"?5:4));
  DP_EXPECT_EQ(observed.at("factory_counts"),J(e.factories));
  DP_EXPECT_EQ(observed.at("method_counts"),J(e.methods));
  DP_EXPECT_EQ(cli.at("stage_invocations").at("partition"),J(e.methods[0]));
  DP_EXPECT_EQ(cli.at("stage_invocations").at("local_build"),J(e.methods[1]));
  DP_EXPECT_EQ(cli.at("stage_invocations").at("merge"),J(e.methods[2]));
  const bool no_terminal=e.c.action.rfind("factory_",0)==0 || e.c.name=="cancel-before" || e.c.cancel_at.rfind("published:",0)==0;
  DP_EXPECT_EQ(cli.contains("build_manifest_path"),!no_terminal);
  if(cli.contains("build_manifest_path")) {
   const auto terminal=read(base/c.name/cli.at("build_manifest_path").get<std::string>());
   DP_EXPECT_EQ(terminal.at("status"),cli.at("outcome"));
   DP_EXPECT_EQ(terminal.at("run_id"),J(c.name));
  }
  if(e.status=="CANCELLED") DP_EXPECT_EQ(cli.at("failure").at("category"),J("cancelled"));
  if(e.status!="SUCCEEDED") DP_EXPECT_TRUE(!cli.contains("evaluation_report_path"));
  if(e.c.name=="cancel-before-evaluation") {
   DP_EXPECT_TRUE(cli.contains("artifact"));
   DP_EXPECT_TRUE(cli.contains("validation_evidence_path"));
  }
 }
}
} // namespace lifecycle

}  // namespace

int main() {
  lifecycle::matrix();
  runtime_services_matrix();
  reject_reuse("reuse-missing-local-claimed", ReuseCase::kMissingLocal, true);
  reject_reuse("reuse-missing-merge-claimed", ReuseCase::kMissingMerge, true);
  reject_reuse("reuse-missing-local-unclaimed", ReuseCase::kMissingLocal);
  reject_reuse("reuse-missing-merge-unclaimed", ReuseCase::kMissingMerge);
  reject_reuse("reuse-invalid-checksum", ReuseCase::kChecksum);
  reject_reuse("reuse-invalid-structure", ReuseCase::kStructure);
  reject_reuse("reuse-invalid-lineage", ReuseCase::kLineage);
  const auto partition_cancelled = run_with_cancel("partition.start", "cancel-partition");
  DP_EXPECT_EQ(partition_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(partition_cancelled.invocations.local_build, std::size_t{0});
  DP_EXPECT_EQ(partition_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!partition_cancelled.artifact.has_value());

  const auto local_cancelled = run_with_cancel("local_build.start", "cancel-local");
  DP_EXPECT_EQ(local_cancelled.status, dp::StageStatus::kCancelled);
  DP_EXPECT_EQ(local_cancelled.invocations.merge, std::size_t{0});
  DP_EXPECT_TRUE(!local_cancelled.artifact.has_value());

  const auto merge_cancelled = run_with_cancel("merge.start", "cancel-merge");
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
