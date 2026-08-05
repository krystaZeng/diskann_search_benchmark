#pragma once

#include <nlohmann/json.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace diskann_pipeline::detail {

using Json = nlohmann::json;

Json parse_json_strict(std::string_view text);
void require_object_shape(const Json& value,
                          std::initializer_list<std::string_view> required,
                          std::initializer_list<std::string_view> optional = {});
std::string dump_json(const Json& value);

}  // namespace diskann_pipeline::detail

