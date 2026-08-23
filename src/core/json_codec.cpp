#include "core/json_codec.hpp"

#include "diskann_pipeline/errors.hpp"
#include "diskann_pipeline/identifiers.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace diskann_pipeline {
namespace {

template <typename Enum>
Enum parse_enum(std::string_view value,
                std::initializer_list<std::pair<std::string_view, Enum>> values,
                const char* label) {
  for (const auto& entry : values) {
    if (entry.first == value) {
      return entry.second;
    }
  }
  throw PipelineError(ErrorCategory::kSchemaInvalid,
                      std::string("unknown ") + label + ": " +
                          std::string(value));
}

}  // namespace

std::string_view to_string(ArtifactType value) noexcept {
  switch (value) {
    case ArtifactType::kDataset:
      return "dataset";
    case ArtifactType::kPartition:
      return "partition";
    case ArtifactType::kLocalGraph:
      return "local_graph";
    case ArtifactType::kGlobalGraph:
      return "global_graph";
    case ArtifactType::kPQ:
      return "pq";
    case ArtifactType::kDiskLayout:
      return "disk_layout";
  }
  return "unknown";
}

std::string_view to_string(IdSpace value) noexcept {
  switch (value) {
    case IdSpace::kNone:
      return "none";
    case IdSpace::kLocal:
      return "local";
    case IdSpace::kGlobal:
      return "global";
  }
  return "unknown";
}

std::string_view to_string(StageKind value) noexcept {
  switch (value) {
    case StageKind::kPartition:
      return "partition";
    case StageKind::kLocalBuild:
      return "local_build";
    case StageKind::kMerge:
      return "merge";
  }
  return "unknown";
}

std::string_view to_string(StageStatus value) noexcept {
  switch (value) {
    case StageStatus::kPlanned:
      return "PLANNED";
    case StageStatus::kRunning:
      return "RUNNING";
    case StageStatus::kSucceeded:
      return "SUCCEEDED";
    case StageStatus::kFailed:
      return "FAILED";
    case StageStatus::kCancelled:
      return "CANCELLED";
    case StageStatus::kInvalidOutput:
      return "INVALID_OUTPUT";
    case StageStatus::kReused:
      return "REUSED";
  }
  return "UNKNOWN";
}

ArtifactType artifact_type_from_string(std::string_view value) {
  return parse_enum<ArtifactType>(
      value,
      {{"dataset", ArtifactType::kDataset},
       {"partition", ArtifactType::kPartition},
       {"local_graph", ArtifactType::kLocalGraph},
       {"global_graph", ArtifactType::kGlobalGraph},
       {"pq", ArtifactType::kPQ},
       {"disk_layout", ArtifactType::kDiskLayout}},
      "ArtifactType");
}

IdSpace id_space_from_string(std::string_view value) {
  return parse_enum<IdSpace>(
      value,
      {{"none", IdSpace::kNone},
       {"local", IdSpace::kLocal},
       {"global", IdSpace::kGlobal}},
      "IdSpace");
}

StageKind stage_kind_from_string(std::string_view value) {
  return parse_enum<StageKind>(
      value,
      {{"partition", StageKind::kPartition},
       {"local_build", StageKind::kLocalBuild},
       {"merge", StageKind::kMerge}},
      "StageKind");
}

StageStatus stage_status_from_string(std::string_view value) {
  return parse_enum<StageStatus>(
      value,
      {{"PLANNED", StageStatus::kPlanned},
       {"RUNNING", StageStatus::kRunning},
       {"SUCCEEDED", StageStatus::kSucceeded},
       {"FAILED", StageStatus::kFailed},
       {"CANCELLED", StageStatus::kCancelled},
       {"INVALID_OUTPUT", StageStatus::kInvalidOutput},
       {"REUSED", StageStatus::kReused}},
      "StageStatus");
}

std::string_view to_string(ErrorCategory value) noexcept {
  switch (value) {
    case ErrorCategory::kSchemaInvalid:
      return "schema_invalid";
    case ErrorCategory::kUnsupportedVersion:
      return "unsupported_version";
    case ErrorCategory::kPathEscape:
      return "path_escape";
    case ErrorCategory::kPayloadMissing:
      return "payload_missing";
    case ErrorCategory::kPayloadLengthMismatch:
      return "payload_length_mismatch";
    case ErrorCategory::kChecksumMismatch:
      return "checksum_mismatch";
    case ErrorCategory::kSourceMismatch:
      return "source_mismatch";
    case ErrorCategory::kIdSpaceMismatch:
      return "id_space_mismatch";
    case ErrorCategory::kIdRangeInvalid:
      return "id_range_invalid";
    case ErrorCategory::kMappingIncomplete:
      return "mapping_incomplete";
    case ErrorCategory::kDegreeExceeded:
      return "degree_exceeded";
    case ErrorCategory::kNodeCountMismatch:
      return "node_count_mismatch";
    case ErrorCategory::kCapabilityUnsatisfied:
      return "capability_unsatisfied";
    case ErrorCategory::kDuplicateImplementation:
      return "duplicate_implementation";
    case ErrorCategory::kImplementationNotFound:
      return "implementation_not_found";
    case ErrorCategory::kRegistrySealed:
      return "registry_sealed";
    case ErrorCategory::kReuseMismatch:
      return "reuse_mismatch";
    case ErrorCategory::kCancelled:
      return "cancelled";
    case ErrorCategory::kCheckpointUnavailable:
      return "checkpoint_unavailable";
    case ErrorCategory::kInternal:
      return "internal";
  }
  return "internal";
}

ErrorCategory error_category_from_string(std::string_view value) {
  return parse_enum<ErrorCategory>(
      value,
      {{"schema_invalid", ErrorCategory::kSchemaInvalid},
       {"unsupported_version", ErrorCategory::kUnsupportedVersion},
       {"path_escape", ErrorCategory::kPathEscape},
       {"payload_missing", ErrorCategory::kPayloadMissing},
       {"payload_length_mismatch", ErrorCategory::kPayloadLengthMismatch},
       {"checksum_mismatch", ErrorCategory::kChecksumMismatch},
       {"source_mismatch", ErrorCategory::kSourceMismatch},
       {"id_space_mismatch", ErrorCategory::kIdSpaceMismatch},
       {"id_range_invalid", ErrorCategory::kIdRangeInvalid},
       {"mapping_incomplete", ErrorCategory::kMappingIncomplete},
       {"degree_exceeded", ErrorCategory::kDegreeExceeded},
       {"node_count_mismatch", ErrorCategory::kNodeCountMismatch},
       {"capability_unsatisfied", ErrorCategory::kCapabilityUnsatisfied},
       {"duplicate_implementation", ErrorCategory::kDuplicateImplementation},
       {"implementation_not_found", ErrorCategory::kImplementationNotFound},
       {"registry_sealed", ErrorCategory::kRegistrySealed},
       {"reuse_mismatch", ErrorCategory::kReuseMismatch},
       {"cancelled", ErrorCategory::kCancelled},
       {"checkpoint_unavailable", ErrorCategory::kCheckpointUnavailable},
       {"internal", ErrorCategory::kInternal}},
      "ErrorCategory");
}

namespace detail {

Json parse_json_strict(std::string_view text) {
  std::vector<std::set<std::string>> object_keys;
  const auto callback = [&object_keys](int, Json::parse_event_t event,
                                       Json& parsed) {
    if (event == Json::parse_event_t::object_start) {
      object_keys.emplace_back();
    } else if (event == Json::parse_event_t::key) {
      if (object_keys.empty() || !object_keys.back().insert(parsed.get<std::string>()).second) {
        throw PipelineError(ErrorCategory::kSchemaInvalid,
                            "duplicate JSON object key");
      }
    } else if (event == Json::parse_event_t::object_end) {
      object_keys.pop_back();
    }
    return true;
  };

  try {
    return Json::parse(text.begin(), text.end(), callback, true, false);
  } catch (const PipelineError&) {
    throw;
  } catch (const std::exception& error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        std::string("invalid JSON: ") + error.what());
  }
}

void require_object_shape(const Json& value,
                          std::initializer_list<std::string_view> required,
                          std::initializer_list<std::string_view> optional) {
  if (!value.is_object()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "expected a JSON object");
  }
  std::set<std::string> allowed;
  for (const auto key : required) {
    allowed.emplace(key);
    if (!value.contains(std::string(key))) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "missing required field: " + std::string(key));
    }
  }
  for (const auto key : optional) {
    allowed.emplace(key);
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (allowed.count(iterator.key()) == 0) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "unknown field: " + iterator.key());
    }
  }
}

std::string dump_json(const Json& value) {
  return value.dump(-1, ' ', false, Json::error_handler_t::strict);
}

}  // namespace detail
}  // namespace diskann_pipeline
