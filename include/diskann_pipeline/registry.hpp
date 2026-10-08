#pragma once

#include "diskann_pipeline/implementation.hpp"
#include "diskann_pipeline/compatibility.hpp"
#include "diskann_pipeline/validation.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace diskann_pipeline {

enum class RegistryAdmissionPolicy {
  kProduction,
  kTest,
};

// Inspect validated metadata and actual services only: no payload I/O,
// checksum calculation, factory creation or algorithm execution. known_inputs
// is incremental; absence of future inputs is neither rejection nor validation.
using InputAdmissionCheck = std::function<CompatibilityResult(
    const std::vector<ValidatedArtifact>& known_inputs,
    const StageContext& context)>;

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

  // Legacy overloads above install no additional admission check. Assemblers
  // wrapping a constrained implementation must also bind its admission callback.
  void register_local_build(ImplementationDescriptor descriptor,
                            LocalBuildFactory factory,
                            InputAdmissionCheck admission);
  void register_merge(ImplementationDescriptor descriptor,
                      MergeFactory factory, InputAdmissionCheck admission);

  // Resolves exact registered identity/version/stage. Success covers only the
  // supplied facts and does not authorize unresolved future Artifact payloads.
  CompatibilityResult check_known_inputs(
      const ImplementationDescriptor& consumer,
      const std::vector<ValidatedArtifact>& known_inputs,
      const StageContext& context) const;

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
