cmake_minimum_required(VERSION 3.20)

project(
  diskann_modular_pipeline
  VERSION 0.1.0
  DESCRIPTION "C++17 contracts for a modular DiskANN experiment pipeline"
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerOptions)

add_subdirectory(include/diskann_pipeline)
add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

