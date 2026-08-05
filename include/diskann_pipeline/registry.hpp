#pragma once

namespace diskann_pipeline {

enum class RegistryAdmissionPolicy {
  kProduction,
  kTest,
};

class ImplementationRegistry {
 public:
  explicit ImplementationRegistry(RegistryAdmissionPolicy admission_policy);
  ImplementationRegistry() = delete;

 private:
  RegistryAdmissionPolicy admission_policy_;
};

}  // namespace diskann_pipeline

