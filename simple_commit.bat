@echo off
echo Starting git operations for task commit...

echo Staging changes...
git add -A

echo Creating commit...
git commit -m "Organize test files: Move test files to tests/ directory and fix CMake configuration"

echo Commit created!
echo.

echo Checking worktree list...
git worktree list --porcelain

echo.
echo Git operations complete!