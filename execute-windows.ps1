# Wiki Population Task - PowerShell Execution
# Windows Environment Script

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Wiki Population Task - Windows Execution" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Stage and commit changes
Write-Host "Step 1: Staging and committing changes..." -ForegroundColor Yellow
git add Home.md _Sidebar.md
git add generate_wiki_simple.py
git add generate-wiki.ps1
git add run-wiki-generator.bat
git add WIKI_GUIDE.md
git add SOLUTION_SUMMARY.md
git add create-pr.sh
git add commit-wiki-changes.sh
git add commit-wiki-changes.ps1
git add execute-task.bat
git add run-task.sh
git add TASK_COMPLETION_SUMMARY.md
git add simple-execute.bat
git add FINAL_SUMMARY.md
git add WINDOWS_ENVIRONMENT.md

$diffOutput = git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Host "No changes to commit" -ForegroundColor Yellow
    $TASK_COMMIT = git rev-parse HEAD
} else {
    Write-Host "Committing changes..." -ForegroundColor Green
    git commit -m "docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add WINDOWS_ENVIRONMENT.md for agent awareness

These files address the issue of an empty GitHub Wiki despite having
extensive research documentation in docs/research/ directory."
    $TASK_COMMIT = git rev-parse HEAD
}

Write-Host "Task commit: $TASK_COMMIT" -ForegroundColor Cyan

# Step 2: Find q_mini_wasm_v2 branch
Write-Host "`nStep 2: Finding q_mini_wasm_v2 branch..." -ForegroundColor Yellow
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

$STASH_CREATED = $false

if (-not $worktreePath) {
    Write-Host "q_mini_wasm_v2 not checked out, using current worktree" -ForegroundColor Yellow
    
    # Check for uncommitted changes
    $status = git status --porcelain
    if ($status) {
        Write-Host "Step 4: Stashing uncommitted changes..." -ForegroundColor Yellow
        git stash push -u -m "kanban-pre-cherry-pick"
        $STASH_CREATED = $true
        Write-Host "Stash created" -ForegroundColor Green
    }
    
    # Checkout q_mini_wasm_v2
    Write-Host "Step 3: Checking out q_mini_wasm_v2..." -ForegroundColor Yellow
    git checkout q_mini_wasm_v2
} else {
    Write-Host "Found worktree: $worktreePath" -ForegroundColor Cyan
    Set-Location $worktreePath
    
    # Verify branch
    $currentBranch = git branch --show-current
    if ($currentBranch -ne "q_mini_wasm_v2") {
        Write-Host "ERROR: Not on q_mini_wasm_v2 branch" -ForegroundColor Red
        exit 1
    }
    
    # Check for uncommitted changes
    $status = git status --porcelain
    if ($status) {
        Write-Host "Step 4: Stashing uncommitted changes..." -ForegroundColor Yellow
        git stash push -u -m "kanban-pre-cherry-pick"
        $STASH_CREATED = $true
        Write-Host "Stash created" -ForegroundColor Green
    }
}

# Step 5: Cherry-pick
Write-Host "`nStep 5: Cherry-picking commit..." -ForegroundColor Yellow
git cherry-pick $TASK_COMMIT
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Cherry-pick failed with conflicts" -ForegroundColor Red
    Write-Host "Please resolve conflicts manually, then run:" -ForegroundColor Yellow
    Write-Host "  git add <resolved-files>" -ForegroundColor White
    Write-Host "  git cherry-pick --continue" -ForegroundColor White
    exit 1
}
Write-Host "Cherry-pick successful" -ForegroundColor Green

# Step 7: Restore stash
if ($STASH_CREATED) {
    Write-Host "`nStep 7: Restoring stash..." -ForegroundColor Yellow
    git stash pop
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WARNING: Stash pop had conflicts" -ForegroundColor Yellow
        Write-Host "Please resolve conflicts manually, then run:" -ForegroundColor Yellow
        Write-Host "  git add <resolved-files>" -ForegroundColor White
        Write-Host "  git stash drop" -ForegroundColor White
    } else {
        Write-Host "Stash restored successfully" -ForegroundColor Green
    }
}

# Final report
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "FINAL REPORT" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
$finalHash = git rev-parse HEAD
$finalMsg = git log -1 --pretty=format:"%s"
$finalBranch = git branch --show-current

Write-Host "Final commit hash: $finalHash" -ForegroundColor White
Write-Host "Final commit message: $finalMsg" -ForegroundColor White
Write-Host "Stash used: $STASH_CREATED" -ForegroundColor White
Write-Host "Conflicts resolved: No" -ForegroundColor White
Write-Host "Branch: $finalBranch" -ForegroundColor White
Write-Host ""
Write-Host "SUCCESS: Changes committed to q_mini_wasm_v2" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Review: git log -1 --stat" -ForegroundColor White
Write-Host "2. Push: git push origin q_mini_wasm_v2" -ForegroundColor White
Write-Host "3. Generate wiki: python generate_wiki_simple.py" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Cyan