#!/usr/bin/env python3
import subprocess
import sys
import os

def run(cmd, cwd=None):
    """Run a command and return output."""
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
        return result.stdout.strip(), result.stderr.strip(), result.returncode
    except Exception as e:
        return "", str(e), 1

def main():
    worktree_path = r"C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"
    os.chdir(worktree_path)
    
    print("=" * 60)
    print("Step 1: Check current status")
    print("=" * 60)
    
    out, err, code = run("git status --short")
    print(f"Git status:\n{out if out else '(clean)'}")
    
    out, err, code = run("git rev-parse --abbrev-ref HEAD")
    print(f"\nCurrent branch: {out}")
    
    print("\n" + "=" * 60)
    print("Step 2: Stage and commit changes")
    print("=" * 60)
    
    # Stage all changes
    out, err, code = run("git add -A")
    print("Staged all changes")
    
    # Check what's staged
    out, err, code = run("git diff --cached --stat")
    print(f"\nStaged changes:\n{out if out else 'No changes staged'}")
    
    # Create commit
    commit_msg = """fix: PR auto-commit investigation and fixes

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
- SOLUTION_SUMMARY.md

Files Modified:
- scripts/generate_detailed_pr.py
- scripts/generate_pr.py
- scripts/auto_commit.py
- .github/workflows/auto-pr-detailed.yml
- .github/workflows/kanban-auto-commit.yml

This fix enables automatic PR creation when code is pushed to develop, feature/*, or fix/* branches."""
    
    out, err, code = run(f'git commit -m "{commit_msg}"')
    if code == 0:
        print(f"\nCommit created successfully!")
    else:
        print(f"\nCommit output: {out}")
        print(f"Commit error: {err}")
    
    # Get commit hash
    out, err, code = run("git rev-parse HEAD")
    commit_hash = out
    print(f"\nCommit hash: {commit_hash}")
    
    print("\n" + "=" * 60)
    print("Step 3: Find q_mini_wasm_v2 worktree location")
    print("=" * 60)
    
    out, err, code = run("git worktree list --porcelain")
    print(f"Worktree list:\n{out}")
    
    # Parse worktree list
    lines = out.split('\n')
    q_mini_path = None
    for i, line in enumerate(lines):
        if line.startswith('branch refs/heads/q_mini_wasm_v2'):
            # Look for worktree path before this
            for j in range(i-1, -1, -1):
                if lines[j].startswith('worktree '):
                    q_mini_path = lines[j].replace('worktree ', '')
                    break
    
    if not q_mini_path:
        # q_mini_wasm_v2 not checked out anywhere, use current worktree
        q_mini_path = worktree_path
        print(f"q_mini_wasm_v2 not checked out elsewhere, using current worktree: {q_mini_path}")
    else:
        print(f"Found q_mini_wasm_v2 at: {q_mini_path}")
    
    print("\n" + "=" * 60)
    print("Step 4: Check for uncommitted changes in target worktree")
    print("=" * 60)
    
    out, err, code = run("git status --short", cwd=q_mini_path)
    has_changes = bool(out.strip())
    print(f"Uncommitted changes in {q_mini_path}: {has_changes}")
    if has_changes:
        print(f"Changes:\n{out}")
    
    stash_used = False
    if has_changes:
        print("\nStashing changes...")
        out, err, code = run('git stash push -u -m "kanban-pre-cherry-pick"', cwd=q_mini_path)
        print(f"Stash output: {out}")
        stash_used = True
    
    print("\n" + "=" * 60)
    print("Step 5: Cherry-pick commit")
    print("=" * 60)
    
    # First, checkout q_mini_wasm_v2 if not already on it
    out, err, code = run("git rev-parse --abbrev-ref HEAD", cwd=q_mini_path)
    if out != "q_mini_wasm_v2":
        print(f"Checking out q_mini_wasm_v2 branch...")
        out, err, code = run("git checkout q_mini_wasm_v2", cwd=q_mini_path)
        print(f"Checkout output: {out}")
    
    # Cherry-pick
    out, err, code = run(f"git cherry-pick {commit_hash}", cwd=q_mini_path)
    conflicts = False
    if code != 0:
        print(f"Cherry-pick had issues: {err}")
        conflicts = True
    else:
        print(f"Cherry-pick successful!")
    
    print("\n" + "=" * 60)
    print("Step 6: Restore stash if used")
    print("=" * 60)
    
    if stash_used:
        out, err, code = run("git stash pop", cwd=q_mini_path)
        if code != 0:
            print(f"Stash pop had conflicts: {err}")
            conflicts = True
        else:
            print(f"Stash restored successfully!")
    
    print("\n" + "=" * 60)
    print("FINAL REPORT")
    print("=" * 60)
    print(f"Final commit hash: {commit_hash}")
    print(f"Final commit message: fix: PR auto-commit investigation and fixes")
    print(f"Stash used: {stash_used}")
    print(f"Conflicts resolved: {conflicts}")
    print(f"Remaining manual follow-up: {'Resolve any conflicts if present' if conflicts else 'None'}")

if __name__ == '__main__':
    main()