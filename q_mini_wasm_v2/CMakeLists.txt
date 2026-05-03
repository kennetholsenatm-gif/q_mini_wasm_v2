cmake_minimum_required(VERSION 3.14)
project(q_mini_wasm_v2 VERSION 1.0.0 LANGUAGES CXX)

# Parent of this CMake tree = repository root (publish q_training.dll next to qminiwasm.exe).
get_filename_component(QMINI_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Executables
# ============================================================================

# STANDALONE TRAINER - No core library linkage to avoid static init crashes
add_executable(q_mini_wasm_v2_trainer core/training/trainer_main.cpp)
# DO NOT link against q_mini_wasm_v2_core - it has problematic global constructors
# target_link_libraries(q_mini_wasm_v2_trainer PRIVATE q_mini_wasm_v2_core)

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

# Native q_mini_wasm_v2 always builds with SYCL device code. `USE_SYCL` is not a CMake option — only a C/C++
# compile definition (USE_SYCL=1 on targets). Strip any legacy/stale CMake cache entry or ignored -DUSE_SYCL=...
# so cmake-gui and half-enabled trees never persist a fake OFF switch.
if(DEFINED CACHE{USE_SYCL})
    unset(USE_SYCL CACHE)
endif()
option(QMINI_TRAINING_OPENMP "OpenMP: parallel GF3 layer loops + multi-expert routes (MSVC /openmp or libgomp)" ON)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_WASM "Build WebAssembly target" OFF)
option(BUILD_DLL "Build DLL with C API" ON)

# ============================================================================
# Source Files
# ============================================================================

# PipelineConfig::default_pipeline_config() — generated from repo-root config/pipeline_defaults.toml
set(QMINI_PIPELINE_DEFAULTS_GEN "${CMAKE_CURRENT_SOURCE_DIR}/core/training/autonomous_training_pipeline_defaults.gen.cpp")
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_command(
        OUTPUT ${QMINI_PIPELINE_DEFAULTS_GEN}
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen_pipeline_default_config.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/../config/pipeline_defaults.toml"
            "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/gen_pipeline_default_config.py"
        COMMENT "Generate default_pipeline_config() from config/pipeline_defaults.toml"
        VERBATIM
    )
else()
    message(STATUS "Python3 not found: using committed autonomous_training_pipeline_defaults.gen.cpp (run generator after editing TOML)")
endif()

set(CORE_SOURCES
    core/ternary/trit.hpp
    core/ternary/packing.hpp
    core/ternary/packing.cpp
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
    core/ingestion/trit_binary_loader.hpp
    core/ingestion/trit_binary_loader.cpp
    core/ingestion/async_staging_buffer.hpp
    core/ingestion/async_staging_buffer.cpp
    core/ingestion/async_transformer.hpp
    core/ingestion/async_transformer.cpp
    core/ingestion/gf3_polynomial_hash.hpp
    core/ingestion/gf3_polynomial_hash.cpp
    core/learning/entropy_goodness.hpp
    core/learning/entropy_goodness.cpp
    core/moe/router.hpp
    core/moe/router.cpp
    core/moe/router_checkpoint_io.cpp
    core/moe/gf3_layers.hpp
    core/moe/gf3_ff_size_checks.hpp
    core/moe/gf3_layers.cpp
    sycl/gf3_layers_sycl.hpp
    sycl/gf3_layers_sycl.cpp
    core/moe/gf3_sycl_probe.hpp
    core/moe/gf3_sycl_probe.cpp
    core/moe/expert_network.hpp
    core/moe/expert_network.cpp
    core/moe/unified_router.hpp
    core/moe/unified_router.cpp
    core/moe/moe_trainer.hpp
    core/moe/moe_trainer.cpp
    core/moe/self_organizing_expert.hpp
    core/moe/self_organizing_expert.cpp
    core/moe/runtime_orchestrator.hpp
    core/moe/runtime_orchestrator.cpp
    core/learning/forward_forward.hpp
    core/learning/forward_forward.cpp
    core/network.hpp
    core/network.cpp
    core/training/data_synthesizer.hpp
    core/training/data_synthesizer.cpp
    core/training/data_acquisition.hpp
    core/training/data_acquisition.cpp
    core/training/dataset_storage.hpp
    core/training/dataset_storage.cpp
    core/qgnn/graph_native.cpp
    core/qgnn/message_passing.hpp
    core/qgnn/message_passing.cpp
    core/qgnn/betti_extractor.hpp
    core/qgnn/betti_extractor.cpp
    core/qgnn/fast_betti_estimator.hpp
    core/qgnn/fast_betti_estimator.cpp
    core/qgnn/graph_tableau.hpp
    core/qgnn/graph_tableau.cpp
    core/persistence/tlwl_checkpoint.hpp
    core/persistence/tlwl_checkpoint.cpp
    core/training/autonomous_training_pipeline.hpp
    core/training/autonomous_training_pipeline.cpp
    core/training/crash_breadcrumb.hpp
    core/training/crash_breadcrumb.cpp
    core/training/autonomous_training_pipeline_defaults.gen.cpp
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
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        target_compile_options(q_mini_wasm_v2_core PRIVATE /Zi-)
    else()
        # VS 18 headers reject legacy macro if present; force-undef for Intel oneAPI clang-cl.
        target_compile_options(q_mini_wasm_v2_core PRIVATE /U_CONTAINER_DEBUG_LEVEL)
    endif()
endif()

target_include_directories(q_mini_wasm_v2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(QMINI_TRAINING_OPENMP)
    if(MSVC)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
            # Intel icx/icx-cl: /Qiopenmp is the supported OpenMP path; keep C++ exceptions enabled (/EHsc).
            target_compile_options(q_mini_wasm_v2_core PRIVATE /Qiopenmp /EHsc)
            message(STATUS "OpenMP: enabled for q_mini_wasm_v2_core (/Qiopenmp + /EHsc, IntelLLVM)")
        else()
            target_compile_options(q_mini_wasm_v2_core PRIVATE /openmp)
            message(STATUS "OpenMP: enabled for q_mini_wasm_v2_core (/openmp)")
        endif()
    else()
        find_package(OpenMP COMPONENTS CXX)
        if(OpenMP_CXX_FOUND)
            target_link_libraries(q_mini_wasm_v2_core PUBLIC OpenMP::OpenMP_CXX)
            message(STATUS "OpenMP: enabled for q_mini_wasm_v2_core (OpenMP::OpenMP_CXX)")
        else()
            message(WARNING "QMINI_TRAINING_OPENMP=ON but OpenMP not found; GF3 loops stay serial")
        endif()
    endif()
else()
    message(STATUS "OpenMP: disabled (QMINI_TRAINING_OPENMP=OFF)")
endif()

# ============================================================================
# DLL Target for CGO Bindings
# ============================================================================

if(BUILD_DLL)
    add_library(q_mini_wasm_v2_dll SHARED
        dll/impl/q_mini_wasm_v2_api.cpp
        core/training/autonomous_training_pipeline.cpp
        core/training/data_synthesizer.cpp
        core/training/dataset_storage.cpp
    )

    target_link_libraries(q_mini_wasm_v2_dll PRIVATE
        q_mini_wasm_v2_core
    )

    target_include_directories(q_mini_wasm_v2_dll PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # q_mini_wasm_v2_api.hpp uses Q_MINI_WASM_V2_EXPORTS for dllexport (CMake only sets <target>_EXPORTS).
    target_compile_definitions(q_mini_wasm_v2_dll PRIVATE Q_MINI_WASM_V2_EXPORTS)

    set_target_properties(q_mini_wasm_v2_dll PROPERTIES
        OUTPUT_NAME "q_mini_wasm_v2"
        PREFIX ""
    )

    if(MSVC)
        target_compile_options(q_mini_wasm_v2_dll PRIVATE /FS)
        set_property(TARGET q_mini_wasm_v2_dll PROPERTY MSVC_DEBUG_INFORMATION_FORMAT "")
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
            target_compile_options(q_mini_wasm_v2_dll PRIVATE /Zi-)
        else()
            target_compile_options(q_mini_wasm_v2_dll PRIVATE /U_CONTAINER_DEBUG_LEVEL)
        endif()
    endif()
endif()

# ============================================================================
# Data Synthesizer Agent - HTTP Client Dependencies
# ============================================================================

option(USE_LIBCURL "Enable libcurl for Data Synthesizer HTTP requests" ON)

if(USE_LIBCURL)
    # MSVC + libcurl: use vcpkg's x64-windows triplet (include/curl/curl.h, lib/libcurl.lib).
    # winget's "cURL" package is the MinGW portable CLI zip — it does not provide this layout; use vcpkg:
    #   git clone https://github.com/microsoft/vcpkg C:\vcpkg
    #   C:\vcpkg\bootstrap-vcpkg.bat
    #   C:\vcpkg\vcpkg install curl:x64-windows
    #   setx VCPKG_ROOT C:\vcpkg
    set(_vcpkg_roots "")
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _vcpkg_roots "$ENV{VCPKG_ROOT}")
    endif()
    list(APPEND _vcpkg_roots "C:/vcpkg" "C:/tools/vcpkg" "C:/src/vcpkg")
    foreach(_vr IN LISTS _vcpkg_roots)
        if(NOT CURL_FOUND AND _vr)
            file(TO_CMAKE_PATH "${_vr}" _vr_norm)
            set(_try "${_vr_norm}/installed/x64-windows")
            if(EXISTS "${_try}/include/curl/curl.h")
                set(CURL_FOUND TRUE)
                set(CURL_INCLUDE_DIR "${_try}/include")
                set(CURL_LIBRARY "${_try}/lib/libcurl.lib")
                message(STATUS "Data Synthesizer: Found libcurl via vcpkg at ${_try}")
                break()
            endif()
        endif()
    endforeach()
    
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
    # 5. Training DLL (Real MoE + Forward-Forward Training)
    # ------------------------------------------------------------------------
    add_library(q_training SHARED
        dll/training/training_api.hpp
        dll/training/training_api.cpp
        dll/training/gf3_negative_probe_sycl.hpp
        dll/training/gf3_negative_probe_sycl.cpp
        dll/training/q_training_link_tableau_kernels.cpp
        dll/training/q_training_link_gf3_layers_sycl.cpp
    )
    
    target_link_libraries(q_training PRIVATE
        q_mini_wasm_v2_core
    )
    # Windows + SYCL: without this, the linker may drop device-code COMDATs from the static core .lib when
    # building q_training.dll, causing "No kernel named ..." at runtime for kernels only referenced via JIT.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        target_link_options(q_training PRIVATE
            "/WHOLEARCHIVE:$<TARGET_FILE:q_mini_wasm_v2_core>"
            # tableau_kernels.cpp is also compiled into q_training_link_tableau_kernels.cpp so the
            # final DLL link registers device code; allow duplicate COMDATs vs the archive pull.
            "/FORCE:MULTIPLE")
    endif()
    
    target_include_directories(q_training PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/dll
    )
    
    target_compile_definitions(q_training PRIVATE
        TRAINING_API_EXPORTS
        QMINI_Q_TRAINING_DLL=1
    )
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        # icx-cl defaults can drop /EHsc on some TU graphs; training_api uses try/catch.
        target_compile_options(q_training PRIVATE /EHsc /U_CONTAINER_DEBUG_LEVEL)
    endif()
    
    set_target_properties(q_training PROPERTIES
        OUTPUT_NAME "q_training"
        PREFIX ""
    )

    # Windows/Go: copy next to qminiwasm.exe when q_training relinks. (CMake skips POST_BUILD when the
    # target is up-to-date; use target q_training_publish to force copy without a no-op link.)
    add_custom_command(TARGET q_training POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC=$<TARGET_FILE:q_training>
            -DDST_PRIMARY=${QMINI_REPO_ROOT}/q_training.dll
            -DDST_ALT=${QMINI_REPO_ROOT}/native_runtime/q_training.dll
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/post_copy_training_dll.cmake"
        COMMENT "q_training: copy DLL to repo root and native_runtime (path_map.toml)"
    )

    add_custom_target(q_training_publish
        COMMAND ${CMAKE_COMMAND}
            -DSRC=$<TARGET_FILE:q_training>
            -DDST_PRIMARY=${QMINI_REPO_ROOT}/q_training.dll
            -DDST_ALT=${QMINI_REPO_ROOT}/native_runtime/q_training.dll
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/post_copy_training_dll.cmake"
        DEPENDS q_training
        COMMENT "Publish q_training.dll to repo root + native_runtime/ (build this to copy even when q_training is up-to-date)"
        VERBATIM
    )

    # libcurl is linked via vcpkg import libs; Windows resolves dependents from the loaded DLL's
    # directory before PATH, so copy libcurl (and common OpenSSL/zlib peers) next to q_training.dll.
    if(WIN32 AND USE_LIBCURL AND CURL_FOUND AND DEFINED ENV{VCPKG_ROOT})
        add_custom_command(TARGET q_training POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DVCPKG_ROOT=$ENV{VCPKG_ROOT}"
                -DCONFIG=$<CONFIG>
                -DDST_PRIMARY_DIR=${QMINI_REPO_ROOT}
                -DDST_ALT_DIR=${QMINI_REPO_ROOT}/native_runtime
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/post_copy_vcpkg_curl_runtime_dlls.cmake"
            COMMENT "q_training: copy vcpkg libcurl runtime DLLs beside published q_training.dll"
        )
    endif()

    # ------------------------------------------------------------------------
    # Install all DLL targets
    # ------------------------------------------------------------------------
    install(TARGETS 
        q_gf3_clifford
        q_gf3_serialize
        q_gf3_wasm
        q_gf3_maxplus
        q_training
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    install(FILES 
        dll/clifford/clifford_api.hpp
        dll/serialize/serialize_api.hpp
        dll/wasm_bridge/wasm_api.hpp
        dll/maxplus/maxplus_api.hpp
        dll/training/training_api.hpp
        DESTINATION include/q_mini_wasm_v2/dll
    )
endif()

# ============================================================================
# SYCL Configuration (XPU Compatible) — mandatory for this CMake project
# ============================================================================

# Avoid "half-enabled" SYCL on Windows: cl.exe ignores -fsycl and then SYCL headers fail late.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR
        "q_mini_wasm_v2 requires a SYCL-capable C++ compiler on Windows. MSVC (cl.exe) cannot compile SYCL kernels.\n"
        "  Use Ninja + icx:  cmake -G Ninja -DCMAKE_CXX_COMPILER=\"<oneAPI>/compiler/latest/bin/icx.exe\" ...\n"
        "  Or Visual Studio + Intel toolset:  cmake -G \"Visual Studio 17 2022\" -A x64 -T IntelLLVM ...\n"
        "Run cmake from an Intel oneAPI command prompt (or after setvars.bat) so the linker finds Intel libraries. See CONTRIBUTING.md.")
endif()

# Help CMake locate oneAPI before package discovery (single prepend; avoid a second find_package that can
# re-load Intel's broken IntelSYCLConfig and drop IntelSYCL::SYCL_CXX from the cache).
if(DEFINED ENV{ONEAPI_ROOT})
    # Append (not prepend): avoid Intel's shipped IntelSYCLConfig.cmake winning the search over IntelSYCL_DIR.
    list(APPEND CMAKE_PREFIX_PATH "$ENV{ONEAPI_ROOT}/compiler/latest/lib/cmake")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{ONEAPI_ROOT}/lib/cmake")
endif()

# Intel's shipped IntelSYCLConfig.cmake can fail configure when the SYCL feature-test leaves test_output empty
# (unquoted ${test_output} → SYCL_FEATURE_TEST_EXTRACT with no args). Prefer the q_mini shim for icx-family compilers.
set(_QMINI_INTEL_SYCL_SHIM "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IntelSYCLConfig.cmake")
if(EXISTS "${_QMINI_INTEL_SYCL_SHIM}")
    get_filename_component(_qmini_cxx_basename "${CMAKE_CXX_COMPILER}" NAME_WE)
    string(TOLOWER "${_qmini_cxx_basename}" _qmini_cxx_basename_lc)
    set(_qmini_use_intel_sycl_shim FALSE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        set(_qmini_use_intel_sycl_shim TRUE)
    elseif(_qmini_cxx_basename_lc MATCHES "^(icx|icpx|dpcpp|icx-cl)$")
        set(_qmini_use_intel_sycl_shim TRUE)
    elseif(CMAKE_CXX_COMPILER MATCHES "[Oo]ne[Aa][Pp][Ii]|[Ii]ntel|oneapi")
        set(_qmini_use_intel_sycl_shim TRUE)
    endif()
    if(_qmini_use_intel_sycl_shim)
        # CACHE FORCE so a stale IntelSYCL_DIR from an older configure cannot skip the shim.
        set(IntelSYCL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" CACHE PATH "q_mini_wasm_v2 IntelSYCL CMake shim" FORCE)
        message(STATUS "q_mini_wasm_v2: IntelSYCL_DIR -> bundled IntelSYCLConfig.cmake shim (icx-family CXX)")
    endif()
endif()

# Try multiple SYCL implementations for XPU compatibility
find_package(IntelSYCL QUIET)
set(QMINI_LLVM_SYCL_FOUND FALSE)

if(NOT IntelSYCL_FOUND)
    # Fallback: Check for AdaptiveCpp (formerly hipSYCL)
    find_package(AdaptiveCpp QUIET)
endif()

# Final fallback: clang++ with built-in -fsycl support (LLVM SYCL toolchain).
if(NOT IntelSYCL_FOUND AND NOT AdaptiveCpp_FOUND AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} --help
        OUTPUT_VARIABLE QMINI_CLANG_HELP
        ERROR_QUIET
        RESULT_VARIABLE QMINI_CLANG_HELP_RC
    )
    if(QMINI_CLANG_HELP_RC EQUAL 0)
        string(FIND "${QMINI_CLANG_HELP}" "-fsycl" QMINI_CLANG_FSYCL_POS)
        if(NOT QMINI_CLANG_FSYCL_POS EQUAL -1)
            set(QMINI_LLVM_SYCL_FOUND TRUE)
            message(STATUS "SYCL: Using clang++ -fsycl fallback")
        endif()
    endif()
endif()

if(IntelSYCL_FOUND OR AdaptiveCpp_FOUND OR QMINI_LLVM_SYCL_FOUND)
    target_compile_definitions(q_mini_wasm_v2_core PUBLIC USE_SYCL=1)

    if(IntelSYCL_FOUND)
            if(TARGET IntelSYCL::SYCL_CXX)
                target_link_libraries(q_mini_wasm_v2_core PUBLIC IntelSYCL::SYCL_CXX)
                message(STATUS "SYCL: Using IntelSYCL::SYCL_CXX target")
            elseif(COMMAND add_sycl_to_target)
                # Some IntelSYCL packages expose helper function but not imported target.
                add_sycl_to_target(TARGET q_mini_wasm_v2_core SOURCES
                    ${SYCL_SOURCES}
                    core/qgnn/message_passing_sycl.cpp
                )
                message(STATUS "SYCL: Using IntelSYCL add_sycl_to_target() helper")
            else()
                if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
                    message(WARNING "IntelSYCL found but compiler is clang++ on Windows. Use icpx (IntelLLVM) for stable SYCL builds.")
                else()
                    message(WARNING "IntelSYCL package found, but no SYCL target/helper exported; falling back to generic -fsycl flags.")
                    target_compile_options(q_mini_wasm_v2_core PRIVATE -fsycl)
                    target_link_options(q_mini_wasm_v2_core PRIVATE -fsycl)
                endif()
            endif()
        elseif(AdaptiveCpp_FOUND)
            target_link_libraries(q_mini_wasm_v2_core PUBLIC AdaptiveCpp::acpp)
            message(STATUS "SYCL: Using AdaptiveCpp")
        elseif(QMINI_LLVM_SYCL_FOUND)
            target_compile_options(q_mini_wasm_v2_core PRIVATE -fsycl)
            target_link_options(q_mini_wasm_v2_core PRIVATE -fsycl)
            message(STATUS "SYCL: Using LLVM clang -fsycl")
        endif()
        
        # Add SYCL kernel sources (tableau + QGNN message passing device code)
        target_sources(q_mini_wasm_v2_core PRIVATE ${SYCL_SOURCES})
        target_sources(q_mini_wasm_v2_core PRIVATE
            core/qgnn/message_passing_sycl.hpp
            core/qgnn/message_passing_sycl.cpp
        )
        
        # XPU-specific optimizations (Intel classic + IntelLLVM / oneAPI icx).
        # Windows icx/icx-cl rejects `native` in -fsycl-targets (Linux JIT host only); use SPIR-V offload only.
        if(WIN32)
            target_compile_options(q_mini_wasm_v2_core PRIVATE
                $<$<CXX_COMPILER_ID:Intel>:-fsycl-targets=spir64_x86_64>
                $<$<CXX_COMPILER_ID:IntelLLVM>:-fsycl-targets=spir64_x86_64>
            )
        else()
            target_compile_options(q_mini_wasm_v2_core PRIVATE
                $<$<CXX_COMPILER_ID:Intel>:-fsycl-targets=spir64_x86_64,native>
                $<$<CXX_COMPILER_ID:IntelLLVM>:-fsycl-targets=spir64_x86_64,native>
            )
        endif()
        
        message(STATUS "SYCL acceleration enabled for XPU")

        # MSVC + static lib core: ensure each SHARED DLL explicitly links the SYCL runtime so device code and
        # sycl7.dll / loader deps resolve at load time (IntelSYCL PUBLIC on core does not always suffice).
        set(_QMINI_SYCL_DLL_TARGETS
            q_mini_wasm_v2_agents
            q_mini_wasm_v2_dll
            q_gf3_clifford
            q_gf3_serialize
            q_gf3_wasm
            q_gf3_maxplus
            q_training
        )
        foreach(_qmini_dll ${_QMINI_SYCL_DLL_TARGETS})
            if(TARGET ${_qmini_dll})
                if(TARGET IntelSYCL::SYCL_CXX)
                    target_link_libraries(${_qmini_dll} PRIVATE IntelSYCL::SYCL_CXX)
                elseif(TARGET AdaptiveCpp::acpp)
                    target_link_libraries(${_qmini_dll} PRIVATE AdaptiveCpp::acpp)
                endif()
            endif()
        endforeach()

        # Infer links core SYCL APIs; standalone trainer intentionally does NOT link core — omit here.
        foreach(_qmini_exe q_mini_wasm_v2_infer)
            if(TARGET ${_qmini_exe})
                if(TARGET IntelSYCL::SYCL_CXX)
                    target_link_libraries(${_qmini_exe} PRIVATE IntelSYCL::SYCL_CXX)
                elseif(TARGET AdaptiveCpp::acpp)
                    target_link_libraries(${_qmini_exe} PRIVATE AdaptiveCpp::acpp)
                endif()
            endif()
        endforeach()
else()
    message(FATAL_ERROR
        "No working SYCL toolchain found for q_mini_wasm_v2 (Intel oneAPI icx/icpx via bundled CMake shim, AdaptiveCpp, or LLVM clang -fsycl on Linux). "
        "Install Intel oneAPI DPC++ or AdaptiveCpp, set CMAKE_CXX_COMPILER to icx/icpx, source setvars.sh on Linux, then reconfigure in a fresh build directory.")
endif()

# Every SHARED DLL that loads q_mini_wasm_v2_core: SYCL probe + tiny parallel_for on attach (USE_SYCL builds).
set(QMINI_SYCL_BOOTSTRAP "${CMAKE_CURRENT_SOURCE_DIR}/dll/common/sycl_dll_bootstrap.cpp")
set(_QMINI_BOOT_TARGETS
    q_mini_wasm_v2_agents
    q_mini_wasm_v2_dll
    q_gf3_clifford
    q_gf3_serialize
    q_gf3_wasm
    q_gf3_maxplus
    q_training
)
foreach(_bt ${_QMINI_BOOT_TARGETS})
    if(TARGET ${_bt} AND EXISTS "${QMINI_SYCL_BOOTSTRAP}")
        target_sources(${_bt} PRIVATE "${QMINI_SYCL_BOOTSTRAP}")
    endif()
endforeach()

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

    add_executable(q_moe_tritpack5_symplectic_test tests/test_moe_tritpack5_symplectic.cpp)
    target_link_libraries(q_moe_tritpack5_symplectic_test PRIVATE q_mini_wasm_v2_core)

    add_executable(q_moe_route_dense_vs_tritpack5_test tests/test_moe_route_dense_vs_tritpack5.cpp)
    target_link_libraries(q_moe_route_dense_vs_tritpack5_test PRIVATE q_mini_wasm_v2_core)

    add_executable(q_e2e_train_infer_smoke tests/e2e_train_infer_smoke.cpp)
    target_link_libraries(q_e2e_train_infer_smoke PRIVATE q_mini_wasm_v2_core)
    
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
    find_program(QMINI_PYTHON_EXE NAMES python3 python)
    if(QMINI_PYTHON_EXE)
        add_test(
            NAME qmini_policy_no_sycl_route_off
            COMMAND ${QMINI_PYTHON_EXE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_no_sycl_route_off.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/..")
        add_test(
            NAME qmini_policy_no_throughput_hidden_caps
            COMMAND ${QMINI_PYTHON_EXE}
                "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_no_throughput_hidden_caps.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/..")
    endif()
    add_test(NAME q_mini_wasm_v2_tests COMMAND q_mini_wasm_v2_tests)
    add_test(NAME q_mini_wasm_v2_network_test COMMAND q_mini_wasm_v2_network_test)
    add_test(NAME q_mini_wasm_v2_remediation_test COMMAND q_mini_wasm_v2_remediation_test)
    add_test(NAME q_mini_wasm_v2_betti_test COMMAND q_mini_wasm_v2_betti_test)
    add_test(NAME q_moe_tritpack5_symplectic_test COMMAND q_moe_tritpack5_symplectic_test)
    add_test(NAME q_moe_route_dense_vs_tritpack5_test COMMAND q_moe_route_dense_vs_tritpack5_test)
    add_test(NAME q_e2e_train_infer_smoke COMMAND q_e2e_train_infer_smoke)
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