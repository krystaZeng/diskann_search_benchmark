target_sources(
  diskann_pipeline_core
  PRIVATE
    native_formats.cpp
    source_build_identity.cpp)

add_library(
  diskann_cpp_main_stages STATIC
  descriptors.cpp
  cpp_main_vector_accessor.cpp
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
