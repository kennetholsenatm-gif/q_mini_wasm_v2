# Auto PR Process with Detailed Messages

## Overview

This automated PR process generates detailed pull request messages based on git changes. It analyzes commits, file changes, and generates comprehensive PR descriptions with statistics.

## Components

### 1. Scripts

- `scripts/generate_pr_part1.py` - Core git analysis functions
- `scripts/generate_pr_part2.py` - PR body generation functions
- `scripts/generate_pr.py` - Combined script for PR generation
- `scripts/test_pr_generator.py` - Test script

### 2. GitHub Actions Workflow

- `.github/workflows/auto-pr-detailed.yml` - Automated PR creation workflow

## Features

### Detailed PR Body Includes:

1. **Pull Request Summary** - Overview section with commit count
2. **Change Statistics** - Files changed, insertions, deletions
3. **File Changes by Status** - Added, modified, deleted, renamed files
4. **Commit History** - Table with commit hash, message, author, date
5. **Automatic Labels** - `automated`, `detailed-pr`

### PR Title Generation:

- Automatically determines PR type based on branch name:
  - `feature/*` → `feat:`
  - `fix/*` → `fix:`
  - `docs/*` → `docs:`
  - Other → `chore:`

## Usage

### Manual PR Generation

```bash
# Generate markdown PR body
python scripts/generate_pr.py \
  --branch feature/new-feature \
  --base main \
  --output pr-body.md \
  --format markdown \
  --include-stats \
  --include-commits

# Generate JSON output
python scripts/generate_pr.py \
  --branch feature/new-feature \
  --base main \
  --output pr-data.json \
  --format json
```

### GitHub Actions Workflow

The workflow automatically runs on:

1. **Push to branches**: `develop`, `feature/*`, `fix/*`
2. **Pull requests**: To `main` or `develop`
3. **Manual trigger**: With options for base branch and statistics

#### Manual Trigger Options:

- `base_branch`: Base branch for PR (default: main)
- `include_stats`: Include file change statistics (default: true)
- `include_commits`: Include commit history (default: true)

## Example Output

### Markdown PR Body:

```markdown
## 📋 Pull Request Summary

### 🎯 Overview

This PR includes **2 commit(s)** from `feature/new-feature` to `main`.

### 📊 Change Statistics

| Metric | Count |
|--------|-------|
| Files Changed | 4 |
| Insertions | +150 |
| Deletions | -30 |

### 🆕 Added Files

- `new_feature.py`
- `test_feature.py`

### ✏️ Modified Files

- `existing.py`
- `config.json`

### 🗑️ Deleted Files

- `old_file.py`

### 📝 Renamed Files

- `old_name.py → new_name.py`

### 📝 Commit History

| Hash | Message | Author | Date |
|------|---------|--------|------|
| `abc1234` | Add new feature | John | 2024-01-01 |
| `def5678` | Fix bug in module | Jane | 2024-01-02 |

---

*Generated on 2024-01-02 10:30:00*
```

### JSON Output:

```json
{
  "metadata": {
    "generated_at": "2024-01-02T10:30:00"
  },
  "summary": {
    "commits": 2,
    "files_changed": 4,
    "insertions": 150,
    "deletions": 30
  },
  "files": {
    "added": ["new_feature.py", "test_feature.py"],
    "modified": ["existing.py", "config.json"],
    "deleted": ["old_file.py"],
    "renamed": ["old_name.py → new_name.py"]
  },
  "commits": [
    {
      "hash": "abc1234",
      "short_hash": "abc1234",
      "subject": "Add new feature",
      "author": "John",
      "date": "2024-01-01"
    },
    {
      "hash": "def5678",
      "short_hash": "def5678",
      "subject": "Fix bug in module",
      "author": "Jane",
      "date": "2024-01-02"
    }
  ]
}
```

## Configuration

### Labels

The workflow automatically adds labels:
- `automated` - For automated PRs
- `detailed-pr` - For PRs with detailed messages

### Branch Naming Convention

For automatic PR type detection:
- `feature/*` - Feature additions
- `fix/*` - Bug fixes
- `docs/*` - Documentation updates

## Testing

Run the test script to verify functionality:

```bash
python scripts/test_pr_generator.py
```

## Integration with Existing Workflows

This auto PR process integrates with existing workflows:

1. **Continuous Improvement Pipeline** - Uses `peter-evans/create-pull-request`
2. **Python Rewrite Pipeline** - Uses `peter-evans/create-pull-request`
3. **Research-to-Production Pipeline** - Uses `peter-evans/create-pull-request`

The new workflow enhances these by providing more detailed PR messages.

## Future Enhancements

1. **Custom Templates** - Support for custom PR body templates
2. **Impact Analysis** - Analysis of code impact based on changes
3. **Test Coverage** - Include test coverage changes
4. **Performance Metrics** - Include performance impact analysis
5. **Security Analysis** - Security vulnerability detection

## Troubleshooting

### Common Issues

1. **Script not found**: Ensure scripts are in `scripts/` directory
2. **Git errors**: Ensure repository is a git repository
3. **Permission errors**: Ensure proper GitHub token permissions

### Debug Mode

Add `--debug` flag to scripts for verbose output.

## License

This auto PR process is part of the q_mini_wasm_v2 project.