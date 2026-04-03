cmake_minimum_required(VERSION 3.14)
project(q_mini_wasm_v2 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Build Options
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
core/learning/ppo_agent.hpp
core/learning/ppo_agent.cpp
    core/network.hpp
    core/network.cpp
)

set(RUNTIME_SOURCES
    runtime/orchestrator.hpp
    runtime/orchestrator.cpp
)

set(SYCL_SOURCES
    sycl/tableau_kernels.hpp
)

set(DLL_SOURCES
    dll/q_mini_wasm_v2_api.hpp
    dll/impl/q_mini_wasm_v2_api.cpp
)

# ============================================================================
# Library Target
# ============================================================================

add_library(q_mini_wasm_v2_core
    \
    \
)

target_include_directories(q_mini_wasm_v2_core PUBLIC
    \
)

# ============================================================================
# DLL Target (C API)
# ============================================================================

if(BUILD_DLL)
    add_library(q_mini_wasm_v2_dll SHARED
        \
    )
    
    target_link_libraries(q_mini_wasm_v2_dll PRIVATE
        q_mini_wasm_v2_core
    )
    
    target_include_directories(q_mini_wasm_v2_dll PUBLIC
        \
    )
    
    # Set DLL export macro
    target_compile_definitions(q_mini_wasm_v2_dll PRIVATE
        Q_MINI_WASM_V2_EXPORTS
    )
    
    # Set output name
    set_target_properties(q_mini_wasm_v2_dll PROPERTIES
        OUTPUT_NAME "q_mini_wasm_v2"
        PREFIX ""
    )
    
    # Install DLL
    install(TARGETS q_mini_wasm_v2_dll
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    # Install DLL header
    install(FILES dll/q_mini_wasm_v2_api.hpp
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
    target_sources(q_mini_wasm_v2_core PRIVATE \)
endif()

# ============================================================================
# Test Suite
# ============================================================================

if(BUILD_TESTS)
    add_executable(q_mini_wasm_v2_tests tests/test_main.cpp)
    target_link_libraries(q_mini_wasm_v2_tests PRIVATE q_mini_wasm_v2_core)
    
    add_executable(q_mini_wasm_v2_network_test tests/test_network.cpp)
    target_link_libraries(q_mini_wasm_v2_network_test PRIVATE q_mini_wasm_v2_core)
    
    add_executable(q_mini_wasm_v2_ppo_test tests/test_ppo.cpp)
    target_link_libraries(q_mini_wasm_v2_ppo_test PRIVATE q_mini_wasm_v2_core)
    
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
    add_test(NAME q_mini_wasm_v2_ppo_test COMMAND q_mini_wasm_v2_ppo_test)
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
            \
            \
        )
        
        target_include_directories(q_mini_wasm_v2_wasm PUBLIC
            \
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
    \$<\$<CXX_COMPILER_ID:MSVC>:/W4>
    \$<\$<NOT:\$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# Release optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(q_mini_wasm_v2_core PRIVATE
        \$<\$<CXX_COMPILER_ID:MSVC>:/O2>
        \$<\$<NOT:\$<CXX_COMPILER_ID:MSVC>>:-O3>
    )
endif()



