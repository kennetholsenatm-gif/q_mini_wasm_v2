#!/usr/bin/env python3
import subprocess
import sys

def run_git_command(args):
    """Run a git command and return the result."""
    cmd = ["git"] + args
    print(f"Running: {' '.join(cmd)}")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(f"STDERR: {result.stderr}")
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        print(f"Error: {e}")
        return -1, "", str(e)

if __name__ == "__main__":
    # Step 1: Check status
    print("=== Step 1: Check current status ===")
    run_git_command(["status"])
    
    # Step 2: Stage all changes
    print("\n=== Step 2: Stage all changes ===")
    run_git_command(["add", "-A"])
    
    # Step 3: Create commit
    print("\n=== Step 3: Create commit ===")
    commit_msg = """feat: Add automatic AI agent recompilation at CI/CD pipeline completion

- Created recompile_agents_simple.py script for agent recompilation
- Created agent-recompilation.yml reusable workflow
- Created agent-recompilation-trigger.yml workflow for automatic triggering
- Agents are recompiled after CI, Continuous Improvement, Agent Automation, and Production Build workflows

Files added:
- scripts/recompile_agents_simple.py
- .github/workflows/agent-recompilation.yml
- .github/workflows/agent-recompilation-trigger.yml"""
    
    run_git_command(["commit", "-m", commit_msg])
    
    # Step 4: Get commit hash
    print("\n=== Step 4: Get commit hash ===")
    _, stdout, _ = run_git_command(["rev-parse", "HEAD"])
    commit_hash = stdout.strip()
    print(f"Commit hash: {commit_hash}")
    
    # Step 5: List worktrees
    print("\n=== Step 5: List worktrees ===")
    run_git_command(["worktree", "list", "--porcelain"])
    
    print(f"\n=== Done ===")
    print(f"Commit hash: {commit_hash}")