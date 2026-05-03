# q_mini_wasm_v2 — IntelSYCLConfig shim for find_package(IntelSYCL)
#
# Intel's shipped IntelSYCLConfig.cmake (oneAPI 2025.x) can call
#   SYCL_FEATURE_TEST_EXTRACT(${test_output})
# with an *unquoted* expansion. When the feature-test compile fails, test_output is
# empty and CMake invokes SYCL_FEATURE_TEST_EXTRACT with zero arguments → configure error.
# This module reproduces the pieces q_mini needs: IntelSYCL_FOUND + IntelSYCL::SYCL_CXX.

if(__QMINI_INTEL_SYCL_CONFIG_SHIM)
    return()
endif()
set(__QMINI_INTEL_SYCL_CONFIG_SHIM 1)

include("${CMAKE_ROOT}/Modules/FindPackageHandleStandardArgs.cmake")

get_filename_component(_qmini_cxx_name "${CMAKE_CXX_COMPILER}" NAME_WE)
string(TOLOWER "${_qmini_cxx_name}" _qmini_cxx_name_lc)
set(_qmini_intel_oneapi_compiler FALSE)
if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    set(_qmini_intel_oneapi_compiler TRUE)
elseif(_qmini_cxx_name_lc MATCHES "^(icx|icpx|dpcpp|icx-cl)$")
    set(_qmini_intel_oneapi_compiler TRUE)
elseif(CMAKE_CXX_COMPILER MATCHES "[Oo]ne[Aa][Pp][Ii]|[Ii]ntel|oneapi")
    set(_qmini_intel_oneapi_compiler TRUE)
endif()

if(NOT _qmini_intel_oneapi_compiler)
    set(IntelSYCL_FOUND FALSE)
    set(IntelSYCL_NOT_FOUND_MESSAGE
        "q_mini IntelSYCL shim: need Intel oneAPI DPC++ (icx/icpx/dpcpp). Got ${CMAKE_CXX_COMPILER_ID}: ${CMAKE_CXX_COMPILER}")
    return()
endif()

set(SYCL_COMPILER "${CMAKE_CXX_COMPILER}")

get_filename_component(_qmini_sycl_compiler_real "${SYCL_COMPILER}" REALPATH)
get_filename_component(_qmini_sycl_bin_dir "${_qmini_sycl_compiler_real}" DIRECTORY)
get_filename_component(SYCL_PACKAGE_DIR "${_qmini_sycl_bin_dir}/.." ABSOLUTE)

find_path(
    SYCL_INCLUDE_DIR
    NAMES sycl/sycl.hpp
    HINTS
        "${SYCL_PACKAGE_DIR}/include"
        "$ENV{ONEAPI_ROOT}/compiler/latest/include"
        "$ENV{ONEAPI_ROOT}/compiler/include"
    NO_DEFAULT_PATH)

find_path(
    SYCL_INCLUDE_SYCL_DIR
    NAMES sycl/sycl.hpp
    HINTS
        "${SYCL_PACKAGE_DIR}/include"
        "$ENV{ONEAPI_ROOT}/compiler/latest/include"
    PATH_SUFFIXES sycl
    NO_DEFAULT_PATH)

if(WIN32)
    set(_qmini_sycl_lib_names sycl8 sycl7 sycl)
else()
    set(_qmini_sycl_lib_names sycl)
endif()

find_library(
    SYCL_LIBRARY
    NAMES ${_qmini_sycl_lib_names}
    HINTS
        "${SYCL_PACKAGE_DIR}/lib"
        "${SYCL_PACKAGE_DIR}/lib/x64"
        "${SYCL_PACKAGE_DIR}/lib/intel64"
        "$ENV{ONEAPI_ROOT}/compiler/latest/lib"
    NO_DEFAULT_PATH)

if(NOT SYCL_LIBRARY)
    set(IntelSYCL_FOUND FALSE)
    set(IntelSYCL_NOT_FOUND_MESSAGE "q_mini IntelSYCL shim: could not find SYCL runtime library under ${SYCL_PACKAGE_DIR}")
    return()
endif()

get_filename_component(SYCL_LIBRARY_DIR "${SYCL_LIBRARY}" DIRECTORY)

execute_process(
    COMMAND "${SYCL_COMPILER}" --version
    OUTPUT_VARIABLE _qmini_compiler_version_text
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

set(SYCL_COMPILER_VERSION 0)
if(_qmini_compiler_version_text MATCHES "([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    math(EXPR SYCL_COMPILER_VERSION
        "${CMAKE_MATCH_1} * 10000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")
endif()
if(SYCL_COMPILER_VERSION EQUAL 0)
    set(SYCL_COMPILER_VERSION 20250101)
endif()

set(SYCL_FLAGS "-fsycl")
set(SYCL_LINK_FLAGS "-fsycl")
if(WIN32)
    list(APPEND SYCL_FLAGS "/EHsc")
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles")
set(_qmini_langver_src "${CMAKE_BINARY_DIR}/CMakeFiles/qmini_sycl_langver_probe.cpp")
set(_qmini_langver_exe "${CMAKE_BINARY_DIR}/CMakeFiles/qmini_sycl_langver_probe${CMAKE_EXECUTABLE_SUFFIX}")
file(
    WRITE "${_qmini_langver_src}"
    "#include <iostream>\n"
    "int main() {\n"
    "#if defined(SYCL_LANGUAGE_VERSION)\n"
    "  std::cout << \"SYCL_LANGUAGE_VERSION=\" << SYCL_LANGUAGE_VERSION << std::endl;\n"
    "#endif\n"
    "  return 0;\n"
    "}\n")

if(WIN32)
    execute_process(
        COMMAND "${SYCL_COMPILER}" /EHsc -fsycl "${_qmini_langver_src}" -o "${_qmini_langver_exe}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles"
        RESULT_VARIABLE _qmini_langver_compile_rc
        OUTPUT_VARIABLE _qmini_langver_compile_out
        ERROR_VARIABLE _qmini_langver_compile_err)
else()
    execute_process(
        COMMAND "${SYCL_COMPILER}" -fsycl "${_qmini_langver_src}" -o "${_qmini_langver_exe}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles"
        RESULT_VARIABLE _qmini_langver_compile_rc
        OUTPUT_VARIABLE _qmini_langver_compile_out
        ERROR_VARIABLE _qmini_langver_compile_err)
endif()

if(NOT _qmini_langver_compile_rc EQUAL 0)
    set(IntelSYCL_FOUND FALSE)
    set(IntelSYCL_NOT_FOUND_MESSAGE
        "q_mini IntelSYCL shim: SYCL probe compile failed (icx -fsycl). stdout:\n${_qmini_langver_compile_out}\nstderr:\n${_qmini_langver_compile_err}")
    return()
endif()

set(SYCL_LANGUAGE_VERSION "")
execute_process(
    COMMAND "${_qmini_langver_exe}"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/CMakeFiles"
    RESULT_VARIABLE _qmini_langver_run_rc
    OUTPUT_VARIABLE _qmini_langver_run_out
    ERROR_VARIABLE _qmini_langver_run_err
    OUTPUT_STRIP_TRAILING_WHITESPACE)

set(_qmini_langver_combined "${_qmini_langver_run_out}${_qmini_langver_run_err}")
if(_qmini_langver_run_rc EQUAL 0 AND _qmini_langver_combined MATCHES "SYCL_LANGUAGE_VERSION=([0-9]+)")
    set(SYCL_LANGUAGE_VERSION "${CMAKE_MATCH_1}")
endif()
if("${SYCL_LANGUAGE_VERSION}" STREQUAL "")
    set(SYCL_LANGUAGE_VERSION "202012")
endif()

list(JOIN SYCL_FLAGS " " SYCL_FLAGS_STRING)
set(SYCL_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SYCL_FLAGS_STRING}")
set(SYCL_IMPLEMENTATION_ID "${CMAKE_CXX_COMPILER_ID}")

add_library(IntelSYCL::SYCL_CXX INTERFACE IMPORTED)
set_property(TARGET IntelSYCL::SYCL_CXX PROPERTY INTERFACE_COMPILE_OPTIONS ${SYCL_FLAGS})
set_property(TARGET IntelSYCL::SYCL_CXX PROPERTY INTERFACE_LINK_OPTIONS ${SYCL_LINK_FLAGS})
set_property(TARGET IntelSYCL::SYCL_CXX PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SYCL_INCLUDE_DIR}")
set_property(TARGET IntelSYCL::SYCL_CXX PROPERTY INTERFACE_LINK_DIRECTORIES "${SYCL_LIBRARY_DIR}")
set_property(TARGET IntelSYCL::SYCL_CXX PROPERTY INTERFACE_LINK_LIBRARIES "${SYCL_LIBRARY}")

find_package_handle_standard_args(
    IntelSYCL
    FOUND_VAR IntelSYCL_FOUND
    REQUIRED_VARS SYCL_INCLUDE_DIR SYCL_LIBRARY_DIR SYCL_FLAGS SYCL_COMPILER_VERSION SYCL_COMPILER SYCL_LIBRARY
    VERSION_VAR SYCL_LANGUAGE_VERSION
    REASON_FAILURE_MESSAGE "q_mini IntelSYCL shim could not satisfy IntelSYCL package contract")
