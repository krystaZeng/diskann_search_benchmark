add_executable(gp_ann_ogp_partition test_ogp_partition.cpp)
target_link_libraries(
  gp_ann_ogp_partition
  PRIVATE diskann_pipeline::gp_ann_ogp_stage diskann_pipeline::fake_stages
          diskann_cpp_main_stages
          nlohmann_json::nlohmann_json)
target_include_directories(
  gp_ann_ogp_partition
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
diskann_pipeline_apply_cxx17(gp_ann_ogp_partition)
add_test(NAME gp_ann_ogp_partition COMMAND gp_ann_ogp_partition)
set_tests_properties(gp_ann_ogp_partition PROPERTIES LABELS "gp-ann-ogp;phase3")

add_executable(gp_ann_ogp_pipeline test_ogp_pipeline.cpp)
target_link_libraries(
  gp_ann_ogp_pipeline
  PRIVATE diskann_pipeline::gp_ann_ogp_stage diskann_pipeline::fake_stages
          diskann_cpp_main_stages nlohmann_json::nlohmann_json)
target_include_directories(
  gp_ann_ogp_pipeline
  PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
diskann_pipeline_apply_cxx17(gp_ann_ogp_pipeline)
add_test(NAME gp_ann_ogp_pipeline COMMAND gp_ann_ogp_pipeline)
set_tests_properties(gp_ann_ogp_pipeline PROPERTIES LABELS "gp-ann-ogp;phase4")
