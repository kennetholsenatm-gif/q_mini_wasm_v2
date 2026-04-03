#!/usr/bin/env python3
"""
Script to check git status and commit changes
"""

import subprocess
import sys
import os
from pathlib import Path


def run_command(cmd, cwd=None):
    """Run a command and return output."""
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            shell=True
        )
        return result.stdout.strip(), result.stderr.strip(), result.returncode
    except Exception as e:
        return "", str(e), 1


def main():
    worktree_path = r"C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"
    
    print(f"Working directory: {worktree_path}")
    print("=" * 60)
    
    # Change to worktree directory
    os.chdir(worktree_path)
    
    # Check git status
    print("\n1. Git status:")
    stdout, stderr, code = run_command("git status")
    print(stdout)
    if stderr:
        print(f"Error: {stderr}")
    
    # Check current branch
    print("\n2. Current branch:")
    stdout, stderr, code = run_command("git branch --show-current")
    print(f"Branch: {stdout}")
    
    # Check if detached HEAD
    print("\n3. Check if detached HEAD:")
    stdout, stderr, code = run_command("git symbolic-ref --short HEAD 2>/dev/null || echo 'DETACHED'")
    print(f"Result: {stdout}")
    
    # List modified files
    print("\n4. Modified files:")
    stdout, stderr, code = run_command("git diff --name-only")
    print(stdout if stdout else "No modified files")
    
    # List untracked files
    print("\n5. Untracked files:")
    stdout, stderr, code = run_command("git ls-files --others --exclude-standard")
    print(stdout if stdout else "No untracked files")
    
    # Check worktree list
    print("\n6. Git worktree list:")
    stdout, stderr, code = run_command("git worktree list --porcelain")
    print(stdout)
    
    # Check git log
    print("\n7. Recent commits:")
    stdout, stderr, code = run_command("git log --oneline -5")
    print(stdout)
    
    print("\n" + "=" * 60)
    print("Analysis complete")


if __name__ == '__main__':
    main()