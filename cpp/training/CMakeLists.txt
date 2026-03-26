cmake_minimum_required(VERSION 3.20)
project(qminiwasm_training_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_library(qminiwasm_training_engine STATIC
  src/training_engine.cpp
  src/training_tracker.cpp
  src/balanced_sampler.cpp
  src/taxonomy_policy.cpp
  src/telemetry_bus.cpp
  src/engine_c_api.cpp
  src/adapters/sycl_adapter.cpp
  src/adapters/avx512_adapter.cpp
  src/adapters/wasmedge_adapter.cpp
  src/grpc/training_engine_service.cpp
  "${TRAINING_PROTO_GEN_DIR}/training_engine.pb.cc"
  "${TRAINING_PROTO_GEN_DIR}/training_engine.grpc.pb.cc"
)

target_include_directories(qminiwasm_training_engine
  PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${TRAINING_PROTO_GEN_DIR}"
)

target_link_libraries(qminiwasm_training_engine
  PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)

add_executable(qminiwasm_training_engine_server
  src/grpc/training_engine_server_main.cpp
)

target_link_libraries(qminiwasm_training_engine_server
  PRIVATE
    qminiwasm_training_engine
)
