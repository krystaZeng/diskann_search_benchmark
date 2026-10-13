add_executable(diskann_kmeans_tests test_diskann_kmeans.cpp)
target_link_libraries(diskann_kmeans_tests PRIVATE diskann_kmeans_partition nlohmann_json::nlohmann_json)
target_include_directories(diskann_kmeans_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp")
target_compile_definitions(diskann_kmeans_tests PRIVATE
  KMEANS_RUNNER="$<TARGET_FILE:kmeans_partition_runner>"
  KMEANS_CONFIG="${PROJECT_SOURCE_DIR}/experiments/configs/diskann-kmeans/small.json"
  KMEANS_TEST_ROOT="${PROJECT_SOURCE_DIR}")
add_dependencies(diskann_kmeans_tests kmeans_partition_runner)
diskann_pipeline_apply_cxx17(diskann_kmeans_tests)
add_test(NAME diskann_kmeans_parameters_contract COMMAND diskann_kmeans_tests unit)
add_test(NAME diskann_kmeans_native_b0 COMMAND diskann_kmeans_tests integration)
set_tests_properties(diskann_kmeans_parameters_contract diskann_kmeans_native_b0
  PROPERTIES LABELS "diskann-kmeans" ENVIRONMENT "OMP_NUM_THREADS=6;MKL_NUM_THREADS=6" TIMEOUT 180)
