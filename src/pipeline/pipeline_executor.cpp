#include "diskann_pipeline/pipeline.hpp"

#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/document_codec.hpp"
#include "diskann_pipeline/quarantine.hpp"
#include "diskann_pipeline/reuse.hpp"
#include "diskann_pipeline/stores.hpp"
#include "diskann_pipeline/workspace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <variant>

namespace diskann_pipeline {
namespace {

using Json = nlohmann::json;

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
                            const std::string& stage,
                            const ImplementationDescriptor& descriptor,
                            const DatasetIdentity& dataset,
                            const std::string& parameters,
                            std::vector<ArtifactRef> inputs) {
  return {request.run_id,
          stage,
          {descriptor.implementation_id, descriptor.implementation_version},
          request.source,
          request.toolchain,
          dataset,
          parameters,
          {context.memory_budget_bytes, context.thread_budget},
          std::move(inputs),
          {},
          StageStatus::kRunning,
          request.started_at,
          request.ended_at,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          "{}"};
}

std::filesystem::path persist_stage_manifest(
    const WorkspaceRef& workspace, const std::string& run_id,
    const std::string& identity, const BuildManifest& manifest) {
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

std::variant<PublishedArtifact, PublicationFailure> validate_and_publish(
    const WorkspaceRef& workspace, const std::string& run_id,
    const std::string& identity, const TemporaryStorageRef& temporary,
    const ArtifactRef& candidate,
    const std::vector<ValidatedArtifact>& sources) {
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

CompatibilityResult preflight(
    const ImplementationDescriptor& partition,
    const ImplementationDescriptor& local,
    const ImplementationDescriptor& merge,
    const ValidatedArtifact& dataset) {
  return CompatibilityChecker{}.check_chain(
      {partition, local, merge},
      {{dataset.reference().artifact_type, dataset.reference().format_version,
        dataset.id_space(), dataset.capabilities()}},
      {{"vector_accessor/v1", "distance_function/l2-v1", "metrics_sink/v1",
        "cancellation_token/v1"},
       false});
}

PipelineExecutionResult terminal_failure(
    PipelineExecutionResult result, StageStatus status, ErrorCategory category,
    std::string reason) {
  result.status = status;
  result.failure = FailureInfo{category, std::move(reason), std::nullopt};
  return result;
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

    const auto& partition_descriptor = registry_.resolve_descriptor(
        request.partition_implementation_id,
        request.partition_implementation_version, StageKind::kPartition);
    const auto& local_descriptor = registry_.resolve_descriptor(
        request.local_implementation_id, request.local_implementation_version,
        StageKind::kLocalBuild);
    const auto& merge_descriptor = registry_.resolve_descriptor(
        request.merge_implementation_id, request.merge_implementation_version,
        StageKind::kMerge);
    const auto compatibility = preflight(partition_descriptor, local_descriptor,
                                         merge_descriptor, dataset);
    if (!compatibility.compatible) {
      return terminal_failure(std::move(result), StageStatus::kFailed,
                              compatibility.failure->category,
                              compatibility.failure->reason);
    }

    std::optional<ValidatedArtifact> validated_partition;
    if (request.reused_partition) {
      const ArtifactReuseService reuse(validator);
      const ArtifactRequirement requirement{
          ArtifactType::kPartition, {"partition-artifact/v1"}, IdSpace::kLocal};
      const auto reused = reuse.resolve(
          *request.reused_partition, requirement,
          {"partition.complete_local_to_global_map/v1"}, {dataset});
      const auto evidence_path = persist_validation_evidence(
          workspace_, request.run_id + "-partition-reuse",
          reused.validation_evidence);
      result.validation_evidence_paths.push_back(evidence_path);
      auto manifest = base_manifest(request, context, "partition",
                                    partition_descriptor, dataset.dataset(),
                                    request.partition_parameters_json,
                                    {request.dataset});
      manifest.validation_evidence_path =
          relative_to(evidence_path, workspace_.root);
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
      manifest.reuse = reused.provenance;
      result.build_manifest_paths.push_back(persist_stage_manifest(
          workspace_, request.run_id, "partition-reused", manifest));
    } else {
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
                                    partition_descriptor, dataset.dataset(),
                                    request.partition_parameters_json,
                                    {request.dataset});
      if (stage.status != StageStatus::kSucceeded || !stage.output_candidate) {
        const auto terminal_status =
            stage.status == StageStatus::kSucceeded ? StageStatus::kFailed
                                                    : stage.status;
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
          *stage.output_candidate, {dataset});
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
            {failure.category, failure.reason, std::nullopt}, request.ended_at);
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
      auto manifest = base_manifest(
          request, context, "local_build", local_descriptor, dataset.dataset(),
          parameters, {validated_partition->reference()});
      if (stage.status != StageStatus::kSucceeded || !stage.output_candidate) {
        const auto terminal_status =
            stage.status == StageStatus::kSucceeded ? StageStatus::kFailed
                                                    : stage.status;
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
          *stage.output_candidate, {*validated_partition});
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
            {failure.category, failure.reason, std::nullopt}, request.ended_at);
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
    auto manifest = base_manifest(request, context, "merge", merge_descriptor,
                                  dataset.dataset(),
                                  request.merge_parameters_json, merge_inputs);
    if (stage.status != StageStatus::kSucceeded || !stage.output_candidate) {
      const auto terminal_status =
          stage.status == StageStatus::kSucceeded ? StageStatus::kFailed
                                                  : stage.status;
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
        *stage.output_candidate, merge_sources);
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
          {failure.category, failure.reason, std::nullopt}, request.ended_at);
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
        request.run_id + "-evaluation", request.ended_at);
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
