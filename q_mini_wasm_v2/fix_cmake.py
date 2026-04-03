import re

# Read the file
with open(r'C:\Users\kenne\.cline\worktrees\bf565\q_mini_wasm_v2\q_mini_wasm_v2\CMakeLists.txt', 'r') as f:
    content = f.read()

# Fix the library target
content = re.sub(
    r'add_library\(q_mini_wasm_v2_core\n\s*\\\n\s*\\\n\)',
    'add_library(q_mini_wasm_v2_core\n    ${CORE_SOURCES}\n    ${RUNTIME_SOURCES}\n)',
    content
)

# Fix the include directories
content = re.sub(
    r'target_include_directories\(q_mini_wasm_v2_core PUBLIC\n\s*\\\n\)',
    'target_include_directories(q_mini_wasm_v2_core PUBLIC\n    ${CMAKE_CURRENT_SOURCE_DIR}\n)',
    content
)

# Fix the DLL target
content = re.sub(
    r'add_library\(q_mini_wasm_v2_dll SHARED\n\s*\\\n\s*\)',
    'add_library(q_mini_wasm_v2_dll SHARED\n        ${DLL_SOURCES}\n    )',
    content
)

# Fix the DLL include directories
content = re.sub(
    r'target_include_directories\(q_mini_wasm_v2_dll PUBLIC\n\s*\\\n\s*\)',
    'target_include_directories(q_mini_wasm_v2_dll PUBLIC\n        ${CMAKE_CURRENT_SOURCE_DIR}\n    )',
    content
)

# Fix the SYCL sources
content = re.sub(
    r'target_sources\(q_mini_wasm_v2_core PRIVATE \\\\',
    'target_sources(q_mini_wasm_v2_core PRIVATE ${SYCL_SOURCES})',
    content
)

# Fix the WASM executable
content = re.sub(
    r'add_executable\(q_mini_wasm_v2_wasm\n\s*\\\n\s*\\\n\s*\)',
    'add_executable(q_mini_wasm_v2_wasm\n            ${CORE_SOURCES}\n            ${RUNTIME_SOURCES}\n        )',
    content
)

# Fix the WASM include directories
content = re.sub(
    r'target_include_directories\(q_mini_wasm_v2_wasm PUBLIC\n\s*\\\n\s*\)',
    'target_include_directories(q_mini_wasm_v2_wasm PUBLIC\n            ${CMAKE_CURRENT_SOURCE_DIR}\n        )',
    content
)

# Fix the compiler flags
content = re.sub(r'\\$<\\$', '$<', content)

# Fix the test suite - add enable_testing() at the beginning and proper add_test() calls
test_section = '''# ============================================================================
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
endif()'''

content = re.sub(
    r'# ============================================================================\n# Test Suite\n# ============================================================================\n\nif\(BUILD_TESTS\)\n\s*add_executable.*?endif\(\)',
    test_section,
    content,
    flags=re.DOTALL
)

# Write the fixed content back
with open(r'C:\Users\kenne\.cline\worktrees\bf565\q_mini_wasm_v2\q_mini_wasm_v2\CMakeLists.txt', 'w') as f:
    f.write(content)

print("CMakeLists.txt fixed successfully!")