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

execute_process(
  COMMAND git -c safe.directory=${PROJECT_SOURCE_DIR} remote get-url origin
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  RESULT_VARIABLE _diskann_search_origin_result
  OUTPUT_VARIABLE _diskann_search_project_repository
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT _diskann_search_origin_result EQUAL 0 OR
   NOT _diskann_search_project_repository)
  set(_diskann_search_project_repository
      "${DISKANN_PIPELINE_SOURCE_REPOSITORY}")
endif()

set(_diskann_search_effective_cxx_flags "${CMAKE_CXX_FLAGS}")
string(TOUPPER "${CMAKE_BUILD_TYPE}" _diskann_search_build_type_upper)
if(_diskann_search_build_type_upper AND
   DEFINED CMAKE_CXX_FLAGS_${_diskann_search_build_type_upper})
  string(APPEND _diskann_search_effective_cxx_flags
         " ${CMAKE_CXX_FLAGS_${_diskann_search_build_type_upper}}")
endif()
if(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  string(APPEND _diskann_search_effective_cxx_flags
         " -mavx2 -mfma -msse2 -fopenmp -fopenmp-simd")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  string(APPEND _diskann_search_effective_cxx_flags
         " -Wall -Wextra -Wpedantic")
elseif(MSVC)
  string(APPEND _diskann_search_effective_cxx_flags " /W4 /permissive-")
endif()
string(STRIP "${_diskann_search_effective_cxx_flags}"
       _diskann_search_effective_cxx_flags)
string(REPLACE "\\" "\\\\" _diskann_search_effective_cxx_flags
       "${_diskann_search_effective_cxx_flags}")
string(REPLACE "\"" "\\\"" _diskann_search_effective_cxx_flags
       "${_diskann_search_effective_cxx_flags}")
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
    DISKANN_SEARCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    DISKANN_SEARCH_PROJECT_REPOSITORY="${_diskann_search_project_repository}"
    DISKANN_SEARCH_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    DISKANN_SEARCH_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    DISKANN_SEARCH_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    DISKANN_SEARCH_PROJECT_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    DISKANN_SEARCH_BUILD_TESTING=$<BOOL:${BUILD_TESTING}>
    DISKANN_SEARCH_BASELINE_ENABLED=$<BOOL:${DISKANN_CPP_MAIN_BASELINE}>
    DISKANN_SEARCH_FEATURE_ENABLED=$<BOOL:${DISKANN_SEARCH_EVALUATION}>
    DISKANN_SEARCH_EXPORT_COMPILE_COMMANDS=$<BOOL:${CMAKE_EXPORT_COMPILE_COMMANDS}>
    DISKANN_SEARCH_EFFECTIVE_CXX_FLAGS="${_diskann_search_effective_cxx_flags}")
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
