#!/usr/bin/env python3
import json
import os
import re
import sys
from pathlib import Path

# Configuration
CONFIG_FILE = ".git/hooks/commit-msg-config.json"
TEMPLATE_FILE = ".gitmessage"
SUGGESTION_LEVELS = {
    "info": "\033[94m[INFO]\033[0m",
    "warning": "\033[93m[WARNING]\033[0m",
    "debug": "\033[90m[DEBUG]\033[0m",
}

# Standard commit types
STANDARD_TYPES = ["feat", "fix", "docs", "style", "refactor", "test", "chore", "security"]


def load_config():
    """Load configuration from config file or use defaults."""
    default_config = {
        "enforce": False,
        "suggest_scopes": True,
        "suggest_types": True,
        "suggest_issues": True,
        "suggest_security": True,
        "verbose": True,
        "doD_standards": True,
        "security_detection": True,
    }
    config_path = Path(CONFIG_FILE)
    if config_path.exists():
        with open(config_path, "r") as f:
            return json.load(f)
    return default_config


def load_template():
    """Load the commit message template."""
    template_path = Path(TEMPLATE_FILE)
    if template_path.exists():
        with open(template_path, "r") as f:
            return f.read()
    return None


def analyze_project_structure():
    """Analyze the project structure to suggest relevant scopes."""
    project_path = Path.cwd()
    scopes = set()

    # Common directories that could be scopes
    common_dirs = ["src", "tests", "docs", "scripts", "config", "utils", "lib", "api", "ui", "core"]

    for dir in common_dirs:
        if (project_path / dir).exists():
            scopes.add(dir)

    # Look for Python packages
    for item in project_path.iterdir():
        if item.is_dir() and any(f.name.endswith(".py") for f in item.glob("*.py")):
            scopes.add(item.name)

    return sorted(scopes)


def analyze_commit_message(message):
    """Analyze the commit message and provide suggestions."""
    suggestions = []
    lines = message.strip().split("\n")

    # Check if message is empty
    if not message.strip():
        suggestions.append(
            (
                SUGGESTION_LEVELS["info"],
                "Commit message is empty. Consider adding a descriptive message.",
            )
        )
        return suggestions

    # Check for header line
    if len(lines) > 0:
        header = lines[0].strip()

        # Check for type
        type_match = re.match(r"^([a-z]+)(\(.*\))?:", header)
        if not type_match:
            suggestions.append(
                (
                    SUGGESTION_LEVELS["info"],
                    f"Consider adding a commit type. Common types: {', '.join(STANDARD_TYPES)}",
                )
            )
        else:
            commit_type = type_match.group(1)
            if commit_type not in STANDARD_TYPES:
                suggestions.append(
                    (
                        SUGGESTION_LEVELS["warning"],
                        f"Type '{commit_type}' not standard. Consider: {', '.join(STANDARD_TYPES)}",
                    )
                )

        # Check for scope
        scope_match = re.search(r"\(([^)]+)\)", header)
        if scope_match:
            scope = scope_match.group(1)
            # Check if scope is relevant to project
            project_scopes = analyze_project_structure()
            if scope not in project_scopes:
                suggestions.append(
                    (
                        SUGGESTION_LEVELS["info"],
                        f"Scope '{scope}' not found in project. Common scopes: {', '.join(project_scopes)}",
                    )
                )
        else:
            suggestions.append(
                (
                    SUGGESTION_LEVELS["info"],
                    "Consider adding a scope in parentheses, e.g., 'feat(ui):'",
                )
            )

        # Check for issue numbers
        issue_match = re.search(r"#\d+", header)
        if not issue_match:
            suggestions.append(
                (
                    SUGGESTION_LEVELS["info"],
                    "Consider adding issue number, e.g., 'fix(auth): resolve #123'",
                )
            )

        # Check for security content
        security_keywords = ["security", "auth", "crypto", "password", "token", "jwt", "encryption"]
        if any(keyword in header.lower() for keyword in security_keywords):
            suggestions.append(
                (
                    SUGGESTION_LEVELS["info"],
                    "This appears to be security-related. Consider using 'security:' type and adding CVSS score if applicable",
                )
            )

    # Check for DoD standards compliance
    if config.get("doD_standards", True):
        classification_keywords = ["unclassified", "confidential", "secret", "top secret"]
        if not any(keyword in message.lower() for keyword in classification_keywords):
            suggestions.append(
                (
                    SUGGESTION_LEVELS["info"],
                    "Consider adding classification level for DoD compliance",
                )
            )

    return suggestions


def print_suggestions(suggestions):
    """Print all suggestions."""
    if suggestions:
        print("\n" + "=" * 50)
        print("COMMIT MESSAGE SUGGESTIONS")
        print("=" * 50)
        for level, suggestion in suggestions:
            print(f"  {level}: {suggestion}")
        print("=" * 50 + "\n")


def main():
    global config
    config = load_config()

    # Get commit message from command line argument or stdin
    if len(sys.argv) > 1:
        commit_msg = sys.argv[1]
    else:
        commit_msg = sys.stdin.read().strip()

    # Analyze commit message
    suggestions = analyze_commit_message(commit_msg)

    # Print suggestions
    print_suggestions(suggestions)

    # Exit with success (0) to not block the commit
    sys.exit(0)


if __name__ == "__main__":
    main()
