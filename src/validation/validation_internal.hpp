#pragma once

#include "core/json_codec.hpp"
#include "diskann_pipeline/validation.hpp"

#include <filesystem>

namespace diskann_pipeline::validation_detail {

struct SchemaRule {
  ArtifactType artifact_type;
  const char* format_version;
  IdSpace id_space;
  const char* required_capability;
};

struct AdmittedArtifact {
  ArtifactRef reference;
  std::filesystem::path metadata_file;
  detail::Json metadata;
  DatasetIdentity dataset;
  IdSpace id_space;
  std::vector<std::string> capabilities;
  std::vector<SourceArtifactIdentity> source_artifacts;
  std::vector<PayloadRef> payloads;
  detail::Json structural;
};

const SchemaRule& schema_rule(ArtifactType artifact_type,
                              std::string_view format_version);
AdmittedArtifact admit_artifact(const WorkspaceRef& workspace,
                                const ArtifactRef& reference);
void validate_payload_identity(const WorkspaceRef& workspace,
                               AdmittedArtifact& artifact);
void validate_artifact_semantics(
    const WorkspaceRef& workspace, AdmittedArtifact& artifact,
    const std::vector<ValidatedArtifact>& validated_sources);

std::vector<GlobalNodeId> validate_local_to_global_map(
    const std::filesystem::path& payload_path, NodeCount expected_count,
    NodeCount global_node_count);
std::uint32_t validate_adjacency_graph(
    const std::filesystem::path& payload_path, NodeCount expected_count,
    NodeCount id_limit, std::uint32_t declared_max_degree);

}  // namespace diskann_pipeline::validation_detail
