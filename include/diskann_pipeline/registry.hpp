#pragma once

#include "diskann_pipeline/implementation.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace diskann_pipeline {

enum class RegistryAdmissionPolicy {
  kProduction,
  kTest,
};

class ImplementationRegistry {
 public:
  struct Impl;

  explicit ImplementationRegistry(RegistryAdmissionPolicy admission_policy);
  ImplementationRegistry() = delete;
  ~ImplementationRegistry();

  ImplementationRegistry(ImplementationRegistry&&) noexcept;
  ImplementationRegistry& operator=(ImplementationRegistry&&) noexcept;
  ImplementationRegistry(const ImplementationRegistry&) = delete;
  ImplementationRegistry& operator=(const ImplementationRegistry&) = delete;

  void register_partition(ImplementationDescriptor descriptor,
                          PartitionFactory factory);
  void register_local_build(ImplementationDescriptor descriptor,
                            LocalBuildFactory factory);
  void register_merge(ImplementationDescriptor descriptor,
                      MergeFactory factory);

  void seal();
  bool sealed() const noexcept;
  std::size_t size() const noexcept;
  RegistryAdmissionPolicy admission_policy() const noexcept;
  std::vector<ImplementationDescriptor> enumerate() const;

  const ImplementationDescriptor& resolve_descriptor(
      const std::string& implementation_id,
      const std::string& implementation_version,
      StageKind expected_stage) const;
  PartitionFactory resolve_partition_factory(
      const std::string& implementation_id,
      const std::string& implementation_version) const;
  LocalBuildFactory resolve_local_build_factory(
      const std::string& implementation_id,
      const std::string& implementation_version) const;
  MergeFactory resolve_merge_factory(
      const std::string& implementation_id,
      const std::string& implementation_version) const;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace diskann_pipeline
