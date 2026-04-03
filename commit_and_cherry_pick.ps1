# Git operations script for committing and cherry-picking

Write-Host "=== Step 1: Check current status ==="
git status

Write-Host "`n=== Step 2: Stage all changes ==="
git add -A

Write-Host "`n=== Step 3: Create commit ==="
$commitMessage = @"
feat: Add automatic AI agent recompilation at CI/CD pipeline completion

- Created recompile_agents_simple.py script for agent recompilation
- Created agent-recompilation.yml reusable workflow
- Created agent-recompilation-trigger.yml workflow for automatic triggering
- Agents are recompiled after CI, Continuous Improvement, Agent Automation, and Production Build workflows

Files added:
- scripts/recompile_agents_simple.py
- .github/workflows/agent-recompilation.yml
- .github/workflows/agent-recompilation-trigger.yml
"@

git commit -m $commitMessage

Write-Host "`n=== Step 4: Get commit hash ==="
$commitHash = git rev-parse HEAD
Write-Host "Commit hash: $commitHash"

Write-Host "`n=== Step 5: Find q_mini_wasm_v2 worktree ==="
git worktree list --porcelain

Write-Host "`n=== Script completed ==="
Write-Host "Commit hash: $commitHash"