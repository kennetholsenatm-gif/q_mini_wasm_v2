#!/usr/bin/env python3
"""
Git operations script for committing and cherry-picking
"""

import subprocess
import sys
import os

def run_command(cmd, cwd=None):
    """Run a command and return output."""
    print(f"Running: {cmd}")
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        print(result.stdout)
        if result.stderr:
            print(f"STDERR: {result.stderr}")
        return result.returncode == 0, result.stdout, result.stderr
    except Exception as e:
        print(f"Error running command: {e}")
        return False, "", str(e)

def main():
    """Main function."""
    print("=== Step 1: Check current status ===")
    run_command("git status")
    
    print("\n=== Step 2: Stage all changes ===")
    run_command("git add -A")
    
    print("\n=== Step 3: Create commit ===")
    commit_message = """feat: Add automatic AI agent recompilation at CI/CD pipeline completion

- Created recompile_agents_simple.py script for agent recompilation
- Created agent-recompilation.yml reusable workflow
- Created agent-recompilation-trigger.yml workflow for automatic triggering
- Agents are recompiled after CI, Continuous Improvement, Agent Automation, and Production Build workflows

Files added:
- scripts/recompile_agents_simple.py
- .github/workflows/agent-recompilation.yml
- .github/workflows/agent-recompilation-trigger.yml"""
    
    # Escape quotes in commit message for shell
    escaped_message = commit_message.replace('"', '\\"').replace('\n', '\\n')
    run_command(f'git commit -m "{escaped_message}"')
    
    print("\n=== Step 4: Get commit hash ===")
    success, stdout, stderr = run_command("git rev-parse HEAD")
    commit_hash = stdout.strip() if success else "unknown"
    print(f"Commit hash: {commit_hash}")
    
    print("\n=== Step 5: Find q_mini_wasm_v2 worktree ===")
    run_command("git worktree list --porcelain")
    
    print("\n=== Script completed ===")
    print(f"Commit hash: {commit_hash}")
    
    return commit_hash

if __name__ == "__main__":
    main()