cmake_minimum_required(VERSION 3.20)
project(qminiwasm_training_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QMINIWASM_TRAINING_WITH_LIBTORCH "Link LibTorch for real TPEM (ternary STE + Adam + interchange v2)" ON)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Threads REQUIRED)

set(TRAINING_PROTO "${CMAKE_CURRENT_LIST_DIR}/../../proto/training_engine.proto")
get_filename_component(TRAINING_PROTO_ABS "${TRAINING_PROTO}" ABSOLUTE)

set(TRAINING_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TRAINING_PROTO_GEN_DIR}")

protobuf_generate(
  LANGUAGE cpp
  PROTOS "${TRAINING_PROTO_ABS}"
  PROTOC_OUT_DIR "${TRAINING_PROTO_GEN_DIR}"
)

protobuf_generate(
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PROTOS "${TRAINING_PROTO_ABS}"
  PROTOC_OUT_DIR "${TRAINING_PROTO_GEN_DIR}"
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
)

if(QMINIWASM_TRAINING_WITH_LIBTORCH)
  if(DEFINED ENV{LIBTORCH_ROOT} AND NOT "$ENV{LIBTORCH_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{LIBTORCH_ROOT}" _qmw_libtorch_root)
    list(PREPEND CMAKE_PREFIX_PATH "${_qmw_libtorch_root}")
    message(STATUS "LIBTORCH_ROOT -> CMAKE_PREFIX_PATH: ${_qmw_libtorch_root}")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
  find_package(Torch REQUIRED)
endif()

# Windows: LibTorch DLLs (torch_cpu.dll, c10.dll, …) live under <libtorch>/lib. The linker uses .lib files
# but the loader does not search there unless PATH includes that folder or DLLs sit next to the .exe.
if(WIN32 AND QMINIWASM_TRAINING_WITH_LIBTORCH AND DEFINED Torch_DIR)
  get_filename_component(_QMW_LIBTORCH_ROOT "${Torch_DIR}/../.." ABSOLUTE)
  file(GLOB _QMW_TORCH_DLLS "${_QMW_LIBTORCH_ROOT}/lib/*.dll")
  if(_QMW_TORCH_DLLS)
    message(STATUS "LibTorch runtime DLLs (POST_BUILD copy): ${_QMW_LIBTORCH_ROOT}/lib")
  endif()
endif()

function(qmw_windows_copy_libtorch_dlls target_name)
  if(NOT WIN32 OR NOT QMINIWASM_TRAINING_WITH_LIBTORCH)
    return()
  endif()
  if(NOT DEFINED Torch_DIR)
    return()
  endif()
  get_filename_component(_lt_root "${Torch_DIR}/../.." ABSOLUTE)
  file(GLOB _dlls "${_lt_root}/lib/*.dll")
  set(_qmw_dll_append 0)
  foreach(_dll ${_dlls})
    if(_qmw_dll_append)
      add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        APPEND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM
      )
    else()
      add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM
      )
      set(_qmw_dll_append 1)
    endif()
  endforeach()
endfunction()

add_library(qminiwasm_training_proto STATIC
  "${TRAINING_PROTO_GEN_DIR}/training_engine.pb.cc"
  "${TRAINING_PROTO_GEN_DIR}/training_engine.grpc.pb.cc"
)
target_include_directories(qminiwasm_training_proto PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${TRAINING_PROTO_GEN_DIR}"
)
target_link_libraries(qminiwasm_training_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

set(QMW_TRAINING_RUNTIME_SOURCES
  src/training_engine.cpp
  src/training_phases.cpp
  src/native_checkpoint.cpp
  src/training_tracker.cpp
  src/balanced_sampler.cpp
  src/taxonomy_policy.cpp
  src/telemetry_bus.cpp
  src/engine_c_api.cpp
  src/adapters/sycl_adapter.cpp
  src/adapters/avx512_adapter.cpp
  src/adapters/wasmedge_adapter.cpp
)
if(QMINIWASM_TRAINING_WITH_LIBTORCH)
  list(APPEND QMW_TRAINING_RUNTIME_SOURCES
    src/safetensors_f32.cpp
    src/bloch_attention.cpp
    src/libtorch_ternary_trainer.cpp
    src/hf_datasets_rows.cpp
  )
endif()

add_library(qminiwasm_training_runtime STATIC ${QMW_TRAINING_RUNTIME_SOURCES})
target_include_directories(qminiwasm_training_runtime PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_link_libraries(qminiwasm_training_runtime PUBLIC Threads::Threads)

if(QMINIWASM_TRAINING_WITH_LIBTORCH)
  target_compile_definitions(qminiwasm_training_runtime PRIVATE QMINIWASM_HAS_LIBTORCH_TRAINING=1)
  target_link_libraries(qminiwasm_training_runtime PRIVATE ${TORCH_LIBRARIES} nlohmann_json::nlohmann_json)
  if(DEFINED TORCH_INCLUDE_DIRS)
    target_include_directories(qminiwasm_training_runtime PRIVATE "${TORCH_INCLUDE_DIRS}")
  endif()
  if(TORCH_CXX_FLAGS)
    separate_arguments(_qmw_torch_cxx_flags NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
    target_compile_options(qminiwasm_training_runtime PRIVATE ${_qmw_torch_cxx_flags})
  endif()
endif()

add_library(qminiwasm_training_grpcsvc STATIC src/grpc/training_engine_service.cpp)
target_include_directories(qminiwasm_training_grpcsvc PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${TRAINING_PROTO_GEN_DIR}"
)
target_link_libraries(qminiwasm_training_grpcsvc PUBLIC qminiwasm_training_proto PRIVATE qminiwasm_training_runtime)

if(MSVC)
  target_compile_options(qminiwasm_training_runtime PRIVATE /W4 /permissive-)
  target_compile_options(qminiwasm_training_grpcsvc PRIVATE /W4 /permissive-)
  if(QMINIWASM_TRAINING_WITH_LIBTORCH)
    target_compile_options(qminiwasm_training_runtime PRIVATE /wd4702)
  endif()
else()
  target_compile_options(qminiwasm_training_runtime PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(qminiwasm_training_grpcsvc PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(qminiwasm_training_engine INTERFACE)
target_link_libraries(qminiwasm_training_engine INTERFACE
  qminiwasm_training_grpcsvc
  qminiwasm_training_runtime
)

add_executable(qminiwasm_training_engine_server
  src/grpc/training_engine_server_main.cpp
)

target_link_libraries(qminiwasm_training_engine_server
  PRIVATE
    qminiwasm_training_engine
)
qmw_windows_copy_libtorch_dlls(qminiwasm_training_engine_server)

if(QMINIWASM_TRAINING_WITH_LIBTORCH)
  add_executable(qminiwasm_tpem_roundtrip
    src/tpem_roundtrip_cli.cpp
  )
  target_link_libraries(qminiwasm_tpem_roundtrip PRIVATE qminiwasm_training_runtime)
  qmw_windows_copy_libtorch_dlls(qminiwasm_tpem_roundtrip)

  add_executable(bloch_golden_test test/bloch_golden_test.cpp)
  target_link_libraries(bloch_golden_test PRIVATE qminiwasm_training_runtime nlohmann_json::nlohmann_json)
  if(MSVC)
    target_compile_options(bloch_golden_test PRIVATE /W4 /permissive-)
  else()
    target_compile_options(bloch_golden_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  qmw_windows_copy_libtorch_dlls(bloch_golden_test)

  add_executable(joint_cascade_trainer_test test/joint_cascade_trainer_test.cpp)
  target_link_libraries(joint_cascade_trainer_test PRIVATE qminiwasm_training_runtime)
  if(MSVC)
    target_compile_options(joint_cascade_trainer_test PRIVATE /W4 /permissive-)
  else()
    target_compile_options(joint_cascade_trainer_test PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  qmw_windows_copy_libtorch_dlls(joint_cascade_trainer_test)
endif()

add_executable(training_phases_test
  test/training_phases_test.cpp
)
target_link_libraries(training_phases_test PRIVATE qminiwasm_training_runtime)

