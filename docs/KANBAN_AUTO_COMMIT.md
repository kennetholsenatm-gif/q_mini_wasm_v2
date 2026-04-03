# Kanban Auto-Commit Automation

This automation allows you to automatically generate and create commit messages when you hit the "Open PR" button on your Kanban board.

## Overview

The system consists of two main components:

1. **Auto-Commit Script** (`scripts/auto_commit.py`): Analyzes staged changes and generates well-formed commit messages
2. **GitHub Actions Workflow** (`.github/workflows/kanban-auto-commit.yml`): Can be triggered from the Kanban board button

## How It Works

When you click the "Open PR" button on your Kanban board:

1. The workflow is triggered via `workflow_dispatch`
2. It checks for staged changes in the repository
3. If changes are found, it runs the auto-commit script
4. The script:
   - Analyzes staged files to determine the type of changes
   - Generates a commit message following project conventions
   - Creates the commit with the generated message
   - Optionally pushes and creates a PR

## Usage

### From GitHub Actions UI

1. Go to **Actions** → **Kanban Auto-Commit**
2. Click **Run workflow**
3. Configure options:
   - **Auto-push**: Push after committing (default: true)
   - **Create PR**: Create a PR after pushing (default: false)
   - **Dry run**: Only generate message, don't commit (default: false)
   - **Repo path**: Path to repository (default: current directory)

### From Kanban Board Button

To integrate with your Kanban board:

1. Add a webhook or button that triggers the workflow
2. Use the GitHub API to trigger the workflow:

```bash
curl -X POST \
  -H "Authorization: token YOUR_GITHUB_TOKEN" \
  -H "Accept: application/vnd.github.v3+json" \
  https://api.github.com/repos/OWNER/REPO/actions/workflows/kanban-auto-commit.yml/dispatches \
  -d '{
    "ref": "main",
    "inputs": {
      "auto_push": "true",
      "create_pr": "false",
      "dry_run": "false"
    }
  }'
```

### Manual Script Usage

You can also run the script directly:

```bash
# Basic usage - commit staged changes
python scripts/auto_commit.py

# Dry run - only generate message
python scripts/auto_commit.py --dry-run

# Auto-push after committing
python scripts/auto_commit.py --auto-push

# Create PR after pushing
python scripts/auto_commit.py --auto-push --create-pr

# JSON output for automation
python scripts/auto_commit.py --json
```

## Commit Message Format

The script generates commit messages following your project's format conventions:

```
type(scope): description
```

**Types:**
- `feat`: New features
- `fix`: Bug fixes
- `docs`: Documentation changes
- `style`: Code style changes
- `refactor`: Code refactoring
- `test`: Test changes
- `chore`: Maintenance tasks
- `quantum`: Quantum-specific changes

**Scopes:**
Automatically determined from file paths:
- `quantum_core`, `dll_bridge`, `go_runtime`, `sycl_accelerator`
- `runtime_engine`, `rag_service`, `test_orchestrator`, `wui_designer`
- `hooks`, `config`

## Examples

### Example 1: Feature Development
```bash
# Stage your changes
git add src/quantum_core/new_feature.cpp

# Run auto-commit
python scripts/auto_commit.py
# Output: feat(quantum_core): add new_feature functionality
```

### Example 2: Documentation Update
```bash
# Stage documentation
git add docs/README.md

# Run auto-commit
python scripts/auto_commit.py
# Output: docs: update documentation in README.md
```

### Example 3: Test Updates
```bash
# Stage test files
git add tests/test_quantum.py

# Run auto-commit
python scripts/auto_commit.py
# Output: test: add/update tests for test_quantum.py
```

### Example 4: Multiple Files
```bash
# Stage multiple files
git add src/core/*.cpp src/core/*.h

# Run auto-commit
python scripts/auto_commit.py
# Output: feat(core): add 5 files with 150 insertions
```

## Integration with Existing PR Automation

This auto-commit system works seamlessly with your existing PR automation:

1. **Auto-commit** creates the commit with a well-formed message
2. **Auto-push** pushes the changes to the remote branch
3. **Existing PR workflow** (`auto-pr-detailed.yml`) can then create or update the PR

## Configuration

### Commit Message Validation

The script validates generated messages against the pattern:
```
^(feat|fix|docs|style|refactor|test|chore|quantum)(\(\w+\))?: .{1,72}$
```

This matches the format defined in `config/hooks.json`.

### File Categorization

Files are automatically categorized based on their paths:

| Category | Patterns |
|----------|----------|
| Source | `.py`, `.cpp`, `.c`, `.h`, `.hpp`, `.go`, `.rs`, `.js`, `.ts` |
| Test | `test`, `tests`, `_test.`, `spec` |
| Docs | `doc`, `docs`, `readme`, `changelog` |
| Config | `.json`, `.yaml`, `.yml`, `.toml`, `.ini`, `.cfg` |
| Build | `makefile`, `cmake`, `dockerfile`, `docker-compose`, `.github/workflows` |

## Troubleshooting

### No Staged Changes
If you get "No staged files to commit":
1. Make sure you've staged your changes with `git add`
2. Check that you're in the correct directory

### Invalid Commit Message
If the generated message doesn't match the required format:
1. The script will show an error with the expected pattern
2. You can manually commit with a properly formatted message

### Permission Issues
If the workflow fails due to permissions:
1. Ensure the `GITHUB_TOKEN` has write permissions
2. Check that the workflow has `contents: write` and `pull-requests: write` permissions

## Future Enhancements

Potential improvements for the automation:

1. **AI-Powered Messages**: Integrate with LLM to generate more descriptive messages
2. **Context Awareness**: Analyze diff content to generate more specific messages
3. **Multi-Commit Support**: Handle multiple staged commits
4. **Custom Templates**: Allow custom commit message templates
5. **Integration with Issue Tracking**: Link commits to Kanban cards/issues