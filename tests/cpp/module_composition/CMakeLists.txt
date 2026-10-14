add_executable(module_composition_tests test_module_composition.cpp)
target_link_libraries(module_composition_tests PRIVATE diskann_cpp_main_stages nlohmann_json::nlohmann_json)
target_include_directories(module_composition_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp")
target_compile_definitions(module_composition_tests PRIVATE
  COMPOSITION_RUNNER="$<TARGET_FILE:module_composition_runner>"
  COMPOSITION_TEST_ROOT="${PROJECT_SOURCE_DIR}")
foreach(option_name IN ITEMS DISKANN_KMEANS_PARTITION DISKANN_GP_ANN_OGP DISKANN_XN_DESCENT DISKANN_HNSW_MERGER)
  if(${option_name})
    target_compile_definitions(module_composition_tests PRIVATE ${option_name}=1)
  endif()
endforeach()
add_dependencies(module_composition_tests module_composition_runner)
diskann_pipeline_apply_cxx17(module_composition_tests)
add_test(NAME module_composition_requests COMMAND module_composition_tests requests)
add_test(NAME module_composition_native COMMAND module_composition_tests native)
set_tests_properties(module_composition_requests module_composition_native PROPERTIES
  TIMEOUT 240 ENVIRONMENT "OMP_NUM_THREADS=6;MKL_NUM_THREADS=6")
