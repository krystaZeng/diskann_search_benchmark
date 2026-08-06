add_subdirectory(cpp/fakes)
add_subdirectory(cpp)

if(DISKANN_CPP_MAIN_BASELINE)
  add_subdirectory(cpp/baseline)
endif()
