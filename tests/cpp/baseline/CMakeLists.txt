function(add_cpp_main_baseline_test name source)
  add_executable("${name}" "${source}")
  target_link_libraries("${name}" PRIVATE diskann_pipeline::core)
  target_include_directories(
    "${name}"
    PRIVATE
      "${PROJECT_SOURCE_DIR}/src"
      "${PROJECT_SOURCE_DIR}/tests/cpp"
      "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main/include")
  diskann_pipeline_apply_cxx17("${name}")
  add_test(NAME "${name}" COMMAND "${name}")
  set_tests_properties("${name}" PROPERTIES LABELS "cpp-main-baseline;phase1")
endfunction()

add_cpp_main_baseline_test(
  cpp_main_native_formats
  test_cpp_main_native_formats.cpp)
target_link_libraries(cpp_main_native_formats PRIVATE nlohmann_json::nlohmann_json)

add_cpp_main_baseline_test(
  cpp_main_source_build_admission
  test_cpp_main_source_build_admission.cpp)
target_compile_definitions(
  cpp_main_source_build_admission
  PRIVATE CPP_MAIN_TEST_REPOSITORY_ROOT="${PROJECT_SOURCE_DIR}")

add_cpp_main_baseline_test(
  cpp_main_link_smoke
  test_cpp_main_link_smoke.cpp)
if(APPLE AND EXISTS "/usr/local/opt/libomp/include/omp.h")
  target_include_directories(
    cpp_main_link_smoke PRIVATE "/usr/local/opt/libomp/include")
endif()
if(TARGET diskann_cpp_main_upstream)
  target_link_libraries(cpp_main_link_smoke PRIVATE diskann_cpp_main_upstream)
endif()
