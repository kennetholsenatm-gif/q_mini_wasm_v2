@echo off
cd /d "C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"
echo Current directory: %CD%
echo.
echo Git status:
git status
echo.
echo Git log (last 5 commits):
git log --oneline -5
echo.
echo Git worktree list:
git worktree list --porcelain
pause