function(add_diskann_pipeline_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries("${name}" PRIVATE diskann_pipeline::core)
  target_include_directories("${name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "phase1")
endfunction()

function(add_diskann_pipeline_phase2_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries(
    "${name}"
    PRIVATE diskann_pipeline::core nlohmann_json::nlohmann_json)
  target_include_directories(
    "${name}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "phase2")
endfunction()

add_diskann_pipeline_test(
  public_api_cxx17
  contract/test_public_api_cxx17.cpp)
add_diskann_pipeline_test(
  checksum_canonicalization
  contract/test_checksum_canonicalization.cpp)
add_diskann_pipeline_test(
  document_codecs
  contract/test_document_codecs.cpp)
add_diskann_pipeline_test(
  stage_context_and_workspace
  unit/test_stage_context_and_workspace.cpp)

add_diskann_pipeline_phase2_test(
  registry_selection
  unit/test_registry_selection.cpp)
add_diskann_pipeline_phase2_test(
  compatibility_preflight
  integration/test_compatibility_preflight.cpp)
add_diskann_pipeline_phase2_test(
  valid_artifacts
  contract/test_valid_artifacts.cpp)
add_diskann_pipeline_phase2_test(
  invalid_artifact_metadata
  contract/test_invalid_artifact_metadata.cpp)
add_diskann_pipeline_phase2_test(
  invalid_graph_properties
  property/test_invalid_graph_properties.cpp)
add_diskann_pipeline_phase2_test(
  quarantine_isolation
  integration/test_quarantine_isolation.cpp)
add_diskann_pipeline_phase2_test(
  artifact_reuse_core
  integration/test_artifact_reuse_core.cpp)
add_diskann_pipeline_phase2_test(
  adapter_policy
  contract/test_adapter_policy.cpp)
