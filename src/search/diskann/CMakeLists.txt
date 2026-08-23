target_sources(
  diskann_pipeline_core
  PRIVATE
    fixed_descriptors.cpp
    post_graph_materializer.cpp
    pqflash_instrumented_runner.cpp
    search_metrics_evaluator.cpp)

if(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  target_compile_definitions(
    diskann_pipeline_core
    PRIVATE DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE=1 USE_AVX2)
  target_compile_options(
    diskann_pipeline_core
    PRIVATE -mavx2 -mfma -msse2 -fopenmp -fopenmp-simd)
  target_link_libraries(
    diskann_pipeline_core
    PRIVATE diskann_cpp_main_upstream OpenMP::OpenMP_CXX)
else()
  target_compile_definitions(
    diskann_pipeline_core
    PRIVATE DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE=0)
endif()

add_executable(
  diskann_search_evaluation_runner
  diskann_search_evaluation_runner.cpp)
target_link_libraries(
  diskann_search_evaluation_runner
  PRIVATE diskann_pipeline::core nlohmann_json::nlohmann_json)
target_include_directories(
  diskann_search_evaluation_runner
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  diskann_search_evaluation_runner
  PRIVATE
    DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE=$<BOOL:${DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE}>
    DISKANN_SEARCH_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    DISKANN_SEARCH_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    DISKANN_SEARCH_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    DISKANN_SEARCH_CMAKE_VERSION="${CMAKE_VERSION}"
    DISKANN_SEARCH_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    DISKANN_SEARCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
if(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  target_link_libraries(
    diskann_search_evaluation_runner
    PRIVATE diskann_cpp_main_upstream OpenMP::OpenMP_CXX)
  target_compile_definitions(
    diskann_search_evaluation_runner PRIVATE USE_AVX2)
  target_compile_options(
    diskann_search_evaluation_runner
    PRIVATE -mavx2 -mfma -msse2 -fopenmp -fopenmp-simd)
endif()
diskann_pipeline_apply_cxx17(diskann_search_evaluation_runner)
