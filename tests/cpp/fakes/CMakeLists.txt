add_library(
  diskann_pipeline_fake_stages STATIC
  fake_fixture.cpp
  fake_stage_context.cpp
  fake_partition.cpp
  fake_local_graph.cpp
  fake_merge.cpp
  fake_registration.cpp
  invalid_fixture_factory.cpp)
add_library(diskann_pipeline::fake_stages ALIAS diskann_pipeline_fake_stages)
target_link_libraries(
  diskann_pipeline_fake_stages
  PUBLIC diskann_pipeline::core
  PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(
  diskann_pipeline_fake_stages
  PUBLIC "${PROJECT_SOURCE_DIR}/tests/cpp")
diskann_pipeline_apply_cxx17(diskann_pipeline_fake_stages)

add_executable(diskann_pipeline_fake_runner fake_runner_main.cpp)
target_link_libraries(
  diskann_pipeline_fake_runner
  PRIVATE diskann_pipeline::fake_stages nlohmann_json::nlohmann_json)
diskann_pipeline_apply_cxx17(diskann_pipeline_fake_runner)

get_target_property(_production_links diskann_pipeline_runner LINK_LIBRARIES)
get_target_property(_production_sources diskann_pipeline_runner SOURCES)
if("${_production_links}" MATCHES "fake")
  message(FATAL_ERROR "production runner must not link a Fake target")
endif()
if("${_production_sources}" MATCHES "fake")
  message(FATAL_ERROR "production runner must not compile Fake sources")
endif()

file(
  GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assembly-targets.txt"
  CONTENT
    "production_links=$<JOIN:$<TARGET_PROPERTY:diskann_pipeline_runner,LINK_LIBRARIES>,|>\nproduction_sources=$<JOIN:$<TARGET_PROPERTY:diskann_pipeline_runner,SOURCES>,|>\nfake_target=$<TARGET_FILE_NAME:diskann_pipeline_fake_runner>\n")
add_test(
  NAME production_target_isolation
  COMMAND
    "${CMAKE_COMMAND}"
    "-DASSEMBLY_FILE=${CMAKE_CURRENT_BINARY_DIR}/assembly-targets.txt"
    -P "${PROJECT_SOURCE_DIR}/tests/cmake/check_target_isolation.cmake")
set_tests_properties(
  production_target_isolation PROPERTIES LABELS "phase3;phase4;assembly")
