cmake_minimum_required(VERSION 3.20)

project(
  diskann_modular_pipeline
  VERSION 0.1.0
  DESCRIPTION "C++17 contracts for a modular DiskANN experiment pipeline"
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(
  DISKANN_CPP_MAIN_BASELINE
  "Build the fixed cpp_main Baseline wrapper and its tests"
  OFF)
option(
  DISKANN_GP_ANN_OGP
  "Build the fixed official GP-ANN OGP Partition integration"
  OFF)
option(
  DISKANN_HNSW_MERGER
  "Build the HNSW-Merger Vamana Merge faithful adaptation"
  OFF)
option(
  DISKANN_XN_DESCENT
  "Build the XN-Graph XN-Descent Local Build faithful adaptation"
  OFF)
option(
  DISKANN_SEARCH_EVALUATION
  "Build the fixed DiskANN post-GlobalGraph search evaluation Feature"
  OFF)

if(DISKANN_SEARCH_EVALUATION AND NOT DISKANN_CPP_MAIN_BASELINE)
  message(FATAL_ERROR
          "DISKANN_SEARCH_EVALUATION requires DISKANN_CPP_MAIN_BASELINE=ON")
endif()

set(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE ON)
endif()

set(DISKANN_HNSW_MERGER_REPOSITORY
    "https://github.com/Kimchuls/HNSWMerger.git")
set(DISKANN_HNSW_MERGER_BRANCH "main")
set(DISKANN_HNSW_MERGER_COMMIT
    "88f6b7179f651cf5f284bd4e52249a4b1323b5af")
set(DISKANN_HNSW_MERGER_INTEGRATION_MODE "source_mapping_only")
set(DISKANN_HNSW_MERGER_OFFICIAL_BINARY_LINKED 0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerOptions)

execute_process(
  COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} rev-parse --show-toplevel
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_REPOSITORY
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} branch --show-current
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
execute_process(
  COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} status --porcelain=v1 --untracked-files=all
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE DISKANN_PIPELINE_SOURCE_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(DISKANN_PIPELINE_SOURCE_STATUS)
  set(DISKANN_PIPELINE_SOURCE_DIRTY 1)
  execute_process(
    COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} diff --binary HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE DISKANN_PIPELINE_TRACKED_DIFF
    ERROR_QUIET)
  execute_process(
    COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} ls-files --others --exclude-standard
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

if(DISKANN_HNSW_MERGER)
  include(HnswMergerProjectIdentity)
  diskann_resolve_hnsw_merger_project_identity("${CMAKE_SOURCE_DIR}")
endif()

add_subdirectory(include/diskann_pipeline)

if(DISKANN_GP_ANN_OGP)
  include(GpAnnOgp)
  diskann_configure_gp_ann_ogp()
endif()

# Feature 005's Linux-native targets name this integration while src/ is being
# configured.  Declare only the interface identity here; the existing pinned
# Baseline admission below binds it to diskann_s after the core target exists.
if(DISKANN_CPP_MAIN_BASELINE AND DISKANN_SEARCH_EVALUATION AND
   DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE)
  find_package(OpenMP REQUIRED)
  add_library(diskann_cpp_main_upstream INTERFACE)
endif()

add_subdirectory(src)

if(DISKANN_CPP_MAIN_BASELINE AND
   (NOT DISKANN_SEARCH_EVALUATION OR DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE))
  include(CppMainBaseline)
  diskann_configure_cpp_main_baseline()
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
