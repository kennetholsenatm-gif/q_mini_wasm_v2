# Script to commit wiki changes and cherry-pick to q_mini_wasm_v2
# PowerShell version for Windows

$ErrorActionPreference = "Stop"

Write-Host "=== Wiki Changes Commit & Cherry-Pick ===" -ForegroundColor Green

# Step 1: Stage and commit changes
Write-Host "1. Staging changes..." -ForegroundColor Yellow
git add Home.md _Sidebar.md
git add generate_wiki_simple.py
git add generate-wiki.ps1
git add run-wiki-generator.bat
git add WIKI_GUIDE.md
git add SOLUTION_SUMMARY.md
git add create-pr.sh
git add commit-wiki-changes.sh

# Check if there are changes to commit
$diffOutput = git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "No changes to commit" -ForegroundColor Yellow
    $TASK_COMMIT = git rev-parse HEAD
} else {
    Write-Host "2. Committing changes..." -ForegroundColor Yellow
    git commit -m "docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add create-pr.sh for automated PR creation

These files address the issue of an empty GitHub Wiki despite having
extensive research documentation in docs/research/ directory."
    $TASK_COMMIT = git rev-parse HEAD
}

Write-Host "Task commit: $TASK_COMMIT" -ForegroundColor Cyan

# Step 2: Find q_mini_wasm_v2 worktree
Write-Host "3. Finding q_mini_wasm_v2 worktree..." -ForegroundColor Yellow
$worktreeList = git worktree list --porcelain
$worktreeLines = $worktreeList -split "`n"
$worktreePath = $null

for ($i = 0; $i -lt $worktreeLines.Length; $i++) {
    if ($worktreeLines[$i] -match "branch refs/heads/q_mini_wasm_v2") {
        if ($i -gt 0 -and $worktreeLines[$i-1] -match "^worktree (.+)") {
            $worktreePath = $matches[1]
            break
        }
    }
}

if (-not $worktreePath) {
    Write-Host "q_mini_wasm_v2 not checked out, using current worktree" -ForegroundColor Yellow
    $P = Get-Location
    
    # Check for uncommitted changes
    $status = git status --porcelain
    if ($status) {
        Write-Host "4. Stashing uncommitted changes..." -ForegroundColor Yellow
        git stash push -u -m "kanban-pre-cherry-pick"
        $STASH_CREATED = $true
    } else {
        $STASH_CREATED = $false
    }
    
    # Checkout q_mini_wasm_v2
    Write-Host "5. Checking out q_mini_wasm_v2..." -ForegroundColor Yellow
    git checkout q_mini_wasm_v2
} else {
    Write-Host "Found worktree: $worktreePath" -ForegroundColor Cyan
    $P = $worktreePath
    Set-Location $P
    
    # Verify branch
    $currentBranch = git branch --show-current
    if ($currentBranch -ne "q_mini_wasm_v2") {
        Write-Host "ERROR: Not on q_mini_wasm_v2 branch" -ForegroundColor Red
        exit 1
    }
    
    # Check for uncommitted changes
    $status = git status --porcelain
    if ($status) {
        Write-Host "4. Stashing uncommitted changes..." -ForegroundColor Yellow
        git stash push -u -m "kanban-pre-cherry-pick"
        $STASH_CREATED = $true
    } else {
        $STASH_CREATED = $false
    }
}

# Step 5: Cherry-pick
Write-Host "6. Cherry-picking commit..." -ForegroundColor Yellow
git cherry-pick $TASK_COMMIT
if ($LASTEXITCODE -ne 0) {
    Write-Host "Cherry-pick failed - conflicts detected" -ForegroundColor Red
    Write-Host "Resolve conflicts, then run: git cherry-pick --continue" -ForegroundColor Yellow
    exit 1
}

# Step 7: Restore stash
if ($STASH_CREATED) {
    Write-Host "7. Restoring stash..." -ForegroundColor Yellow
    git stash pop
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Stash pop failed - conflicts detected" -ForegroundColor Red
        Write-Host "Resolve conflicts, then run: git stash drop" -ForegroundColor Yellow
        exit 1
    }
}

# Final report
Write-Host "`n=== Final Report ===" -ForegroundColor Green
Write-Host "Commit hash: $(git rev-parse HEAD)" -ForegroundColor Cyan
Write-Host "Commit message: $(git log -1 --pretty=format:'%s')" -ForegroundColor Cyan
Write-Host "Stash used: $STASH_CREATED" -ForegroundColor Cyan
Write-Host "Branch: $(git branch --show-current)" -ForegroundColor Cyan
Write-Host "`nSUCCESS: Changes committed to q_mini_wasm_v2" -ForegroundColor Green