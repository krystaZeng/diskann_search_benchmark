add_library(
  diskann_hnsw_merger_vamana STATIC
  descriptors.cpp
  m1_algorithm.cpp
  m1_merge.cpp)
add_library(diskann_pipeline::hnsw_merger_vamana ALIAS diskann_hnsw_merger_vamana)
target_link_libraries(
  diskann_hnsw_merger_vamana
  PUBLIC diskann_pipeline::core
  PRIVATE diskann_cpp_main_upstream nlohmann_json::nlohmann_json)
target_include_directories(
  diskann_hnsw_merger_vamana
  PUBLIC "${PROJECT_SOURCE_DIR}/src"
  PRIVATE "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main/include")
target_compile_definitions(
  diskann_hnsw_merger_vamana
  PRIVATE
    HNSW_MERGER_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    HNSW_MERGER_REPOSITORY="${DISKANN_HNSW_MERGER_REPOSITORY}"
    HNSW_MERGER_BRANCH="${DISKANN_HNSW_MERGER_BRANCH}"
    HNSW_MERGER_COMMIT="${DISKANN_HNSW_MERGER_COMMIT}"
    HNSW_MERGER_PROJECT_REPOSITORY="${HNSW_MERGER_PROJECT_REPOSITORY}"
    HNSW_MERGER_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    HNSW_MERGER_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    HNSW_MERGER_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    HNSW_MERGER_PROJECT_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    HNSW_MERGER_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    HNSW_MERGER_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    HNSW_MERGER_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    HNSW_MERGER_CMAKE_VERSION="${CMAKE_VERSION}"
    HNSW_MERGER_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    HNSW_MERGER_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    HNSW_MERGER_BINARY_DIR="${CMAKE_BINARY_DIR}")
diskann_pipeline_apply_cxx17(diskann_hnsw_merger_vamana)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/m1_merge.cpp" _m1_wrapper_sha)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/m1_algorithm.cpp" _m1_algorithm_sha)
file(SHA256 "${PROJECT_SOURCE_DIR}/src/baseline/cpp_main/merge_call_record.hpp" _merge_record_sha)
target_compile_definitions(diskann_hnsw_merger_vamana PRIVATE
  MERGE_WRAPPER_FILE="${CMAKE_CURRENT_SOURCE_DIR}/m1_merge.cpp"
  MERGE_WRAPPER_SHA="${_m1_wrapper_sha}"
  MERGE_ALGORITHM_FILE="${CMAKE_CURRENT_SOURCE_DIR}/m1_algorithm.cpp"
  MERGE_ALGORITHM_SHA="${_m1_algorithm_sha}"
  MERGE_RECORD_FILE="${PROJECT_SOURCE_DIR}/src/baseline/cpp_main/merge_call_record.hpp"
  MERGE_RECORD_SHA="${_merge_record_sha}"
  MERGE_LIBRARY="$<TARGET_FILE:diskann_hnsw_merger_vamana>")

add_executable(
  hnsw_merger_experiment_runner
  hnsw_merger_experiment_runner.cpp)
target_link_libraries(
  hnsw_merger_experiment_runner
  PRIVATE diskann_pipeline::hnsw_merger_vamana diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  hnsw_merger_experiment_runner
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  hnsw_merger_experiment_runner
  PRIVATE
    HNSW_MERGER_RUNNER_PROJECT_REPOSITORY="${HNSW_MERGER_PROJECT_REPOSITORY}"
    HNSW_MERGER_RUNNER_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    HNSW_MERGER_RUNNER_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    HNSW_MERGER_RUNNER_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    HNSW_MERGER_RUNNER_PROJECT_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    HNSW_MERGER_RUNNER_PROJECT_CANONICAL_REPOSITORY_RESOLVED=${HNSW_MERGER_PROJECT_CANONICAL_REPOSITORY_RESOLVED})
set_target_properties(
  hnsw_merger_experiment_runner
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/src/papers/hnsw_merger/vamana")
diskann_pipeline_apply_cxx17(hnsw_merger_experiment_runner)
