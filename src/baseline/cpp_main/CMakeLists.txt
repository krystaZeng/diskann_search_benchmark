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
