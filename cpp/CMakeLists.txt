# Q-Mini-WASM native C++ library (C++23): packing, SIMD dispatch, agent, KV, QUBO, optional gRPC/SYCL/WasmEdge.
cmake_minimum_required(VERSION 3.20)
project(qminiwasm_cpp VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QMINIWASM_WITH_AVX512 "Compile AVX-512 translation units" ON)
option(QMINIWASM_WITH_GRPC "Build gRPC delta sync service (needs Protobuf + gRPC + plugins)" OFF)
option(QMINIWASM_WITH_TRAINING_ENGINE "Build C++20 training engine foundation (needs Protobuf + gRPC + threads)" OFF)
option(QMINIWASM_WITH_SYCL "Build SYCL host stub shared library (use Intel oneAPI for real kernels)" OFF)
option(QMINIWASM_WITH_WASMEDGE "Build WasmEdge C++ host helpers (needs WasmEdge SDK)" OFF)
option(QMINIWASM_WITH_NLOPT "Link NLopt for COBYLA QUBO solver" ON)
option(QMINIWASM_BUILD_PYBIND "Build pybind11 qminiwasm_cpp_native module" ON)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

include(FetchContent)

# --- Core library (always) ---
add_library(qminiwasm_core STATIC
    simd/cpuid.cpp
    pack/w158_pack.cpp
    kernels/ternary_forward_scalar.cpp
    kernels/ternary_forward_avx2.cpp
    kernels/ternary_forward_dispatch.cpp
    agent/ota_state_machine.cpp
    grammar/json_subset_pda.cpp
    grammar/grammar_mask_scalar.cpp
    grammar/grammar_dispatch.cpp
    wasm/wles_hooks.cpp
    qubo/qaoa_small_n.cpp
)
set_target_properties(qminiwasm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(qminiwasm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if(QMINIWASM_WITH_AVX512)
    target_compile_definitions(qminiwasm_core PRIVATE QMINIWASM_HAS_AVX512_TU=1)
else()
    target_compile_definitions(qminiwasm_core PRIVATE QMINIWASM_HAS_AVX512_TU=0)
endif()
if(MSVC)
    target_compile_options(qminiwasm_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(qminiwasm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    set_source_files_properties(kernels/ternary_forward_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
else()
    set_source_files_properties(kernels/ternary_forward_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()

# AVX-512 object library (separate flags)
if(QMINIWASM_WITH_AVX512)
    add_library(qminiwasm_avx512 OBJECT
        kernels/ternary_forward_avx512.cpp
        grammar/grammar_mask_avx512.cpp
    )
    target_include_directories(qminiwasm_avx512 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    if(MSVC)
        target_compile_options(qminiwasm_avx512 PRIVATE /arch:AVX512 /W4 /permissive-)
    else()
        target_compile_options(qminiwasm_avx512 PRIVATE -mavx512f -mavx512vl -mavx512bw -mavx512vnni -Wall -Wextra)
    endif()
    target_compile_definitions(qminiwasm_avx512 PRIVATE QMINIWASM_HAS_AVX512_TU=1)
    target_sources(qminiwasm_core PRIVATE $<TARGET_OBJECTS:qminiwasm_avx512>)
endif()

# NLopt for COBYLA
if(QMINIWASM_WITH_NLOPT)
    set(NLOPT_FORTRAN OFF CACHE BOOL "" FORCE)
    set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
    set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
    set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
    set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
    set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt.git
        GIT_TAG v2.7.1
    )
    FetchContent_MakeAvailable(nlopt)
    target_link_libraries(qminiwasm_core PRIVATE nlopt)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NLOPT=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NLOPT=0)
endif()

# Optional SYCL (stub shared object; swap for DPC++ flags when using oneAPI)
if(QMINIWASM_WITH_SYCL)
    add_library(qminiwasm_sycl SHARED sycl/ternary_joint_matrix.cpp)
    target_include_directories(qminiwasm_sycl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Optional WasmEdge
if(QMINIWASM_WITH_WASMEDGE)
    find_path(WASMEDGE_INCLUDE_DIR NAMES wasmedge/wasmedge.h
        PATHS
        "$ENV{WASMEDGE_ROOT}/include"
        "${WASMEDGE_ROOT}/include"
    )
    find_library(WASMEDGE_LIBRARY NAMES wasmedge
        PATHS
        "$ENV{WASMEDGE_ROOT}/lib"
        "${WASMEDGE_ROOT}/lib"
    )
    if(NOT WASMEDGE_INCLUDE_DIR OR NOT WASMEDGE_LIBRARY)
        message(FATAL_ERROR "QMINIWASM_WITH_WASMEDGE=ON but WasmEdge not found. Set WASMEDGE_ROOT or install the SDK.")
    endif()
    add_library(qminiwasm_wasmedge STATIC
        wasmedge/orchestrator.cpp
        wasmedge/host_memory_read.cpp
    )
    target_include_directories(qminiwasm_wasmedge PUBLIC ${WASMEDGE_INCLUDE_DIR})
    target_link_libraries(qminiwasm_wasmedge PUBLIC ${WASMEDGE_LIBRARY})
endif()

# Optional gRPC / proto code generation
if(QMINIWASM_WITH_GRPC)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    if(NOT TARGET protobuf::protoc OR NOT TARGET gRPC::grpc_cpp_plugin)
        message(FATAL_ERROR "QMINIWASM_WITH_GRPC requires CMake CONFIG targets protobuf::protoc and gRPC::grpc_cpp_plugin (e.g. vcpkg toolchain).")
    endif()
    set(PROTO_FILE "${REPO_ROOT}/proto/delta_sync.proto")
    set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto_gen")
    file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
    set(DELTA_PB_CC "${PROTO_GEN_DIR}/delta_sync.pb.cc")
    set(DELTA_PB_H "${PROTO_GEN_DIR}/delta_sync.pb.h")
    set(DELTA_GRPC_CC "${PROTO_GEN_DIR}/delta_sync.grpc.pb.cc")
    set(DELTA_GRPC_H "${PROTO_GEN_DIR}/delta_sync.grpc.pb.h")
    add_custom_command(
        OUTPUT ${DELTA_PB_CC} ${DELTA_PB_H} ${DELTA_GRPC_CC} ${DELTA_GRPC_H}
        COMMAND protobuf::protoc
        ARGS
            --cpp_out=${PROTO_GEN_DIR}
            --grpc_out=${PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            -I ${REPO_ROOT}/proto
            ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        VERBATIM
    )
    add_library(qminiwasm_grpc STATIC
        grpc/delta_sync_service.cpp
        ${DELTA_PB_CC}
        ${DELTA_GRPC_CC}
    )
    target_include_directories(qminiwasm_grpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_GEN_DIR})
    target_link_libraries(qminiwasm_grpc PUBLIC gRPC::grpc++ protobuf::libprotobuf)
endif()

# Optional C++20 training engine foundation with gRPC telemetry service
if(QMINIWASM_WITH_TRAINING_ENGINE)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    find_package(Threads REQUIRED)
    if(NOT TARGET protobuf::protoc OR NOT TARGET gRPC::grpc_cpp_plugin)
        message(FATAL_ERROR "QMINIWASM_WITH_TRAINING_ENGINE requires protobuf::protoc and gRPC::grpc_cpp_plugin targets.")
    endif()
    set(TRAINING_PROTO_FILE "${REPO_ROOT}/proto/training_engine.proto")
    set(TRAINING_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/training_proto_gen")
    file(MAKE_DIRECTORY ${TRAINING_PROTO_GEN_DIR})
    set(TRAINING_PB_CC "${TRAINING_PROTO_GEN_DIR}/training_engine.pb.cc")
    set(TRAINING_PB_H "${TRAINING_PROTO_GEN_DIR}/training_engine.pb.h")
    set(TRAINING_GRPC_CC "${TRAINING_PROTO_GEN_DIR}/training_engine.grpc.pb.cc")
    set(TRAINING_GRPC_H "${TRAINING_PROTO_GEN_DIR}/training_engine.grpc.pb.h")
    add_custom_command(
        OUTPUT ${TRAINING_PB_CC} ${TRAINING_PB_H} ${TRAINING_GRPC_CC} ${TRAINING_GRPC_H}
        COMMAND protobuf::protoc
        ARGS
            --cpp_out=${TRAINING_PROTO_GEN_DIR}
            --grpc_out=${TRAINING_PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            -I ${REPO_ROOT}/proto
            ${TRAINING_PROTO_FILE}
        DEPENDS ${TRAINING_PROTO_FILE}
        VERBATIM
    )

    add_library(qminiwasm_training_engine STATIC
        training/src/training_engine.cpp
        training/src/training_tracker.cpp
        training/src/balanced_sampler.cpp
        training/src/taxonomy_policy.cpp
        training/src/telemetry_bus.cpp
        training/src/engine_c_api.cpp
        training/src/adapters/sycl_adapter.cpp
        training/src/adapters/avx512_adapter.cpp
        training/src/adapters/wasmedge_adapter.cpp
        training/src/grpc/training_engine_service.cpp
        ${TRAINING_PB_CC}
        ${TRAINING_GRPC_CC}
    )
    target_include_directories(qminiwasm_training_engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/training/include
        ${TRAINING_PROTO_GEN_DIR}
    )
    target_link_libraries(qminiwasm_training_engine PUBLIC gRPC::grpc++ protobuf::libprotobuf Threads::Threads)
    if(MSVC)
        target_compile_options(qminiwasm_training_engine PRIVATE /W4 /permissive-)
    else()
        target_compile_options(qminiwasm_training_engine PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(qminiwasm_training_engine_server
        training/src/grpc/training_engine_server_main.cpp
    )
    target_link_libraries(qminiwasm_training_engine_server PRIVATE qminiwasm_training_engine)
endif()

# Tests executable
add_executable(qminiwasm_cpp_tests
    tests/test_main.cpp
    tests/test_w158_pack.cpp
    tests/test_ternary_matvec.cpp
    tests/test_grammar_mask.cpp
    tests/test_wles_hooks.cpp
    tests/test_qaoa.cpp
)
target_link_libraries(qminiwasm_cpp_tests PRIVATE qminiwasm_core)
if(MSVC)
    target_compile_options(qminiwasm_cpp_tests PRIVATE /W4 /permissive-)
endif()

# Pybind11 module
if(QMINIWASM_BUILD_PYBIND)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
    pybind11_add_module(qminiwasm_cpp_native MODULE
        bindings/pybind_module.cpp
    )
    target_link_libraries(qminiwasm_cpp_native PRIVATE qminiwasm_core pybind11::module)
    if(MSVC)
        target_compile_options(qminiwasm_cpp_native PRIVATE /bigobj)
    endif()
    install(TARGETS qminiwasm_cpp_native LIBRARY DESTINATION .)
endif()

install(TARGETS qminiwasm_core ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
