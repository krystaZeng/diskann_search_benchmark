include_guard(GLOBAL)

include(FetchContent)

if(APPLE)
  foreach(_openssl_prefix IN ITEMS /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
    if(EXISTS "${_openssl_prefix}")
      list(PREPEND CMAKE_PREFIX_PATH "${_openssl_prefix}")
    endif()
  endforeach()
endif()

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(nlohmann_json 3.11 CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  set(JSON_Install OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()

