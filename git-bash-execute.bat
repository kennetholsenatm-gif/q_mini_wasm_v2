@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Wiki Population - Git Bash Execution
echo ========================================
echo.

REM Use Git Bash to execute commands
set GIT_BASH="C:\Program Files\Git\bin\bash.exe"

if not exist %GIT_BASH% (
    echo Git Bash not found at default location
    echo Trying to find Git Bash...
    for /f "tokens=*" %%i in ('where bash 2^>nul') do set GIT_BASH="%%i"
)

echo Using Git Bash: %GIT_BASH%
echo.

REM Step 1: Stage and commit changes
echo Step 1: Staging and committing changes...
%GIT_BASH% -c "git add Home.md _Sidebar.md generate_wiki_simple.py generate-wiki.ps1 run-wiki-generator.bat WIKI_GUIDE.md SOLUTION_SUMMARY.md WINDOWS_ENVIRONMENT.md WINDOWS_EXECUTION_GUIDE.md"

%GIT_BASH% -c "git diff --cached --quiet"
if !errorlevel! equ 0 (
    echo No changes to commit
    for /f "tokens=*" %%i in ('%GIT_BASH% -c "git rev-parse HEAD"') do set TASK_COMMIT=%%i
) else (
    echo Committing changes...
    %GIT_BASH% -c "git commit -m 'docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add WINDOWS_ENVIRONMENT.md for Windows environment
- Add WINDOWS_EXECUTION_GUIDE.md for Windows users'"

    for /f "tokens=*" %%i in ('%GIT_BASH% -c "git rev-parse HEAD"') do set TASK_COMMIT=%%i
)

echo Task commit: !TASK_COMMIT!
echo.

REM Step 2: Find q_mini_wasm_v2 branch
echo Step 2: Finding q_mini_wasm_v2 branch...
%GIT_BASH% -c "git worktree list --porcelain" > worktree_list.txt

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
    %GIT_BASH% -c "git diff --quiet"
    set HAS_DIFF=!errorlevel!
    %GIT_BASH% -c "git diff --cached --quiet"
    set HAS_CACHED=!errorlevel!
    
    if !HAS_DIFF! neq 0 goto :do_stash
    if !HAS_CACHED! neq 0 goto :do_stash
    goto :checkout_branch
    
    :do_stash
    echo Step 4: Stashing uncommitted changes...
    %GIT_BASH% -c "git stash push -u -m 'kanban-pre-cherry-pick'"
    set STASH_CREATED=true
    echo Stash created
    
    :checkout_branch
    echo Step 3: Checking out q_mini_wasm_v2...
    %GIT_BASH% -c "git checkout q_mini_wasm_v2"
) else (
    echo Found worktree: !WORKTREE_PATH!
    cd /d "!WORKTREE_PATH!"
    
    REM Verify branch
    for /f "tokens=*" %%i in ('%GIT_BASH% -c "git branch --show-current"') do set CURRENT_BRANCH=%%i
    if not "!CURRENT_BRANCH!"=="q_mini_wasm_v2" (
        echo ERROR: Not on q_mini_wasm_v2 branch
        exit /b 1
    )
    
    REM Step 4: Stash if needed
    %GIT_BASH% -c "git diff --quiet"
    set HAS_DIFF=!errorlevel!
    %GIT_BASH% -c "git diff --cached --quiet"
    set HAS_CACHED=!errorlevel!
    
    if !HAS_DIFF! neq 0 goto :do_stash2
    if !HAS_CACHED! neq 0 goto :do_stash2
    goto :cherry_pick
    
    :do_stash2
    echo Step 4: Stashing uncommitted changes...
    %GIT_BASH% -c "git stash push -u -m 'kanban-pre-cherry-pick'"
    set STASH_CREATED=true
    echo Stash created
)

:cherry_pick
echo.
echo Step 5: Cherry-picking commit: !TASK_COMMIT!
%GIT_BASH% -c "git cherry-pick !TASK_COMMIT!"
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
    %GIT_BASH% -c "git stash pop"
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
for /f "tokens=*" %%i in ('%GIT_BASH% -c "git rev-parse HEAD"') do set FINAL_HASH=%%i
for /f "tokens=*" %%i in ('%GIT_BASH% -c "git log -1 --pretty=format:'%%s'"') do set FINAL_MSG=%%i
for /f "tokens=*" %%i in ('%GIT_BASH% -c "git branch --show-current"') do set FINAL_BRANCH=%%i

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