add_library(
  diskann_gp_ann_ogp_stage STATIC
  descriptor.cpp
  source_build_identity.cpp
  official_partition_process.cpp
  cluster_output.cpp
  ogp_partition.cpp)
add_library(diskann_pipeline::gp_ann_ogp_stage ALIAS diskann_gp_ann_ogp_stage)
add_dependencies(diskann_gp_ann_ogp_stage gp_ann_partition_official)
target_link_libraries(
  diskann_gp_ann_ogp_stage
  PUBLIC diskann_pipeline::core
  PRIVATE diskann_cpp_main_stages
  PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(
  diskann_gp_ann_ogp_stage PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(
  diskann_gp_ann_ogp_stage
  PRIVATE
    GPANN_OGP_SOURCE_ROOT="${GPANN_SOURCE_DIR}"
    GPANN_OGP_BUILD_ROOT="${CMAKE_BINARY_DIR}/gp-ann-official"
    GPANN_OGP_PARTITION_EXECUTABLE="${GPANN_PARTITION_EXECUTABLE}"
    GPANN_OGP_BUILD_IDENTITY_FILE="${GPANN_BUILD_IDENTITY_FILE}")
diskann_pipeline_apply_cxx17(diskann_gp_ann_ogp_stage)

add_executable(ogp_experiment_runner ogp_experiment_runner.cpp)
target_link_libraries(
  ogp_experiment_runner
  PRIVATE diskann_pipeline::gp_ann_ogp_stage diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  ogp_experiment_runner PRIVATE "${PROJECT_SOURCE_DIR}/src")
diskann_pipeline_apply_cxx17(ogp_experiment_runner)
