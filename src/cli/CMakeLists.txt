add_executable(
  diskann_pipeline_runner
  runner_main.cpp
  production_registry.cpp)
target_link_libraries(diskann_pipeline_runner PRIVATE diskann_pipeline::core)
diskann_pipeline_apply_cxx17(diskann_pipeline_runner)

install(TARGETS diskann_pipeline_runner RUNTIME DESTINATION bin)
