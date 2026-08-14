#include "validation/parameter_validator.hpp"

#include "core/json_codec.hpp"
#include "diskann_pipeline/errors.hpp"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace diskann_pipeline::detail {
namespace {

std::filesystem::path confined_schema_path(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& relative) {
  if (relative.empty() || relative.is_absolute()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "parameter_schema_path must be repository-relative");
  }
  for (const auto& component : relative) {
    if (component == "..") {
      throw PipelineError(ErrorCategory::kSchemaInvalid,
                          "parameter_schema_path escapes the repository");
    }
  }
  std::error_code error;
  const auto root = std::filesystem::weakly_canonical(repository_root, error);
  if (error) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "repository root cannot be resolved");
  }
  const auto path = std::filesystem::weakly_canonical(root / relative, error);
  if (error || !std::filesystem::is_regular_file(path)) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "parameter_schema_path does not name an existing file");
  }
  const auto confined = std::filesystem::relative(path, root, error);
  if (error || confined.empty() || confined.is_absolute() ||
      *confined.begin() == "..") {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "parameter_schema_path escapes the repository");
  }
  return path;
}

Json read_schema(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream bytes;
  bytes << input.rdbuf();
  if (!input && !input.eof()) {
    throw PipelineError(ErrorCategory::kSchemaInvalid,
                        "parameter schema cannot be read");
  }
  return parse_json_strict(bytes.str());
}

void reject(std::string message) {
  throw PipelineError(ErrorCategory::kSchemaInvalid, std::move(message));
}

void validate_value(const Json& value, const Json& schema,
                    const std::string& location) {
  if (schema.contains("type")) {
    const auto type = schema.at("type").get<std::string>();
    const bool matches =
        (type == "object" && value.is_object()) ||
        (type == "array" && value.is_array()) ||
        (type == "string" && value.is_string()) ||
        (type == "boolean" && value.is_boolean()) ||
        (type == "integer" && value.is_number_integer()) ||
        (type == "number" && value.is_number()) ||
        (type == "null" && value.is_null());
    if (!matches) reject(location + " has the wrong value type");
  }
  if (schema.contains("const") && value != schema.at("const")) {
    reject(location + " does not equal its required constant");
  }
  if (!value.is_object()) return;

  if (schema.contains("required")) {
    for (const auto& required : schema.at("required")) {
      const auto name = required.get<std::string>();
      if (!value.contains(name)) reject(location + " is missing " + name);
    }
  }
  const auto properties = schema.value("properties", Json::object());
  if (schema.value("additionalProperties", true)) {
    // Feature 001's frozen Fake definitions are closed objects, but retain
    // ordinary JSON Schema behavior for any explicitly open definition.
  } else {
    for (auto item = value.begin(); item != value.end(); ++item) {
      if (!properties.contains(item.key())) {
        reject(location + " contains an additional property: " + item.key());
      }
    }
  }
  for (auto item = properties.begin(); item != properties.end(); ++item) {
    if (value.contains(item.key())) {
      validate_value(value.at(item.key()), item.value(),
                     location + "." + item.key());
    }
  }
}

}  // namespace

void validate_implementation_parameters(
    const ImplementationDescriptor& descriptor,
    std::string_view normalized_parameters_json,
    const std::filesystem::path& repository_root) {
  const auto path = confined_schema_path(repository_root,
                                         descriptor.parameter_schema_path);
  const auto document = read_schema(path);
  if (!document.is_object()) {
    reject("parameter schema is not an object");
  }
  const Json* selected_schema = &document;
  if (document.contains("$defs")) {
    if (!document.at("$defs").is_object()) {
      reject("parameter schema $defs is not an object");
    }
    const auto definition_name = std::string(to_string(descriptor.stage));
    if (!document.at("$defs").contains(definition_name)) {
      reject("parameter schema has no definition for selected stage");
    }
    selected_schema = &document.at("$defs").at(definition_name);
  }
  const auto parameters = parse_json_strict(normalized_parameters_json);
  validate_value(parameters, *selected_schema,
                 "stage_parameters." +
                     std::string(to_string(descriptor.stage)));
}

}  // namespace diskann_pipeline::detail
