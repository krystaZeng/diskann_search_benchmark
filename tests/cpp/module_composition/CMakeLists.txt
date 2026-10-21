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
add_test(NAME module_composition_fixed_k COMMAND module_composition_tests fixed_k)
set_tests_properties(module_composition_requests module_composition_native module_composition_fixed_k PROPERTIES
  TIMEOUT 240 ENVIRONMENT "OMP_NUM_THREADS=6;MKL_NUM_THREADS=6")

add_executable(p0_fixed_k_tests test_p0_fixed_k.cpp)
target_link_libraries(p0_fixed_k_tests PRIVATE diskann_cpp_main_fixed_k nlohmann_json::nlohmann_json)
get_directory_property(_native_includes DIRECTORY
  "${PROJECT_SOURCE_DIR}/upstream/diskann-cpp-main" INCLUDE_DIRECTORIES)
target_include_directories(p0_fixed_k_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/cpp" ${_native_includes})
target_compile_options(p0_fixed_k_tests PRIVATE -fopenmp)
target_compile_definitions(p0_fixed_k_tests PRIVATE FIXED_K_TEST_ROOT="${PROJECT_SOURCE_DIR}")
diskann_pipeline_apply_cxx17(p0_fixed_k_tests)
add_test(NAME p0_fixed_k_parameters_contract COMMAND p0_fixed_k_tests unit)
add_test(NAME p0_fixed_k_native COMMAND p0_fixed_k_tests native)
set_tests_properties(p0_fixed_k_parameters_contract p0_fixed_k_native PROPERTIES
  TIMEOUT 180 ENVIRONMENT "OMP_NUM_THREADS=6;MKL_NUM_THREADS=6")
