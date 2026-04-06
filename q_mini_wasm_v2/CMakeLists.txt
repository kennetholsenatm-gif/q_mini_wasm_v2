cmake_minimum_required(VERSION 3.14)
project(q_mini_wasm_v2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Executables
# ============================================================================

add_executable(q_mini_wasm_v2_trainer core/training/trainer_main.cpp)
target_link_libraries(q_mini_wasm_v2_trainer PRIVATE q_mini_wasm_v2_core)

add_executable(q_mini_wasm_v2_infer core/inference/infer_main.cpp)
target_link_libraries(q_mini_wasm_v2_infer PRIVATE q_mini_wasm_v2_core)

# C++ Agent Implementations
add_library(q_mini_wasm_v2_agents SHARED
    agents/documentation_agent.cpp
)

target_link_libraries(q_mini_wasm_v2_agents PRIVATE
    q_mini_wasm_v2_core
)

target_include_directories(q_mini_wasm_v2_agents PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(q_mini_wasm_v2_agents PROPERTIES
    OUTPUT_NAME "q_mini_wasm_v2_agents"
    PREFIX ""
)

# ============================================================================
# Runtime Orchestrator Operations
# ============================================================================

option(USE_SYCL "Enable SYCL acceleration for multi-core/multi-thread" OFF)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_WASM "Build WebAssembly target" OFF)
option(BUILD_DLL "Build DLL with C API" ON)

# ============================================================================
# Source Files
# ============================================================================

set(CORE_SOURCES
    core/ternary/trit.hpp
    core/qutrit_stabilizer.h
    core/qutrit_stabilizer.cpp
    core/stabilizer/tableau.hpp
    core/stabilizer/tableau.cpp
    core/stabilizer/clifford_synthesis.hpp
    core/stabilizer/clifford_synthesis.cpp
    core/steane/qutrit_steane.hpp
    core/steane/qutrit_steane.cpp
    core/ingestion/absmean_quantizer.hpp
    core/ingestion/absmean_quantizer.cpp
    core/ingestion/clifford_shadow.hpp
    core/ingestion/clifford_shadow.cpp
    core/learning/entropy_goodness.hpp
    core/learning/entropy_goodness.cpp
    core/moe/router.hpp
    core/moe/router.cpp
    core/learning/forward_forward.hpp
    core/learning/forward_forward.cpp
    core/network.hpp
    core/network.cpp
    core/inference/geometric_context.hpp
    core/inference/geometric_context.cpp
    core/inference/entanglement_token.hpp
    core/inference/entanglement_token.cpp
    core/inference/householder_synthesizer.hpp
    core/inference/householder_synthesizer.cpp
    core/inference/latency_profiler.hpp
    core/inference/inference_pipeline.hpp
    core/inference/inference_pipeline.cpp
    core/flash_cim/flash_cim.cpp
    core/training/trainer_main.cpp
    core/qgnn/graph_native.cpp
    core/qgnn/graph_moe_router.cpp
    core/qgnn/graph_migration_adapter.cpp
    core/qgnn/zx_calculus.cpp
    core/qgnn/error_correction.cpp
    core/qgnn/graph_moe_router.cpp
    core/qgnn/graph_migration_adapter.hpp
    core/qgnn/graph_migration_adapter.cpp
    core/qgnn/message_passing.hpp
    core/qgnn/message_passing.cpp
    core/qgnn/ternary_tree.hpp
    core/qgnn/ternary_tree.cpp
    core/qgnn/betti_extractor.hpp
    core/qgnn/betti_extractor.cpp
)

set(RUNTIME_SOURCES
    runtime/orchestrator.hpp
    runtime/orchestrator.cpp
)

set(SYCL_SOURCES
    sycl/tableau_kernels.hpp
    sycl/tableau_kernels.cpp
)

# ============================================================================
# Library Target
# ============================================================================

add_library(q_mini_wasm_v2_core
    ${CORE_SOURCES}
    ${RUNTIME_SOURCES}
)

target_include_directories(q_mini_wasm_v2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Modular DLL Targets (4 GF(3) Acceleration Libraries)
# ============================================================================

if(BUILD_DLL)

    # ------------------------------------------------------------------------
    # 1. GF(3) Clifford Operations DLL
    # ------------------------------------------------------------------------
    add_library(q_gf3_clifford SHARED
        dll/clifford/clifford_api.hpp
        dll/clifford/clifford_api.cpp
        dll/clifford/clifford_kernels.cpp
    )
    
    target_link_libraries(q_gf3_clifford PRIVATE
        q_mini_wasm_v2_core
    )
    
    target_include_directories(q_gf3_clifford PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dll
    )
    
    target_compile_definitions(q_gf3_clifford PRIVATE
        Q_GF3_CLIFFORD_EXPORTS
    )
    
    set_target_properties(q_gf3_clifford PROPERTIES
        OUTPUT_NAME "q_gf3_clifford"
        PREFIX ""
    )

    # ------------------------------------------------------------------------
    # 2. GF(3) Serialization DLL
    # ------------------------------------------------------------------------
    add_library(q_gf3_serialize SHARED
        dll/serialize/serialize_api.hpp
        dll/serialize/serialize_api.cpp
        dll/serialize/packing_kernels.cpp
    )
    
    target_link_libraries(q_gf3_serialize PRIVATE
        q_mini_wasm_v2_core
    )
    
    target_include_directories(q_gf3_serialize PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dll
    )
    
    target_compile_definitions(q_gf3_serialize PRIVATE
        Q_GF3_SERIALIZE_EXPORTS
    )
    
    set_target_properties(q_gf3_serialize PROPERTIES
        OUTPUT_NAME "q_gf3_serialize"
        PREFIX ""
    )

    # ------------------------------------------------------------------------
    # 3. GF(3) WASM Bridge DLL
    # ------------------------------------------------------------------------
    add_library(q_gf3_wasm SHARED
        dll/wasm_bridge/wasm_api.hpp
        dll/wasm_bridge/wasm_api.cpp
        dll/wasm_bridge/sycl_runtime.cpp
    )
    
    target_link_libraries(q_gf3_wasm PRIVATE
        q_mini_wasm_v2_core
        q_gf3_clifford
        q_gf3_serialize
    )
    
    target_include_directories(q_gf3_wasm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dll
    )
    
    target_compile_definitions(q_gf3_wasm PRIVATE
        Q_GF3_WASM_EXPORTS
    )
    
    set_target_properties(q_gf3_wasm PROPERTIES
        OUTPUT_NAME "q_gf3_wasm"
        PREFIX ""
    )

    # ------------------------------------------------------------------------
    # 4. GF(3) Max-Plus Topologies DLL
    # ------------------------------------------------------------------------
    add_library(q_gf3_maxplus SHARED
        dll/maxplus/maxplus_api.hpp
        dll/maxplus/maxplus_api.cpp
        dll/maxplus/maxplus_kernels.cpp
    )
    
    target_link_libraries(q_gf3_maxplus PRIVATE
        q_mini_wasm_v2_core
    )
    
    target_include_directories(q_gf3_maxplus PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dll
    )
    
    target_compile_definitions(q_gf3_maxplus PRIVATE
        Q_GF3_MAXPLUS_EXPORTS
    )
    
    set_target_properties(q_gf3_maxplus PROPERTIES
        OUTPUT_NAME "q_gf3_maxplus"
        PREFIX ""
    )

    # ------------------------------------------------------------------------
    # Install all DLL targets
    # ------------------------------------------------------------------------
    install(TARGETS 
        q_gf3_clifford
        q_gf3_serialize
        q_gf3_wasm
        q_gf3_maxplus
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    install(FILES 
        dll/clifford/clifford_api.hpp
        dll/serialize/serialize_api.hpp
        dll/wasm_bridge/wasm_api.hpp
        dll/maxplus/maxplus_api.hpp
        DESTINATION include/q_mini_wasm_v2/dll
    )
endif()

# ============================================================================
# SYCL Configuration (Optional)
# ============================================================================

if(USE_SYCL)
    find_package(IntelSYCL REQUIRED)
    target_compile_definitions(q_mini_wasm_v2_core PUBLIC USE_SYCL=1)
    target_link_libraries(q_mini_wasm_v2_core PUBLIC IntelSYCL::SYCL_CXX)
    
    # Add SYCL kernel sources
    target_sources(q_mini_wasm_v2_core PRIVATE ${SYCL_SOURCES})
endif()

# ============================================================================
# Test Suite
# ============================================================================

if(BUILD_TESTS)
    add_executable(q_mini_wasm_v2_tests tests/test_main.cpp)
    target_link_libraries(q_mini_wasm_v2_tests PRIVATE q_mini_wasm_v2_core)
    
    add_executable(q_mini_wasm_v2_network_test tests/test_network.cpp)
    target_link_libraries(q_mini_wasm_v2_network_test PRIVATE q_mini_wasm_v2_core)
    
    add_executable(q_mini_wasm_v2_remediation_test tests/test_remediation.cpp)
    target_link_libraries(q_mini_wasm_v2_remediation_test PRIVATE q_mini_wasm_v2_core)
    
    add_executable(q_mini_wasm_v2_betti_test tests/test_betti_extractor.cpp)
    target_link_libraries(q_mini_wasm_v2_betti_test PRIVATE q_mini_wasm_v2_core)
    
    # DLL integration test
    if(BUILD_DLL)
        add_executable(q_mini_wasm_v2_dll_test tests/integration_test.cpp)
        target_link_libraries(q_mini_wasm_v2_dll_test PRIVATE
            q_mini_wasm_v2_dll
            q_mini_wasm_v2_core
        )
        add_test(NAME q_mini_wasm_v2_dll_test COMMAND q_mini_wasm_v2_dll_test)
        
        # DLL API test
        add_executable(q_mini_wasm_v2_dll_api_test tests/test_dll_api.cpp)
        target_link_libraries(q_mini_wasm_v2_dll_api_test PRIVATE
            q_mini_wasm_v2_dll
            q_mini_wasm_v2_core
        )
        add_test(NAME q_mini_wasm_v2_dll_api_test COMMAND q_mini_wasm_v2_dll_api_test)
    endif()
    
    enable_testing()
    add_test(NAME q_mini_wasm_v2_tests COMMAND q_mini_wasm_v2_tests)
    add_test(NAME q_mini_wasm_v2_network_test COMMAND q_mini_wasm_v2_network_test)
    add_test(NAME q_mini_wasm_v2_remediation_test COMMAND q_mini_wasm_v2_remediation_test)
    add_test(NAME q_mini_wasm_v2_betti_test COMMAND q_mini_wasm_v2_betti_test)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS q_mini_wasm_v2_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY core/ DESTINATION include/q_mini_wasm_v2/core
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY runtime/ DESTINATION include/q_mini_wasm_v2/runtime
    FILES_MATCHING PATTERN "*.hpp"
)

# ============================================================================
# WASM Configuration (Optional)
# ============================================================================

if(BUILD_WASM)
    # Check for Emscripten
    if(NOT EMSCRIPTEN)
        message(WARNING "BUILD_WASM requires Emscripten toolchain")
    else()
        add_executable(q_mini_wasm_v2_wasm
            tests/test_main.cpp
        )
        
        target_include_directories(q_mini_wasm_v2_wasm PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
        
        # Emscripten-specific flags
        target_compile_options(q_mini_wasm_v2_wasm PRIVATE
            -s USE_PTHREADS=1
            -s PTHREAD_POOL_SIZE=4
        )
        
        target_link_options(q_mini_wasm_v2_wasm PRIVATE
            -s WASM=1
            -s ALLOW_MEMORY_GROWTH=1
            -s MAXIMUM_MEMORY=4GB
            -s USE_PTHREADS=1
            -s PTHREAD_POOL_SIZE=4
            -s MODULARIZE=1
            -s EXPORT_NAME="QMiniWasmV2"
            -s EXPORTED_FUNCTIONS="['_malloc','_free']"
            -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap']"
        )
        
        set_target_properties(q_mini_wasm_v2_wasm PROPERTIES
            OUTPUT_NAME "q_mini_wasm_v2"
            SUFFIX ".js"
        )
        
        install(TARGETS q_mini_wasm_v2_wasm
            RUNTIME DESTINATION bin/wasm
        )
    endif()
endif()

# ============================================================================
# Compiler Flags
# ============================================================================

# Enable warnings
target_compile_options(q_mini_wasm_v2_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# Release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(q_mini_wasm_v2_core PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/O2>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>
    )
endif()