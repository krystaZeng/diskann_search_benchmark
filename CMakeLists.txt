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

execute_process(
  COMMAND git rev-parse --show-toplevel
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_REPOSITORY
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git branch --show-current
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git status --porcelain=v1 --untracked-files=all
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(DISKANN_PIPELINE_SOURCE_STATUS)
  set(DISKANN_PIPELINE_SOURCE_DIRTY 1)
  execute_process(
    COMMAND git diff --binary HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE DISKANN_PIPELINE_TRACKED_DIFF
    ERROR_QUIET)
  execute_process(
    COMMAND git ls-files --others --exclude-standard
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE DISKANN_PIPELINE_UNTRACKED_FILES
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  set(_dirty_material
      "status:\n${DISKANN_PIPELINE_SOURCE_STATUS}\ntracked:\n${DISKANN_PIPELINE_TRACKED_DIFF}")
  string(REPLACE "\n" ";" _untracked_list "${DISKANN_PIPELINE_UNTRACKED_FILES}")
  foreach(_untracked IN LISTS _untracked_list)
    if(_untracked AND EXISTS "${CMAKE_SOURCE_DIR}/${_untracked}"
       AND NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/${_untracked}")
      file(SHA256 "${CMAKE_SOURCE_DIR}/${_untracked}" _untracked_sha256)
      string(APPEND _dirty_material
             "\nuntracked:${_untracked}:${_untracked_sha256}")
    endif()
  endforeach()
  string(SHA256 DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM "${_dirty_material}")
else()
  set(DISKANN_PIPELINE_SOURCE_DIRTY 0)
  set(DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM
      "0000000000000000000000000000000000000000000000000000000000000000")
endif()
if(CMAKE_BUILD_TYPE)
  set(DISKANN_PIPELINE_RECORDED_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
  set(DISKANN_PIPELINE_RECORDED_BUILD_TYPE "unspecified")
endif()

add_subdirectory(include/diskann_pipeline)
add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
