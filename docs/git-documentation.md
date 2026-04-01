# Git Documentation

This MCP provides comprehensive Git documentation and version control assistance for the QMINIWASM project.

## Tools

### git_status
Shows the current git status of the repository.

**Usage**:
```bash
mcp_git_documentation.py --tool=git_status
```

**Output**:
```
## On branch main
## Your branch is up to date with 'origin/main'.
##
## Untracked files:
##   (use "git add <file>..." to include in what will be committed)
##         .cursor/
##
## nothing added to commit but untracked files present (use "git add" to track)
```

### git_log
Shows commit history with optional limit.

**Usage**:
```bash
mcp_git_documentation.py --tool=git_log --args='{"limit": 5}'
```

**Output**:
```
2fb517110a8fc3034281dfff81e42ad91cf30aeb (HEAD -> main, origin/main, origin/HEAD)
e3b0c44298fc1c149afbf4c8996fb92427ae41e4d3b7c09dfcf4b8c7f4a6e4d3
...
```

### git_diff
Shows changes between commits.

**Usage**:
```bash
mcp_git_documentation.py --tool=git_diff --args='{"from": "HEAD~2", "to": "HEAD"}'
```

**Output**:
```
diff --git a/.cursor/mcp-git-documentation.json b/.cursor/mcp-git-documentation.json
index 1234567..89abcde 100644
--- a/.cursor/mcp-git-documentation.json
+++ b/.cursor/mcp-git-documentation.json
@@ -1,10 +1,10 @@
 {
-  "name": "Git Documentation",
+  "name": "Git Documentation MCP",
   "displayName": "Git Documentation",
   "description": "Provides Git documentation and version control assistance",
```

### git_branch
Lists and manages branches.

**Usage**:
```bash
# List branches
mcp_git_documentation.py --tool=git_branch --args='{"action": "list"}'

# Create branch
mcp_git_documentation.py --tool=git_branch --args='{"action": "create", "branch": "feature/new-feature"}'

# Delete branch
mcp_git_documentation.py --tool=git_branch --args='{"action": "delete", "branch": "feature/new-feature"}'
```

### git_commit
Creates commits with message templates.

**Usage**:
```bash
mcp_git_documentation.py --tool=git_commit --args='{"message": "feat: add new MCP configuration"}'
```

### git_push
Pushes changes to remote.

**Usage**:
```bash
# Push to default remote/branch
mcp_git_documentation.py --tool=git_push

# Push to specific remote/branch
mcp_git_documentation.py --tool=git_push --args='{"remote": "origin", "branch": "feature/new-feature"}'
```

### git_pull
Pulls changes from remote.

**Usage**:
```bash
# Pull from default remote/branch
mcp_git_documentation.py --tool=git_pull

# Pull from specific remote/branch
mcp_git_documentation.py --tool=git_pull --args='{"remote": "origin", "branch": "main"}'
```

## Git Workflow Examples

### Basic Workflow
1. Check status: `git_status`
2. Add files: `git add .`
3. Commit changes: `git_commit "feat: implement new feature"`
4. Push to remote: `git_push`

### Feature Branch Workflow
1. Create branch: `git_branch create feature/new-feature`
2. Make changes
3. Commit changes: `git_commit "feat: implement new feature"`
4. Push branch: `git_push origin feature/new-feature`
5. Create pull request

### Update Workflow
1. Pull changes: `git_pull`
2. Check status: `git_status`
3. Resolve conflicts if any
4. Commit and push

## Git Best Practices

### Commit Messages
- Use conventional commits: `type(scope): description`
- Types: feat, fix, docs, style, refactor, test, chore
- Keep messages concise and descriptive

### Branch Naming
- Use descriptive names: `feature/user-authentication`
- Use prefixes: `feat/`, `fix/`, `docs/`, `chore/`
- Keep names lowercase with hyphens

### Code Review
- Create pull requests for all changes
- Include detailed descriptions
- Request reviews from team members
- Address all feedback before merging

## Troubleshooting

### Common Issues
- **Merge conflicts**: Resolve conflicts manually, then commit
- **Detached HEAD**: Create branch from current state
- **Authentication**: Check SSH keys or credentials
- **Network issues**: Verify remote URL and connectivity

### Useful Commands
- `git status` - Check current status
- `git log --oneline` - View commit history
- `git diff` - Show changes
- `git branch -a` - List all branches
- `git remote -v` - Show remote URLs

## Integration with QMINIWASM

This MCP integrates with the QMINIWASM project by:
- Providing version control for all project files
- Supporting the project's branching strategy
- Enabling collaborative development
- Maintaining code quality through proper commit practices

## Configuration

The MCP configuration is stored in `.cursor/mcp-git-documentation.json` and can be modified to add new tools or change existing ones.

## Dependencies

- Git (version 2.0 or higher)
- Python 3.8 or higher
- Access to the QMINIWASM repository