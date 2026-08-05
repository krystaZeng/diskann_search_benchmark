add_library(
  diskann_pipeline_core
  core/json_codec.cpp
  core/document_codecs.cpp
  core/workspace.cpp
  core/artifact_resolver.cpp
  core/provenance_store.cpp
  core/evaluation_store.cpp
  core/quarantine_manager.cpp
  core/diagnostic_quarantine_resolver.cpp
  pipeline/artifact_reuse.cpp
  registry/implementation_registry.cpp
  registry/compatibility_checker.cpp
  validation/canonical_json.cpp
  validation/sha256.cpp
  validation/schema_catalog.cpp
  validation/validation_gate.cpp
  validation/artifact_validator.cpp
  validation/mapping_validator.cpp
  validation/graph_validator.cpp
  validation/validator.cpp
  validation/adapter_policy.cpp)
add_library(diskann_pipeline::core ALIAS diskann_pipeline_core)

target_link_libraries(
  diskann_pipeline_core
  PUBLIC diskann_pipeline::headers
  PRIVATE nlohmann_json::nlohmann_json OpenSSL::Crypto)
target_include_directories(diskann_pipeline_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
diskann_pipeline_apply_cxx17(diskann_pipeline_core)

add_executable(diskann_pipeline_runner cli/runner_stub.cpp)
target_link_libraries(diskann_pipeline_runner PRIVATE diskann_pipeline::core)
diskann_pipeline_apply_cxx17(diskann_pipeline_runner)

install(TARGETS diskann_pipeline_core diskann_pipeline_runner)
