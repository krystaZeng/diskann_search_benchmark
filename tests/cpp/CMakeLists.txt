function(add_diskann_pipeline_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries("${name}" PRIVATE diskann_pipeline::core)
  target_include_directories("${name}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "phase1")
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

