# Best-effort copy so a locked q_training.dll at repo root does not fail the build.
if(NOT DEFINED SRC OR NOT DEFINED DST)
  message(WARNING "post_copy_training_dll.cmake: SRC or DST not set")
  return()
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DST}"
  RESULT_VARIABLE _copy_rv
)
if(NOT _copy_rv EQUAL 0)
  message(WARNING "Could not copy q_training.dll to repo root (exit ${_copy_rv}). Close qminiwasm.exe and copy manually from the build tree, or rebuild.")
endif()
