#!/usr/bin/env python3
"""
Cognitive Ergonomics Documentation Linter

Validates markdown files against principles from the
Cognitive Ergonomics Model Protocol Research:

  - Miller's Law: chunking, 7±2 items
  - Progressive Disclosure: headers every ±200 words
  - Line length: ≤ 75 characters (optimal saccade)
  - Paragraph length: ≤ 4 lines
  - Code blocks: ≤ 15 lines
  - Navigation depth: ≤ 3 levels
"""

import sys
import re
from pathlib import Path

MAX_LINE_LENGTH = 75
MAX_PARAGRAPH_LINES = 4
MAX_CODE_BLOCK_LINES = 15
HEADER_INTERVAL_WORDS = 200
MAX_NAV_DEPTH = 3

class Violation:
    def __init__(self, file, line, rule, message):
        self.file = file
        self.line = line
        self.rule = rule
        self.message = message

    def __str__(self):
        return f"{self.file}:{self.line} [{self.rule}] {self.message}"

def check_line_length(lines, filepath):
    """Check for lines exceeding optimal reading width."""
    violations = []
    for i, line in enumerate(lines, 1):
        # Skip code blocks and tables
        stripped = line.rstrip()
        if stripped.startswith("|") or stripped.startswith("```"):
            continue
        if len(stripped) > MAX_LINE_LENGTH:
            violations.append(Violation(
                filepath, i, "line-length",
                f"Line is {len(stripped)} chars (max {MAX_LINE_LENGTH})"
            ))
    return violations

def check_paragraph_chunking(lines, filepath):
    """Check paragraphs don't exceed 4 lines."""
    violations = []
    para_start = None
    para_lines = 0
    in_code = False

    for i, line in enumerate(lines, 1):
        stripped = line.strip()

        # Track code blocks
        if stripped.startswith("```"):
            in_code = not in_code
            para_lines = 0
            continue

        if in_code:
            continue

        # Empty line or header resets paragraph
        if stripped == "" or stripped.startswith("#"):
            if para_lines > MAX_PARAGRAPH_LINES and para_start:
                violations.append(Violation(
                    filepath, para_start, "paragraph-chunking",
                    f"Paragraph has {para_lines} lines (max {MAX_PARAGRAPH_LINES})"
                ))
            para_start = None
            para_lines = 0
            continue

        if para_start is None:
            para_start = i
            para_lines = 1
        else:
            para_lines += 1

    # Check final paragraph
    if para_lines > MAX_PARAGRAPH_LINES and para_start:
        violations.append(Violation(
            filepath, para_start, "paragraph-chunking",
            f"Paragraph has {para_lines} lines (max {MAX_PARAGRAPH_LINES})"
        ))

    return violations

def check_header_frequency(lines, filepath):
    """Check headers appear every ~200 words (Progressive Disclosure)."""
    violations = []
    word_count = 0
    last_header_line = 0
    in_code = False

    for i, line in enumerate(lines, 1):
        stripped = line.strip()

        if stripped.startswith("```"):
            in_code = not in_code
            continue

        if in_code:
            continue

        if stripped.startswith("#"):
            word_count = 0
            last_header_line = i
            continue

        word_count += len(stripped.split())

        if word_count > HEADER_INTERVAL_WORDS * 2 and last_header_line > 0:
            violations.append(Violation(
                filepath, i, "header-frequency",
                f"{word_count} words since last header (target: every ~{HEADER_INTERVAL_WORDS})"
            ))
            word_count = 0  # Reset to avoid repeated warnings

    return violations

def check_code_block_length(lines, filepath):
    """Check code blocks don't exceed 15 lines."""
    violations = []
    in_code = False
    code_start = 0
    code_lines = 0

    for i, line in enumerate(lines, 1):
        stripped = line.strip()

        if stripped.startswith("```"):
            if in_code:
                if code_lines > MAX_CODE_BLOCK_LINES:
                    violations.append(Violation(
                        filepath, code_start, "code-block-length",
                        f"Code block has {code_lines} lines (max {MAX_CODE_BLOCK_LINES})"
                    ))
                in_code = False
                code_lines = 0
            else:
                in_code = True
                code_start = i
            continue

        if in_code:
            code_lines += 1

    return violations

def check_nav_depth(lines, filepath):
    """Check heading depth doesn't exceed 3 levels."""
    violations = []
    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("#"):
            depth = len(stripped) - len(stripped.lstrip("#"))
            if depth > MAX_NAV_DEPTH:
                violations.append(Violation(
                    filepath, i, "nav-depth",
                    f"Heading depth is {depth} (max {MAX_NAV_DEPTH})"
                ))
    return violations

def lint_file(filepath):
    """Lint a single markdown file."""
    try:
        content = Path(filepath).read_text(encoding="utf-8")
    except Exception as e:
        return [Violation(filepath, 0, "read-error", str(e))]

    lines = content.split("\n")
    violations = []

    violations.extend(check_line_length(lines, filepath))
    violations.extend(check_paragraph_chunking(lines, filepath))
    violations.extend(check_header_frequency(lines, filepath))
    violations.extend(check_code_block_length(lines, filepath))
    violations.extend(check_nav_depth(lines, filepath))

    return violations

def main():
    if len(sys.argv) < 2:
        print("Usage: cognitive_linter.py <directory>")
        sys.exit(1)

    target = Path(sys.argv[1])
    all_violations = []

    if target.is_file():
        all_violations.extend(lint_file(str(target)))
    elif target.is_dir():
        for md_file in sorted(target.rglob("*.md")):
            # Skip research papers (they're reference material)
            if "research" in str(md_file):
                continue
            all_violations.extend(lint_file(str(md_file)))

    if all_violations:
        print(f"Cognitive Ergonomics Violations ({len(all_violations)}):\n")
        for v in all_violations:
            print(f"  {v}")
        print(f"\nTotal: {len(all_violations)} violations")
        sys.exit(1)
    else:
        print("All documentation passes Cognitive Ergonomics checks.")
        sys.exit(0)

if __name__ == "__main__":
    main()