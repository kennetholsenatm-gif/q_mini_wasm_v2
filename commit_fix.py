#!/usr/bin/env python3
import subprocess, os, sys

def run(cmd, cwd=None):
    try:
        r = subprocess.run(cmd if isinstance(cmd, list) else cmd.split(), 
                          cwd=cwd, capture_output=True, text=True)
        return r.stdout.strip(), r.stderr.strip(), r.returncode
    except Exception as e:
        return "", str(e), 1

def main():
    wp = r"C:\Users\kenne\.cline\worktrees\b7e49\q_mini_wasm_v2"
    os.chdir(wp)
    
    # Commit message
    msg = """fix: PR auto-commit investigation and fixes

Issues Fixed:
1. Missing scripts - Created generate_pr_part1.py
2. Incomplete implementation - Fixed generate_detailed_pr.py
3. Workflow configuration - Updated kanban-auto-commit.yml
4. Auto-commit script - Updated auto_commit.py

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
- .github/workflows/kanban-auto-commit.yml"""
    
    # Step 1: Stage and commit
    run(["git", "add", "-A"])
    out, err, code = run(["git", "commit", "-m", msg])
    if code != 0:
        print(f"Commit failed: {err}")
        return
    
    out, _, _ = run(["git", "rev-parse", "HEAD"])
    commit_hash = out
    print(f"Commit hash: {commit_hash}")
    
    # Step 2: Find q_mini_wasm_v2
    out, _, _ = run(["git", "worktree", "list", "--porcelain"])
    lines = out.split('\n')
    q_path = wp  # default to current
    for i, line in enumerate(lines):
        if 'refs/heads/q_mini_wasm_v2' in line:
            for j in range(i-1, -1, -1):
                if lines[j].startswith('worktree '):
                    q_path = lines[j][9:]
                    break
    
    # Step 3: Verify branch
    out, _, _ = run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=q_path)
    if out != "q_mini_wasm_v2":
        run(["git", "checkout", "q_mini_wasm_v2"], cwd=q_path)
    
    # Step 4: Stash if needed
    out, _, _ = run(["git", "status", "--short"], cwd=q_path)
    stash_used = False
    if out.strip():
        run(["git", "stash", "push", "-u", "-m", "kanban-pre-cherry-pick"], cwd=q_path)
        stash_used = True
    
    # Step 5: Cherry-pick
    out, err, code = run(["git", "cherry-pick", commit_hash], cwd=q_path)
    conflicts = (code != 0)
    
    # Step 6: Restore stash
    if stash_used:
        out, err, code = run(["git", "stash", "pop"], cwd=q_path)
        if code != 0:
            conflicts = True
    
    # Report
    print(f"\nFINAL REPORT")
    print(f"Commit hash: {commit_hash}")
    print(f"Stash used: {stash_used}")
    print(f"Conflicts: {conflicts}")

if __name__ == '__main__':
    main()