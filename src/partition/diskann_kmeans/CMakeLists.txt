# The pinned archive does not export the ID-only template. Instantiate it from
# the unchanged source outside upstream, with the upstream compilation options.
add_library(diskann_kmeans_native OBJECT native_partition.cpp)
target_link_libraries(diskann_kmeans_native PRIVATE diskann_cpp_main_upstream)
target_compile_definitions(diskann_kmeans_native PRIVATE
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
target_compile_options(diskann_kmeans_native PRIVATE ${_native_flags} ${_native_options})
target_include_directories(diskann_kmeans_native PRIVATE ${_native_includes})
target_compile_definitions(diskann_kmeans_native PRIVATE ${_native_definitions})
target_compile_features(diskann_kmeans_native PRIVATE cxx_std_17)

add_library(diskann_kmeans_partition STATIC parameters.cpp partition.cpp
  $<TARGET_OBJECTS:diskann_kmeans_native>)
target_include_directories(diskann_kmeans_partition PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(diskann_kmeans_partition PRIVATE ${_native_includes})
target_compile_options(diskann_kmeans_partition PRIVATE -fopenmp)
target_link_libraries(diskann_kmeans_partition PUBLIC diskann_cpp_main_stages
  PRIVATE diskann_cpp_main_upstream nlohmann_json::nlohmann_json)
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
