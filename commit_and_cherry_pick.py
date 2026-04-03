#!/usr/bin/env python3
"""
Script to commit changes and cherry-pick into q_mini_wasm_v2 branch
"""

import subprocess
import sys
import os
from pathlib import Path


def run_git(cmd, cwd=None):
    """Run git command and return output."""
    try:
        result = subprocess.run(
            ["git"] + cmd if isinstance(cmd, list) else cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            shell=True if isinstance(cmd, str) else False
        )
        return result.stdout.strip(), result.stderr.strip(), result.returncode
    except Exception as e:
        return "", str(e), 1


def main():
    # Current worktree path
    worktree_path = r"C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"
    
    print("=" * 70)
    print("PR Auto-Commit Investigation - Commit and Merge Script")
    print("=" * 70)
    
    # Change to worktree directory
    os.chdir(worktree_path)
    print(f"Working directory: {os.getcwd()}")
    
    # Step 1: Check current status
    print("\n1. Checking git status...")
    stdout, stderr, code = run_git(["status"])
    print(stdout)
    
    # Step 2: Check current branch/HEAD
    print("\n2. Checking current branch...")
    stdout, stderr, code = run_git(["rev-parse", "--abbrev-ref", "HEAD"])
    current_branch = stdout
    print(f"Current branch: {current_branch}")
    
    # Check if detached HEAD
    stdout, stderr, code = run_git(["symbolic-ref", "--short", "HEAD"])
    is_detached = (code != 0)
    print(f"Detached HEAD: {is_detached}")
    
    # Step 3: List all modified and new files
    print("\n3. Listing changes...")
    
    # Get modified files
    stdout, stderr, code = run_git(["diff", "--name-only"])
    modified_files = stdout.split('\n') if stdout else []
    
    # Get untracked files
    stdout, stderr, code = run_git(["ls-files", "--others", "--exclude-standard"])
    untracked_files = stdout.split('\n') if stdout else []
    
    # Get staged files
    stdout, stderr, code = run_git(["diff", "--cached", "--name-only"])
    staged_files = stdout.split('\n') if stdout else []
    
    print(f"Modified files: {len(modified_files)}")
    print(f"Untracked files: {len(untracked_files)}")
    print(f"Staged files: {len(staged_files)}")
    
    # List the files I created/modified
    print("\n4. Files created/modified in this investigation:")
    
    expected_files = [
        "scripts/generate_pr_part1.py",
        "PR_INVESTIGATION_SUMMARY.md",
        "PR_FIX_SUMMARY.md",
        "PR_FIX_COMPLETE.md",
        "PR_AUTO_COMMIT_INVESTIGATION.md",
        "check_git_status.bat",
        "check_status.py",
        "commit_and_cherry_pick.py"
    ]
    
    for f in expected_files:
        full_path = os.path.join(worktree_path, f)
        if os.path.exists(full_path):
            print(f"  ✓ {f}")
        else:
            print(f"  ✗ {f} (not found)")
    
    # Step 5: Check worktree list
    print("\n5. Git worktree list:")
    stdout, stderr, code = run_git(["worktree", "list", "--porcelain"])
    print(stdout)
    
    # Step 6: Find where q_mini_wasm_v2 is checked out
    print("\n6. Finding q_mini_wasm_v2 branch location...")
    stdout, stderr, code = run_git(["worktree", "list"])
    print(stdout)
    
    # Parse worktree list to find q_mini_wasm_v2
    lines = stdout.split('\n')
    q_mini_path = None
    for i, line in enumerate(lines):
        if 'q_mini_wasm_v2' in line:
            # The path is on the previous line
            if i > 0 and lines[i-1].startswith('worktree '):
                q_mini_path = lines[i-1].split('worktree ')[1]
                print(f"Found q_mini_wasm_v2 at: {q_mini_path}")
    
    # Step 7: Create a summary of changes
    print("\n7. Creating commit message...")
    
    commit_message = """fix: PR auto-commit investigation and fixes

Issues Fixed:
1. Missing scripts - Created generate_pr_part1.py with core git analysis functions
2. Incomplete implementation - Fixed generate_detailed_pr.py (removed duplicates, added missing methods)
3. Workflow configuration - Updated kanban-auto-commit.yml to auto-trigger and create PRs
4. Auto-commit script - Updated auto_commit.py to properly create PRs using GitHub CLI

Files Created:
- scripts/generate_pr_part1.py
- PR_INVESTIGATION_SUMMARY.md
- PR_FIX_SUMMARY.md
- PR_FIX_COMPLETE.md

Files Modified:
- scripts/generate_detailed_pr.py
- scripts/generate_pr.py
- scripts/auto_commit.py
- .github/workflows/auto-pr-detailed.yml
- .github/workflows/kanban-auto-commit.yml

This fix enables automatic PR creation when code is pushed to develop, feature/*, or fix/* branches.
"""
    
    print("Commit message prepared.")
    print("\n" + "=" * 70)
    print("Summary:")
    print("=" * 70)
    print(f"Current worktree: {worktree_path}")
    print(f"Current branch/HEAD: {current_branch}")
    print(f"Detached HEAD: {is_detached}")
    print(f"Files to commit: {len(expected_files)}")
    print("\nNext steps:")
    print("1. Stage all changes: git add -A")
    print("2. Create commit with the prepared message")
    print("3. Find q_mini_wasm_v2 branch and cherry-pick the commit")
    print("4. Handle any conflicts if they arise")
    print("=" * 70)


if __name__ == '__main__':
    main()