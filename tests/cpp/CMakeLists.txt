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

function(add_diskann_pipeline_phase3_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries(
    "${name}"
    PRIVATE diskann_pipeline::fake_stages nlohmann_json::nlohmann_json)
  target_include_directories(
    "${name}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "phase3")
endfunction()

function(add_diskann_pipeline_phase4_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries(
    "${name}"
    PRIVATE diskann_pipeline::fake_stages nlohmann_json::nlohmann_json)
  target_include_directories(
    "${name}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
  target_compile_definitions(
    "${name}" PRIVATE DISKANN_PIPELINE_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}/tests")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "phase4")
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

add_diskann_pipeline_phase3_test(
  fake_stage_context
  unit/test_fake_stage_context.cpp)
add_diskann_pipeline_phase3_test(
  fake_stage_contracts
  contract/test_fake_stage_contracts.cpp)
add_diskann_pipeline_phase3_test(
  fake_merge_properties
  property/test_fake_merge_properties.cpp)
add_diskann_pipeline_phase3_test(
  pipeline_orchestration
  integration/test_pipeline_orchestration.cpp)
add_diskann_pipeline_phase3_test(
  pipeline_failure_gates
  integration/test_pipeline_failure_gates.cpp)

add_diskann_pipeline_phase4_test(
  fake_pipeline_e2e
  integration/test_fake_pipeline_e2e.cpp)
add_diskann_pipeline_phase4_test(
  negative_matrix_e2e
  integration/test_negative_matrix_e2e.cpp)
add_diskann_pipeline_phase4_test(
  parameter_schema_gate
  integration/test_parameter_schema_gate.cpp)
set_tests_properties(parameter_schema_gate PROPERTIES LABELS "phase5")
add_diskann_pipeline_phase4_test(
  terminal_output_contract
  integration/test_terminal_output_contract.cpp)
set_tests_properties(terminal_output_contract PROPERTIES LABELS "phase5")

add_executable(production_surface contract/test_production_surface.cpp)
target_sources(
  production_surface PRIVATE "${PROJECT_SOURCE_DIR}/src/cli/production_registry.cpp")
target_link_libraries(production_surface PRIVATE diskann_pipeline::core)
target_include_directories(
  production_surface
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(production_surface)
add_test(NAME production_surface COMMAND production_surface)
set_tests_properties(production_surface PROPERTIES LABELS "phase4;assembly")
