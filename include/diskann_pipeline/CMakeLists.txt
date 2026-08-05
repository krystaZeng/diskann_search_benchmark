add_library(diskann_pipeline_headers INTERFACE)
add_library(diskann_pipeline::headers ALIAS diskann_pipeline_headers)

target_include_directories(
  diskann_pipeline_headers
  INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)
target_compile_features(diskann_pipeline_headers INTERFACE cxx_std_17)

install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/include/diskann_pipeline"
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.hpp")

