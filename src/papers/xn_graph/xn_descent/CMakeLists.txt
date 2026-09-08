add_library(
  diskann_xn_descent STATIC
  parameters.cpp
  xn_descent_algorithm.cpp
  xn_descent_local_build.cpp)
add_library(diskann_pipeline::xn_descent ALIAS diskann_xn_descent)
target_link_libraries(
  diskann_xn_descent
  PUBLIC diskann_pipeline::core
  PRIVATE nlohmann_json::nlohmann_json)
if(DISKANN_CPP_MAIN_BASELINE)
  target_link_libraries(
    diskann_xn_descent
    PRIVATE diskann_cpp_main_stages)
else()
  target_sources(
    diskann_xn_descent
    PRIVATE
      ${PROJECT_SOURCE_DIR}/src/baseline/cpp_main/cpp_main_vector_accessor.cpp)
endif()
target_include_directories(
  diskann_xn_descent PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  diskann_xn_descent
  PRIVATE
    XN_DESCENT_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
    XN_DESCENT_PROJECT_REPOSITORY="${DISKANN_PIPELINE_SOURCE_REPOSITORY}"
    XN_DESCENT_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    XN_DESCENT_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    XN_DESCENT_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    XN_DESCENT_PROJECT_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    XN_DESCENT_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    XN_DESCENT_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    XN_DESCENT_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    XN_DESCENT_CMAKE_VERSION="${CMAKE_VERSION}"
    XN_DESCENT_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    XN_DESCENT_BUILD_TYPE="${DISKANN_PIPELINE_RECORDED_BUILD_TYPE}"
    XN_DESCENT_BINARY_DIR="${CMAKE_BINARY_DIR}"
    XN_DESCENT_CPP_MAIN_BASELINE_ENABLED=$<BOOL:${DISKANN_CPP_MAIN_BASELINE}>)
diskann_pipeline_apply_cxx17(diskann_xn_descent)
