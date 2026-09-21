#include "diskann_pipeline/pipeline.hpp"

#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/checksum.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/quarantine.hpp"
#include "diskann_pipeline/reuse.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"
#include "validation/parameter_validator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <variant>

namespace diskann_pipeline {
namespace {

using Json = nlohmann::json;

#ifndef DISKANN_PIPELINE_PROJECT_ROOT
#define DISKANN_PIPELINE_PROJECT_ROOT ""
#endif

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

std::string manifest_parameters(const PipelineExecutionRequest& request,
                                const std::string& stage_parameters) {
  return Json{{"stage_parameters", Json::parse(stage_parameters)},
              {"source_config", Json::parse(request.source_config_json)}}
      .dump();
}

const SourceIdentity& source_for(const PipelineExecutionRequest& request,
                                 StageKind stage) {
  if (!request.stage_sources) {
    return request.source;
  }
  switch (stage) {
    case StageKind::kPartition:
      return request.stage_sources->partition;
    case StageKind::kLocalBuild:
      return request.stage_sources->local_build;
    case StageKind::kMerge:
      return request.stage_sources->merge;
  }
  throw PipelineError(ErrorCategory::kSchemaInvalid,
                      "unknown StageKind for source selection");
}

std::filesystem::path relative_to(const std::filesystem::path& path,
                                  const std::filesystem::path& root) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, root, error);
  if (error || relative.empty()) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to form workspace-relative provenance path");
  }
  return relative;
}

std::filesystem::path write_parameters(const WorkspaceRef& workspace,
                                       const std::string& run_id,
                                       const std::string& identity,
                                       const std::string& json) {
  const auto target = resolve_confined_path(
      workspace.manifest_root,
      std::filesystem::path(run_id) / "parameters" / (identity + ".json"));
  write_immutable_text(target, json);
  return target;
}

BuildManifest base_manifest(const PipelineExecutionRequest& request,
                            const StageContext& context,
                            const std::string& stage, StageKind stage_kind,
                            const ImplementationDescriptor& descriptor,
                            const DatasetIdentity& dataset,
                            const std::string& parameters,
                            std::vector<ArtifactRef> inputs,
                            std::string started_at) {
  return {request.run_id,
          stage,
          {descriptor.implementation_id, descriptor.implementation_version},
          source_for(request, stage_kind),
          request.toolchain,
          dataset,
          manifest_parameters(request, parameters),
          {context.memory_budget_bytes, context.thread_budget},
          std::move(inputs),
          {},
          StageStatus::kRunning,
          std::move(started_at),
          {},
          std::nullopt,
          std::nullopt,
          std::nullopt,
          "{}"};
}

std::string read_immutable_input(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "stage-owned BuildManifest is missing");
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  if (!input.eof() && input.fail()) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to read stage-owned BuildManifest");
  }
  return bytes.str();
}

std::filesystem::path confined_stage_manifest_path(
    const WorkspaceRef& workspace, const std::filesystem::path& path) {
  std::error_code error;
  const auto relative = path.is_absolute()
                            ? std::filesystem::relative(path, workspace.root,
                                                        error)
                            : path;
  if (error) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "unable to confine stage-owned BuildManifest path");
  }
  const auto confined = resolve_confined_path(workspace.root, relative);
  if (!std::filesystem::is_regular_file(confined)) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "stage-owned BuildManifest is not a regular file");
  }
  return confined;
}

bool terminal_manifest_status(StageStatus status) {
  return status != StageStatus::kPlanned && status != StageStatus::kRunning;
}

void attach_stage_execution_manifest(
    const WorkspaceRef& workspace, const PipelineExecutionRequest& request,
    const std::string& identity, const std::string& expected_stage,
    const StageResult& stage_result, BuildManifest& parent) {
  if (stage_result.build_manifest_path.empty()) {
    return;
  }
  const auto source_path = confined_stage_manifest_path(
      workspace, stage_result.build_manifest_path);
  const auto bytes = read_immutable_input(source_path);
  const auto subordinate = parse_build_manifest(bytes);
  if (subordinate.run_id != request.run_id ||
      subordinate.stage != expected_stage) {
    throw PipelineError(
        ErrorCategory::kSourceMismatch,
        "stage-owned BuildManifest does not bind the current run and stage");
  }
  if (!terminal_manifest_status(subordinate.status)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "stage-owned BuildManifest status is not terminal");
  }
  const bool requires_failure =
      subordinate.status == StageStatus::kFailed ||
      subordinate.status == StageStatus::kInvalidOutput;
  if (requires_failure != subordinate.failure.has_value()) {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "stage-owned BuildManifest has inconsistent failure evidence");
  }

  const auto persisted = resolve_confined_path(
      workspace.manifest_root,
      std::filesystem::path(request.run_id) / "stage-execution" /
          (identity + ".json"));
  write_immutable_text(persisted, bytes);
  auto parent_parameters = Json::parse(parent.parameters_json);
  parent_parameters["stage_execution_manifest"] =
      Json{{"path", relative_to(persisted, workspace.root).generic_string()},
           {"sha256", sha256_file(persisted)}};
  parent.parameters_json = parent_parameters.dump();
}

std::filesystem::path persist_stage_manifest(
    const WorkspaceRef& workspace, const std::string& run_id,
    const std::string& identity, BuildManifest manifest) {
  manifest.ended_at = now_utc();
  const auto target = resolve_confined_path(
      workspace.manifest_root,
      std::filesystem::path(run_id) / "stages" / (identity + ".json"));
  write_immutable_text(target, serialize_build_manifest(manifest));
  return target;
}

struct PublishedArtifact {
  ArtifactRef reference;
  ValidatedArtifact validated;
  std::filesystem::path evidence_path;
};

struct PublicationFailure {
  ErrorCategory category;
  std::string reason;
  std::filesystem::path evidence_path;
};

AvailableArtifactContract available_contract(const ValidatedArtifact& artifact) {
  return {artifact.reference().artifact_type,
          artifact.reference().format_version,
          artifact.id_space(),
          artifact.capabilities()};
}

CompatibilityResult preflight(
    const std::vector<ImplementationDescriptor>& chain,
    const std::vector<AvailableArtifactContract>& inputs,
    const DatasetIdentity& dataset, const StageContext& context) {
  for (const auto& consumer : chain) {
    const auto services = check_runtime_services(consumer.requires, context, dataset);
    if (!services.compatible) return services;
  }
  return CompatibilityChecker{}.check_chain(
      chain, inputs, runtime_service_capabilities(context, dataset));
}

void require_runtime_services(const ImplementationDescriptor& consumer,
                              const DatasetIdentity& dataset,
                              const StageContext& context) {
  const auto services = check_runtime_services(consumer.requires, context, dataset);
  if (!services.compatible)
    throw PipelineError(services.failure->category, services.failure->reason);
}

std::optional<FailureInfo> producer_contract_mismatch(
    const ValidatedArtifact& artifact,
    const ImplementationDescriptor& producer) {
  const auto& reference = artifact.reference();
  if (reference.artifact_type != producer.provides.artifact_type) {
    return FailureInfo{ErrorCategory::kCapabilityUnsatisfied,
                       "validated output Artifact type differs from producer provides",
                       std::nullopt};
  }
  if (reference.format_version != producer.provides.format_version) {
    return FailureInfo{ErrorCategory::kUnsupportedVersion,
                       "validated output format differs from producer provides",
                       std::nullopt};
  }
  if (artifact.id_space() != producer.provides.output_id_space) {
    return FailureInfo{ErrorCategory::kIdSpaceMismatch,
                       "validated output ID space differs from producer provides",
                       std::nullopt};
  }
  auto observed = artifact.capabilities();
  auto declared = producer.provides.capabilities;
  std::sort(observed.begin(), observed.end());
  std::sort(declared.begin(), declared.end());
  if (observed != declared) {
    return FailureInfo{ErrorCategory::kCapabilityUnsatisfied,
                       "validated output capabilities differ from producer provides",
                       std::nullopt};
  }
  return std::nullopt;
}

std::variant<PublishedArtifact, PublicationFailure> validate_and_publish(
    const WorkspaceRef& workspace, const std::string& run_id,
    const std::string& identity, const TemporaryStorageRef& temporary,
    const ArtifactRef& candidate,
    const std::vector<ValidatedArtifact>& sources,
    const ImplementationDescriptor& producer,
    const ImplementationDescriptor* next_consumer) {
  auto temporary_workspace = workspace;
  temporary_workspace.normal_root = temporary.stage_root;
  const Validator candidate_validator(temporary_workspace);
  const auto candidate_validation = candidate_validator.validate(candidate, sources);
  if (!candidate_validation.valid()) {
    const auto evidence = persist_validation_evidence(
        workspace, run_id + "-" + identity + "-invalid",
        candidate_validation.evidence);
    return PublicationFailure{
        candidate_validation.evidence.failure_category.value_or(
            ErrorCategory::kSchemaInvalid),
        candidate_validation.evidence.checks.empty()
            ? "candidate validation failed"
            : candidate_validation.evidence.checks.back().message,
        evidence};
  }

  const auto& validated_candidate = *candidate_validation.validated_artifact;
  std::optional<FailureInfo> handoff_failure =
      producer_contract_mismatch(validated_candidate, producer);
  if (!handoff_failure && next_consumer != nullptr) {
    std::vector<AvailableArtifactContract> available;
    available.reserve(sources.size() + 1);
    for (const auto& source : sources) {
      available.push_back(available_contract(source));
    }
    available.push_back(available_contract(validated_candidate));
    // Service assembly is not a property of this Artifact. Recheck it before
    // downstream factory creation, after recording this valid publication.
    // All existing Artifact/producer handoff rules remain in this gate.
    auto artifact_consumer = *next_consumer;
    artifact_consumer.requires.required_services.clear();
    const auto compatibility = CompatibilityChecker{}.check(
        artifact_consumer, available, {});
    if (!compatibility.compatible) handoff_failure = compatibility.failure;
  }
  if (handoff_failure) {
    auto evidence = candidate_validation.evidence;
    evidence.result = ValidationResult::kInvalid;
    evidence.failure_category = handoff_failure->category;
    evidence.checks.push_back(
        {"implementation_handoff", false, handoff_failure->reason,
         std::nullopt, std::nullopt});
    const auto evidence_path = persist_validation_evidence(
        workspace, run_id + "-" + identity + "-handoff-invalid", evidence);
    return PublicationFailure{handoff_failure->category,
                              handoff_failure->reason, evidence_path};
  }

  const auto source_directory = temporary.stage_root / "candidate";
  const auto destination_relative =
      std::filesystem::path(run_id) / identity;
  const auto destination =
      resolve_confined_path(workspace.normal_root, destination_relative);
  if (std::filesystem::exists(destination)) {
    throw PipelineError(ErrorCategory::kPathEscape,
                        "normal Artifact destination already exists");
  }
  std::filesystem::create_directories(destination.parent_path());
  std::error_code error;
  std::filesystem::rename(source_directory, destination, error);
  if (error) {
    throw PipelineError(ErrorCategory::kInternal,
                        "atomic Artifact publication failed: " + error.message());
  }
  ArtifactRef published{destination_relative / "artifact.json",
                        candidate.artifact_type,
                        candidate.format_version,
                        candidate.aggregate_checksum};
  const Validator normal_validator(workspace);
  const auto normal_validation = normal_validator.validate(published, sources);
  if (!normal_validation.valid()) {
    throw PipelineError(ErrorCategory::kInternal,
                        "fresh validation failed after atomic publication");
  }
  const auto evidence = persist_validation_evidence(
      workspace, run_id + "-" + identity + "-valid",
      normal_validation.evidence);
  return PublishedArtifact{published, *normal_validation.validated_artifact,
                           evidence};
}

PipelineExecutionResult terminal_failure(
    PipelineExecutionResult result, StageStatus status, ErrorCategory category,
    std::string reason) {
  result.status = status;
  result.failure = FailureInfo{category, std::move(reason), std::nullopt};
  return result;
}

PipelineExecutionResult terminal_output_violation(
    PipelineExecutionResult result, const PipelineExecutionRequest& request,
    const WorkspaceRef& workspace, const TemporaryStorageRef& temporary,
    const std::string& identity, const ImplementationDescriptor& descriptor,
    BuildManifest manifest, const StageResult& stage) {
  const std::string reason =
      "stage violated the terminal-output contract";
  const auto candidate_root = temporary.stage_root / "candidate";
  std::filesystem::create_directories(candidate_root);
  std::filesystem::path candidate_metadata = candidate_root / "artifact.json";
  if (!stage.output_candidate) {
    candidate_metadata = candidate_root / "terminal-output.json";
    write_immutable_text(
        candidate_metadata,
        Json{{"diagnostic", "output_candidate_missing"},
             {"stage_status", to_string(stage.status)}}
            .dump());
  } else {
    candidate_metadata = temporary.stage_root /
                         stage.output_candidate->metadata_path;
  }
  ValidationEvidence evidence;
  evidence.validator_version = "terminal-output-contract/v1";
  evidence.artifact_candidate = {
      descriptor.provides.artifact_type, descriptor.provides.format_version,
      std::filesystem::relative(candidate_metadata, temporary.stage_root),
      stage.output_candidate
          ? std::optional<std::string>(
                stage.output_candidate->aggregate_checksum)
          : std::nullopt};
  evidence.checks = {{"terminal_output_contract", false, reason,
                      std::nullopt, std::nullopt}};
  evidence.result = ValidationResult::kInvalid;
  evidence.failure_category = ErrorCategory::kSchemaInvalid;
  const auto evidence_path = persist_validation_evidence(
      workspace, request.run_id + "-" + identity + "-terminal-output",
      evidence);
  manifest.status = StageStatus::kInvalidOutput;
  manifest.failure =
      FailureInfo{ErrorCategory::kSchemaInvalid, reason, std::nullopt};
  manifest.validation_evidence_path = relative_to(evidence_path, workspace.root);
  const auto manifest_path = persist_stage_manifest(
      workspace, request.run_id, identity, manifest);
  const auto quarantine_id = request.run_id + "-" + identity + "-invalid";
  QuarantineManager(workspace).publish(
      quarantine_id, candidate_root, candidate_metadata, manifest_path,
      evidence_path,
      {ErrorCategory::kSchemaInvalid, reason, std::nullopt}, now_utc());
  result.build_manifest_paths.push_back(manifest_path);
  result.validation_evidence_paths.push_back(evidence_path);
  result.quarantine_id = quarantine_id;
  return terminal_failure(std::move(result), StageStatus::kInvalidOutput,
                          ErrorCategory::kSchemaInvalid, reason);
}

std::vector<PartitionId> partition_ids(const ValidatedArtifact& partition) {
  const auto structural = Json::parse(partition.structural_json());
  std::vector<PartitionId> result;
  for (const auto& record : structural.at("partitions")) {
    result.push_back(record.at("partition_id").get<PartitionId>());
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace

PipelineExecutor::PipelineExecutor(ImplementationRegistry& registry,
                                   WorkspaceRef workspace)
    : registry_(registry), workspace_(std::move(workspace)) {}

PipelineExecutionResult PipelineExecutor::execute(
    const PipelineExecutionRequest& request, StageContext& context) const {
  PipelineExecutionResult result;
  try {
    validate_stage_context(context);
    const Validator validator(workspace_);
    const auto dataset_validation = validator.validate(request.dataset);
    if (!dataset_validation.valid()) {
      return terminal_failure(
          std::move(result), StageStatus::kFailed,
          dataset_validation.evidence.failure_category.value_or(
              ErrorCategory::kSchemaInvalid),
          "dataset input failed validation");
    }
    const auto dataset = *dataset_validation.validated_artifact;

    if (request.merge_only_inputs) {
      if (request.reused_partition) {
        return terminal_failure(
            std::move(result), StageStatus::kFailed,
            ErrorCategory::kSchemaInvalid,
            "merge_only_inputs and reused_partition are mutually exclusive");
      }
      const auto& merge_descriptor = registry_.resolve_descriptor(
          request.merge_implementation_id,
          request.merge_implementation_version, StageKind::kMerge);
      detail::validate_implementation_parameters(
          merge_descriptor, request.merge_parameters_json,
          DISKANN_PIPELINE_PROJECT_ROOT);

      const auto dataset_evidence = persist_validation_evidence(
          workspace_, request.run_id + "-merge-only-dataset-valid",
          dataset_validation.evidence);
      result.validation_evidence_paths.push_back(dataset_evidence);

      const auto partition_validation = validator.validate(
          request.merge_only_inputs->partition, {dataset});
      const auto partition_evidence = persist_validation_evidence(
          workspace_, request.run_id + "-merge-only-partition-" +
                          (partition_validation.valid() ? "valid" : "invalid"),
          partition_validation.evidence);
      result.validation_evidence_paths.push_back(partition_evidence);
      if (!partition_validation.valid()) {
        return terminal_failure(
            std::move(result), StageStatus::kFailed,
            partition_validation.evidence.failure_category.value_or(
                ErrorCategory::kSchemaInvalid),
            "Merge-only Partition failed fresh validation");
      }
      const auto partition = *partition_validation.validated_artifact;
      const auto expected_ids = partition_ids(partition);
      if (request.merge_only_inputs->local_graphs.size() !=
          expected_ids.size()) {
        return terminal_failure(std::move(result), StageStatus::kFailed,
                                ErrorCategory::kSourceMismatch,
                                "Merge-only LocalGraph set is incomplete");
      }
      std::vector<ValidatedArtifact> validated_locals;
      validated_locals.reserve(expected_ids.size());
      for (std::size_t index = 0; index < expected_ids.size(); ++index) {
        const auto& reference =
            request.merge_only_inputs->local_graphs[index];
        const auto local_validation = validator.validate(reference, {partition});
        const auto local_evidence = persist_validation_evidence(
            workspace_, request.run_id + "-merge-only-local-" +
                            std::to_string(index) + "-" +
                            (local_validation.valid() ? "valid" : "invalid"),
            local_validation.evidence);
        result.validation_evidence_paths.push_back(local_evidence);
        if (!local_validation.valid() ||
            !local_validation.validated_artifact->partition_id() ||
            *local_validation.validated_artifact->partition_id() !=
                expected_ids[index]) {
          return terminal_failure(
              std::move(result), StageStatus::kFailed,
              local_validation.evidence.failure_category.value_or(
                  ErrorCategory::kSourceMismatch),
              "Merge-only LocalGraph ownership/order failed fresh validation");
        }
        validated_locals.push_back(*local_validation.validated_artifact);
      }

      std::vector<AvailableArtifactContract> available{
          available_contract(dataset), available_contract(partition)};
      for (const auto& local : validated_locals) {
        available.push_back(available_contract(local));
      }
      const auto compatibility = preflight(
          {merge_descriptor}, available, dataset.dataset(), context);
      if (!compatibility.compatible) {
        return terminal_failure(std::move(result), StageStatus::kFailed,
                                compatibility.failure->category,
                                compatibility.failure->reason);
      }

      const auto merge_started_at = now_utc();
      const auto merge_parameters = write_parameters(
          workspace_, request.run_id, "merge", request.merge_parameters_json);
      context.temporary_storage =
          create_temporary_storage(workspace_, request.run_id + "-merge");
      auto merge = registry_.resolve_merge_factory(
          request.merge_implementation_id,
          request.merge_implementation_version)();
      ++result.invocations.merge;
      result.stage_order.push_back(StageKind::kMerge);
      const auto stage = merge->merge(
          partition.reference(), request.merge_only_inputs->local_graphs,
          merge_parameters, context);
      std::vector<ArtifactRef> manifest_inputs{request.dataset,
                                               partition.reference()};
      manifest_inputs.insert(manifest_inputs.end(),
                             request.merge_only_inputs->local_graphs.begin(),
                             request.merge_only_inputs->local_graphs.end());
      auto manifest = base_manifest(
          request, context, "merge", StageKind::kMerge, merge_descriptor,
          dataset.dataset(), request.merge_parameters_json,
          std::move(manifest_inputs), merge_started_at);
      attach_stage_execution_manifest(workspace_, request, "merge", "merge",
                                      stage, manifest);
      const bool terminal_output_valid =
          (stage.status == StageStatus::kSucceeded && stage.output_candidate) ||
          ((stage.status == StageStatus::kFailed ||
            stage.status == StageStatus::kCancelled) &&
           !stage.output_candidate);
      if (!terminal_output_valid) {
        return terminal_output_violation(
            std::move(result), request, workspace_, context.temporary_storage,
            "merge", merge_descriptor, manifest, stage);
      }
      if (stage.status != StageStatus::kSucceeded) {
        manifest.status = stage.status;
        if (stage.status == StageStatus::kFailed) {
          manifest.failure = FailureInfo{ErrorCategory::kInternal,
                                         "Merge implementation failed",
                                         std::nullopt};
        }
        result.build_manifest_paths.push_back(persist_stage_manifest(
            workspace_, request.run_id, "merge", manifest));
        return terminal_failure(
            std::move(result), stage.status,
            stage.status == StageStatus::kCancelled
                ? ErrorCategory::kCancelled
                : ErrorCategory::kInternal,
            stage.status == StageStatus::kCancelled
                ? "Merge was cancelled"
                : "Merge implementation failed");
      }
      std::vector<ValidatedArtifact> merge_sources{partition};
      merge_sources.insert(merge_sources.end(), validated_locals.begin(),
                           validated_locals.end());
      auto publication = validate_and_publish(
          workspace_, request.run_id, "merge", context.temporary_storage,
          *stage.output_candidate, merge_sources, merge_descriptor, nullptr);
      if (std::holds_alternative<PublicationFailure>(publication)) {
        const auto& failure = std::get<PublicationFailure>(publication);
        manifest.status = StageStatus::kInvalidOutput;
        manifest.failure =
            FailureInfo{failure.category, failure.reason, std::nullopt};
        manifest.validation_evidence_path =
            relative_to(failure.evidence_path, workspace_.root);
        const auto manifest_path = persist_stage_manifest(
            workspace_, request.run_id, "merge", manifest);
        result.build_manifest_paths.push_back(manifest_path);
        const auto quarantine_id = request.run_id + "-merge-invalid";
        QuarantineManager(workspace_).publish(
            quarantine_id,
            context.temporary_storage.stage_root / "candidate",
            context.temporary_storage.stage_root / "candidate/artifact.json",
            manifest_path, failure.evidence_path,
            {failure.category, failure.reason, std::nullopt}, now_utc());
        result.quarantine_id = quarantine_id;
        result.validation_evidence_paths.push_back(failure.evidence_path);
        return terminal_failure(std::move(result),
                                StageStatus::kInvalidOutput,
                                failure.category, failure.reason);
      }
      auto published = std::get<PublishedArtifact>(std::move(publication));
      result.artifact = published.reference;
      result.validation_evidence_paths.push_back(published.evidence_path);
      manifest.status = StageStatus::kSucceeded;
      manifest.output_artifacts = {published.reference};
      manifest.validation_evidence_path =
          relative_to(published.evidence_path, workspace_.root);
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, "merge", manifest));
      const auto report = evaluate_validated_global_graph(
          workspace_, published.validated,
          relative_to(published.evidence_path, workspace_.root),
          request.run_id + "-evaluation", now_utc());
      result.evaluation_report_path =
          persist_evaluation_report(workspace_, report);
      result.status = StageStatus::kSucceeded;
      return result;
    }

    const auto& partition_descriptor = registry_.resolve_descriptor(
        request.partition_implementation_id,
        request.partition_implementation_version, StageKind::kPartition);
    const auto& local_descriptor = registry_.resolve_descriptor(
        request.local_implementation_id, request.local_implementation_version,
        StageKind::kLocalBuild);
    const auto& merge_descriptor = registry_.resolve_descriptor(
        request.merge_implementation_id, request.merge_implementation_version,
        StageKind::kMerge);
    if (!request.reused_partition) {
      const auto compatibility = preflight(
          {partition_descriptor, local_descriptor, merge_descriptor},
          {available_contract(dataset)}, dataset.dataset(), context);
      if (!compatibility.compatible) {
        return terminal_failure(std::move(result), StageStatus::kFailed,
                                compatibility.failure->category,
                                compatibility.failure->reason);
      }
    }
    detail::validate_implementation_parameters(
        partition_descriptor, request.partition_parameters_json,
        DISKANN_PIPELINE_PROJECT_ROOT);
    detail::validate_implementation_parameters(
        local_descriptor, request.local_parameters_json,
        DISKANN_PIPELINE_PROJECT_ROOT);
    detail::validate_implementation_parameters(
        merge_descriptor, request.merge_parameters_json,
        DISKANN_PIPELINE_PROJECT_ROOT);

    std::optional<ValidatedArtifact> validated_partition;
    if (request.reused_partition) {
      const auto stage_started_at = now_utc();
      const ArtifactReuseService reuse(validator);
      const ArtifactRequirement requirement{
          ArtifactType::kPartition, {"partition-artifact/v1"}, IdSpace::kLocal};
      auto reused = reuse.resolve(
          *request.reused_partition, requirement,
          {"partition.complete_local_to_global_map/v1"}, {dataset});
      if (reused.status == StageStatus::kReused) {
        // The skipped producer's promises do not describe this cached input.
        // Only the future LocalGraph uses provides; its actual output is still
        // validated at publication before Merge can consume it.
        const auto compatibility = preflight(
            {local_descriptor, merge_descriptor},
            {available_contract(dataset),
             available_contract(*reused.validated_artifact)},
            dataset.dataset(), context);
        if (!compatibility.compatible) {
          reused.status = StageStatus::kFailed;
          reused.failure = compatibility.failure;
          reused.provenance = {false, compatibility.failure->reason,
                               *request.reused_partition};
          reused.validated_artifact.reset();
          // Preserve the independent VALID evidence: this is admission failure,
          // not a corrupt Artifact or an invalid newly produced output.
        }
      }
      const auto evidence_path = persist_validation_evidence(
          workspace_, request.run_id + "-partition-reuse",
          reused.validation_evidence);
      result.validation_evidence_paths.push_back(evidence_path);
      auto manifest = base_manifest(request, context, "partition",
                                    StageKind::kPartition,
                                    partition_descriptor, dataset.dataset(),
                                    request.partition_parameters_json,
                                    {request.dataset}, stage_started_at);
      manifest.validation_evidence_path =
          relative_to(evidence_path, workspace_.root);
      manifest.reuse = reused.provenance;
      if (reused.status != StageStatus::kReused) {
        manifest.status = StageStatus::kFailed;
        manifest.failure = reused.failure;
        result.build_manifest_paths.push_back(persist_stage_manifest(
            workspace_, request.run_id, "partition-reuse-failed", manifest));
        return terminal_failure(
            std::move(result), StageStatus::kFailed,
            reused.failure ? reused.failure->category : ErrorCategory::kReuseMismatch,
            reused.failure ? reused.failure->reason : "Partition reuse failed");
      }
      validated_partition = reused.validated_artifact;
      result.partition_artifact = request.reused_partition;
      manifest.status = StageStatus::kReused;
      manifest.output_artifacts = {*request.reused_partition};
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, "partition-reused", manifest));
    } else {
      const auto stage_started_at = now_utc();
      const auto parameter_path = write_parameters(
          workspace_, request.run_id, "partition",
          request.partition_parameters_json);
      context.temporary_storage =
          create_temporary_storage(workspace_, request.run_id + "-partition");
      auto partition = registry_.resolve_partition_factory(
          request.partition_implementation_id,
          request.partition_implementation_version)();
      ++result.invocations.partition;
      result.stage_order.push_back(StageKind::kPartition);
      const auto stage = partition->run(request.dataset, parameter_path, context);
      auto manifest = base_manifest(request, context, "partition",
                                    StageKind::kPartition,
                                    partition_descriptor, dataset.dataset(),
                                    request.partition_parameters_json,
                                    {request.dataset}, stage_started_at);
      attach_stage_execution_manifest(workspace_, request, "partition",
                                      "partition", stage, manifest);
      const bool terminal_output_valid =
          (stage.status == StageStatus::kSucceeded && stage.output_candidate) ||
          ((stage.status == StageStatus::kFailed ||
            stage.status == StageStatus::kCancelled) &&
           !stage.output_candidate);
      if (!terminal_output_valid) {
        return terminal_output_violation(
            std::move(result), request, workspace_, context.temporary_storage,
            "partition", partition_descriptor, manifest, stage);
      }
      if (stage.status != StageStatus::kSucceeded) {
        const auto terminal_status = stage.status;
        manifest.status = terminal_status;
        if (stage.status == StageStatus::kFailed) {
          manifest.failure = FailureInfo{ErrorCategory::kInternal,
                                         "Partition implementation failed",
                                         std::nullopt};
        }
        result.build_manifest_paths.push_back(persist_stage_manifest(
            workspace_, request.run_id, "partition", manifest));
        return terminal_failure(
            std::move(result), terminal_status,
            terminal_status == StageStatus::kCancelled
                ? ErrorCategory::kCancelled
                : ErrorCategory::kInternal,
            terminal_status == StageStatus::kCancelled
                ? "Partition was cancelled"
                : "Partition implementation failed");
      }
      auto publication = validate_and_publish(
          workspace_, request.run_id, "partition", context.temporary_storage,
          *stage.output_candidate, {dataset}, partition_descriptor,
          &local_descriptor);
      if (std::holds_alternative<PublicationFailure>(publication)) {
        const auto& failure = std::get<PublicationFailure>(publication);
        manifest.status = StageStatus::kInvalidOutput;
        manifest.failure = FailureInfo{failure.category, failure.reason,
                                       std::nullopt};
        manifest.validation_evidence_path =
            relative_to(failure.evidence_path, workspace_.root);
        const auto manifest_path = persist_stage_manifest(
            workspace_, request.run_id, "partition", manifest);
        result.build_manifest_paths.push_back(manifest_path);
        const auto quarantine_id = request.run_id + "-partition-invalid";
        QuarantineManager(workspace_).publish(
            quarantine_id, context.temporary_storage.stage_root / "candidate",
            context.temporary_storage.stage_root / "candidate/artifact.json",
            manifest_path, failure.evidence_path,
            {failure.category, failure.reason, std::nullopt}, now_utc());
        result.quarantine_id = quarantine_id;
        result.validation_evidence_paths.push_back(failure.evidence_path);
        return terminal_failure(std::move(result), StageStatus::kInvalidOutput,
                                failure.category, failure.reason);
      }
      auto published = std::get<PublishedArtifact>(std::move(publication));
      validated_partition = published.validated;
      result.partition_artifact = published.reference;
      result.validation_evidence_paths.push_back(published.evidence_path);
      manifest.status = StageStatus::kSucceeded;
      manifest.output_artifacts = {published.reference};
      manifest.validation_evidence_path =
          relative_to(published.evidence_path, workspace_.root);
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, "partition", manifest));
    }

    const auto ids = partition_ids(*validated_partition);
    std::vector<ValidatedArtifact> validated_locals;
    for (const auto id : ids) {
      // A stage may have changed the service assembly. Reject any remaining
      // known incompatibility before another expensive Local factory is made.
      require_runtime_services(local_descriptor, dataset.dataset(), context);
      require_runtime_services(merge_descriptor, dataset.dataset(), context);
      const auto stage_started_at = now_utc();
      result.local_partition_ids.push_back(id);
      const auto identity = "local-" + std::to_string(id);
      Json local_parameters = Json::parse(request.local_parameters_json);
      local_parameters["partition_id"] = id;
      const auto parameters = local_parameters.dump();
      const auto parameter_path = write_parameters(
          workspace_, request.run_id, identity, parameters);
      context.temporary_storage = create_temporary_storage(
          workspace_, request.run_id + "-" + identity);
      auto local = registry_.resolve_local_build_factory(
          request.local_implementation_id,
          request.local_implementation_version)();
      ++result.invocations.local_build;
      result.stage_order.push_back(StageKind::kLocalBuild);
      const auto stage = local->build(
          validated_partition->reference(), id, {parameter_path}, context);
      std::vector<ArtifactRef> local_inputs;
      const auto declares_dataset = std::any_of(
          local_descriptor.requires.input_artifacts.begin(),
          local_descriptor.requires.input_artifacts.end(),
          [](const ArtifactRequirement& requirement) {
            return requirement.artifact_type == ArtifactType::kDataset;
          });
      if (declares_dataset) {
        local_inputs.push_back(request.dataset);
      }
      local_inputs.push_back(validated_partition->reference());
      auto manifest = base_manifest(
          request, context, "local_build", StageKind::kLocalBuild,
          local_descriptor, dataset.dataset(),
          parameters, std::move(local_inputs), stage_started_at);
      attach_stage_execution_manifest(workspace_, request, identity,
                                      "local_build", stage, manifest);
      const bool terminal_output_valid =
          (stage.status == StageStatus::kSucceeded && stage.output_candidate) ||
          ((stage.status == StageStatus::kFailed ||
            stage.status == StageStatus::kCancelled) &&
           !stage.output_candidate);
      if (!terminal_output_valid) {
        return terminal_output_violation(
            std::move(result), request, workspace_, context.temporary_storage,
            identity, local_descriptor, manifest, stage);
      }
      if (stage.status != StageStatus::kSucceeded) {
        const auto terminal_status = stage.status;
        manifest.status = terminal_status;
        if (stage.status == StageStatus::kFailed) {
          manifest.failure = FailureInfo{ErrorCategory::kInternal,
                                         "Local Build implementation failed",
                                         std::nullopt};
        }
        result.build_manifest_paths.push_back(persist_stage_manifest(
            workspace_, request.run_id, identity, manifest));
        return terminal_failure(
            std::move(result), terminal_status,
            terminal_status == StageStatus::kCancelled
                ? ErrorCategory::kCancelled
                : ErrorCategory::kInternal,
            terminal_status == StageStatus::kCancelled
                ? "Local Build was cancelled"
                : "Local Build implementation failed");
      }
      auto publication = validate_and_publish(
          workspace_, request.run_id, identity, context.temporary_storage,
          *stage.output_candidate, {*validated_partition}, local_descriptor,
          &merge_descriptor);
      if (std::holds_alternative<PublicationFailure>(publication)) {
        const auto& failure = std::get<PublicationFailure>(publication);
        manifest.status = StageStatus::kInvalidOutput;
        manifest.failure = FailureInfo{failure.category, failure.reason,
                                       std::nullopt};
        manifest.validation_evidence_path =
            relative_to(failure.evidence_path, workspace_.root);
        const auto manifest_path = persist_stage_manifest(
            workspace_, request.run_id, identity, manifest);
        result.build_manifest_paths.push_back(manifest_path);
        const auto quarantine_id = request.run_id + "-" + identity + "-invalid";
        QuarantineManager(workspace_).publish(
            quarantine_id, context.temporary_storage.stage_root / "candidate",
            context.temporary_storage.stage_root / "candidate/artifact.json",
            manifest_path, failure.evidence_path,
            {failure.category, failure.reason, std::nullopt}, now_utc());
        result.quarantine_id = quarantine_id;
        result.validation_evidence_paths.push_back(failure.evidence_path);
        return terminal_failure(std::move(result), StageStatus::kInvalidOutput,
                                failure.category, failure.reason);
      }
      auto published = std::get<PublishedArtifact>(std::move(publication));
      validated_locals.push_back(published.validated);
      result.local_artifacts.push_back(published.reference);
      result.validation_evidence_paths.push_back(published.evidence_path);
      manifest.status = StageStatus::kSucceeded;
      manifest.output_artifacts = {published.reference};
      manifest.validation_evidence_path =
          relative_to(published.evidence_path, workspace_.root);
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, identity, manifest));
    }

    require_runtime_services(merge_descriptor, dataset.dataset(), context);
    const auto merge_started_at = now_utc();
    const auto merge_parameters = write_parameters(
        workspace_, request.run_id, "merge", request.merge_parameters_json);
    context.temporary_storage =
        create_temporary_storage(workspace_, request.run_id + "-merge");
    auto merge = registry_.resolve_merge_factory(
        request.merge_implementation_id, request.merge_implementation_version)();
    ++result.invocations.merge;
    result.stage_order.push_back(StageKind::kMerge);
    const auto stage = merge->merge(validated_partition->reference(),
                                    result.local_artifacts, merge_parameters,
                                    context);
    std::vector<ArtifactRef> merge_inputs{validated_partition->reference()};
    merge_inputs.insert(merge_inputs.end(), result.local_artifacts.begin(),
                        result.local_artifacts.end());
    auto manifest = base_manifest(request, context, "merge", StageKind::kMerge,
                                  merge_descriptor, dataset.dataset(),
                                  request.merge_parameters_json, merge_inputs,
                                  merge_started_at);
    attach_stage_execution_manifest(workspace_, request, "merge", "merge",
                                    stage, manifest);
    const bool terminal_output_valid =
        (stage.status == StageStatus::kSucceeded && stage.output_candidate) ||
        ((stage.status == StageStatus::kFailed ||
          stage.status == StageStatus::kCancelled) &&
         !stage.output_candidate);
    if (!terminal_output_valid) {
      return terminal_output_violation(
          std::move(result), request, workspace_, context.temporary_storage,
          "merge", merge_descriptor, manifest, stage);
    }
    if (stage.status != StageStatus::kSucceeded) {
      const auto terminal_status = stage.status;
      manifest.status = terminal_status;
      if (stage.status == StageStatus::kFailed) {
        manifest.failure = FailureInfo{ErrorCategory::kInternal,
                                       "Merge implementation failed",
                                       std::nullopt};
      }
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, "merge", manifest));
      return terminal_failure(
          std::move(result), terminal_status,
          terminal_status == StageStatus::kCancelled
              ? ErrorCategory::kCancelled
              : ErrorCategory::kInternal,
          terminal_status == StageStatus::kCancelled
              ? "Merge was cancelled"
              : "Merge implementation failed");
    }
    std::vector<ValidatedArtifact> merge_sources{*validated_partition};
    merge_sources.insert(merge_sources.end(), validated_locals.begin(),
                         validated_locals.end());
    auto publication = validate_and_publish(
        workspace_, request.run_id, "merge", context.temporary_storage,
        *stage.output_candidate, merge_sources, merge_descriptor, nullptr);
    if (std::holds_alternative<PublicationFailure>(publication)) {
      const auto& failure = std::get<PublicationFailure>(publication);
      manifest.status = StageStatus::kInvalidOutput;
      manifest.failure =
          FailureInfo{failure.category, failure.reason, std::nullopt};
      manifest.validation_evidence_path =
          relative_to(failure.evidence_path, workspace_.root);
      const auto manifest_path = persist_stage_manifest(
          workspace_, request.run_id, "merge", manifest);
      result.build_manifest_paths.push_back(manifest_path);
      const auto quarantine_id = request.run_id + "-merge-invalid";
      QuarantineManager(workspace_).publish(
          quarantine_id, context.temporary_storage.stage_root / "candidate",
          context.temporary_storage.stage_root / "candidate/artifact.json",
          manifest_path, failure.evidence_path,
          {failure.category, failure.reason, std::nullopt}, now_utc());
      result.quarantine_id = quarantine_id;
      result.validation_evidence_paths.push_back(failure.evidence_path);
      return terminal_failure(std::move(result), StageStatus::kInvalidOutput,
                              failure.category, failure.reason);
    }
    auto published = std::get<PublishedArtifact>(std::move(publication));
    result.artifact = published.reference;
    result.validation_evidence_paths.push_back(published.evidence_path);
    manifest.status = StageStatus::kSucceeded;
    manifest.output_artifacts = {published.reference};
    manifest.validation_evidence_path =
        relative_to(published.evidence_path, workspace_.root);
    result.build_manifest_paths.push_back(persist_stage_manifest(
        workspace_, request.run_id, "merge", manifest));

    const auto report = evaluate_validated_global_graph(
        workspace_, published.validated,
        relative_to(published.evidence_path, workspace_.root),
        request.run_id + "-evaluation", now_utc());
    result.evaluation_report_path = persist_evaluation_report(workspace_, report);
    result.status = StageStatus::kSucceeded;
    return result;
  } catch (const PipelineError& error) {
    return terminal_failure(std::move(result), StageStatus::kFailed,
                            error.category(), error.what());
  } catch (const std::exception& error) {
    return terminal_failure(std::move(result), StageStatus::kFailed,
                            ErrorCategory::kInternal, error.what());
  }
}

}  // namespace diskann_pipeline
