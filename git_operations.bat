@echo off
setlocal enabledelayedexpansion

echo === Starting Git Operations ===

echo.
echo 1. Checking current git status...
git status

echo.
echo 2. Staging changes...
git add -A

echo.
echo 3. Checking for changes to commit...
git status --porcelain > temp_status.txt
set /p has_changes=<temp_status.txt
del temp_status.txt

if defined has_changes (
    echo 3. Creating commit for task changes...
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
    echo 4. Commit created with hash: !commitHash!
) else (
    echo 3. No changes to commit
    set commitHash=
)

echo.
echo 5. Checking worktree list...
git worktree list --porcelain

echo.
echo 6. Looking for q_mini_wasm_v2 branch checkout...
set qMiniPath=
for /f "tokens=*" %%i in ('git worktree list --porcelain') do (
    set line=%%i
    if "!line!"=="branch refs/heads/q_mini_wasm_v2" (
        set qMiniPath=!previousLine:worktree =!
    )
    set previousLine=!line!
)

if defined qMiniPath (
    echo 7. Found q_mini_wasm_v2 branch at: !qMiniPath!
    
    echo.
    echo 8. Verifying branch in !qMiniPath!...
    pushd !qMiniPath!
    git branch --show-current
    popd
    
    echo.
    echo 9. Checking for uncommitted changes in !qMiniPath!...
    pushd !qMiniPath!
    git status --porcelain > temp_target_status.txt
    set /p targetStatus=<temp_target_status.txt
    del temp_target_status.txt
    popd
    
    set stashCreated=false
    if defined targetStatus (
        echo 10. Stashing uncommitted changes in !qMiniPath!...
        pushd !qMiniPath!
        git stash push -u -m "kanban-pre-cherry-pick"
        set stashCreated=true
        popd
    )
    
    if defined commitHash (
        echo.
        echo 11. Cherry-picking commit !commitHash! into !qMiniPath!...
        pushd !qMiniPath!
        git cherry-pick !commitHash!
        set cherryPickResult=!errorlevel!
        popd
        
        if !cherryPickResult! equ 0 (
            echo 12. Cherry-pick successful!
        ) else (
            echo 12. Cherry-pick had conflicts - manual resolution needed
        )
    )
    
    if "!stashCreated!"=="true" (
        echo.
        echo 13. Restoring stashed changes...
        pushd !qMiniPath!
        git stash pop
        set stashPopResult=!errorlevel!
        popd
        
        if !stashPopResult! equ 0 (
            echo 14. Stash restored successfully
        ) else (
            echo 14. Stash pop had conflicts - manual resolution needed
        )
    )
    
    echo.
    echo === Final Report ===
    echo Commit hash: !commitHash!
    echo Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration
    echo Stash used: !stashCreated!
    echo Cherry-pick result: !cherryPickResult!
    echo Stash pop result: !stashPopResult!
    
) else (
    echo 7. q_mini_wasm_v2 branch not found in worktree list
    echo Current worktree is the target
    
    echo.
    echo === Final Report ===
    echo Commit hash: !commitHash!
    echo Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration
    echo Stash used: false
    echo Cherry-pick result: N/A (no separate worktree)
    echo Stash pop result: N/A
)

echo.
echo === Git Operations Complete ===