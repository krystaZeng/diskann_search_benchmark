add_executable(hnsw_merger_m1_semantics test_m1_semantics.cpp)
target_link_libraries(
  hnsw_merger_m1_semantics
  PRIVATE diskann_pipeline::hnsw_merger_vamana
          diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  hnsw_merger_m1_semantics
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
target_compile_definitions(
  hnsw_merger_m1_semantics
  PRIVATE HNSW_MERGER_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")
diskann_pipeline_apply_cxx17(hnsw_merger_m1_semantics)
add_test(NAME hnsw_merger_m1_semantics COMMAND hnsw_merger_m1_semantics)
set_tests_properties(
  hnsw_merger_m1_semantics PROPERTIES LABELS "hnsw-merger;unit")

add_executable(hnsw_merger_m1_native_output test_m1_native_output.cpp)
target_link_libraries(
  hnsw_merger_m1_native_output
  PRIVATE diskann_pipeline::hnsw_merger_vamana diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  hnsw_merger_m1_native_output
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
diskann_pipeline_apply_cxx17(hnsw_merger_m1_native_output)
add_test(NAME hnsw_merger_m1_native_output COMMAND hnsw_merger_m1_native_output)
set_tests_properties(
  hnsw_merger_m1_native_output PROPERTIES LABELS "hnsw-merger;native")

add_executable(hnsw_merger_merge_only_execution test_merge_only_execution.cpp)
target_link_libraries(
  hnsw_merger_merge_only_execution
  PRIVATE diskann_pipeline::hnsw_merger_vamana diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  hnsw_merger_merge_only_execution
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
target_compile_definitions(
  hnsw_merger_merge_only_execution
  PRIVATE
    HNSW_MERGER_TEST_PROJECT_REPOSITORY="${HNSW_MERGER_PROJECT_REPOSITORY}"
    HNSW_MERGER_TEST_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    HNSW_MERGER_TEST_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    HNSW_MERGER_TEST_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    HNSW_MERGER_TEST_PROJECT_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    HNSW_MERGER_TEST_PROJECT_CANONICAL_REPOSITORY_RESOLVED=${HNSW_MERGER_PROJECT_CANONICAL_REPOSITORY_RESOLVED})
diskann_pipeline_apply_cxx17(hnsw_merger_merge_only_execution)
add_test(
  NAME hnsw_merger_merge_only_execution
  COMMAND hnsw_merger_merge_only_execution)
set_tests_properties(
  hnsw_merger_merge_only_execution PROPERTIES LABELS "hnsw-merger;integration")

add_executable(hnsw_merger_m1_pipeline test_m1_pipeline.cpp)
target_link_libraries(
  hnsw_merger_m1_pipeline
  PRIVATE diskann_pipeline::hnsw_merger_vamana diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  hnsw_merger_m1_pipeline
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
diskann_pipeline_apply_cxx17(hnsw_merger_m1_pipeline)
add_test(NAME hnsw_merger_m1_pipeline COMMAND hnsw_merger_m1_pipeline)
set_tests_properties(
  hnsw_merger_m1_pipeline PROPERTIES LABELS "hnsw-merger;small-real")
