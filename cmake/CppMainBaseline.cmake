function(diskann_configure_cpp_main_baseline)
  set(_pinned_repository "https://github.com/microsoft/DiskANN.git")
  set(_pinned_branch "cpp_main")
  set(_pinned_commit "78256bbab4685e1774e78d331e081a153be26823")
  set(_upstream_source "${CMAKE_SOURCE_DIR}/upstream/diskann-cpp-main")
  set(_upstream_binary "${CMAKE_BINARY_DIR}/cpp-main-upstream")

  execute_process(
    COMMAND git -c safe.directory=${CMAKE_SOURCE_DIR} -C "${CMAKE_SOURCE_DIR}" ls-files -s --
            upstream/diskann-cpp-main
    OUTPUT_VARIABLE _gitlink
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _gitlink_result)
  if(NOT _gitlink_result EQUAL 0 OR
     NOT _gitlink MATCHES
         "^160000 ${_pinned_commit} 0[\t ]+upstream/diskann-cpp-main$")
    message(FATAL_ERROR "cpp_main parent gitlink is not the pinned commit")
  endif()

  execute_process(
    COMMAND git -c safe.directory=${_upstream_source} -C "${_upstream_source}" rev-parse HEAD
    OUTPUT_VARIABLE _upstream_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _head_result)
  execute_process(
    COMMAND git -c safe.directory=${_upstream_source} -C "${_upstream_source}" branch --show-current
    OUTPUT_VARIABLE _upstream_branch
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _branch_result)
  execute_process(
    COMMAND git -c safe.directory=${_upstream_source} -C "${_upstream_source}" remote get-url origin
    OUTPUT_VARIABLE _upstream_remote
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _remote_result)
  execute_process(
    COMMAND git -c safe.directory=${_upstream_source} -C "${_upstream_source}" status --porcelain=v1
            --untracked-files=all
    OUTPUT_VARIABLE _upstream_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _status_result)
  if(NOT _head_result EQUAL 0 OR NOT _branch_result EQUAL 0 OR
     NOT _remote_result EQUAL 0 OR NOT _status_result EQUAL 0 OR
     NOT _upstream_head STREQUAL _pinned_commit OR
     NOT _upstream_branch STREQUAL _pinned_branch OR
     NOT _upstream_remote STREQUAL _pinned_repository OR _upstream_status)
    message(FATAL_ERROR
            "cpp_main source admission requires pinned HEAD/branch/remote and clean state")
  endif()

  file(REAL_PATH "${_upstream_source}" _upstream_source_real)
  file(MAKE_DIRECTORY "${_upstream_binary}")
  file(REAL_PATH "${_upstream_binary}" _upstream_binary_real)
  string(FIND "${_upstream_binary_real}/" "${_upstream_source_real}/"
         _binary_inside_source)
  if(_binary_inside_source EQUAL 0)
    message(FATAL_ERROR "cpp_main binary directory must be outside upstream")
  endif()

  set(PYBIND OFF CACHE BOOL "" FORCE)
  set(RESTAPI OFF CACHE BOOL "" FORCE)
  set(UNIT_TEST OFF CACHE BOOL "" FORCE)
  set(PORTABLE OFF CACHE BOOL "" FORCE)
  add_subdirectory("${_upstream_source}" "${_upstream_binary}"
                   EXCLUDE_FROM_ALL)
  if(NOT TARGET diskann_s)
    message(FATAL_ERROR "pinned cpp_main did not define diskann_s")
  endif()

  if(NOT TARGET diskann_cpp_main_upstream)
    add_library(diskann_cpp_main_upstream INTERFACE)
  endif()
  target_link_libraries(diskann_cpp_main_upstream INTERFACE diskann_s)
  if(NOT MSVC)
    # LinuxAlignedFileReader is part of the admitted static surface, so libaio
    # must follow diskann_s on every thin caller's final link line.
    target_link_libraries(diskann_cpp_main_upstream INTERFACE aio)
  endif()
  target_include_directories(
    diskann_cpp_main_upstream
    INTERFACE "${_upstream_source}/include")

  set(_effective_definitions
      "DISKANN_CPP_MAIN_BASELINE=ON|PYBIND=OFF|RESTAPI=OFF|UNIT_TEST=OFF|PORTABLE=OFF")
  set(_upstream_compile_options
      "-mavx2 -mfma -msse2 -ftree-vectorize -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -fopenmp -fopenmp-simd -funroll-loops -Wfatal-errors -DUSE_AVX2 -DNDEBUG -Ofast -march=native -mtune=native")
  set(_configure_command
      "cmake -S . -B build/cpp-main-baseline -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DBUILD_TESTING=ON -DDISKANN_CPP_MAIN_BASELINE=ON")
  set(_build_command
      "cmake --build build/cpp-main-baseline --target diskann_s")

  add_subdirectory(
    "${CMAKE_SOURCE_DIR}/src/baseline/cpp_main"
    "${CMAKE_BINARY_DIR}/baseline/cpp_main")

  file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/cpp-main-baseline-build-identity.cmake"
    CONTENT
"set(CPP_MAIN_IDENTITY_COMMIT \"${_pinned_commit}\")
set(CPP_MAIN_IDENTITY_UPSTREAM_SOURCE_DIR \"${_upstream_source}\")
set(CPP_MAIN_IDENTITY_UPSTREAM_BINARY_DIR \"${_upstream_binary}\")
set(CPP_MAIN_IDENTITY_LIBRARY_FILE \"$<TARGET_FILE:diskann_s>\")
set(CPP_MAIN_IDENTITY_COMPILER \"${CMAKE_CXX_COMPILER}\")
set(CPP_MAIN_IDENTITY_COMPILER_VERSION \"${CMAKE_CXX_COMPILER_VERSION}\")
set(CPP_MAIN_IDENTITY_CMAKE_VERSION \"${CMAKE_VERSION}\")
set(CPP_MAIN_IDENTITY_GENERATOR \"${CMAKE_GENERATOR}\")
set(CPP_MAIN_IDENTITY_BUILD_TYPE \"${CMAKE_BUILD_TYPE}\")
set(CPP_MAIN_IDENTITY_CONFIGURE_COMMAND \"${_configure_command}\")
set(CPP_MAIN_IDENTITY_BUILD_COMMAND \"${_build_command}\")
set(CPP_MAIN_IDENTITY_EFFECTIVE_DEFINITIONS \"${_effective_definitions}\")
set(CPP_MAIN_IDENTITY_PROJECT_COMPILE_OPTIONS \"${_upstream_compile_options}\")
")
endfunction()
