function(diskann_resolve_hnsw_merger_project_identity source_dir)
  set(_repository "${DISKANN_PIPELINE_SOURCE_REPOSITORY}")
  set(_canonical_resolved 0)

  execute_process(
    COMMAND git -c safe.directory=${source_dir} remote
    WORKING_DIRECTORY "${source_dir}"
    RESULT_VARIABLE _remote_result
    OUTPUT_VARIABLE _remote_output
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(_remote_result EQUAL 0 AND _remote_output)
    string(REPLACE "\n" ";" _remotes "${_remote_output}")
    set(_repositories)
    foreach(_remote IN LISTS _remotes)
      if(NOT _remote)
        continue()
      endif()
      execute_process(
        COMMAND git -c safe.directory=${source_dir} remote get-url ${_remote}
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _url_result
        OUTPUT_VARIABLE _url
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      if(_url_result EQUAL 0 AND _url)
        list(APPEND _repositories "${_url}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _repositories)
    list(LENGTH _repositories _repository_count)
    if(_repository_count EQUAL 1)
      list(GET _repositories 0 _candidate)
      if(_candidate MATCHES "^(https://|ssh://|git@[^/: ]+:).+")
        set(_repository "${_candidate}")
        set(_canonical_resolved 1)
      endif()
    endif()
  endif()

  set(HNSW_MERGER_PROJECT_REPOSITORY "${_repository}" PARENT_SCOPE)
  set(HNSW_MERGER_PROJECT_CANONICAL_REPOSITORY_RESOLVED
      ${_canonical_resolved} PARENT_SCOPE)
endfunction()
