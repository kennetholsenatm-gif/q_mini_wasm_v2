# Simple git operations
Write-Host "=== Starting Git Operations ==="

# Stage changes
Write-Host "Staging changes..."
git add -A

# Create commit
Write-Host "Creating commit..."
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

# Get commit hash
$commitHash = git rev-parse HEAD
Write-Host "Commit created with hash: $commitHash"

# Check worktree list
Write-Host "Checking worktree list..."
git worktree list --porcelain

# Find q_mini_wasm_v2 branch
Write-Host "Looking for q_mini_wasm_v2 branch..."
$worktrees = git worktree list --porcelain
$qMiniPath = $null

for ($i = 0; $i -lt $worktrees.Count; $i++) {
    if ($worktrees[$i] -match "branch refs/heads/q_mini_wasm_v2") {
        # Previous line contains worktree path
        if ($i -gt 0) {
            $prevLine = $worktrees[$i-1]
            if ($prevLine -match "^worktree (.+)") {
                $qMiniPath = $matches[1]
            }
        }
        break
    }
}

if ($qMiniPath) {
    Write-Host "Found q_mini_wasm_v2 branch at: $qMiniPath"
    
    # Check branch
    Write-Host "Verifying branch in $qMiniPath..."
    Push-Location $qMiniPath
    git branch --show-current
    Pop-Location
    
    # Check for uncommitted changes
    Write-Host "Checking for uncommitted changes in $qMiniPath..."
    Push-Location $qMiniPath
    $targetStatus = git status --porcelain
    Pop-Location
    
    $stashCreated = $false
    if ($targetStatus) {
        Write-Host "Stashing uncommitted changes in $qMiniPath..."
        Push-Location $qMiniPath
        git stash push -u -m "kanban-pre-cherry-pick"
        $stashCreated = $true
        Pop-Location
    }
    
    # Cherry-pick
    Write-Host "Cherry-picking commit $commitHash into $qMiniPath..."
    Push-Location $qMiniPath
    git cherry-pick $commitHash
    $cherryPickResult = $LASTEXITCODE
    Pop-Location
    
    if ($cherryPickResult -eq 0) {
        Write-Host "Cherry-pick successful!"
    } else {
        Write-Host "Cherry-pick had conflicts - manual resolution needed"
    }
    
    # Restore stash
    if ($stashCreated) {
        Write-Host "Restoring stashed changes..."
        Push-Location $qMiniPath
        git stash pop
        $stashPopResult = $LASTEXITCODE
        Pop-Location
        
        if ($stashPopResult -eq 0) {
            Write-Host "Stash restored successfully"
        } else {
            Write-Host "Stash pop had conflicts - manual resolution needed"
        }
    }
    
    # Report
    Write-Host "`n=== Final Report ==="
    Write-Host "Commit hash: $commitHash"
    Write-Host "Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration"
    Write-Host "Stash used: $stashCreated"
    Write-Host "Cherry-pick result: $cherryPickResult"
    Write-Host "Stash pop result: $stashPopResult"
    
} else {
    Write-Host "q_mini_wasm_v2 branch not found in worktree list"
    Write-Host "Current worktree is the target"
    
    # Report
    Write-Host "`n=== Final Report ==="
    Write-Host "Commit hash: $commitHash"
    Write-Host "Commit message: Organize test files: Move test files to tests/ directory and fix CMake configuration"
    Write-Host "Stash used: false"
    Write-Host "Cherry-pick result: N/A (no separate worktree)"
    Write-Host "Stash pop result: N/A"
}

Write-Host "`n=== Git Operations Complete ==="