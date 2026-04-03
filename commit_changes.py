import subprocess
import sys

# Run git status
print("=== Git Status ===")
result = subprocess.run(["git", "status"], capture_output=True, text=True)
print(result.stdout)

# Stage all changes
print("\n=== Staging all changes ===")
subprocess.run(["git", "add", "-A"])

# Create commit
print("\n=== Creating commit ===")
commit_msg = """feat: Add automatic AI agent recompilation at CI/CD pipeline completion

- Created recompile_agents_simple.py script for agent recompilation
- Created agent-recompilation.yml reusable workflow
- Created agent-recompilation-trigger.yml workflow for automatic triggering
- Agents are recompiled after CI, Continuous Improvement, Agent Automation, and Production Build workflows

Files added:
- scripts/recompile_agents_simple.py
- .github/workflows/agent-recompilation.yml
- .github/workflows/agent-recompilation-trigger.yml"""

result = subprocess.run(["git", "commit", "-m", commit_msg], capture_output=True, text=True)
print(result.stdout)
if result.stderr:
    print(f"STDERR: {result.stderr}")

# Get commit hash
print("\n=== Getting commit hash ===")
result = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
commit_hash = result.stdout.strip()
print(f"Commit hash: {commit_hash}")

# List worktrees
print("\n=== Listing worktrees ===")
result = subprocess.run(["git", "worktree", "list", "--porcelain"], capture_output=True, text=True)
print(result.stdout)

print(f"\n=== Done ===")
print(f"Commit hash: {commit_hash}")