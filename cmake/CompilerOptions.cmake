include_guard(GLOBAL)

function(diskann_pipeline_apply_cxx17 target)
  set_target_properties(
    "${target}"
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED ON
               CXX_EXTENSIONS OFF)

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
  elseif(MSVC)
    target_compile_options("${target}" PRIVATE /W4 /permissive-)
  endif()
endfunction()

