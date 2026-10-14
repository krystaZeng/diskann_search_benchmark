add_subdirectory(cpp/fakes)
add_subdirectory(cpp)

if(DISKANN_MODULE_COMPOSITION)
  add_subdirectory(cpp/module_composition)
endif()

if(DISKANN_KMEANS_PARTITION)
  add_subdirectory(cpp/partition)
endif()

if(DISKANN_CPP_MAIN_BASELINE AND
   (NOT DISKANN_SEARCH_EVALUATION OR DISKANN_SEARCH_LINUX_NATIVE_AVAILABLE))
  add_subdirectory(cpp/baseline)
endif()

if(DISKANN_GP_ANN_OGP)
  add_subdirectory(cpp/papers/gp_ann)
endif()

if(DISKANN_HNSW_MERGER)
  add_subdirectory(cpp/papers/hnsw_merger)
endif()

if(DISKANN_XN_DESCENT)
  add_subdirectory(cpp/papers/xn_graph)
endif()
