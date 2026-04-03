@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Wiki Population Task - Commit to q_mini_wasm_v2
echo ========================================
echo.

REM Step 1: Stage and commit changes in current worktree
echo Step 1: Staging and committing changes...
echo.

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
git add FINAL_SUMMARY.md
git add simple-execute.bat

git diff --cached --quiet
if !errorlevel! equ 0 (
    echo No changes to commit
    for /f "tokens=*" %%i in ('git rev-parse HEAD') do set TASK_COMMIT=%%i
) else (
    git commit -m "docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add create-pr.sh for automated PR creation
- Add commit-wiki-changes.sh for git operations
- Add commit-wiki-changes.ps1 for Windows PowerShell
- Add execute-task.bat for task execution
- Add run-task.sh for bash execution
- Add TASK_COMPLETION_SUMMARY.md for documentation

These files address the issue of an empty GitHub Wiki despite having
extensive research documentation in docs/research/ directory."

    for /f "tokens=*" %%i in ('git rev-parse HEAD') do set TASK_COMMIT=%%i
)

echo Task commit: !TASK_COMMIT!
echo.

REM Step 2: Find where q_mini_wasm_v2 is checked out
echo Step 2: Finding q_mini_wasm_v2 branch...
echo.

git worktree list --porcelain > worktree_list.txt 2>&1

set WORKTREE_PATH=
set STASH_CREATED=false

for /f "tokens=1,* delims= " %%a in (worktree_list.txt) do (
    if "%%a"=="worktree" (
        set CURRENT_WT=%%b
    )
    if "%%b"=="refs/heads/q_mini_wasm_v2" (
        set WORKTREE_PATH=!CURRENT_WT!
    )
)

del worktree_list.txt

if "!WORKTREE_PATH!"=="" (
    echo q_mini_wasm_v2 not checked out in any worktree
    echo Using current worktree
    
    REM Step 4: Stash if needed
    git diff --quiet
    set HAS_DIFF=!errorlevel!
    git diff --cached --quiet
    set HAS_CACHED=!errorlevel!
    
    if !HAS_DIFF! neq 0 goto :do_stash
    if !HAS_CACHED! neq 0 goto :do_stash
    goto :checkout_branch
    
    :do_stash
    echo Step 4: Stashing uncommitted changes...
    git stash push -u -m "kanban-pre-cherry-pick"
    set STASH_CREATED=true
    echo Stash created
    
    :checkout_branch
    echo Step 3: Checking out q_mini_wasm_v2...
    git checkout q_mini_wasm_v2
) else (
    echo Found worktree: !WORKTREE_PATH!
    cd /d "!WORKTREE_PATH!"
    
    REM Verify branch
    for /f "tokens=*" %%i in ('git branch --show-current') do set CURRENT_BRANCH=%%i
    if not "!CURRENT_BRANCH!"=="q_mini_wasm_v2" (
        echo ERROR: Not on q_mini_wasm_v2 branch
        exit /b 1
    )
    
    REM Step 4: Stash if needed
    git diff --quiet
    set HAS_DIFF=!errorlevel!
    git diff --cached --quiet
    set HAS_CACHED=!errorlevel!
    
    if !HAS_DIFF! neq 0 goto :do_stash2
    if !HAS_CACHED! neq 0 goto :do_stash2
    goto :cherry_pick
    
    :do_stash2
    echo Step 4: Stashing uncommitted changes...
    git stash push -u -m "kanban-pre-cherry-pick"
    set STASH_CREATED=true
    echo Stash created
)

:cherry_pick
echo.
echo Step 5: Cherry-picking commit: !TASK_COMMIT!
git cherry-pick !TASK_COMMIT!
if !errorlevel! neq 0 (
    echo ERROR: Cherry-pick failed with conflicts
    echo Please resolve conflicts manually, then run:
    echo   git add ^<resolved-files^>
    echo   git cherry-pick --continue
    exit /b 1
)
echo Cherry-pick successful

REM Step 7: Restore stash if created
if "!STASH_CREATED!"=="true" (
    echo.
    echo Step 7: Restoring stash...
    git stash pop
    if !errorlevel! neq 0 (
        echo WARNING: Stash pop had conflicts
        echo Please resolve conflicts manually, then run:
        echo   git add ^<resolved-files^>
        echo   git stash drop
    ) else (
        echo Stash restored successfully
    )
)

REM Final report
echo.
echo ========================================
echo FINAL REPORT
echo ========================================
for /f "tokens=*" %%i in ('git rev-parse HEAD') do set FINAL_HASH=%%i
for /f "tokens=*" %%i in ('git log -1 --pretty=format:"%%s"') do set FINAL_MSG=%%i
for /f "tokens=*" %%i in ('git branch --show-current') do set FINAL_BRANCH=%%i

echo Final commit hash: !FINAL_HASH!
echo Final commit message: !FINAL_MSG!
echo Stash used: !STASH_CREATED!
echo Conflicts resolved: No
echo Branch: !FINAL_BRANCH!
echo.
echo SUCCESS: Changes committed to q_mini_wasm_v2
echo.
echo Next steps:
echo 1. Review: git log -1 --stat
echo 2. Push: git push origin q_mini_wasm_v2
echo 3. Generate wiki: python generate_wiki_simple.py
echo ========================================