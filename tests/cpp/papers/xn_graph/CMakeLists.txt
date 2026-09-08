function(add_xn_descent_test_target target source)
  add_executable(${target} ${source})
  target_link_libraries(
    ${target}
    PRIVATE diskann_pipeline::xn_descent nlohmann_json::nlohmann_json)
  if(DISKANN_CPP_MAIN_BASELINE)
    target_link_libraries(${target} PRIVATE diskann_cpp_main_stages)
  endif()
  target_include_directories(
    ${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests/cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(
    ${target} PRIVATE XN_DESCENT_TEST_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")
  diskann_pipeline_apply_cxx17(${target})
endfunction()

add_xn_descent_test_target(
  xn_descent_parameters_test xn_descent_parameters_test.cpp)
add_test(NAME xn_descent_parameters_test COMMAND xn_descent_parameters_test)
set_tests_properties(
  xn_descent_parameters_test PROPERTIES LABELS "xn-descent;contract;us1")

add_xn_descent_test_target(
  xn_descent_equation_try_insert_test
  xn_descent_equation_try_insert_test.cpp)
add_test(
  NAME xn_descent_equation_test
  COMMAND xn_descent_equation_try_insert_test equation)
add_test(
  NAME xn_descent_try_insert_test
  COMMAND xn_descent_equation_try_insert_test try_insert)
set_tests_properties(
  xn_descent_equation_test xn_descent_try_insert_test
  PROPERTIES LABELS "xn-descent;algorithm;us1")

add_xn_descent_test_target(
  xn_descent_round_oracle_test xn_descent_round_oracle_test.cpp)
add_test(
  NAME xn_descent_round_oracle_test COMMAND xn_descent_round_oracle_test)
set_tests_properties(
  xn_descent_round_oracle_test PROPERTIES LABELS "xn-descent;oracle;us1")

add_xn_descent_test_target(
  xn_descent_local_artifact_test xn_descent_local_artifact_test.cpp)
add_test(
  NAME xn_descent_local_artifact_test COMMAND xn_descent_local_artifact_test)
set_tests_properties(
  xn_descent_local_artifact_test PROPERTIES LABELS "xn-descent;artifact;us1")
