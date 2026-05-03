cmake_minimum_required(VERSION 3.16)
project(q_mini_incus VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same policy as ../q_mini_wasm_v2: SYCL is mandatory. `USE_SYCL` is not a CMake cache option (compile-only define).
if(DEFINED CACHE{USE_SYCL})
    unset(USE_SYCL CACHE)
endif()

# Intel SYCL (required for tableau_kernels device code — same toolchain as parent tree)
set(_QMINI_INCUS_INTEL_SYCL_SHIM "${CMAKE_CURRENT_SOURCE_DIR}/../q_mini_wasm_v2/cmake/IntelSYCLConfig.cmake")
if(EXISTS "${_QMINI_INCUS_INTEL_SYCL_SHIM}")
    set(IntelSYCL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../q_mini_wasm_v2/cmake" CACHE PATH "q_mini_incus IntelSYCL shim" FORCE)
endif()
find_package(IntelSYCL REQUIRED)

# ============================================================================
# Build Options
# ============================================================================

option(BUILD_INCUS_INFERENCE "Build inference container target" ON)
option(BUILD_INCUS_TRAINING "Build training container target" ON)
option(BUILD_INCUS_WUI "Build WUI container target" ON)
option(BUILD_INCUS_AGENTS "Build agent container target" ON)
option(BUILD_SINGLE_CONTAINER "Build unified single-container image" OFF)

# ============================================================================
# Incus Native Configuration
# ============================================================================

# Ternary-native userspace - no WASM bridge
add_compile_definitions(
    Q_MINI_INCUS_NATIVE=1
    Q_MINI_NO_WASM_BRIDGE=1
    Q_MINI_NAMESPACE_BOUNDARY=1
)

# ============================================================================
# Core Sources (shared with WASM version)
# ============================================================================

set(CORE_SOURCES
    # From parent q_mini_wasm_v2/core
    ../q_mini_wasm_v2/core/ternary/trit.hpp
    ../q_mini_wasm_v2/core/qutrit_stabilizer.h
    ../q_mini_wasm_v2/core/qutrit_stabilizer.cpp
    ../q_mini_wasm_v2/core/stabilizer/tableau.hpp
    ../q_mini_wasm_v2/core/stabilizer/tableau.cpp
    ../q_mini_wasm_v2/core/stabilizer/clifford_synthesis.hpp
    ../q_mini_wasm_v2/core/stabilizer/clifford_synthesis.cpp
    ../q_mini_wasm_v2/core/steane/qutrit_steane.hpp
    ../q_mini_wasm_v2/core/steane/qutrit_steane.cpp
    ../q_mini_wasm_v2/core/ingestion/absmean_quantizer.hpp
    ../q_mini_wasm_v2/core/ingestion/absmean_quantizer.cpp
    ../q_mini_wasm_v2/core/ingestion/clifford_shadow.hpp
    ../q_mini_wasm_v2/core/ingestion/clifford_shadow.cpp
    ../q_mini_wasm_v2/core/learning/entropy_goodness.hpp
    ../q_mini_wasm_v2/core/learning/entropy_goodness.cpp
    ../q_mini_wasm_v2/core/moe/router.hpp
    ../q_mini_wasm_v2/core/moe/router.cpp
    ../q_mini_wasm_v2/core/moe/self_organizing_expert.hpp
    ../q_mini_wasm_v2/core/moe/self_organizing_expert.cpp
    ../q_mini_wasm_v2/core/learning/forward_forward.hpp
    ../q_mini_wasm_v2/core/learning/forward_forward.cpp
    ../q_mini_wasm_v2/core/network.hpp
    ../q_mini_wasm_v2/core/network.cpp
    ../q_mini_wasm_v2/core/training/data_synthesizer.hpp
    ../q_mini_wasm_v2/core/training/data_synthesizer.cpp
    ../q_mini_wasm_v2/core/qgnn/graph_native.cpp
    ../q_mini_wasm_v2/core/qgnn/betti_extractor.hpp
    ../q_mini_wasm_v2/core/qgnn/betti_extractor.cpp
)

# ============================================================================
# Incus Runtime Sources
# ============================================================================

set(INCUS_RUNTIME_SOURCES
    runtime/qminid/init_daemon.cpp
    runtime/qminid/init_daemon.hpp
    runtime/qminid/service_manager.cpp
    runtime/qminid/service_manager.hpp
    runtime/namespace_bridge/network_bridge.cpp
    runtime/namespace_bridge/network_bridge.hpp
    runtime/namespace_bridge/filesystem_bridge.cpp
    runtime/namespace_bridge/filesystem_bridge.hpp
    runtime/namespace_bridge/ternary_converter.cpp
    runtime/namespace_bridge/ternary_converter.hpp
    runtime/ipc/ternary_ipc.cpp
    runtime/ipc/ternary_ipc.hpp
    runtime/ipc/ternary_socket.cpp
    runtime/ipc/ternary_socket.hpp
)

# ============================================================================
# Core Library
# ============================================================================

add_library(q_mini_incus_core
    ${CORE_SOURCES}
    ${INCUS_RUNTIME_SOURCES}
)

target_include_directories(q_mini_incus_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../q_mini_wasm_v2
)

# SYCL device code (always — no CPU-only incus core)
target_sources(q_mini_incus_core PRIVATE
    ../q_mini_wasm_v2/sycl/tableau_kernels.hpp
    ../q_mini_wasm_v2/sycl/tableau_kernels.cpp
)
target_compile_definitions(q_mini_incus_core PRIVATE USE_SYCL=1)
if(TARGET IntelSYCL::SYCL_CXX)
    target_link_libraries(q_mini_incus_core PUBLIC IntelSYCL::SYCL_CXX)
endif()
if(WIN32)
    target_compile_options(q_mini_incus_core PRIVATE
        $<$<CXX_COMPILER_ID:IntelLLVM>:-fsycl-targets=spir64_x86_64>
    )
endif()

# ============================================================================
# Container Executables
# ============================================================================

if(BUILD_INCUS_INFERENCE)
    add_executable(q_mini_incus_infer
        containers/inference/main.cpp
        containers/inference/inference_service.cpp
        containers/inference/inference_service.hpp
    )
    target_link_libraries(q_mini_incus_infer PRIVATE q_mini_incus_core)
    set_target_properties(q_mini_incus_infer PROPERTIES
        OUTPUT_NAME "qmini-infer"
    )
endif()

if(BUILD_INCUS_TRAINING)
    add_executable(q_mini_incus_trainer
        containers/training/main.cpp
        containers/training/training_service.cpp
        containers/training/training_service.hpp
    )
    target_link_libraries(q_mini_incus_trainer PRIVATE q_mini_incus_core)
    set_target_properties(q_mini_incus_trainer PROPERTIES
        OUTPUT_NAME "qmini-trainer"
    )
endif()

if(BUILD_INCUS_WUI)
    add_executable(q_mini_incus_wui
        containers/wui/main.cpp
        containers/wui/wui_service.cpp
        containers/wui/wui_service.hpp
    )
    target_link_libraries(q_mini_incus_wui PRIVATE q_mini_incus_core)
    set_target_properties(q_mini_incus_wui PROPERTIES
        OUTPUT_NAME "qmini-wui"
    )
endif()

if(BUILD_INCUS_AGENTS)
    add_executable(q_mini_incus_agents
        containers/agent/main.cpp
        containers/agent/agent_service.cpp
        containers/agent/agent_service.hpp
    )
    target_link_libraries(q_mini_incus_agents PRIVATE q_mini_incus_core)
    set_target_properties(q_mini_incus_agents PROPERTIES
        OUTPUT_NAME "qmini-agents"
    )
endif()

# ============================================================================
# Unified Single-Container Build
# ============================================================================

if(BUILD_SINGLE_CONTAINER)
    add_executable(q_mini_incus_unified
        containers/unified/main.cpp
        containers/unified/unified_service.cpp
        containers/unified/unified_service.hpp
    )
    target_link_libraries(q_mini_incus_unified PRIVATE q_mini_incus_core)
    target_compile_definitions(q_mini_incus_unified PRIVATE BUILD_SINGLE_CONTAINER=1)
    set_target_properties(q_mini_incus_unified PROPERTIES
        OUTPUT_NAME "qmini-unified"
    )
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS q_mini_incus_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

if(BUILD_INCUS_INFERENCE)
    install(TARGETS q_mini_incus_infer RUNTIME DESTINATION bin)
endif()
if(BUILD_INCUS_TRAINING)
    install(TARGETS q_mini_incus_trainer RUNTIME DESTINATION bin)
endif()
if(BUILD_INCUS_WUI)
    install(TARGETS q_mini_incus_wui RUNTIME DESTINATION bin)
endif()
if(BUILD_INCUS_AGENTS)
    install(TARGETS q_mini_incus_agents RUNTIME DESTINATION bin)
endif()
if(BUILD_SINGLE_CONTAINER)
    install(TARGETS q_mini_incus_unified RUNTIME DESTINATION bin)
endif()

install(DIRECTORY runtime/ DESTINATION include/q_mini_incus/runtime
    FILES_MATCHING PATTERN "*.hpp"
)
