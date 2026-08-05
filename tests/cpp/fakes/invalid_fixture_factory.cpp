#include "diskann_pipeline/errors.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace diskann_pipeline::test {

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
