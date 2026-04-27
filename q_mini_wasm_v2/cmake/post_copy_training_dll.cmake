# Dual publish: primary next to qminiwasm.exe, alternate under native_runtime/ (see config/path_map.toml).
# Alternate copy usually succeeds when the repo-root DLL is locked by a running process.
if(NOT DEFINED SRC OR NOT DEFINED DST_PRIMARY OR NOT DEFINED DST_ALT)
  message(WARNING "post_copy_training_dll.cmake: SRC or DST_PRIMARY or DST_ALT not set")
  return()
endif()
get_filename_component(_alt_dir "${DST_ALT}" DIRECTORY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${_alt_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST_PRIMARY}"
  RESULT_VARIABLE _copy_primary
)
if(NOT _copy_primary EQUAL 0)
  message(WARNING "Could not copy q_training.dll to primary location (exit ${_copy_primary}). If qminiwasm.exe is running, close it or copy from the alternate path after build.")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST_ALT}"
  RESULT_VARIABLE _copy_alt
)
if(NOT _copy_alt EQUAL 0)
  message(WARNING "Could not copy q_training.dll to alternate native_runtime (exit ${_copy_alt}).")
endif()
