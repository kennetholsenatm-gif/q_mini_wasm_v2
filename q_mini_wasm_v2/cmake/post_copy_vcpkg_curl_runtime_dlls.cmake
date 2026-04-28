# Copy libcurl and common vcpkg peers next to q_training.dll (same dirs as post_copy_training_dll.cmake).
# Windows loads dependent DLLs from the directory of the loaded module before PATH.
if(NOT DEFINED VCPKG_ROOT OR NOT DEFINED CONFIG OR NOT DEFINED DST_PRIMARY_DIR OR NOT DEFINED DST_ALT_DIR)
  message(WARNING "post_copy_vcpkg_curl_runtime_dlls.cmake: VCPKG_ROOT, CONFIG, DST_PRIMARY_DIR, or DST_ALT_DIR not set")
  return()
endif()
string(REPLACE "\\" "/" VCPKG_ROOT "${VCPKG_ROOT}")
set(_bin_release "${VCPKG_ROOT}/installed/x64-windows/bin")
set(_bin_debug "${VCPKG_ROOT}/installed/x64-windows/debug/bin")
if(CONFIG STREQUAL "Debug" AND EXISTS "${_bin_debug}/libcurl.dll")
  set(VCPKG_BIN "${_bin_debug}")
elseif(EXISTS "${_bin_release}/libcurl.dll")
  set(VCPKG_BIN "${_bin_release}")
elseif(EXISTS "${_bin_debug}/libcurl.dll")
  set(VCPKG_BIN "${_bin_debug}")
else()
  message(WARNING "post_copy_vcpkg_curl_runtime_dlls: libcurl.dll not found under vcpkg x64-windows (release or debug bin)")
  return()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${DST_ALT_DIR}")

# Names seen with curl[openssl,http2,...] on vcpkg x64-windows; unknown names are skipped.
set(_runtime_dlls
  libcurl.dll
  zlib1.dll
  libssl-3-x64.dll
  libcrypto-3-x64.dll
  nghttp2.dll
  nghttp3.dll
  libssh2.dll
  brotlidec.dll
  brotlicommon.dll
  brotlienc.dll
  zstd.dll
  liblzma.dll
)

foreach(_dll IN LISTS _runtime_dlls)
  set(_src "${VCPKG_BIN}/${_dll}")
  if(EXISTS "${_src}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_src}" "${DST_PRIMARY_DIR}/${_dll}"
      RESULT_VARIABLE _r1
    )
    if(NOT _r1 EQUAL 0)
      message(WARNING "Could not copy ${_dll} to ${DST_PRIMARY_DIR} (exit ${_r1})")
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_src}" "${DST_ALT_DIR}/${_dll}"
      RESULT_VARIABLE _r2
    )
    if(NOT _r2 EQUAL 0)
      message(WARNING "Could not copy ${_dll} to ${DST_ALT_DIR} (exit ${_r2})")
    endif()
  endif()
endforeach()
