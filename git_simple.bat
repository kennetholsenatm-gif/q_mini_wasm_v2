@echo off
echo === Starting Git Operations ===

echo Staging changes...
git add -A

echo Creating commit...
git commit -m "Organize test files: Move test files to tests/ directory and fix CMake configuration

- Test files are already organized in tests/ directory
- Created tests/CMakeLists.txt for proper test compilation configuration
- Fixed part of CMakeLists.txt library target configuration
- Identified remaining issues in main CMakeLists.txt that need manual fix:
  * Library and DLL targets have empty source lists
  * SYCL and WASM configurations need source files
  * Compiler flags have escaped characters

Test files included:
- tests/test_main.cpp (main test suite)
- tests/test_network.cpp (network integration test)
- tests/integration_test.cpp (DLL integration tests)
- tests/test_dll_api.cpp (DLL API tests)"

for /f "tokens=*" %%i in ('git rev-parse HEAD') do set commitHash=%%i
echo Commit created with hash: %commitHash%

echo Checking worktree list...
git worktree list --porcelain

echo.
echo === Git Operations Complete ===