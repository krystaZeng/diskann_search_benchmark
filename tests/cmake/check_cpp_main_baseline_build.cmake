cmake_minimum_required(VERSION 3.20)

get_filename_component(REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED CPP_MAIN_BUILD_DIR)
  set(CPP_MAIN_BUILD_DIR "${REPOSITORY_ROOT}/build/cpp-main-baseline")
endif()

set(IDENTITY_FILE
    "${CPP_MAIN_BUILD_DIR}/cpp-main-baseline-build-identity.cmake")
if(NOT EXISTS "${IDENTITY_FILE}")
  message(FATAL_ERROR "cpp_main baseline build identity is missing: ${IDENTITY_FILE}")
endif()
include("${IDENTITY_FILE}")

foreach(required IN ITEMS
        CPP_MAIN_IDENTITY_COMMIT
        CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR
        CPP_MAIN_IDENTITY_UPSTREAM_BINARY_DIR
        CPP_MAIN_IDENTITY_LIBRARY_FILE
        CPP_MAIN_IDENTITY_COMPILER
        CPP_MAIN_IDENTITY_COMPILER_VERSION
        CPP_MAIN_IDENTITY_CMAKE_VERSION
        CPP_MAIN_IDENTITY_GENERATOR
        CPP_MAIN_IDENTITY_BUILD_TYPE
        CPP_MAIN_IDENTITY_CONFIGURE_COMMAND
        CPP_MAIN_IDENTITY_BUILD_COMMAND)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "build identity field is missing: ${required}")
  endif()
endforeach()

if(NOT CPP_MAIN_IDENTITY_COMMIT STREQUAL
   "78256bbab4685e1774e78d331e081a153be26823")
  message(FATAL_ERROR "build identity commit is not pinned")
endif()

file(REAL_PATH "${CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR}" upstream_source)
file(REAL_PATH "${CPP_MAIN_IDENTITY_UPSTREAM_BINARY_DIR}" upstream_binary)
string(FIND "${upstream_binary}/" "${upstream_source}/" inside_upstream)
if(inside_upstream EQUAL 0)
  message(FATAL_ERROR "upstream binary directory is inside the source tree")
endif()

execute_process(
  COMMAND git -c safe.directory=${CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR}
          -C "${CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE actual_head
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT actual_head STREQUAL CPP_MAIN_IDENTITY_COMMIT)
  message(FATAL_ERROR "upstream HEAD changed after configure")
endif()
if(NOT EXISTS "${CPP_MAIN_IDENTITY_LIBRARY_FILE}")
  message(FATAL_ERROR "linked upstream diskann_s is missing")
endif()
execute_process(
  COMMAND git -c safe.directory=${CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR}
          -C "${CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR}" status --porcelain=v1
  OUTPUT_VARIABLE upstream_status
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
if(NOT upstream_status STREQUAL "")
  message(FATAL_ERROR "upstream source is dirty")
endif()
