#include "fakes/fake_pipeline.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/quarantine.hpp"
#include "diskann_pipeline/reuse.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/validation.hpp"
#include "support/contract_artifact_fixture.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace diskann_pipeline::test {

namespace {

class InvalidLocalGraph final : public LocalGraphBuilder {
 public:
  StageResult build(const ArtifactRef& partition, PartitionId partition_id,
                    const LocalBuildConfig&, StageContext& context) override {
    return {StageStatus::kSucceeded,
            materialize_local_candidate(context.temporary_storage.stage_root,
                                        context.workspace, partition,
                                        partition_id, true),
            {}};
  }
};

class InvalidMerge final : public GraphMerger {
 public:
  StageResult merge(const ArtifactRef& partition,
                    const std::vector<ArtifactRef>& local_graphs,
                    const std::filesystem::path&,
                    StageContext& context) override {
    return {StageStatus::kSucceeded,
            materialize_global_candidate(context.temporary_storage.stage_root,
                                         context.workspace, partition,
                                         local_graphs, true),
            {}};
  }
};

class EmptyPartition final : public PartitionStrategy {
 public:
  StageResult run(const ArtifactRef&, const std::filesystem::path&,
                  StageContext&) override {
    return {StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class EmptyLocalGraph final : public LocalGraphBuilder {
 public:
  StageResult build(const ArtifactRef&, PartitionId, const LocalBuildConfig&,
                    StageContext&) override {
    return {StageStatus::kSucceeded, std::nullopt, {}};
  }
};

class EmptyMerge final : public GraphMerger {
 public:
  StageResult merge(const ArtifactRef&, const std::vector<ArtifactRef>&,
                    const std::filesystem::path&, StageContext&) override {
    return {StageStatus::kSucceeded, std::nullopt, {}};
  }
};

}  // namespace

std::unique_ptr<LocalGraphBuilder> make_invalid_fake_local_graph() {
  return std::make_unique<InvalidLocalGraph>();
}

std::unique_ptr<GraphMerger> make_invalid_fake_merge() {
  return std::make_unique<InvalidMerge>();
}

std::unique_ptr<PartitionStrategy> make_empty_fake_partition() {
  return std::make_unique<EmptyPartition>();
}

std::unique_ptr<LocalGraphBuilder> make_empty_fake_local_graph() {
  return std::make_unique<EmptyLocalGraph>();
}

std::unique_ptr<GraphMerger> make_empty_fake_merge() {
  return std::make_unique<EmptyMerge>();
}

namespace {

NegativeCaseObservation artifact_case(const std::string& case_id) {
  ContractArtifactFixture fixture;
  ArtifactRef* target = &fixture.partition;
  enum class Target { kPartition, kLocal, kGlobal } selected = Target::kPartition;

  if (case_id == "map_absent") {
    replace_payload(fixture.workspace, fixture.partition,
                    "partition/0/local_to_global_map", "{}");
  } else if (case_id == "map_wrong_length") {
    replace_payload(fixture.workspace, fixture.partition,
                    "partition/0/local_to_global_map",
                    "{\"local_to_global\":[0,1,2]}");
  } else if (case_id == "map_out_of_range") {
    replace_payload(fixture.workspace, fixture.partition,
                    "partition/0/local_to_global_map",
                    "{\"local_to_global\":[0,1,2,6]}");
  } else if (case_id == "partition_id_missing") {
    target = &fixture.local0;
    selected = Target::kLocal;
    mutate_metadata(fixture.workspace, *target, [](auto& metadata) {
      metadata["structural"].erase("partition_id");
    });
  } else if (case_id == "partition_id_out_of_range") {
    target = &fixture.local0;
    selected = Target::kLocal;
    mutate_metadata(fixture.workspace, *target, [](auto& metadata) {
      metadata["structural"]["partition_id"] = 2;
    });
  } else if (case_id == "local_neighbor_out_of_range") {
    target = &fixture.local0;
    selected = Target::kLocal;
    replace_payload(fixture.workspace, *target, "local_graph",
                    "{\"adjacency\":[[4],[0],[1],[]]}");
  } else if (case_id == "global_neighbor_out_of_range") {
    target = &fixture.global;
    selected = Target::kGlobal;
    replace_payload(fixture.workspace, *target, "global_graph",
                    "{\"adjacency\":[[6],[0],[1],[2],[3],[]]}");
  } else if (case_id == "payload_checksum_altered") {
    mutate_metadata(fixture.workspace, fixture.partition, [](auto& metadata) {
      metadata["payloads"][0]["sha256"] = std::string(64, '0');
    });
  } else if (case_id == "aggregate_checksum_altered") {
    fixture.partition.aggregate_checksum = std::string(64, '0');
  } else if (case_id == "degree_exceeded") {
    target = &fixture.local0;
    selected = Target::kLocal;
    replace_payload(fixture.workspace, *target, "local_graph",
                    "{\"adjacency\":[[1,2,3],[0],[1],[]]}");
  } else if (case_id == "wrong_node_count") {
    target = &fixture.global;
    selected = Target::kGlobal;
    replace_payload(fixture.workspace, *target, "global_graph",
                    "{\"adjacency\":[[1],[0],[1],[2],[3]]}");
  } else if (case_id == "required_payload_missing") {
    std::filesystem::remove(metadata_path(fixture.workspace, fixture.partition)
                                .parent_path() /
                            "p0-map.json");
  } else {
    throw PipelineError(ErrorCategory::kInternal,
                        "unknown Artifact negative case");
  }

  Validator validator(fixture.workspace);
  const auto dataset = validator.validate(fixture.dataset);
  std::vector<ValidatedArtifact> sources;
  if (selected == Target::kPartition) {
    sources = {*dataset.validated_artifact};
  } else {
    const auto partition =
        validator.validate(fixture.partition, {*dataset.validated_artifact});
    if (selected == Target::kLocal) {
      sources = {*partition.validated_artifact};
    } else {
      const auto local0 =
          validator.validate(fixture.local0, {*partition.validated_artifact});
      const auto local1 =
          validator.validate(fixture.local1, {*partition.validated_artifact});
      sources = {*partition.validated_artifact, *local0.validated_artifact,
                 *local1.validated_artifact};
    }
  }

  const auto temporary =
      create_temporary_storage(fixture.workspace, "negative-" + case_id);
  const auto source_root = metadata_path(fixture.workspace, *target).parent_path();
  const auto candidate_root = temporary.stage_root / "candidate";
  std::filesystem::rename(source_root, candidate_root);
  ArtifactRef candidate{"candidate/artifact.json", target->artifact_type,
                        target->format_version, target->aggregate_checksum};
  auto candidate_workspace = fixture.workspace;
  candidate_workspace.normal_root = temporary.stage_root;
  const auto result = Validator(candidate_workspace).validate(candidate, sources);
  if (result.valid() || !result.evidence.failure_category) {
    throw PipelineError(ErrorCategory::kInternal,
                        "negative Artifact unexpectedly passed validation");
  }
  const auto evidence_path = persist_validation_evidence(
      fixture.workspace, case_id, result.evidence);
  BuildManifest manifest;
  manifest.run_id = "negative-" + case_id;
  manifest.stage = "validate";
  manifest.implementation = {"negative_fixture_validator", "1.0.0"};
  manifest.source = {"local", "master",
                     "0123456789abcdef0123456789abcdef01234567", false,
                     std::nullopt};
  manifest.toolchain = {"test", "c++", "C++17", "3.20", "CTest",
                        "Debug", {}, {}, {}};
  manifest.dataset = dataset.validated_artifact->dataset();
  manifest.parameters_json = "{}";
  manifest.resource_budget = {1, 1};
  manifest.input_artifacts = {candidate};
  manifest.status = StageStatus::kInvalidOutput;
  manifest.started_at = "2026-08-05T00:00:00Z";
  manifest.ended_at = "2026-08-05T00:00:01Z";
  manifest.failure = {*result.evidence.failure_category,
                      "negative fixture rejected", std::nullopt};
  manifest.validation_evidence_path =
      std::filesystem::relative(evidence_path, fixture.workspace.root);
  const auto manifest_path =
      persist_terminal_build_manifest(fixture.workspace, manifest);
  QuarantineManager(fixture.workspace).publish(
      "negative-" + case_id, candidate_root,
      candidate_root / "artifact.json", manifest_path, evidence_path,
      {*result.evidence.failure_category, "negative fixture rejected",
       std::nullopt},
      "2026-08-05T00:00:02Z");
  return {*result.evidence.failure_category,
          (case_id == "partition_id_missing" ||
                   case_id == "partition_id_out_of_range"
               ? "local_input"
               : "artifact_validation"),
          StageStatus::kInvalidOutput,
          {}, 0, 0, 0, true, false, true};
}

NegativeCaseObservation preflight_case(const std::string& case_id) {
  FakeStageEnvironment environment("negative-" + case_id);
  const auto dataset = materialize_fake_dataset(environment.workspace());
  auto partition = fake_partition_descriptor();
  if (case_id == "unsupported_version") {
    partition.requires.input_artifacts[0].accepted_format_versions = {
        "fake-dataset-json/v2"};
  } else if (case_id == "wrong_id_space") {
    partition.requires.input_artifacts[0].input_id_space = IdSpace::kGlobal;
  } else if (case_id == "capability_unsatisfied") {
    partition.requires.required_capabilities.push_back("missing.capability/v1");
  } else if (case_id == "checkpoint_unavailable") {
    partition.requires.required_services.push_back("checkpoint_store/v1");
  }
  ImplementationRegistry registry(RegistryAdmissionPolicy::kTest);
  registry.register_partition(partition, make_fake_partition);
  registry.register_local_build(fake_local_graph_descriptor(),
                                make_fake_local_graph);
  registry.register_merge(fake_merge_descriptor(), make_fake_merge);
  auto context = environment.make_context(case_id + "-context");
  const auto result = PipelineExecutor(registry, environment.workspace())
                          .execute(canonical_execution_request(case_id, dataset),
                                   context);
  return {result.failure ? result.failure->category : ErrorCategory::kInternal,
          "preflight", result.status, result.invocations, 0, 0, 0,
          result.quarantine_id.has_value(), result.artifact.has_value(), true};
}

class CancellationEvent final : public MetricsSink {
 public:
  CancellationEvent(FakeStageEnvironment& env, std::string name)
      : env_(env), name_(std::move(name)) {}
  void event(const std::string& name) override {
    env_.metrics().event(name);
    if (name == name_) { observed = true; env_.cancellation().cancel_now(); }
  }
  void increment(const std::string& name, std::uint64_t amount) override {
    env_.metrics().increment(name, amount);
  }
  bool observed{false};
 private:
  FakeStageEnvironment& env_;
  std::string name_;
};
class AfterPartitionPublication final : public CancellationToken {
 public:
  explicit AfterPartitionPublication(std::filesystem::path marker) : marker_(std::move(marker)) {}
  bool is_cancelled() const noexcept override {
    std::error_code error;
    observed = std::filesystem::is_regular_file(marker_, error);
    return observed;
  }
  mutable bool observed{false};
 private: std::filesystem::path marker_;
};
NegativeCaseObservation cancellation_case(const std::string& case_id, std::string gate) {
  FakeStageEnvironment environment("negative-" + case_id);
  const auto dataset = materialize_fake_dataset(environment.workspace());
  ImplementationRegistry registry(RegistryAdmissionPolicy::kTest);
  register_fake_implementations(registry);
  auto checkpoint = gate;
  if (checkpoint.rfind("local.", 0) == 0) checkpoint.replace(0, 6, "local_build.");
  CancellationEvent metrics(environment, checkpoint);
  AfterPartitionPublication boundary(environment.workspace().manifest_root / case_id / "stages/partition.json");
  auto context = environment.make_context(case_id + "-context");
  context.metrics_sink = &metrics;
  if (case_id == "cancel_before_stage") environment.cancellation().cancel_now();
  if (case_id == "cancel_between_stages") context.cancellation_token = &boundary;
  const auto result = PipelineExecutor(registry, environment.workspace())
                          .execute(canonical_execution_request(case_id, dataset), context);
  if (case_id != "cancel_before_stage" && !metrics.observed && !boundary.observed)
    throw std::runtime_error("cancellation checkpoint was not reached: " + gate);
  return {result.failure ? result.failure->category : ErrorCategory::kInternal,
          std::move(gate), result.status, result.invocations,
          result.evaluation_report_path ? 1U : 0U, 0, 0,
          result.quarantine_id.has_value(), result.artifact.has_value(), true};
}

}  // namespace

NegativeCaseObservation execute_negative_case(const std::string& case_id) {
  static const std::unordered_map<std::string, std::string>
      cancellations{
          {"cancel_before_stage", "stage_start"},
          {"cancel_between_stages", "next_stage_start"},
          {"cancel_partition_start", "partition.start"},
          {"cancel_partition_after_input", "partition.after_input"},
          {"cancel_partition_before_publish", "partition.before_publish"},
          {"cancel_local_start", "local.start"},
          {"cancel_local_after_input", "local.after_input"},
          {"cancel_local_before_publish", "local.before_publish"},
          {"cancel_merge_start", "merge.start"},
          {"cancel_merge_after_input", "merge.after_input"},
          {"cancel_merge_before_publish", "merge.before_publish"}};
  const auto cancellation = cancellations.find(case_id);
  if (cancellation != cancellations.end()) {
    return cancellation_case(case_id, cancellation->second);
  }
  if (case_id == "unsupported_version" || case_id == "wrong_id_space" ||
      case_id == "capability_unsatisfied" ||
      case_id == "checkpoint_unavailable") {
    return preflight_case(case_id);
  }
  if (case_id == "duplicate_registry_identity") {
    ImplementationRegistry registry(RegistryAdmissionPolicy::kTest);
    registry.register_partition(fake_partition_descriptor(), make_fake_partition);
    try {
      registry.register_partition(fake_partition_descriptor(),
                                  make_fake_partition);
    } catch (const PipelineError& error) {
      return {error.category(), "registry", StageStatus::kFailed, {}, 0, 0, 0,
              false, false, true};
    }
  }
  if (case_id == "reuse_dataset_mismatch") {
    ContractArtifactFixture fixture;
    Validator validator(fixture.workspace);
    const auto dataset = validator.validate(fixture.dataset);
    const auto before = sha256_file(metadata_path(fixture.workspace,
                                                  fixture.partition));
    const ArtifactRequirement incompatible{
        ArtifactType::kPartition, {"partition-artifact/v2"}, IdSpace::kLocal};
    const auto result = ArtifactReuseService(validator).resolve(
        fixture.partition, incompatible,
        {"partition.complete_local_to_global_map/v1"},
        {*dataset.validated_artifact});
    const auto after = sha256_file(metadata_path(fixture.workspace,
                                                 fixture.partition));
    return {result.failure ? result.failure->category : ErrorCategory::kInternal,
            "reuse", result.status, {}, 0, 0, 0, false, false,
            before == after};
  }
  return artifact_case(case_id);
}

void corrupt_fake_payload_for_case(const std::filesystem::path& payload,
                                   const std::string& case_id) {
  if (case_id.empty() || !std::filesystem::is_regular_file(payload)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "negative fixture payload is unavailable");
  }
  const auto size = std::filesystem::file_size(payload);
  std::ofstream output(payload, std::ios::binary | std::ios::trunc);
  output << std::string(static_cast<std::size_t>(size), 'x');
  if (!output) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to materialize negative fixture corruption");
  }
}

}  // namespace diskann_pipeline::test
