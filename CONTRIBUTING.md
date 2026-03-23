# Contributing to LLM_Pract

## Branch Types and Naming Conventions

This repository uses a GitFlow-inspired branching model with the following branch types:

### Main Branches
- **master**: Production-ready code (protected)
- **develop**: Integration branch for new features (protected)

### Feature Branches
- **feature/**: New features and functionality
  - Example: `feature/user-authentication`
  - Created from: develop
  - Merged into: develop

### Bug Fix Branches
- **bugfix/**: Bug fixes and patches
  - Example: `bugfix/login-bug`
  - Created from: develop
  - Merged into: develop

### Release Branches
- **release/**: Release preparation and stabilization
  - Example: `release/v1.0.0`
  - Created from: develop
  - Merged into: master and develop

### Security Branches
- **security/**: Security-related changes and fixes
  - Example: `security/critical-fix`
  - Created from: develop
  - Merged into: develop

### Development Branches
- **dev/**: Experimental development and testing
  - Example: `dev/new-experiment`
  - Created from: develop
  - Merged into: develop

## Workflow
1. Create feature/bugfix branches from develop
2. Submit pull requests for all changes
3. Code review required for all pull requests
4. Merge approved changes back to develop
5. Create release branches for production deployments

## Line endings (Windows)

Python sources are **`LF` only** (see [`.gitattributes`](.gitattributes): `*.py text eol=lf`) so Linux CI (Black, tests) matches your checkout.

If Git warns *“CRLF will be replaced by LF the next time Git touches it”*, set this **once per clone**:

```bash
git config core.autocrlf false
```

Then refresh text files from the index (discards only line-ending mismatch, not real edits):

```bash
git add --renormalize .
git status   # review; then commit if you intend a normalization commit
```

Or let your editor use LF for `*.py` (this repo includes [`.editorconfig`](.editorconfig)).

## Rules
- Direct pushes to master and develop are prohibited
- All changes must go through pull requests
- Minimum 1 reviewer required for all pull requests
- Branch names must follow the specified conventions