include_guard(GLOBAL)

if(GPANN_RECORD_IDENTITY_MODE)
  if(NOT EXISTS "${GPANN_PARTITION_EXECUTABLE}")
    message(FATAL_ERROR "official GP-ANN Partition executable is missing")
  endif()
  file(SHA256 "${GPANN_PARTITION_EXECUTABLE}" _partition_sha256)
  file(WRITE "${GPANN_IDENTITY_OUTPUT}"
"set(GPANN_IDENTITY_REPOSITORY \"${GPANN_IDENTITY_REPOSITORY}\")
set(GPANN_IDENTITY_BRANCH \"${GPANN_IDENTITY_BRANCH}\")
set(GPANN_IDENTITY_COMMIT \"${GPANN_IDENTITY_COMMIT}\")
set(GPANN_IDENTITY_PARLAYLIB_COMMIT \"${GPANN_IDENTITY_PARLAYLIB_COMMIT}\")
set(GPANN_IDENTITY_KAMINPAR_COMMIT \"${GPANN_IDENTITY_KAMINPAR_COMMIT}\")
set(GPANN_IDENTITY_HNSWLIB_COMMIT \"${GPANN_IDENTITY_HNSWLIB_COMMIT}\")
set(GPANN_IDENTITY_MESSAGE_QUEUE_COMMIT \"${GPANN_IDENTITY_MESSAGE_QUEUE_COMMIT}\")
set(GPANN_IDENTITY_SOURCE_DIR \"${GPANN_IDENTITY_SOURCE_DIR}\")
set(GPANN_IDENTITY_BINARY_DIR \"${GPANN_IDENTITY_BINARY_DIR}\")
set(GPANN_IDENTITY_PARTITION_EXECUTABLE \"${GPANN_PARTITION_EXECUTABLE}\")
set(GPANN_IDENTITY_PARTITION_SHA256 \"${_partition_sha256}\")
set(GPANN_IDENTITY_COMPILER \"${GPANN_IDENTITY_COMPILER}\")
set(GPANN_IDENTITY_COMPILER_VERSION \"${GPANN_IDENTITY_COMPILER_VERSION}\")
set(GPANN_IDENTITY_CMAKE_VERSION \"${GPANN_IDENTITY_CMAKE_VERSION}\")
set(GPANN_IDENTITY_GENERATOR \"${GPANN_IDENTITY_GENERATOR}\")
set(GPANN_IDENTITY_BUILD_TYPE \"${GPANN_IDENTITY_BUILD_TYPE}\")
set(GPANN_IDENTITY_EFFECTIVE_OPTIONS \"${GPANN_IDENTITY_EFFECTIVE_OPTIONS}\")
set(GPANN_IDENTITY_CONFIGURE_COMMAND \"${GPANN_IDENTITY_CONFIGURE_COMMAND}\")
set(GPANN_IDENTITY_BUILD_COMMAND \"${GPANN_IDENTITY_BUILD_COMMAND}\")
")
  return()
endif()

include(ExternalProject)

function(_gpann_git output result source_dir)
  execute_process(
    COMMAND git -c safe.directory=${source_dir} -C "${source_dir}" ${ARGN}
    OUTPUT_VARIABLE _output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _result)
  set(${output} "${_output}" PARENT_SCOPE)
  set(${result} "${_result}" PARENT_SCOPE)
endfunction()

function(diskann_configure_gp_ann_ogp)
  set(_repository "https://github.com/larsgottesbueren/gp-ann.git")
  set(_branch "main")
  set(_commit "df5efb4a6b303011c795666ed267f29d994b558c")
  set(_parlaylib "ea671f6fbae17e6d6cd25fe9b48134d3c0601787")
  set(_kaminpar "a9e4b1f6d92b4110d4d4a31ee2e7dee5a0761b05")
  set(_hnswlib "359b2ba87358224963986f709e593d799064ace6")
  set(_message_queue "f9b0856dde02248cc91a63077f7b6d5267004f25")

  if(NOT GPANN_SOURCE_DIR)
    message(FATAL_ERROR "GPANN_SOURCE_DIR is required when DISKANN_GP_ANN_OGP=ON")
  endif()
  file(REAL_PATH "${GPANN_SOURCE_DIR}" _source)
  if(NOT EXISTS "${_source}/.git")
    message(FATAL_ERROR "GP-ANN formal source must be an independent Git checkout")
  endif()

  _gpann_git(_remote _remote_result "${_source}" remote get-url origin)
  _gpann_git(_head _head_result "${_source}" rev-parse HEAD)
  _gpann_git(_actual_branch _branch_result "${_source}" branch --show-current)
  _gpann_git(_status _status_result "${_source}"
             status --porcelain=v1 --untracked-files=all)
  if(NOT _remote_result EQUAL 0 OR NOT _head_result EQUAL 0 OR
     NOT _branch_result EQUAL 0 OR NOT _status_result EQUAL 0 OR
     NOT _remote STREQUAL _repository OR NOT _head STREQUAL _commit OR
     NOT _actual_branch STREQUAL _branch OR _status)
    message(FATAL_ERROR
            "GP-ANN source admission requires pinned remote/branch/HEAD and clean state")
  endif()

  foreach(_dependency IN ITEMS
      "external/parlaylib|${_parlaylib}"
      "external/KaMinPar|${_kaminpar}"
      "external/hnswlib|${_hnswlib}"
      "external/message-queue|${_message_queue}")
    string(REPLACE "|" ";" _parts "${_dependency}")
    list(GET _parts 0 _path)
    list(GET _parts 1 _expected)
    _gpann_git(_dependency_head _dependency_result "${_source}/${_path}"
               rev-parse HEAD)
    _gpann_git(_dependency_status _dependency_status_result
               "${_source}/${_path}"
               status --porcelain=v1 --untracked-files=all)
    if(NOT _dependency_result EQUAL 0 OR
       NOT _dependency_status_result EQUAL 0 OR
       NOT _dependency_head STREQUAL _expected OR _dependency_status)
      message(FATAL_ERROR
              "GP-ANN dependency ${_path} is missing, mismatched, or dirty")
    endif()
  endforeach()

  set(_binary "${CMAKE_BINARY_DIR}/gp-ann-official")
  file(MAKE_DIRECTORY "${_binary}")
  file(REAL_PATH "${_binary}" _binary_real)
  string(FIND "${_binary_real}/" "${_source}/" _inside_source)
  if(_inside_source EQUAL 0)
    message(FATAL_ERROR "GP-ANN binary directory must be outside its source tree")
  endif()

  set(_partition_executable "${_binary}/Partition${CMAKE_EXECUTABLE_SUFFIX}")
  set(_identity "${CMAKE_BINARY_DIR}/gp-ann-ogp-build-identity.cmake")
  set(_effective_options
      "CMAKE_CXX_STANDARD=20|MIPS_DISTANCE=OFF|KAMINPAR_64BIT_WEIGHTS=ON|KAMINPAR_64BIT_EDGE_IDS=ON|KAMINPAR_ENABLE_TIMERS=OFF")
  set(_configure_command
      "cmake -S ${_source} -B ${_binary} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DMIPS_DISTANCE=OFF -DKAMINPAR_64BIT_WEIGHTS=ON -DKAMINPAR_64BIT_EDGE_IDS=ON -DKAMINPAR_ENABLE_TIMERS=OFF")
  set(_build_command
      "cmake --build ${_binary} --target Partition --parallel 1")

  ExternalProject_Add(
    gp_ann_partition_official
    SOURCE_DIR "${_source}"
    BINARY_DIR "${_binary}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    INSTALL_COMMAND ""
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DMIPS_DISTANCE=OFF
      -DKAMINPAR_64BIT_WEIGHTS=ON
      -DKAMINPAR_64BIT_EDGE_IDS=ON
      -DKAMINPAR_ENABLE_TIMERS=OFF
    BUILD_COMMAND
      "${CMAKE_COMMAND}" --build "${_binary}" --target Partition --parallel 1
    BUILD_BYPRODUCTS "${_partition_executable}")

  add_custom_command(
    TARGET gp_ann_partition_official POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}"
      -DGPANN_RECORD_IDENTITY_MODE=ON
      -DGPANN_PARTITION_EXECUTABLE=${_partition_executable}
      -DGPANN_IDENTITY_OUTPUT=${_identity}
      -DGPANN_IDENTITY_REPOSITORY=${_repository}
      -DGPANN_IDENTITY_BRANCH=${_branch}
      -DGPANN_IDENTITY_COMMIT=${_commit}
      -DGPANN_IDENTITY_PARLAYLIB_COMMIT=${_parlaylib}
      -DGPANN_IDENTITY_KAMINPAR_COMMIT=${_kaminpar}
      -DGPANN_IDENTITY_HNSWLIB_COMMIT=${_hnswlib}
      -DGPANN_IDENTITY_MESSAGE_QUEUE_COMMIT=${_message_queue}
      -DGPANN_IDENTITY_SOURCE_DIR=${_source}
      -DGPANN_IDENTITY_BINARY_DIR=${_binary}
      -DGPANN_IDENTITY_COMPILER=${CMAKE_CXX_COMPILER}
      -DGPANN_IDENTITY_COMPILER_VERSION=${CMAKE_CXX_COMPILER_VERSION}
      -DGPANN_IDENTITY_CMAKE_VERSION=${CMAKE_VERSION}
      -DGPANN_IDENTITY_GENERATOR=${CMAKE_GENERATOR}
      -DGPANN_IDENTITY_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DGPANN_IDENTITY_EFFECTIVE_OPTIONS=${_effective_options}
      -DGPANN_IDENTITY_CONFIGURE_COMMAND=${_configure_command}
      -DGPANN_IDENTITY_BUILD_COMMAND=${_build_command}
      -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
    BYPRODUCTS "${_identity}"
    VERBATIM)

  set(GPANN_PARTITION_EXECUTABLE "${_partition_executable}"
      CACHE FILEPATH "Admitted official GP-ANN Partition executable" FORCE)
  set(GPANN_BUILD_IDENTITY_FILE "${_identity}"
      CACHE FILEPATH "Admitted official GP-ANN build identity" FORCE)
endfunction()
