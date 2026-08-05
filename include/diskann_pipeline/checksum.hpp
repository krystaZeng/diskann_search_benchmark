#pragma once

#include "diskann_pipeline/artifact_ref.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace diskann_pipeline {

struct AggregatePayloadIdentity {
  std::string role;
  std::uint64_t byte_length;
  std::string sha256;
};

struct AggregateIdentityProjection {
  ArtifactType artifact_type;
  std::string format_version;
  std::string dataset_checksum;
  IdSpace id_space;
  std::vector<std::string> capabilities;
  std::vector<AggregatePayloadIdentity> payloads;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::string structural_json;
};

std::string sha256_bytes(std::string_view bytes);
std::string sha256_file(const std::filesystem::path& path);
std::string canonicalize_restricted_json(std::string_view json_text);
std::string make_aggregate_projection_json(
    const AggregateIdentityProjection& projection);
std::string aggregate_checksum(const AggregateIdentityProjection& projection);

}  // namespace diskann_pipeline

