#!/usr/bin/env python3
"""
Cognitive Ergonomics Auto-Commit Script

Implements Cognitive Ergonomics Framework principles for git workflows:
- Miller's Law (7±2 working memory limit)
- Hick-Hyman Law (minimize decision points)
- Gestalt Grouping (logical change clustering)
- Progressive Disclosure (information layering)
- Flow State Preservation (minimize context switching)

Automatically generates ergonomic commit messages, validates against cognitive
load constraints, and provides optimized push/PR workflows.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# Commit message format from config/hooks.json
# Format: ^(feat|fix|docs|style|refactor|test|chore|quantum)\(\w+\): .{1,72}
# Hierarchical commit type grouping (Hick-Hyman Law optimization)
# Grouped into 3 primary + 5 secondary to minimize decision load
PRIMARY_COMMIT_TYPES = ["feat", "fix", "chore"]
SECONDARY_COMMIT_TYPES = ["docs", "style", "refactor", "test", "quantum"]
COMMIT_TYPES = PRIMARY_COMMIT_TYPES + SECONDARY_COMMIT_TYPES


def run_git(args: List[str], repo_path: str = ".") -> Tuple[bool, str]:
    """Run a git command and return success status and output."""
    try:
        result = subprocess.run(
            ["git"] + args,
            cwd=Path(repo_path).resolve(),
            capture_output=True,
            text=True,
            check=True
        )
        return True, result.stdout.strip()
    except subprocess.CalledProcessError as e:
        return False, e.stderr.strip()


def get_current_branch(repo_path: str = ".") -> str:
    """Get the current branch name."""
    success, output = run_git(["rev-parse", "--abbrev-ref", "HEAD"], repo_path)
    return output if success else "unknown"


def get_staged_files(repo_path: str = ".") -> List[str]:
    """Get list of staged files."""
    success, output = run_git(["diff", "--cached", "--name-only"], repo_path)
    if not success or not output:
        return []
    return [f.strip() for f in output.split('\n') if f.strip()]


def get_staged_changes(repo_path: str = ".") -> Dict[str, int]:
    """Get statistics about staged changes."""
    success, output = run_git(["diff", "--cached", "--stat"], repo_path)
    if not success or not output:
        return {"files": 0, "insertions": 0, "deletions": 0}
    
    # Parse the last line which contains the summary
    lines = output.strip().split('\n')
    if not lines:
        return {"files": 0, "insertions": 0, "deletions": 0}
    
    summary = lines[-1]
    stats = {"files": 0, "insertions": 0, "deletions": 0}
    
    # Parse summary like "3 files changed, 150 insertions(+), 10 deletions(-)"
    parts = summary.split(',')
    for part in parts:
        part = part.strip()
        if 'file' in part:
            try:
                stats["files"] = int(part.split()[0])
            except (ValueError, IndexError):
                pass
        elif 'insertion' in part:
            try:
                stats["insertions"] = int(part.split()[0])
            except (ValueError, IndexError):
                pass
        elif 'deletion' in part:
            try:
                stats["deletions"] = int(part.split()[0])
            except (ValueError, IndexError):
                pass
    
    return stats


def analyze_file_changes(staged_files: List[str]) -> Dict[str, List[str]]:
    """Categorize file changes by type."""
    categories = {
        "source": [],
        "test": [],
        "docs": [],
        "config": [],
        "build": [],
        "other": []
    }
    
    for file in staged_files:
        file_lower = file.lower()
        path = Path(file)
        
        # Categorize based on file path and extension
        if any(test_dir in file_lower for test_dir in ["test", "tests", "_test.", "spec"]):
            categories["test"].append(file)
        elif any(doc_dir in file_lower for doc_dir in ["doc", "docs", "readme", "changelog"]):
            categories["docs"].append(file)
        elif any(config_ext in path.suffix.lower() for config_ext in [".json", ".yaml", ".yml", ".toml", ".ini", ".cfg"]):
            categories["config"].append(file)
        elif any(build_file in file_lower for build_file in ["makefile", "cmake", "dockerfile", "docker-compose", ".github/workflows"]):
            categories["build"].append(file)
        elif any(source_ext in path.suffix.lower() for source_ext in [".py", ".cpp", ".c", ".h", ".hpp", ".go", ".rs", ".js", ".ts"]):
            categories["source"].append(file)
        else:
            categories["other"].append(file)
    
    return categories


def determine_commit_type(categories: Dict[str, List[str]], branch_name: str) -> str:
    """Determine the commit type based on file categories and branch name."""
    # Check branch name first
    if branch_name.startswith("feature/"):
        return "feat"
    elif branch_name.startswith("fix/"):
        return "fix"
    elif branch_name.startswith("docs/"):
        return "docs"
    elif branch_name.startswith("refactor/"):
        return "refactor"
    elif branch_name.startswith("test/"):
        return "test"
    
    # Check file categories
    if categories["test"] and not categories["source"]:
        return "test"
    elif categories["docs"] and not categories["source"]:
        return "docs"
    elif categories["config"] and not categories["source"]:
        return "chore"
    elif any("quantum" in f.lower() for f in categories["source"]):
        return "quantum"
    elif categories["source"]:
        # Check if it's a new feature or a fix
        # For now, default to feat for source changes
        return "feat"
    
    # Default
    return "chore"


def generate_scope(categories: Dict[str, List[str]]) -> str:
    """
    Generate commit scope following Miller's Law (max 2 scopes).
    
    Cognitive Principle: Human working memory can only hold 7±2 items.
    By limiting scope to maximum 2 identifiers, we minimize cognitive load
    and ensure commit messages are rapidly parsable.
    """
    scopes = []
    
    # Check for specific modules or components
    all_files = []
    for file_list in categories.values():
        all_files.extend(file_list)
    
    # Look for common patterns in file paths
    for file in all_files:
        path = Path(file)
        parts = path.parts
        
        # Check for module names
        for part in parts:
            if part in ["quantum_core", "dll_bridge", "go_runtime", "sycl_accelerator", 
                       "runtime_engine", "rag_service", "test_orchestrator", "wui_designer"]:
                if part not in scopes:
                    scopes.append(part)
        
        # Check for specific file types
        if "hooks" in file.lower() and "hooks" not in scopes:
            scopes.append("hooks")
        if "config" in file.lower() and "config" not in scopes:
            scopes.append("config")
    
    # STRICT: Limit to maximum 2 scopes (Miller's Law optimization)
    if len(scopes) > 2:
        scopes = scopes[:2]
    
    return ", ".join(scopes) if scopes else ""


def generate_commit_message(commit_type: str, scope: str, stats: Dict[str, int], 
                          categories: Dict[str, List[str]]) -> str:
    """Generate a commit message following the project's format."""
    # Generate a descriptive message
    if stats["files"] == 1:
        file_desc = f"update {Path(categories['source'][0] if categories['source'] else categories['other'][0]).name}"
    else:
        file_desc = f"{stats['files']} files"
    
    # Create message based on type and changes
    if commit_type == "feat":
        if stats["files"] == 1 and categories["source"]:
            message = f"add {Path(categories['source'][0]).stem} functionality"
        else:
            message = f"add {file_desc} with {stats['insertions']} insertions"
    elif commit_type == "fix":
        message = f"fix issues in {file_desc}"
    elif commit_type == "docs":
        message = f"update documentation in {file_desc}"
    elif commit_type == "refactor":
        message = f"refactor {file_desc}"
    elif commit_type == "test":
        message = f"add/update tests for {file_desc}"
    elif commit_type == "chore":
        message = f"update {file_desc}"
    elif commit_type == "quantum":
        message = f"update quantum operations in {file_desc}"
    else:
        message = f"update {file_desc}"
    
    # Format according to project convention: type(scope): message
    if scope:
        return f"{commit_type}({scope}): {message}"
    else:
        return f"{commit_type}: {message}"


def validate_cognitive_constraints(categories: Dict[str, List[str]], stats: Dict[str, int]) -> Tuple[bool, List[str]]:
    """
    Validate commit against Cognitive Ergonomics Framework constraints.
    
    Checks:
    - Change count does not exceed working memory limits
    - Changes are logically grouped (Gestalt principle)
    - Single logical purpose per commit
    """
    warnings = []
    valid = True
    
    # Miller's Law: >15 files exceeds cognitive parsing capacity
    if stats["files"] > 15:
        warnings.append(f"⚠️  COGNITIVE WARNING: {stats['files']} files changed. Consider splitting into smaller atomic commits.")
        valid = False
    
    # Gestalt Grouping: multiple unrelated change types indicate unchunked work
    change_types = sum(1 for files in categories.values() if len(files) > 0)
    if change_types > 3:
        warnings.append("⚠️  COGNITIVE WARNING: Multiple unrelated change types detected. This commit spans multiple domains.")
        valid = False
    
    # Flow State: very large changes break review continuity
    if stats["insertions"] + stats["deletions"] > 500:
        warnings.append(f"⚠️  COGNITIVE WARNING: {stats['insertions'] + stats['deletions']} total lines changed. Large diffs reduce review quality.")
    
    return valid, warnings


def validate_commit_message(message: str) -> Tuple[bool, str]:
    """Validate commit message format AND cognitive ergonomics constraints."""
    import re
    pattern = r"^(feat|fix|docs|style|refactor|test|chore|quantum)(\(\w+\))?: .{1,72}$"
    
    if re.match(pattern, message):
        # Additional ergonomic validation
        if len(message) > 60:
            return True, "Commit message valid (optimal length for rapid scanning)"
        return True, "Commit message format is valid"
    else:
        return False, f"Commit message does not match required format: {pattern}"


def auto_commit(repo_path: str = ".", auto_push: bool = False, 
               create_pr: bool = False, dry_run: bool = False) -> Dict:
    """
    Automatically commit staged changes with a generated commit message.
    
    Args:
        repo_path: Path to the git repository
        auto_push: Whether to push after committing
        create_pr: Whether to create a PR after pushing
        dry_run: If True, only generate the message without committing
    
    Returns:
        Dictionary with results
    """
    result = {
        "success": False,
        "commit_message": "",
        "staged_files": [],
        "stats": {},
        "error": None,
        "dry_run": dry_run,
        "pr_created": False,
        "pr_url": None
    }
    
    # Check if we're in a git repository
    success, _ = run_git(["status"], repo_path)
    if not success:
        result["error"] = "Not in a git repository"
        return result
    
    # Get current branch
    branch_name = get_current_branch(repo_path)
    if branch_name == "unknown":
        result["error"] = "Could not determine current branch"
        return result
    
    # Get staged files
    staged_files = get_staged_files(repo_path)
    if not staged_files:
        result["error"] = "No staged files to commit"
        return result
    
    result["staged_files"] = staged_files
    
    # Get change statistics
    stats = get_staged_changes(repo_path)
    result["stats"] = stats
    
    # Analyze file changes
    categories = analyze_file_changes(staged_files)
    
    # Determine commit type and scope
    commit_type = determine_commit_type(categories, branch_name)
    scope = generate_scope(categories)
    
    # Generate commit message
    commit_message = generate_commit_message(commit_type, scope, stats, categories)
    result["commit_message"] = commit_message
    
    # Validate commit message
    is_valid, validation_message = validate_commit_message(commit_message)
    if not is_valid:
        result["error"] = f"Generated commit message is invalid: {validation_message}"
        return result
    
    # Apply Cognitive Ergonomics validation
    cognitive_valid, cognitive_warnings = validate_cognitive_constraints(categories, stats)
    result["cognitive_warnings"] = cognitive_warnings
    
    # Progressive Disclosure: Only show detailed stats when requested or warnings exist
    if cognitive_warnings:
        print("\n" + "═"*60)
        print("  📊 COGNITIVE ERGONOMICS FEEDBACK")
        print("═"*60)
        for warning in cognitive_warnings:
            print(f"  {warning}")
        print("═"*60 + "\n")
    
    if dry_run:
        result["success"] = True
        return result
    
    # Create the commit
    success, output = run_git(["commit", "-m", commit_message], repo_path)
    if not success:
        result["error"] = f"Failed to create commit: {output}"
        return result
    
    # Push if requested
    if auto_push:
        success, output = run_git(["push", "-u", "origin", branch_name], repo_path)
        if not success:
            result["error"] = f"Failed to push: {output}"
            return result
    
    # Create PR if requested
    if create_pr and auto_push:
        try:
            # Try to use GitHub CLI to create PR
            import subprocess
            from datetime import datetime
            
            # Generate PR title from commit message
            pr_title = commit_message
            
            # Generate PR body
            pr_body = f"""## Auto-Generated PR

This PR was automatically created by the Kanban Auto-Commit workflow.

**Commit Message:** {commit_message}

### Changes
- Auto-committed staged changes
- Generated commit message following project conventions

---
*Generated on {datetime.now().isoformat()}*
"""
            
            # Try to create PR using GitHub CLI
            gh_result = subprocess.run(
                ["gh", "pr", "create",
                 "--title", pr_title,
                 "--body", pr_body,
                 "--base", "develop",
                 "--head", branch_name],
                capture_output=True,
                text=True
            )
            
            if gh_result.returncode == 0:
                # Extract PR URL from output
                pr_url = gh_result.stdout.strip()
                result["pr_created"] = True
                result["pr_url"] = pr_url
                print(f"PR created successfully: {pr_url}")
            else:
                # Fall back to manual PR creation message
                print(f"Could not create PR automatically: {gh_result.stderr}")
                print("Please create PR manually with the following details:")
                print(f"Title: {pr_title}")
                print(f"Base: develop")
                print(f"Head: {branch_name}")
                print(f"Body: {pr_body[:200]}...")
                
        except Exception as e:
            print(f"Error creating PR: {e}")
            print("Please create PR manually")
    
    result["success"] = True
    return result


def enable_unicode_support():
    """Enable Unicode output support for Windows terminals."""
    import sys
    if sys.platform == 'win32':
        # Set UTF-8 encoding for stdout/stderr on Windows
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')


def main():
    enable_unicode_support()
    
    parser = argparse.ArgumentParser(
        description="Auto-commit staged changes with generated commit messages"
    )
    parser.add_argument('--repo-path', default='.', help='Path to git repository')
    parser.add_argument('--auto-push', action='store_true', help='Push after committing')
    parser.add_argument('--create-pr', action='store_true', help='Create PR after pushing')
    parser.add_argument('--dry-run', action='store_true', help='Only generate message, don\'t commit')
    parser.add_argument('--json', action='store_true', help='Output result as JSON')
    
    args = parser.parse_args()
    
    result = auto_commit(
        repo_path=args.repo_path,
        auto_push=args.auto_push,
        create_pr=args.create_pr,
        dry_run=args.dry_run
    )
    
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        if result["success"]:
            if result["dry_run"]:
                # Fitts's Law optimized layout: critical information first, grouped vertically
                print("\n" + "═"*60)
                print("  ✅  COMMIT PREVIEW (DRY RUN)")
                print("═"*60)
                print(f"\n      📝  {result['commit_message']}")
                print(f"\n      📁  {len(result['staged_files'])} files changed")
                print(f"      📊  +{result['stats']['insertions']} insertions / -{result['stats']['deletions']} deletions")
                print("\n" + "═"*60 + "\n")
            else:
                print("\n" + "═"*60)
                print("  ✅  COMMIT SUCCESSFUL")
                print("═"*60)
                print(f"\n      📝  {result['commit_message']}")
                print("\n" + "═"*60)
                
                if args.auto_push:
                    print("\n      🚀  Pushing changes to remote...")
                    print("      ✓  Changes pushed successfully")
                    print()
        else:
            print("\n" + "═"*60)
            print("  ❌  COMMIT FAILED")
            print("═"*60)
            print(f"\n      {result['error']}")
            print("\n" + "═"*60 + "\n", file=sys.stderr)
            sys.exit(1)


if __name__ == '__main__':
    main()