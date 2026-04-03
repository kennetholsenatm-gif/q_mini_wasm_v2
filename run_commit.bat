@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo PR Auto-Commit Investigation - Commit and Merge Script
echo ============================================================

cd /d "C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"

echo.
echo Current directory: %CD%
echo.

echo 1. Checking git status...
git status
echo.

echo 2. Current branch:
for /f "tokens=*" %%i in ('git rev-parse --abbrev-ref HEAD') do set CURRENT_BRANCH=%%i
echo Branch: %CURRENT_BRANCH%
echo.

echo 3. Checking if detached HEAD...
git symbolic-ref --short HEAD >nul 2>&1
if errorlevel 1 (
    echo DETACHED HEAD detected
    set IS_DETACHED=YES
) else (
    echo On a branch
    set IS_DETACHED=NO
)
echo.

echo 4. Listing modified files...
git diff --name-only
echo.

echo 5. Listing untracked files...
git ls-files --others --exclude-standard
echo.

echo 6. Git worktree list:
git worktree list --porcelain
echo.

echo 7. Recent commits:
git log --oneline -5
echo.

echo ============================================================
echo Staging and committing changes...
echo ============================================================

echo.
echo Staging all changes...
git add -A
echo.

echo Creating commit...
git commit -m "fix: PR auto-commit investigation and fixes

Issues Fixed:
1. Missing scripts - Created generate_pr_part1.py with core git analysis functions
2. Incomplete implementation - Fixed generate_detailed_pr.py (removed duplicates, added missing methods)
3. Workflow configuration - Updated kanban-auto-commit.yml to auto-trigger and create PRs
4. Auto-commit script - Updated auto_commit.py to properly create PRs using GitHub CLI

Files Created:
- scripts/generate_pr_part1.py
- PR_INVESTIGATION_SUMMARY.md
- PR_FIX_SUMMARY.md
- PR_FIX_COMPLETE.md

Files Modified:
- scripts/generate_detailed_pr.py
- scripts/generate_pr.py
- scripts/auto_commit.py
- .github/workflows/auto-pr-detailed.yml
- .github/workflows/kanban-auto-commit.yml

This fix enables automatic PR creation when code is pushed to develop, feature/*, or fix/* branches."
echo.

echo Commit created!
echo.

echo ============================================================
echo Finding q_mini_wasm_v2 branch...
echo ============================================================

for /f "tokens=*" %%i in ('git rev-parse HEAD') do set COMMIT_HASH=%%i
echo Commit hash: %COMMIT_HASH%
echo.

echo Checking worktree list for q_mini_wasm_v2...
git worktree list
echo.

echo ============================================================
echo Instructions for cherry-pick:
echo ============================================================
echo.
echo 1. Find where q_mini_wasm_v2 is checked out from the worktree list above
echo 2. Navigate to that directory
echo 3. Run: git stash push -u -m "kanban-pre-cherry-pick" (if there are uncommitted changes)
echo 4. Run: git cherry-pick %COMMIT_HASH%
echo 5. If conflicts occur, resolve them and run: git cherry-pick --continue
echo 6. Run: git stash pop (if you stashed changes)
echo 7. If stash pop has conflicts, resolve them
echo.
echo Commit hash to cherry-pick: %COMMIT_HASH%
echo.

pause