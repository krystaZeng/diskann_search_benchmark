add_executable(module_composition_runner runner.cpp)
target_include_directories(module_composition_runner PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(module_composition_runner PRIVATE
  diskann_cpp_main_stages nlohmann_json::nlohmann_json)
target_compile_definitions(module_composition_runner PRIVATE
  COMPOSITION_BASELINE_LIBRARY="$<TARGET_FILE:diskann_cpp_main_stages>")
foreach(pair IN ITEMS
    "DISKANN_KMEANS_PARTITION|diskann_kmeans_partition"
    "DISKANN_GP_ANN_OGP|diskann_gp_ann_ogp_stage"
    "DISKANN_XN_DESCENT|diskann_xn_descent"
    "DISKANN_HNSW_MERGER|diskann_hnsw_merger_vamana")
  string(REPLACE "|" ";" fields "${pair}")
  list(GET fields 0 option_name)
  list(GET fields 1 library)
  if(${option_name})
    target_link_libraries(module_composition_runner PRIVATE ${library})
    target_compile_definitions(module_composition_runner PRIVATE ${option_name}=1)
    target_compile_definitions(module_composition_runner PRIVATE
      ${option_name}_LIBRARY="$<TARGET_FILE:${library}>")
  endif()
endforeach()

# The executable records its own compile/link commands, not a dedicated
# runner's build record. Retain the configure-time source bytes as well as HEAD.
file(GLOB_RECURSE _identity_inputs RELATIVE "${PROJECT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp"
  "${PROJECT_SOURCE_DIR}/src/CMakeLists.txt" "${PROJECT_SOURCE_DIR}/src/*/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/include/*.hpp" "${PROJECT_SOURCE_DIR}/cmake/*.cmake")
list(APPEND _identity_inputs CMakeLists.txt)
set(_source_json "{")
foreach(path IN LISTS _identity_inputs)
  file(SHA256 "${PROJECT_SOURCE_DIR}/${path}" checksum)
  string(APPEND _source_json "\"${path}\":\"${checksum}\",")
endforeach()
string(REGEX REPLACE ",$" "}" _source_json "${_source_json}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source-identity.json" "${_source_json}")
file(SHA256 "${CMAKE_CURRENT_BINARY_DIR}/source-identity.json" _identity_sha)
target_compile_definitions(module_composition_runner PRIVATE
  COMPOSITION_ROOT="${PROJECT_SOURCE_DIR}"
  COMPOSITION_BUILD="${CMAKE_BINARY_DIR}"
  COMPOSITION_IDENTITY_SHA="${_identity_sha}"
  COMPOSITION_SOURCE_REPOSITORY="${DISKANN_PIPELINE_SOURCE_REPOSITORY}"
  COMPOSITION_SOURCE_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
  COMPOSITION_SOURCE_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
  COMPOSITION_SOURCE_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
  COMPOSITION_SOURCE_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
  COMPOSITION_M1_REPOSITORY="${HNSW_MERGER_PROJECT_REPOSITORY}")
diskann_pipeline_apply_cxx17(module_composition_runner)
