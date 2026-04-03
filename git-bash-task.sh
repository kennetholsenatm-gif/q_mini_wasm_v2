#!/bin/bash
# Wiki Population Task - Git Bash Execution
# Run this script from Git Bash

set -e

echo "========================================"
echo "Wiki Population Task - Git Bash"
echo "========================================"
echo

# Step 1: Stage and commit changes
echo "Step 1: Staging and committing changes..."
git add Home.md _Sidebar.md
git add generate_wiki_simple.py
git add generate-wiki.ps1
git add run-wiki-generator.bat
git add WIKI_GUIDE.md
git add SOLUTION_SUMMARY.md
git add WINDOWS_ENVIRONMENT.md
git add WINDOWS_EXECUTION_GUIDE.md
git add git-bash-execute.bat
git add git-bash-task.sh

# Check if there are changes
if git diff --cached --quiet; then
    echo "No changes to commit"
    TASK_COMMIT=$(git rev-parse HEAD)
else
    echo "Committing changes..."
    git commit -m "docs: Add wiki population files and documentation

- Add Home.md and _Sidebar.md for GitHub Wiki
- Add generate_wiki_simple.py script to generate wiki from docs/
- Add generate-wiki.ps1 PowerShell script for Windows
- Add run-wiki-generator.bat for easy execution
- Add WIKI_GUIDE.md with detailed instructions
- Add SOLUTION_SUMMARY.md explaining the wiki population issue
- Add WINDOWS_ENVIRONMENT.md for Windows environment
- Add WINDOWS_EXECUTION_GUIDE.md for Windows users
- Add git-bash-execute.bat for Git Bash execution
- Add git-bash-task.sh for Git Bash script"
    TASK_COMMIT=$(git rev-parse HEAD)
fi

echo "Task commit: $TASK_COMMIT"

# Step 2: Find q_mini_wasm_v2 branch
echo
echo "Step 2: Finding q_mini_wasm_v2 branch..."
WORKTREE_PATH=$(git worktree list --porcelain | grep -B1 "branch refs/heads/q_mini_wasm_v2" | grep "worktree" | cut -d' ' -f2)

STASH_CREATED=false

if [ -z "$WORKTREE_PATH" ]; then
    echo "q_mini_wasm_v2 not checked out, using current worktree"
    
    # Check for uncommitted changes
    if ! git diff --quiet || ! git diff --cached --quiet; then
        echo "Step 4: Stashing uncommitted changes..."
        git stash push -u -m "kanban-pre-cherry-pick"
        STASH_CREATED=true
        echo "Stash created"
    fi
    
    # Checkout q_mini_wasm_v2
    echo "Step 3: Checking out q_mini_wasm_v2..."
    git checkout q_mini_wasm_v2
else
    echo "Found worktree: $WORKTREE_PATH"
    cd "$WORKTREE_PATH"
    
    # Verify branch
    if [ "$(git branch --show-current)" != "q_mini_wasm_v2" ]; then
        echo "ERROR: Not on q_mini_wasm_v2 branch"
        exit 1
    fi
    
    # Check for uncommitted changes
    if ! git diff --quiet || ! git diff --cached --quiet; then
        echo "Step 4: Stashing uncommitted changes..."
        git stash push -u -m "kanban-pre-cherry-pick"
        STASH_CREATED=true
        echo "Stash created"
    fi
fi

# Step 5: Cherry-pick
echo
echo "Step 5: Cherry-picking commit..."
if git cherry-pick "$TASK_COMMIT"; then
    echo "Cherry-pick successful"
else
    echo "Cherry-pick failed - conflicts detected"
    echo "Please resolve conflicts, then run:"
    echo "  git add <resolved-files>"
    echo "  git cherry-pick --continue"
    exit 1
fi

# Step 7: Restore stash
if [ "$STASH_CREATED" = true ]; then
    echo
    echo "Step 7: Restoring stash..."
    if git stash pop; then
        echo "Stash restored"
    else
        echo "Stash pop failed - conflicts detected"
        echo "Please resolve conflicts, then run:"
        echo "  git add <resolved-files>"
        echo "  git stash drop"
        exit 1
    fi
fi

# Final report
echo
echo "========================================"
echo "FINAL REPORT"
echo "========================================"
echo "Final commit hash: $(git rev-parse HEAD)"
echo "Final commit message: $(git log -1 --pretty=format:'%s')"
echo "Stash used: $STASH_CREATED"
echo "Conflicts resolved: No"
echo "Branch: $(git branch --show-current)"
echo
echo "SUCCESS: Changes committed to q_mini_wasm_v2"
echo
echo "Next steps:"
echo "1. Review: git log -1 --stat"
echo "2. Push: git push origin q_mini_wasm_v2"
echo "3. Generate wiki: python generate_wiki_simple.py"
echo "========================================"