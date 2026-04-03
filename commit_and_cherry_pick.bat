@echo off
echo === Step 1: Check current status ===
git status

echo.
echo === Step 2: Stage all changes ===
git add -A

echo.
echo === Step 3: Create commit ===
git commit -m "feat: Add automatic AI agent recompilation at CI/CD pipeline completion" -m "- Created recompile_agents_simple.py script for agent recompilation" -m "- Created agent-recompilation.yml reusable workflow" -m "- Created agent-recompilation-trigger.yml workflow for automatic triggering" -m "- Agents are recompiled after CI, Continuous Improvement, Agent Automation, and Production Build workflows" -m "" -m "Files added:" -m "- scripts/recompile_agents_simple.py" -m "- .github/workflows/agent-recompilation.yml" -m "- .github/workflows/agent-recompilation-trigger.yml"

echo.
echo === Step 4: Get commit hash ===
for /f "tokens=*" %%i in ('git rev-parse HEAD') do set COMMIT_HASH=%%i
echo Commit hash: %COMMIT_HASH%

echo.
echo === Step 5: Find q_mini_wasm_v2 worktree ===
git worktree list --porcelain

echo.
echo === Script completed ===
echo Commit hash: %COMMIT_HASH%