add_library(
  diskann_pipeline_core
  core/json_codec.cpp
  core/document_codecs.cpp
  core/workspace.cpp
  core/artifact_resolver.cpp
  core/provenance_store.cpp
  core/evaluation_store.cpp
  core/quarantine_manager.cpp
  core/diagnostic_quarantine_resolver.cpp
  pipeline/artifact_reuse.cpp
  pipeline/pipeline_executor.cpp
  pipeline/evaluator.cpp
  cli/runner_protocol.cpp
  registry/implementation_registry.cpp
  registry/compatibility_checker.cpp
  validation/canonical_json.cpp
  validation/sha256.cpp
  validation/schema_catalog.cpp
  validation/validation_gate.cpp
  validation/artifact_validator.cpp
  search/diskann/native_search_formats.cpp
  baseline/cpp_main/native_formats.cpp
  validation/mapping_validator.cpp
  validation/graph_validator.cpp
  validation/validator.cpp
  validation/adapter_policy.cpp)
target_sources(diskann_pipeline_core PRIVATE validation/parameter_validator.cpp)
add_library(diskann_pipeline::core ALIAS diskann_pipeline_core)

target_link_libraries(
  diskann_pipeline_core
  PUBLIC diskann_pipeline::headers
  PRIVATE nlohmann_json::nlohmann_json OpenSSL::Crypto)
target_include_directories(diskann_pipeline_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(
  diskann_pipeline_core
  PRIVATE
    DISKANN_PIPELINE_SOURCE_REPOSITORY="${DISKANN_PIPELINE_SOURCE_REPOSITORY}"
    DISKANN_PIPELINE_SOURCE_BRANCH="${DISKANN_PIPELINE_SOURCE_BRANCH}"
    DISKANN_PIPELINE_SOURCE_COMMIT="${DISKANN_PIPELINE_SOURCE_COMMIT}"
    DISKANN_PIPELINE_SOURCE_DIRTY=${DISKANN_PIPELINE_SOURCE_DIRTY}
    DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM="${DISKANN_PIPELINE_SOURCE_DIRTY_CHECKSUM}"
    DISKANN_PIPELINE_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
    DISKANN_PIPELINE_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
    DISKANN_PIPELINE_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    DISKANN_PIPELINE_CMAKE_VERSION="${CMAKE_VERSION}"
    DISKANN_PIPELINE_CMAKE_GENERATOR="${CMAKE_GENERATOR}"
    DISKANN_PIPELINE_BUILD_TYPE="${DISKANN_PIPELINE_RECORDED_BUILD_TYPE}")
target_compile_definitions(
  diskann_pipeline_core
  PRIVATE
    DISKANN_PIPELINE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    DISKANN_PIPELINE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
diskann_pipeline_apply_cxx17(diskann_pipeline_core)

add_subdirectory(cli)

if(DISKANN_SEARCH_EVALUATION)
  add_subdirectory(search/diskann)
endif()

if(DISKANN_GP_ANN_OGP)
  add_subdirectory(papers/gp_ann/ogp)
endif()

if(DISKANN_HNSW_MERGER)
  add_subdirectory(papers/hnsw_merger/vamana)
endif()

if(DISKANN_XN_DESCENT)
  add_subdirectory(papers/xn_graph/xn_descent)
endif()

install(TARGETS diskann_pipeline_core)
