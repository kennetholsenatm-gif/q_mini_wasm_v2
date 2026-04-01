# Q-Mini-WASM native C++ library (C++23): packing, SIMD dispatch, agent, KV, QUBO, optional gRPC/SYCL/WasmEdge.
cmake_minimum_required(VERSION 3.20)
project(qminiwasm_cpp VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QMINIWASM_WITH_AVX512 "Compile AVX-512 translation units" ON)
option(QMINIWASM_WITH_GRPC "Build gRPC delta sync service (needs Protobuf + gRPC + plugins)" OFF)
option(QMINIWASM_WITH_TRAINING_ENGINE "Build C++20 training engine foundation (needs Protobuf + gRPC + threads)" OFF)
option(QMINIWASM_WITH_NATIVE_RL_RUNTIME "Build native cascade RL rollout runtime helpers" ON)
option(QMINIWASM_WITH_NATIVE_WASM_HOST "Build native wasm host bridge helpers" ON)
option(QMINIWASM_WITH_NATIVE_LOTA_QAF "Build native LOTA/QAF kernels" ON)
option(QMINIWASM_WITH_NATIVE_TPEM_BUILDER "Build native TPEM bundle builder helpers" ON)
option(QMINIWASM_WITH_NATIVE_DQAOA_ROUTING "Build native dQAOA routing helpers" ON)
option(QMINIWASM_WITH_SYCL "Build SYCL host stub shared library (use Intel oneAPI for real kernels)" OFF)
option(QMINIWASM_WITH_WASMEDGE "Build WasmEdge C++ host helpers (needs WasmEdge SDK)" OFF)
option(QMINIWASM_WITH_NLOPT "Link NLopt for COBYLA QUBO solver" ON)
option(QMINIWASM_WITH_QUANTUM "OpenQASM 3 subset + trinary LibTorch simulator (CPU; needs LibTorch)" OFF)
option(QMINIWASM_BUILD_PYBIND "Build pybind11 qminiwasm_cpp_native module" ON)
option(QMINIWASM_BUILD_SHARED_QUTRIT "Build qutrit Clifford library as shared (DLL)" OFF)
option(QMINIWASM_WITH_QUTRIT_GRPC "Build qutrit Clifford gRPC service (needs Protobuf + gRPC)" OFF)
option(QMINIWASM_WITH_QUTRIT_TRAINING_GRPC "Build qutrit training gRPC service (needs Protobuf + gRPC)" OFF)

# CMakeLists.txt lives in cpp/; repository root (proto/, qminiwasm/) is one level up.
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

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
    wasm/linear_memory_encode.cpp
    wasm/wasm_host_hooks.cpp
    escalation/escalation_policy.cpp
    escalation/escalation_quantum_hook.cpp
    router/expert_fleet.cpp
    qubo/qaoa_small_n.cpp
)
if(QMINIWASM_WITH_NATIVE_RL_RUNTIME)
    target_sources(qminiwasm_core PRIVATE rl/cascade_rollout_runtime.cpp)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_RL_RUNTIME=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_RL_RUNTIME=0)
endif()
if(QMINIWASM_WITH_NATIVE_WASM_HOST)
    target_sources(qminiwasm_core PRIVATE wasmedge/engine_bridge.cpp)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_WASM_HOST=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_WASM_HOST=0)
endif()
if(QMINIWASM_WITH_NATIVE_LOTA_QAF)
    target_sources(qminiwasm_core PRIVATE layers/lota_kernels.cpp)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_LOTA_QAF=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_LOTA_QAF=0)
endif()
if(QMINIWASM_WITH_NATIVE_TPEM_BUILDER)
    target_sources(qminiwasm_core PRIVATE tpem/tpem_bundle_builder.cpp)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_TPEM_BUILDER=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_TPEM_BUILDER=0)
endif()
if(QMINIWASM_WITH_NATIVE_DQAOA_ROUTING)
    target_sources(qminiwasm_core PRIVATE qubo/dqaoa_routing_runtime.cpp)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_DQAOA_ROUTING=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NATIVE_DQAOA_ROUTING=0)
endif()
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

# NLopt for COBYLA (v2.10.1+ for cmake_minimum_required >= 3.18; required for CMake 4.x / recent VS bundles)
if(QMINIWASM_WITH_NLOPT)
    set(NLOPT_FORTRAN OFF CACHE BOOL "" FORCE)
    set(NLOPT_SWIG OFF CACHE BOOL "" FORCE)
    set(NLOPT_PYTHON OFF CACHE BOOL "" FORCE)
    set(NLOPT_OCTAVE OFF CACHE BOOL "" FORCE)
    set(NLOPT_MATLAB OFF CACHE BOOL "" FORCE)
    set(NLOPT_TESTS OFF CACHE BOOL "" FORCE)
    set(NLOPT_GUILE OFF CACHE BOOL "" FORCE)
    set(NLOPT_JAVA OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlopt
        GIT_REPOSITORY https://github.com/stevengj/nlopt.git
        GIT_TAG v2.10.1
    )
    FetchContent_MakeAvailable(nlopt)
    target_link_libraries(qminiwasm_core PRIVATE nlopt)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NLOPT=1)
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_NLOPT=0)
endif()

if(QMINIWASM_WITH_QUANTUM)
    if(DEFINED ENV{LIBTORCH_ROOT} AND NOT "$ENV{LIBTORCH_ROOT}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{LIBTORCH_ROOT}" _qmw_libtorch_root_quantum)
        list(PREPEND CMAKE_PREFIX_PATH "${_qmw_libtorch_root_quantum}")
        message(STATUS "LIBTORCH_ROOT (quantum) -> CMAKE_PREFIX_PATH: ${_qmw_libtorch_root_quantum}")
    endif()
    find_package(Torch REQUIRED)
    add_library(qminiwasm_quantum STATIC
        quantum/openqasm3/lexer.cpp
        quantum/openqasm3/parser.cpp
        quantum/trinary/device.cpp
        quantum/qml/circuit.cpp
        quantum/quantum_bridge.cpp
        quantum/quantum_bridge_c_api.cpp
        quantum/qutrit_tableau.cpp
        quantum/error_correction.cpp
        quantum/graph_states.cpp
        quantum/scrambling.cpp
        quantum/attention.cpp
    )
    target_include_directories(qminiwasm_quantum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(qminiwasm_quantum PUBLIC ${TORCH_LIBRARIES})
    target_compile_definitions(qminiwasm_quantum PUBLIC QMINIWASM_HAS_QUANTUM=1)
    if(TORCH_CXX_FLAGS)
        separate_arguments(_qmw_torch_cxx_quantum NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
        target_compile_options(qminiwasm_quantum PRIVATE ${_qmw_torch_cxx_quantum})
    endif()
    if(DEFINED TORCH_INCLUDE_DIRS)
        target_include_directories(qminiwasm_quantum PRIVATE "${TORCH_INCLUDE_DIRS}")
    endif()
    if(MSVC)
        target_compile_options(qminiwasm_quantum PRIVATE /W4 /permissive- /bigobj)
    else()
        target_compile_options(qminiwasm_quantum PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(qminiwasm_core PUBLIC qminiwasm_quantum)
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_QUANTUM=1)

    # Optional shared qutrit Clifford library (DLL)
    if(QMINIWASM_BUILD_SHARED_QUTRIT)
        add_library(qminiwasm_qutrit_clifford SHARED
            quantum/qutrit_tableau.cpp
            quantum/error_correction.cpp
            quantum/graph_states.cpp
            quantum/scrambling.cpp
            quantum/attention.cpp
        )
        target_include_directories(qminiwasm_qutrit_clifford PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(qminiwasm_qutrit_clifford PRIVATE QMINIWASM_DLL_EXPORTS)
        set_target_properties(qminiwasm_qutrit_clifford PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            VERSION ${PROJECT_VERSION}
            SOVERSION ${QUTRIT_ABI_VERSION}
        )
        if(MSVC)
            target_compile_options(qminiwasm_qutrit_clifford PRIVATE /W4 /permissive- /bigobj)
        else()
            target_compile_options(qminiwasm_qutrit_clifford PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        install(TARGETS qminiwasm_qutrit_clifford
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin
        )
        install(FILES
            quantum/qutrit_export.hpp
            quantum/qutrit_tableau.hpp
            quantum/error_correction.hpp
            quantum/graph_states.hpp
            quantum/scrambling.hpp
            quantum/attention.hpp
            DESTINATION include/qminiwasm/quantum
        )
    endif()
else()
    target_compile_definitions(qminiwasm_core PUBLIC QMINIWASM_HAS_QUANTUM=0)
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
if(QMINIWASM_WITH_GRPC OR QMINIWASM_WITH_QUTRIT_GRPC)
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

# Optional qutrit Clifford gRPC service
if(QMINIWASM_WITH_QUTRIT_GRPC)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    if(NOT TARGET protobuf::protoc OR NOT TARGET gRPC::grpc_cpp_plugin)
        message(FATAL_ERROR "QMINIWASM_WITH_QUTRIT_GRPC requires CMake CONFIG targets protobuf::protoc and gRPC::grpc_cpp_plugin.")
    endif()
    set(QUTRIT_PROTO_FILE "${REPO_ROOT}/proto/qutrit_clifford.proto")
    set(QUTRIT_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/qutrit_proto_gen")
    file(MAKE_DIRECTORY ${QUTRIT_PROTO_GEN_DIR})
    set(QUTRIT_PB_CC "${QUTRIT_PROTO_GEN_DIR}/qutrit_clifford.pb.cc")
    set(QUTRIT_PB_H "${QUTRIT_PROTO_GEN_DIR}/qutrit_clifford.pb.h")
    set(QUTRIT_GRPC_CC "${QUTRIT_PROTO_GEN_DIR}/qutrit_clifford.grpc.pb.cc")
    set(QUTRIT_GRPC_H "${QUTRIT_PROTO_GEN_DIR}/qutrit_clifford.grpc.pb.h")
    add_custom_command(
        OUTPUT ${QUTRIT_PB_CC} ${QUTRIT_PB_H} ${QUTRIT_GRPC_CC} ${QUTRIT_GRPC_H}
        COMMAND protobuf::protoc
        ARGS
            --cpp_out=${QUTRIT_PROTO_GEN_DIR}
            --grpc_out=${QUTRIT_PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            -I ${REPO_ROOT}/proto
            ${QUTRIT_PROTO_FILE}
        DEPENDS ${QUTRIT_PROTO_FILE}
        VERBATIM
    )

    if(NOT QMINIWASM_WITH_QUANTUM)
        message(FATAL_ERROR "QMINIWASM_WITH_QUTRIT_GRPC requires QMINIWASM_WITH_QUANTUM=ON")
    endif()

    add_library(qminiwasm_qutrit_grpcsvc STATIC
        quantum/qutrit_clifford_service.cpp
        ${QUTRIT_PB_CC}
        ${QUTRIT_GRPC_CC}
    )
    target_include_directories(qminiwasm_qutrit_grpcsvc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${QUTRIT_PROTO_GEN_DIR}
    )
    target_link_libraries(qminiwasm_qutrit_grpcsvc PUBLIC
        qminiwasm_quantum
        gRPC::grpc++
        protobuf::libprotobuf
    )

    if(MSVC)
        target_compile_options(qminiwasm_qutrit_grpcsvc PRIVATE /W4 /permissive- /bigobj)
    else()
        target_compile_options(qminiwasm_qutrit_grpcsvc PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(qminiwasm_qutrit_clifford_server
        quantum/qutrit_clifford_server_main.cpp
    )
    target_link_libraries(qminiwasm_qutrit_clifford_server PRIVATE qminiwasm_qutrit_grpcsvc)
endif()

# Optional qutrit training gRPC service
if(QMINIWASM_WITH_QUTRIT_TRAINING_GRPC)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
    if(NOT TARGET protobuf::protoc OR NOT TARGET gRPC::grpc_cpp_plugin)
        message(FATAL_ERROR "QMINIWASM_WITH_QUTRIT_TRAINING_GRPC requires CMake CONFIG targets protobuf::protoc and gRPC::grpc_cpp_plugin.")
    endif()
    set(QUTRIT_TRAINING_PROTO_FILE "${REPO_ROOT}/proto/qutrit_training.proto")
    set(QUTRIT_TRAINING_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/qutrit_training_proto_gen")
    file(MAKE_DIRECTORY ${QUTRIT_TRAINING_PROTO_GEN_DIR})
    set(QUTRIT_TRAINING_PB_CC "${QUTRIT_TRAINING_PROTO_GEN_DIR}/qutrit_training.pb.cc")
    set(QUTRIT_TRAINING_PB_H "${QUTRIT_TRAINING_PROTO_GEN_DIR}/qutrit_training.pb.h")
    set(QUTRIT_TRAINING_GRPC_CC "${QUTRIT_TRAINING_PROTO_GEN_DIR}/qutrit_training.grpc.pb.cc")
    set(QUTRIT_TRAINING_GRPC_H "${QUTRIT_TRAINING_PROTO_GEN_DIR}/qutrit_training.grpc.pb.h")
    add_custom_command(
        OUTPUT ${QUTRIT_TRAINING_PB_CC} ${QUTRIT_TRAINING_PB_H} ${QUTRIT_TRAINING_GRPC_CC} ${QUTRIT_TRAINING_GRPC_H}
        COMMAND protobuf::protoc
        ARGS
            --cpp_out=${QUTRIT_TRAINING_PROTO_GEN_DIR}
            --grpc_out=${QUTRIT_TRAINING_PROTO_GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            -I ${REPO_ROOT}/proto
            ${QUTRIT_TRAINING_PROTO_FILE}
        DEPENDS ${QUTRIT_TRAINING_PROTO_FILE}
        VERBATIM
    )

    if(NOT QMINIWASM_WITH_QUANTUM)
        message(FATAL_ERROR "QMINIWASM_WITH_QUTRIT_TRAINING_GRPC requires QMINIWASM_WITH_QUANTUM=ON")
    endif()

    add_library(qminiwasm_qutrit_training_grpcsvc STATIC
        quantum/qutrit_training_service.cpp
        ${QUTRIT_TRAINING_PB_CC}
        ${QUTRIT_TRAINING_GRPC_CC}
    )
    target_include_directories(qminiwasm_qutrit_training_grpcsvc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${QUTRIT_TRAINING_PROTO_GEN_DIR}
    )
    target_link_libraries(qminiwasm_qutrit_training_grpcsvc PUBLIC
        qminiwasm_quantum
        gRPC::grpc++
        protobuf::libprotobuf
    )

    if(MSVC)
        target_compile_options(qminiwasm_qutrit_training_grpcsvc PRIVATE /W4 /permissive- /bigobj)
    else()
        target_compile_options(qminiwasm_qutrit_training_grpcsvc PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(qminiwasm_qutrit_training_server
        quantum/qutrit_training_server_main.cpp
    )
    target_link_libraries(qminiwasm_qutrit_training_server PRIVATE qminiwasm_qutrit_training_grpcsvc)
endif()

# Optional C++20 training engine foundation with gRPC telemetry service
if(QMINIWASM_WITH_TRAINING_ENGINE)
    option(QMINIWASM_TRAINING_WITH_LIBTORCH "Link LibTorch for real TPEM (ternary STE + interchange v2)" ON)
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

    # Protobuf/gRPC generated sources: isolated from LibTorch includes (LibTorch ships google/protobuf
    # headers that conflict with vcpkg protobuf PROTOBUF_VERSION macros).
    add_library(qminiwasm_training_proto STATIC ${TRAINING_PB_CC} ${TRAINING_GRPC_CC})
    target_include_directories(qminiwasm_training_proto PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/training/include
        ${TRAINING_PROTO_GEN_DIR}
    )
    target_link_libraries(qminiwasm_training_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

    set(_QMW_TRAINING_RUNTIME_SRCS
        training/src/training_engine.cpp
        training/src/native_checkpoint.cpp
        training/src/training_tracker.cpp
        training/src/balanced_sampler.cpp
        training/src/taxonomy_policy.cpp
        training/src/telemetry_bus.cpp
        training/src/engine_c_api.cpp
        training/src/adapters/sycl_adapter.cpp
        training/src/adapters/avx512_adapter.cpp
        training/src/adapters/wasmedge_adapter.cpp
    )
    if(QMINIWASM_TRAINING_WITH_LIBTORCH)
        list(APPEND _QMW_TRAINING_RUNTIME_SRCS
            training/src/safetensors_f32.cpp
            training/src/bloch_attention.cpp
            training/src/libtorch_ternary_trainer.cpp
            training/src/hf_datasets_rows.cpp
        )
    endif()

    add_library(qminiwasm_training_runtime STATIC ${_QMW_TRAINING_RUNTIME_SRCS})
    target_include_directories(qminiwasm_training_runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/training/include
    )
    target_link_libraries(qminiwasm_training_runtime PUBLIC Threads::Threads)
    if(QMINIWASM_TRAINING_WITH_LIBTORCH)
        target_compile_definitions(qminiwasm_training_runtime PRIVATE QMINIWASM_HAS_LIBTORCH_TRAINING=1)
        target_link_libraries(qminiwasm_training_runtime PRIVATE ${TORCH_LIBRARIES} nlohmann_json::nlohmann_json)
        if(DEFINED TORCH_INCLUDE_DIRS)
            target_include_directories(qminiwasm_training_runtime PRIVATE "${TORCH_INCLUDE_DIRS}")
        endif()
        if(TORCH_CXX_FLAGS)
            separate_arguments(_qmw_torch_cxx_flags_root NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
            target_compile_options(qminiwasm_training_runtime PRIVATE ${_qmw_torch_cxx_flags_root})
        endif()
    endif()

    add_library(qminiwasm_training_grpcsvc STATIC training/src/grpc/training_engine_service.cpp)
    target_include_directories(qminiwasm_training_grpcsvc PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/training/include
        ${TRAINING_PROTO_GEN_DIR}
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
        training/src/grpc/training_engine_server_main.cpp
    )
    target_link_libraries(qminiwasm_training_engine_server PRIVATE qminiwasm_training_engine)

    if(QMINIWASM_TRAINING_WITH_LIBTORCH)
        add_executable(qminiwasm_tpem_roundtrip training/src/tpem_roundtrip_cli.cpp)
        target_link_libraries(qminiwasm_tpem_roundtrip PRIVATE qminiwasm_training_runtime)
        add_executable(bloch_golden_test training/test/bloch_golden_test.cpp)
        target_link_libraries(bloch_golden_test PRIVATE qminiwasm_training_runtime nlohmann_json::nlohmann_json)
        if(MSVC)
            target_compile_options(bloch_golden_test PRIVATE /W4 /permissive-)
        else()
            target_compile_options(bloch_golden_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_executable(joint_cascade_trainer_test training/test/joint_cascade_trainer_test.cpp)
        target_link_libraries(joint_cascade_trainer_test PRIVATE qminiwasm_training_runtime)
        if(MSVC)
            target_compile_options(joint_cascade_trainer_test PRIVATE /W4 /permissive-)
        else()
            target_compile_options(joint_cascade_trainer_test PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

# Tests executable
set(_QMW_CPP_TEST_SRCS
    tests/test_main.cpp
    tests/test_escalation_policy.cpp
    tests/test_expert_fleet.cpp
    tests/test_w158_pack.cpp
    tests/test_ternary_matvec.cpp
    tests/test_grammar_mask.cpp
    tests/test_linear_memory_encode.cpp
    tests/test_wles_hooks.cpp
    tests/test_wasm_hooks.cpp
    tests/test_qaoa.cpp
)
if(QMINIWASM_WITH_QUANTUM)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_openqasm_quantum.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_qutrit_tableau.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_error_correction.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_graph_states.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_scrambling.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_attention.cpp)
    list(APPEND _QMW_CPP_TEST_SRCS tests/test_qutrit_training_service.cpp)
endif()
add_executable(qminiwasm_cpp_tests ${_QMW_CPP_TEST_SRCS})
target_link_libraries(qminiwasm_cpp_tests PRIVATE qminiwasm_core)
if(MSVC)
    target_compile_options(qminiwasm_cpp_tests PRIVATE /W4 /permissive-)
endif()

# Pybind11 module
if(QMINIWASM_BUILD_PYBIND)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.13.6
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
