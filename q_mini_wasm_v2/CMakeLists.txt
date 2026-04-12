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
    core/moe/self_organizing_expert.hpp
    core/moe/self_organizing_expert.cpp
    core/learning/forward_forward.hpp
    core/learning/forward_forward.cpp
    core/network.hpp
    core/network.cpp
    core/training/data_synthesizer.hpp
    core/training/data_synthesizer.cpp
    core/qgnn/graph_native.cpp
    core/qgnn/betti_extractor.hpp
    core/qgnn/betti_extractor.cpp
    # core/inference/geometric_context.hpp  # TODO: Fix std::min and extract_context_vector
    # core/inference/geometric_context.cpp
    # core/inference/householder_synthesizer.hpp  # TODO: Fix compute_reflection_error
    # core/inference/householder_synthesizer.cpp
    core/inference/latency_profiler.hpp
    # core/inference/inference_pipeline.hpp
    # core/inference/inference_pipeline.cpp
    # core/flash_cim/flash_cim.cpp  # TODO: Exclude from build - not essential for core training
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

# Add /FS for multi-process PDB writes on MSVC, or disable PDB for Debug
if(MSVC)
    target_compile_options(q_mini_wasm_v2_core PRIVATE /FS)
    # Disable debug info to avoid PDB conflicts entirely
    set_property(TARGET q_mini_wasm_v2_core PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "")
    target_compile_options(q_mini_wasm_v2_core PRIVATE /Zi-)
endif()

target_include_directories(q_mini_wasm_v2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Data Synthesizer Agent - HTTP Client Dependencies
# ============================================================================

option(USE_LIBCURL "Enable libcurl for Data Synthesizer HTTP requests" ON)

if(USE_LIBCURL)
    # Try vcpkg first if available
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_CURL_ROOT "$ENV{VCPKG_ROOT}/installed/x64-windows")
        if(EXISTS "${VCPKG_CURL_ROOT}/include/curl/curl.h")
            set(CURL_FOUND TRUE)
            set(CURL_INCLUDE_DIR "${VCPKG_CURL_ROOT}/include")
            set(CURL_LIBRARY "${VCPKG_CURL_ROOT}/lib/libcurl.lib")
            message(STATUS "Data Synthesizer: Found libcurl via vcpkg at ${VCPKG_CURL_ROOT}")
        endif()
    endif()
    
    # Fallback to find_package
    if(NOT CURL_FOUND)
        find_package(CURL QUIET)
    endif()
    
    if(CURL_FOUND)
        target_compile_definitions(q_mini_wasm_v2_core PUBLIC HAS_LIBCURL=1)
        if(CURL_INCLUDE_DIR)
            target_include_directories(q_mini_wasm_v2_core PUBLIC ${CURL_INCLUDE_DIR})
        endif()
        if(CURL_LIBRARY)
            target_link_libraries(q_mini_wasm_v2_core PUBLIC ${CURL_LIBRARY})
        else()
            target_link_libraries(q_mini_wasm_v2_core PUBLIC CURL::libcurl)
        endif()
        message(STATUS "Data Synthesizer: libcurl enabled for HTTP API requests")
    else()
        message(WARNING "libcurl not found. Install libcurl to enable real API data synthesis.")
        message(STATUS "  Windows: vcpkg install curl:x64-windows")
        message(STATUS "  Linux: sudo apt-get install libcurl4-openssl-dev")
        message(STATUS "  macOS: brew install curl")
    endif()
endif()

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
# SYCL Configuration (XPU Compatible)
# ============================================================================

if(USE_SYCL)
    # Try multiple SYCL implementations for XPU compatibility
    find_package(IntelSYCL QUIET)
    
    if(NOT IntelSYCL_FOUND)
        # Fallback: Check for AdaptiveCpp (formerly hipSYCL)
        find_package(AdaptiveCpp QUIET)
    endif()
    
    if(NOT IntelSYCL_FOUND AND NOT AdaptiveCpp_FOUND)
        # Fallback: Check for DPC++ via environment
        if(DEFINED ENV{ONEAPI_ROOT})
            message(STATUS "Using oneAPI DPC++ from ONEAPI_ROOT")
            set(CMAKE_CXX_COMPILER "$ENV{ONEAPI_ROOT}/compiler/latest/bin/dpcpp.exe" CACHE FILEPATH "DPC++ compiler" FORCE)
            set(IntelSYCL_FOUND TRUE)
        endif()
    endif()
    
    if(IntelSYCL_FOUND OR AdaptiveCpp_FOUND)
        target_compile_definitions(q_mini_wasm_v2_core PUBLIC USE_SYCL=1)
        
        if(IntelSYCL_FOUND)
            target_link_libraries(q_mini_wasm_v2_core PUBLIC IntelSYCL::SYCL_CXX)
            message(STATUS "SYCL: Using IntelSYCL")
        elseif(AdaptiveCpp_FOUND)
            target_link_libraries(q_mini_wasm_v2_core PUBLIC AdaptiveCpp::acpp)
            message(STATUS "SYCL: Using AdaptiveCpp")
        endif()
        
        # Add SYCL kernel sources
        target_sources(q_mini_wasm_v2_core PRIVATE ${SYCL_SOURCES})
        
        # XPU-specific optimizations
        target_compile_options(q_mini_wasm_v2_core PRIVATE
            $<$<CXX_COMPILER_ID:Intel>:-fsycl-targets=spir64_x86_64,native>
        )
        
        message(STATUS "SYCL acceleration enabled for XPU")
    else()
        message(WARNING "SYCL requested but no SYCL implementation found. Install Intel oneAPI or AdaptiveCpp.")
        message(STATUS "Continuing without SYCL acceleration")
    endif()
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
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/FS>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# Release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(q_mini_wasm_v2_core PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/O2>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>
    )
endif()