#include "diskann_pipeline/checksum.hpp"

#include "core/json_codec.hpp"
#include "diskann_pipeline/errors.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace diskann_pipeline {
namespace {

using detail::Json;

void validate_restricted_value(const Json& value) {
  if (value.is_null() || value.is_number_float() || value.is_binary() ||
      value.is_discarded()) {
    throw PipelineError(
        ErrorCategory::kSchemaInvalid,
        "restricted canonical JSON permits no null, float, or binary values");
  }
  if (value.is_array()) {
    for (const auto& item : value) {
      validate_restricted_value(item);
    }
    return;
  }
  if (value.is_object()) {
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      validate_restricted_value(iterator.value());
    }
    return;
  }
  if (!value.is_string() && !value.is_boolean() &&
      !value.is_number_integer() && !value.is_number_unsigned()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "unsupported restricted canonical JSON value");
  }
}

Json source_json(const SourceArtifactIdentity& source) {
  return {{"artifact_type", to_string(source.artifact_type)},
          {"format_version", source.format_version},
          {"aggregate_checksum", source.aggregate_checksum}};
}

}  // namespace

std::string canonicalize_restricted_json(std::string_view json_text) {
  const auto parsed = detail::parse_json_strict(json_text);
  validate_restricted_value(parsed);
  return detail::dump_json(parsed);
}

std::string make_aggregate_projection_json(
    const AggregateIdentityProjection& projection) {
  auto capabilities = projection.capabilities;
  std::sort(capabilities.begin(), capabilities.end());
  if (std::adjacent_find(capabilities.begin(), capabilities.end()) !=
      capabilities.end()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "aggregate capabilities contain duplicates");
  }

  Json payloads = Json::array();
  std::set<std::string> payload_roles;
  for (const auto& payload : projection.payloads) {
    if (!payload_roles.insert(payload.role).second) {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "aggregate payload roles contain duplicates");
    }
    payloads.push_back({{"byte_length", payload.byte_length},
                        {"role", payload.role},
                        {"sha256", payload.sha256}});
  }

  Json sources = Json::array();
  for (const auto& source : projection.source_artifacts) {
    sources.push_back(source_json(source));
  }

  auto structural = detail::parse_json_strict(projection.structural_json);
  validate_restricted_value(structural);
  if (!structural.is_object()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "aggregate structural projection must be an object");
  }

  const Json result{
      {"artifact_type", to_string(projection.artifact_type)},
      {"canonicalization", "diskann-artifact-c14n-v1"},
      {"capabilities", std::move(capabilities)},
      {"dataset_checksum", projection.dataset_checksum},
      {"format_version", projection.format_version},
      {"id_space", to_string(projection.id_space)},
      {"payloads", std::move(payloads)},
      {"schema_version", "1.0.0"},
      {"source_artifacts", std::move(sources)},
      {"structural", std::move(structural)}};
  validate_restricted_value(result);
  return detail::dump_json(result);
}

std::string aggregate_checksum(const AggregateIdentityProjection& projection) {
  return sha256_bytes(make_aggregate_projection_json(projection));
}

}  // namespace diskann_pipeline

