#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace diskann_pipeline {

using GlobalNodeId = std::uint32_t;
using LocalNodeId = std::uint32_t;
using PartitionId = std::uint32_t;
using NodeCount = std::uint64_t;

inline constexpr NodeCount kMaximumNodeCount =
    static_cast<NodeCount>(std::numeric_limits<std::uint32_t>::max()) + 1ULL;

enum class ArtifactType {
  kDataset,
  kPartition,
  kLocalGraph,
  kGlobalGraph,
  kPQ,
  kDiskLayout,
};

enum class IdSpace {
  kNone,
  kLocal,
  kGlobal,
};

enum class StageKind {
  kPartition,
  kLocalBuild,
  kMerge,
};

enum class StageStatus {
  kPlanned,
  kRunning,
  kSucceeded,
  kFailed,
  kCancelled,
  kInvalidOutput,
  kReused,
};

constexpr bool is_valid_count(NodeCount count) noexcept {
  return count > 0 && count <= kMaximumNodeCount;
}

constexpr bool is_valid_global_node_id(GlobalNodeId id,
                                       NodeCount global_node_count) noexcept {
  return is_valid_count(global_node_count) &&
         static_cast<NodeCount>(id) < global_node_count;
}

constexpr bool is_valid_local_node_id(LocalNodeId id,
                                      NodeCount local_node_count) noexcept {
  return is_valid_count(local_node_count) &&
         static_cast<NodeCount>(id) < local_node_count;
}

constexpr bool is_valid_partition_id(PartitionId id,
                                     NodeCount partition_count) noexcept {
  return is_valid_count(partition_count) &&
         static_cast<NodeCount>(id) < partition_count;
}

std::string_view to_string(ArtifactType value) noexcept;
std::string_view to_string(IdSpace value) noexcept;
std::string_view to_string(StageKind value) noexcept;
std::string_view to_string(StageStatus value) noexcept;

ArtifactType artifact_type_from_string(std::string_view value);
IdSpace id_space_from_string(std::string_view value);
StageKind stage_kind_from_string(std::string_view value);
StageStatus stage_status_from_string(std::string_view value);

}  // namespace diskann_pipeline
