add_executable(
  diskann_search_link_smoke
  test_diskann_search_link_smoke.cpp)
target_link_libraries(
  diskann_search_link_smoke
  PRIVATE diskann_pipeline::core)
target_include_directories(
  diskann_search_link_smoke
  PRIVATE "${PROJECT_SOURCE_DIR}/src")

if(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  target_link_libraries(
    diskann_search_link_smoke
    PRIVATE diskann_cpp_main_upstream OpenMP::OpenMP_CXX)
  target_compile_definitions(
    diskann_search_link_smoke PRIVATE DISKANN_SEARCH_LINUX_NATIVE_SMOKE=1 USE_AVX2)
  target_compile_options(
    diskann_search_link_smoke
    PRIVATE -mavx2 -mfma -msse2 -fopenmp -fopenmp-simd)
else()
  target_compile_definitions(
    diskann_search_link_smoke
    PRIVATE DISKANN_SEARCH_LINUX_NATIVE_SMOKE=0)
endif()

diskann_pipeline_apply_cxx17(diskann_search_link_smoke)
add_test(NAME diskann_search_link_smoke COMMAND diskann_search_link_smoke)
set_tests_properties(
  diskann_search_link_smoke
  PROPERTIES
    LABELS "search;linux-native"
    SKIP_RETURN_CODE 77)

add_executable(
  search_artifact_contracts
  test_search_artifact_contracts.cpp)
target_link_libraries(
  search_artifact_contracts
  PRIVATE diskann_pipeline::core nlohmann_json::nlohmann_json)
target_include_directories(
  search_artifact_contracts
  PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp" "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(search_artifact_contracts)
add_test(NAME search_artifact_contracts COMMAND search_artifact_contracts)
set_tests_properties(search_artifact_contracts PROPERTIES LABELS "search;contract")

add_executable(
  post_graph_materialization
  test_post_graph_materialization.cpp)
target_link_libraries(
  post_graph_materialization
  PRIVATE diskann_pipeline::core nlohmann_json::nlohmann_json)
target_include_directories(
  post_graph_materialization
  PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp" "${PROJECT_SOURCE_DIR}/src")
if(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  add_dependencies(post_graph_materialization diskann_search_evaluation_runner)
  target_link_libraries(
    post_graph_materialization
    PRIVATE diskann_cpp_main_upstream OpenMP::OpenMP_CXX)
  target_compile_definitions(
    post_graph_materialization
    PRIVATE
      DISKANN_SEARCH_LINUX_NATIVE_TEST=1
      DISKANN_SEARCH_TEST_RUNNER="$<TARGET_FILE:diskann_search_evaluation_runner>"
      DISKANN_SEARCH_TEST_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
      DISKANN_SEARCH_TEST_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
      DISKANN_SEARCH_TEST_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
      DISKANN_SEARCH_TEST_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
      DISKANN_SEARCH_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
      DISKANN_SEARCH_TEST_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
      DISKANN_SEARCH_TEST_CMAKE_VERSION="${CMAKE_VERSION}"
      DISKANN_SEARCH_TEST_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
      USE_AVX2)
  target_compile_options(
    post_graph_materialization
    PRIVATE -mavx2 -mfma -msse2 -fopenmp -fopenmp-simd)
else()
  target_compile_definitions(
    post_graph_materialization PRIVATE DISKANN_SEARCH_LINUX_NATIVE_TEST=0)
endif()
diskann_pipeline_apply_cxx17(post_graph_materialization)
add_test(NAME post_graph_materialization COMMAND post_graph_materialization)
set_tests_properties(post_graph_materialization PROPERTIES LABELS "search;us1")

add_executable(pqflash_observation test_pqflash_observation.cpp)
target_link_libraries(pqflash_observation PRIVATE diskann_pipeline::core)
target_include_directories(
  pqflash_observation
  PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp" "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(pqflash_observation)
add_test(NAME pqflash_observation COMMAND pqflash_observation)
set_tests_properties(pqflash_observation PROPERTIES LABELS "search;us2")

add_executable(search_runner_parity test_search_runner_parity.cpp)
target_link_libraries(search_runner_parity PRIVATE diskann_pipeline::core)
target_include_directories(
  search_runner_parity
  PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp" "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(search_runner_parity)
add_test(NAME search_runner_parity COMMAND search_runner_parity)
set_tests_properties(search_runner_parity PROPERTIES LABELS "search;us2;parity")
