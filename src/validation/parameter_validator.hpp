#pragma once

#include "diskann_pipeline/implementation.hpp"

#include <filesystem>
#include <string_view>

namespace diskann_pipeline::detail {

void validate_implementation_parameters(
    const ImplementationDescriptor& descriptor,
    std::string_view normalized_parameters_json,
    const std::filesystem::path& repository_root);

}  // namespace diskann_pipeline::detail
