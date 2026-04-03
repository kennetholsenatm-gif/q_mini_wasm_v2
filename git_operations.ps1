# Git operations script for kanban task completion
Write-Host "=== Starting Git Operations ==="

# Step 1: Check current status
Write-Host "`n1. Checking current git status..."
git status

# Step 2: Stage all changes
Write-Host "`n2. Staging changes..."
git add -A

# Step 3: Check if there are changes to commit
$status = git status --porcelain
if ($status) {
    Write-Host "`n3. Creating commit for task changes..."
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
    
    # Get the commit hash
    $commitHash = git rev-parse HEAD
    Write-Host "`n4. Commit created with hash: $commitHash"
} else {
    Write-Host "`n3. No changes to commit"
    $commitHash = $null
}

# Step 4: Check worktree list
Write-Host "`n5. Checking worktree list..."
git worktree list --porcelain

# Step 5: Find q_mini_wasm_v2 branch checkout location
Write-Host "`n6. Looking for q_mini_wasm_v2 branch checkout..."
$worktrees = git worktree list --porcelain
$qMiniPath = $null

foreach ($line in $worktrees) {
    if ($line -match "branch refs/heads/q_mini_wasm_v2") {
        # Extract the worktree path from previous line
        $qMiniPath = $previousLine -replace "^worktree ", ""
        break
    }
    $previousLine = $line
}

if ($qMiniPath) {
    Write-Host "`n7. Found q_mini_wasm_v2 branch at: $qMiniPath"
    
    # Step 6: Verify branch
    Write-Host "`n8. Verifying branch in $qMiniPath..."
    Push-Location $qMiniPath
    git branch --show-current
    Pop-Location
    
    # Step 7: Check for uncommitted changes in target
    Write-Host "`n9. Checking for uncommitted changes in $qMiniPath..."
    Push-Location $qMiniPath
    $targetStatus = git status --porcelain
    Pop-Location
    
    if ($targetStatus) {
        Write-Host "`n10. Stashing uncommitted changes in $qMiniPath..."
        Push-Location $qMiniPath
        git stash push -u -m "kanban-pre-cherry-pick"
        $stashCreated = $true
        Pop-Location
    } else {
        $stashCreated = $false
    }
    
    # Step 8: Cherry-pick the commit if we have one
    if ($commitHash) {
        Write-Host "`n11. Cherry-picking commit $commitHash into $qMiniPath..."
        Push-Location $qMiniPath
        git cherry-pick $commitHash
        $cherryPickResult = $LASTEXITCODE
        Pop-Location
        
        if ($cherryPickResult -eq 0) {
            Write-Host "`n12. Cherry-pick successful!"
        } else {
            Write-Host "`n12. Cherry-pick had conflicts - manual resolution needed"
        }
    }
    
    # Step 9: Restore stash if we created one
    if ($stashCreated) {
        Write-Host "`n13. Restoring stashed changes..."
        Push-Location $qMiniPath
        git stash pop
        $stashPopResult = $LASTEXITCODE
        Pop-Location
        
        if ($stashPopResult -eq 0) {
            Write-Host "`n14. Stash restored successfully"
        } else {
            Write-Host "`n14. Stash pop had conflicts - manual resolution needed"
        }
    }
    
    # Step 10: Report final status
    Write-Host "`n=== Final Report ==="
    Write-Host "Commit hash: $commitHash"
    Write-Host "Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration"
    Write-Host "Stash used: $stashCreated"
    Write-Host "Cherry-pick result: $cherryPickResult"
    Write-Host "Stash pop result: $stashPopResult"
    
} else {
    Write-Host "`n7. q_mini_wasm_v2 branch not found in worktree list"
    Write-Host "Current worktree is the target"
    
    # Current worktree is the target
    Write-Host "`n=== Final Report ==="
    Write-Host "Commit hash: $commitHash"
    Write-Host "Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration"
    Write-Host "Stash used: false"
    Write-Host "Cherry-pick result: N/A (no separate worktree)"
    Write-Host "Stash pop result: N/A"
}

Write-Host "`n=== Git Operations Complete ==="