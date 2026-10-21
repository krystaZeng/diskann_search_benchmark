target_sources(
  diskann_pipeline_core
  PRIVATE
    source_build_identity.cpp)

add_library(
  diskann_cpp_main_stages STATIC
  descriptors.cpp
  parameters.cpp
  cpp_main_vector_accessor.cpp
  native_reference_capture.cpp
  p0_partition.cpp
  b0_local_build.cpp
  m0_merge.cpp)
target_link_libraries(
  diskann_cpp_main_stages
  PUBLIC diskann_pipeline::core
  PRIVATE diskann_cpp_main_upstream nlohmann_json::nlohmann_json)
target_include_directories(
  diskann_cpp_main_stages
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(diskann_cpp_main_stages)

add_executable(
  cpp_main_baseline_runner
  cpp_main_baseline_runner.cpp)
target_link_libraries(
  cpp_main_baseline_runner
  PRIVATE diskann_cpp_main_stages nlohmann_json::nlohmann_json)
target_include_directories(
  cpp_main_baseline_runner
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_target_properties(
  cpp_main_baseline_runner
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/src/baseline/cpp_main")
diskann_pipeline_apply_cxx17(cpp_main_baseline_runner)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(
    native_reference_capture_runner
    native_reference_capture_runner.cpp)
  target_link_libraries(
    native_reference_capture_runner
    PRIVATE diskann_cpp_main_stages nlohmann_json::nlohmann_json)
  target_include_directories(
    native_reference_capture_runner
    PRIVATE "${PROJECT_SOURCE_DIR}/src")
  set_target_properties(
    native_reference_capture_runner
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/src/baseline/cpp_main")
  diskann_pipeline_apply_cxx17(native_reference_capture_runner)
endif()
target_compile_definitions(
  diskann_pipeline_core
  PRIVATE
    CPP_MAIN_REPOSITORY_ROOT="${CMAKE_SOURCE_DIR}"
    CPP_MAIN_UPSTREAM_SOURCE_DIR="${_upstream_source}"
    CPP_MAIN_UPSTREAM_BINARY_DIR="${_upstream_binary}"
    CPP_MAIN_UPSTREAM_LIBRARY_FILE="$<TARGET_FILE:diskann_s>"
    CPP_MAIN_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    CPP_MAIN_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    CPP_MAIN_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    CPP_MAIN_CMAKE_VERSION="${CMAKE_VERSION}"
    CPP_MAIN_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    CPP_MAIN_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    CPP_MAIN_EFFECTIVE_DEFINITIONS="${_effective_definitions}"
    CPP_MAIN_PROJECT_COMPILE_OPTIONS="${_upstream_compile_options}"
    CPP_MAIN_CONFIGURE_COMMAND="${_configure_command}"
    CPP_MAIN_BUILD_COMMAND="${_build_command}")

# One archive owns the unchanged native template bridge for both consumers.
# Keep its source in place; fixed K must not require the optional KMeans module.
if(DISKANN_KMEANS_PARTITION OR DISKANN_MODULE_COMPOSITION)
  # The pinned archive does not export the ID-only template. Instantiate it from
  # the unchanged source outside upstream, with the upstream compilation options.
  add_library(diskann_native_partition STATIC
      "${PROJECT_SOURCE_DIR}/src/partition/diskann_kmeans/native_partition.cpp")
  target_link_libraries(diskann_native_partition PUBLIC diskann_cpp_main_upstream)
  target_compile_definitions(diskann_native_partition PRIVATE
    KMEANS_NATIVE_SOURCE="${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main/src/partition.cpp")
  get_directory_property(_native_flags DIRECTORY
    "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" DEFINITION CMAKE_CXX_FLAGS)
  string(TOUPPER "${CMAKE_BUILD_TYPE}" _config)
  get_directory_property(_native_config_flags DIRECTORY
    "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" DEFINITION "CMAKE_CXX_FLAGS_${_config}")
  get_directory_property(_native_options DIRECTORY
    "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" COMPILE_OPTIONS)
  get_directory_property(_native_includes DIRECTORY
    "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" INCLUDE_DIRECTORIES)
  get_directory_property(_native_definitions DIRECTORY
    "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" COMPILE_DEFINITIONS)
  separate_arguments(_native_flags UNIX_COMMAND "${_native_flags} ${_native_config_flags}")
  target_compile_options(diskann_native_partition PRIVATE ${_native_flags} ${_native_options})
  target_include_directories(diskann_native_partition PRIVATE ${_native_includes})
  target_compile_definitions(diskann_native_partition PRIVATE ${_native_definitions})
  target_compile_features(diskann_native_partition PRIVATE cxx_std_17)
endif()

if(DISKANN_MODULE_COMPOSITION)
  add_library(diskann_cpp_main_fixed_k STATIC p0_fixed_k_partition.cpp)
  target_include_directories(diskann_cpp_main_fixed_k PUBLIC "${PROJECT_SOURCE_DIR}/src"
    PRIVATE ${_native_includes})
  target_compile_options(diskann_cpp_main_fixed_k PRIVATE -fopenmp)
  target_compile_definitions(diskann_cpp_main_fixed_k PRIVATE ${_native_definitions})
  target_link_libraries(diskann_cpp_main_fixed_k PUBLIC diskann_cpp_main_stages
    PRIVATE diskann_native_partition nlohmann_json::nlohmann_json)
  diskann_pipeline_apply_cxx17(diskann_cpp_main_fixed_k)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/p0_fixed_k_partition.cpp" _fixed_k_source_sha)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/p0_fixed_k_partition.hpp" _fixed_k_header_sha)
  file(SHA256 "${PROJECT_SOURCE_DIR}/src/partition/diskann_kmeans/native_partition.cpp" _fixed_k_bridge_sha)
  file(SHA256 "${PROJECT_SOURCE_DIR}/contracts/partition/cpp-main-p0-fixed-k-v1/parameters.schema.json" _fixed_k_schema_sha)
  target_compile_definitions(diskann_cpp_main_fixed_k PRIVATE
    FIXED_K_BUILD_DIR="${CMAKE_BINARY_DIR}"
    FIXED_K_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    FIXED_K_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    FIXED_K_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    FIXED_K_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    FIXED_K_PROJECT_DIRTY_SHA="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    FIXED_K_SOURCE_SHA="${_fixed_k_source_sha}"
    FIXED_K_HEADER_SHA="${_fixed_k_header_sha}"
    FIXED_K_BRIDGE_SHA="${_fixed_k_bridge_sha}"
    FIXED_K_SCHEMA_SHA="${_fixed_k_schema_sha}"
    FIXED_K_LIBRARY="$<TARGET_FILE:diskann_cpp_main_fixed_k>"
    FIXED_K_BRIDGE_LIBRARY="$<TARGET_FILE:diskann_native_partition>")
endif()
