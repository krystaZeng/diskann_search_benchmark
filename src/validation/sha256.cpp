#include "diskann_pipeline/checksum.hpp"

#include "diskann_pipeline/errors.hpp"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace diskann_pipeline {
namespace {

using DigestContext =
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

DigestContext new_context() {
  DigestContext context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to initialize SHA-256 context");
  }
  return context;
}

std::string finish_digest(EVP_MD_CTX* context) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context, digest.data(), &length) != 1 || length != 32) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to finalize SHA-256 digest");
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < length; ++index) {
    output << std::setw(2) << static_cast<unsigned int>(digest[index]);
  }
  return output.str();
}

}  // namespace

std::string sha256_bytes(std::string_view bytes) {
  auto context = new_context();
  if (EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1) {
    throw PipelineError(ErrorCategory::kInternal,
                        "unable to update SHA-256 digest");
  }
  return finish_digest(context.get());
}

std::string sha256_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "unable to open payload for SHA-256: " + path.string());
  }
  auto context = new_context();
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0 &&
        EVP_DigestUpdate(context.get(), buffer.data(),
                         static_cast<std::size_t>(count)) != 1) {
      throw PipelineError(ErrorCategory::kInternal,
                          "unable to update payload SHA-256 digest");
    }
  }
  if (!input.eof()) {
    throw PipelineError(ErrorCategory::kPayloadMissing,
                        "unable to read complete payload: " + path.string());
  }
  return finish_digest(context.get());
}

}  // namespace diskann_pipeline

