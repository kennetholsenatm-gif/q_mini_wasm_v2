# Simple script to commit changes
Write-Host "=== Starting Git Operations ==="

# Stage changes
Write-Host "Staging changes..."
git add -A

# Create commit
Write-Host "Creating commit..."
$commitMessage = @"
Organize test files: Move test files to tests/ directory and fix CMake configuration

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
- tests/test_dll_api.cpp (DLL API tests)
"@

git commit -m $commitMessage

# Get commit hash
$commitHash = git rev-parse HEAD
Write-Host "Commit created with hash: $commitHash"

# Check worktree list
Write-Host "Checking worktree list..."
git worktree list --porcelain

Write-Host "`n=== Git Operations Complete ==="