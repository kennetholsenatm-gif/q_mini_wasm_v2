import subprocess
import os

print("=== Starting Git Operations ===")

# Stage changes
print("Staging changes...")
subprocess.run(["git", "add", "-A"], check=True)

# Create commit
print("Creating commit...")
commit_message = """Organize test files: Move test files to tests/ directory and fix CMake configuration

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
- tests/test_dll_api.cpp (DLL API tests)"""

subprocess.run(["git", "commit", "-m", commit_message], check=True)

# Get commit hash
result = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
commit_hash = result.stdout.strip()
print(f"Commit created with hash: {commit_hash}")

# Check worktree list
print("Checking worktree list...")
subprocess.run(["git", "worktree", "list", "--porcelain"], check=True)

print("\n=== Git Operations Complete ===")