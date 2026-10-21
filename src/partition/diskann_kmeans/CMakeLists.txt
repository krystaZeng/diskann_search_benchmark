# The native bridge is built once by the Baseline CMake integration, independently
# of this optional implementation. Do not embed another partition.cpp object.
get_directory_property(_native_includes DIRECTORY
  "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" INCLUDE_DIRECTORIES)
add_library(diskann_kmeans_partition STATIC parameters.cpp partition.cpp)
target_include_directories(diskann_kmeans_partition PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(diskann_kmeans_partition PRIVATE ${_native_includes})
target_compile_options(diskann_kmeans_partition PRIVATE -fopenmp)
target_link_libraries(diskann_kmeans_partition PUBLIC diskann_cpp_main_stages
  PRIVATE diskann_native_partition nlohmann_json::nlohmann_json)
diskann_pipeline_apply_cxx17(diskann_kmeans_partition)
target_compile_definitions(diskann_kmeans_partition PRIVATE
  KMEANS_BUILD_DIR="${CMAKE_BINARY_DIR}"
  KMEANS_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
  KMEANS_PROJECT_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
  KMEANS_PROJECT_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
  KMEANS_PROJECT_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
  KMEANS_PROJECT_DIRTY_SHA="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}")

add_executable(kmeans_partition_runner runner.cpp)
target_link_libraries(kmeans_partition_runner PRIVATE diskann_kmeans_partition
  nlohmann_json::nlohmann_json)
diskann_pipeline_apply_cxx17(kmeans_partition_runner)
