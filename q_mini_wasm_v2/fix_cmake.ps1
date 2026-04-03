$file = "C:\Users\kenne\.cline\worktrees\bf565\q_mini_wasm_v2\q_mini_wasm_v2\CMakeLists.txt"
$content = Get-Content $file -Raw

# Fix library target
$content = $content -replace 'add_library\(q_mini_wasm_v2_core\r?\n\s*\\\r?\n\s*\\\r?\n\)', "add_library(q_mini_wasm_v2_core`n    `${CORE_SOURCES}`n    `${RUNTIME_SOURCES}`n)"

# Fix library include directories
$content = $content -replace 'target_include_directories\(q_mini_wasm_v2_core PUBLIC\r?\n\s*\\\r?\n\)', "target_include_directories(q_mini_wasm_v2_core PUBLIC`n    `${CMAKE_CURRENT_SOURCE_DIR}`n)"

# Fix DLL target
$content = $content -replace 'add_library\(q_mini_wasm_v2_dll SHARED\r?\n\s*\\\r?\n\s*\)', "add_library(q_mini_wasm_v2_dll SHARED`n        `${DLL_SOURCES}`n    )"

# Fix DLL include directories
$content = $content -replace 'target_include_directories\(q_mini_wasm_v2_dll PUBLIC\r?\n\s*\\\r?\n\s*\)', "target_include_directories(q_mini_wasm_v2_dll PUBLIC`n        `${CMAKE_CURRENT_SOURCE_DIR}`n    )"

# Fix SYCL sources
$content = $content -replace 'target_sources\(q_mini_wasm_v2_core PRIVATE \\\\\)', "target_sources(q_mini_wasm_v2_core PRIVATE `${SYCL_SOURCES})"

# Fix WASM executable
$content = $content -replace 'add_executable\(q_mini_wasm_v2_wasm\r?\n\s*\\\r?\n\s*\\\r?\n\s*\)', "add_executable(q_mini_wasm_v2_wasm`n            `${CORE_SOURCES}`n            `${RUNTIME_SOURCES}`n        )"

# Fix WASM include directories
$content = $content -replace 'target_include_directories\(q_mini_wasm_v2_wasm PUBLIC\r?\n\s*\\\r?\n\s*\)', "target_include_directories(q_mini_wasm_v2_wasm PUBLIC`n            `${CMAKE_CURRENT_SOURCE_DIR}`n        )"

# Fix compiler flags
$content = $content -replace '\\\\$<\\\\$', '$<'

# Fix test section
$testSection = @"
# ============================================================================
# Test Suite
# ============================================================================

if(BUILD_TESTS)
    enable_testing()
    
    # Main test suite
    add_executable(q_mini_wasm_v2_tests tests/test_main.cpp)
    target_link_libraries(q_mini_wasm_v2_tests PRIVATE q_mini_wasm_v2_core)
    add_test(NAME q_mini_wasm_v2_tests COMMAND q_mini_wasm_v2_tests)
    
    # Network integration test
    add_executable(q_mini_wasm_v2_network_test tests/test_network.cpp)
    target_link_libraries(q_mini_wasm_v2_network_test PRIVATE q_mini_wasm_v2_core)
    add_test(NAME q_mini_wasm_v2_network_test COMMAND q_mini_wasm_v2_network_test)
    
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
endif()
"@

$content = $content -replace '# ============================================================================\r?\n# Test Suite\r?\n# ============================================================================\r?\n\r?\nif\(BUILD_TESTS\).*?endif\(\)', $testSection, 'Singleline'

Set-Content $file -Value $content
Write-Host "CMakeLists.txt fixed successfully!"