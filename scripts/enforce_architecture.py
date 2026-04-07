#!/usr/bin/env python3
"""
Architecture Enforcement Script for q_mini_wasm_v2.

This script enforces the repository constitution by scanning source files for
forbidden architectural patterns:
- PPO / RL / global gradient learning keywords
- Non-MCP transport surfaces (HTTP/gRPC/socket server stacks)
- Dense stabilizer tracking signals (e.g. Gaussian elimination terminology)
- Float contamination in strict GF(3) core paths
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

# ANSI escape codes for coloring
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RESET = "\033[0m"

# Define the root of the project
ROOT_DIR = Path(__file__).resolve().parent.parent

EXCLUDE_DIRS = {
    ".git",
    ".github",
    "docs",
    "wiki-output",
    "node_modules",
    "venv",
    "__pycache__",
    "build",
    "build_clean",
    "build_final",
    "reports",
    "datasets",
}

SOURCE_EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".go", ".py"}
COMMENT_SENSITIVE_EXTENSIONS = {".cpp", ".hpp", ".h", ".cc", ".cxx", ".go"}

STRICT_GF3_CORE_PREFIXES = (
    "q_mini_wasm_v2/core/learning/",
    "q_mini_wasm_v2/core/moe/",
    "q_mini_wasm_v2/core/stabilizer/",
    "q_mini_wasm_v2/core/qgnn/",
    "q_mini_wasm_v2/core/ternary/",
    "q_mini_wasm_v2/core/steane/",
)

TRANSPORT_SURFACE_PREFIXES = (
    "agents/",
    "q_mini_wasm_v2/go/",
)

EXECUTION_SURFACE_PREFIXES = (
    "agents/",
    "q_mini_wasm_v2/core/",
    "q_mini_wasm_v2/runtime/",
    "q_mini_wasm_v2/go/",
)


@dataclass(frozen=True)
class Rule:
    rule_id: str
    pattern: re.Pattern
    extensions: set[str]
    message: str
    scope: str = "all"  # all | core_strict | transport_surface | execution_surface


@dataclass(frozen=True)
class Violation:
    rule_id: str
    file_path: str
    line_number: int
    snippet: str
    message: str


FORBIDDEN_RULES = [
    Rule(
        rule_id="PPO_REINFORCEMENT_LEARNING",
        pattern=re.compile(
            r"\b(ppo|proximal policy optimization|reinforcement learning|policy gradient)\b",
            re.IGNORECASE,
        ),
        extensions={".cpp", ".hpp", ".h", ".py", ".go"},
        message="PPO/RL is forbidden. Use Forward-Forward local updates only.",
        scope="execution_surface",
    ),
    Rule(
        rule_id="GLOBAL_BACKPROPAGATION",
        pattern=re.compile(r"\b(backpropagation|global gradient|gradient descent)\b", re.IGNORECASE),
        extensions={".cpp", ".hpp", ".h", ".py", ".go"},
        message="Global gradient-based learning is forbidden. Use Forward-Forward local goodness updates.",
        scope="execution_surface",
    ),
    Rule(
        rule_id="NON_MCP_TRANSPORT",
        pattern=re.compile(
            r"\b(grpc|net/http|flask|fastapi|django|http\.ListenAndServe|websocket)\b",
            re.IGNORECASE,
        ),
        extensions={".go", ".py"},
        message="Non-MCP transport detected. Agents/gateway communication must use MCP JSON-RPC 2.0.",
        scope="transport_surface",
    ),
    Rule(
        rule_id="DENSE_TABLEAU_TRACKING",
        pattern=re.compile(r"\b(gaussian elimination|dense matrix|dense symplectic|O\(N\^3\))\b", re.IGNORECASE),
        extensions={".cpp", ".hpp", ".h"},
        message="Dense tracking is forbidden. Use graph-state standard form with sparse adjacency + vertex operators.",
    ),
    Rule(
        rule_id="FLASH_CIM_OVERRIDE",
        pattern=re.compile(r"\b(cim_override|flash_cim_tracking|bypass_stabilizer)\b", re.IGNORECASE),
        extensions={".cpp", ".hpp", ".h"},
        message="Flash-CIM cannot bypass GF(3) stabilizer tracking. Use only magic-state injection pathway.",
    ),
    Rule(
        rule_id="FLOAT_CONTAMINATION_STRICT_CORE",
        pattern=re.compile(r"\b(float|double|f32|f64|std::vector<double|std::vector<float)\b", re.IGNORECASE),
        extensions={".cpp", ".hpp", ".h"},
        message="Floating-point contamination in strict GF(3) core path.",
        scope="core_strict",
    ),
]


def should_scan_file(file_path: Path) -> bool:
    return file_path.suffix.lower() in SOURCE_EXTENSIONS


def is_core_strict(relative_path: str) -> bool:
    normalized = relative_path.replace("\\", "/")
    return normalized.startswith(STRICT_GF3_CORE_PREFIXES)


def is_transport_surface(relative_path: str) -> bool:
    normalized = relative_path.replace("\\", "/")
    return normalized.startswith(TRANSPORT_SURFACE_PREFIXES)


def is_execution_surface(relative_path: str) -> bool:
    normalized = relative_path.replace("\\", "/")
    return normalized.startswith(EXECUTION_SURFACE_PREFIXES)


def should_suppress_violation(rule_id: str, snippet: str) -> bool:
    """Allow explicit negation phrases in documentation strings/comments embedded in code."""
    lowered = snippet.lower()

    if rule_id == "GLOBAL_BACKPROPAGATION":
        if "no backpropagation" in lowered or "without backpropagation" in lowered:
            return True

    if rule_id == "PPO_REINFORCEMENT_LEARNING":
        if "without reinforcement learning" in lowered or "no reinforcement learning" in lowered:
            return True

    return False


def strip_comments(line: str, ext: str, in_block_comment: bool) -> tuple[str, bool]:
    """Return sanitized line and updated block-comment state."""
    sanitized = line

    if ext in COMMENT_SENSITIVE_EXTENSIONS:
        while True:
            if in_block_comment:
                end = sanitized.find("*/")
                if end == -1:
                    return "", True
                sanitized = sanitized[end + 2 :]
                in_block_comment = False

            start = sanitized.find("/*")
            if start == -1:
                break

            end = sanitized.find("*/", start + 2)
            if end == -1:
                sanitized = sanitized[:start]
                in_block_comment = True
                break

            sanitized = sanitized[:start] + " " + sanitized[end + 2 :]

        sanitized = sanitized.split("//", 1)[0]

    elif ext == ".py":
        sanitized = sanitized.split("#", 1)[0]

    return sanitized, in_block_comment


def scan_file(file_path: Path) -> list[Violation]:
    violations: list[Violation] = []
    rel = str(file_path.relative_to(ROOT_DIR)).replace("\\", "/")
    ext = file_path.suffix.lower()

    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            in_block_comment = False
            for line_num, raw_line in enumerate(f, 1):
                line, in_block_comment = strip_comments(raw_line.rstrip("\n"), ext, in_block_comment)
                snippet = line.strip()
                if not snippet:
                    continue

                for rule in FORBIDDEN_RULES:
                    if ext not in rule.extensions:
                        continue
                    if rule.scope == "core_strict" and not is_core_strict(rel):
                        continue
                    if rule.scope == "transport_surface" and not is_transport_surface(rel):
                        continue
                    if rule.scope == "execution_surface" and not is_execution_surface(rel):
                        continue

                    if rule.pattern.search(snippet):
                        if should_suppress_violation(rule.rule_id, snippet):
                            continue

                        violations.append(
                            Violation(
                                rule_id=rule.rule_id,
                                file_path=rel,
                                line_number=line_num,
                                snippet=snippet,
                                message=rule.message,
                            )
                        )
    except Exception as exc:
        print(f"{YELLOW}Warning: Could not read {file_path}. Error: {exc}{RESET}")

    return violations


def collect_violations() -> tuple[list[Violation], int]:
    all_violations: list[Violation] = []
    scanned_files = 0

    for root, dirs, files in os.walk(ROOT_DIR):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        for name in files:
            file_path = Path(root) / name

            if file_path.name in {"enforce_architecture.py", ".clinerules"}:
                continue

            if not should_scan_file(file_path):
                continue

            scanned_files += 1
            all_violations.extend(scan_file(file_path))

    return all_violations, scanned_files


def print_report(violations: list[Violation], scanned_files: int) -> None:
    print(f"{GREEN}Starting Architecture Enforcement Scan...{RESET}")

    if violations:
        for violation in violations:
            print(f"\n{RED}VIOLATION FOUND IN: {violation.file_path}{RESET}")
            print(f"  {YELLOW}Line {violation.line_number}:{RESET} {violation.snippet}")
            print(f"  {RED}Rule ({violation.rule_id}):{RESET} {violation.message}")

    print(f"\n{GREEN}Scan Complete.{RESET} Analyzed {scanned_files} files.")

    counts: dict[str, int] = {}
    for violation in violations:
        counts[violation.rule_id] = counts.get(violation.rule_id, 0) + 1

    if counts:
        print("\nViolation summary by rule:")
        for rule_id, count in sorted(counts.items()):
            print(f"  - {rule_id}: {count}")

    if violations:
        print(f"\n{RED}FAILED: {len(violations)} architectural violations found. Code cannot be merged.{RESET}")
    else:
        print(
            f"\n{GREEN}PASSED: No architectural violations found. "
            "Code is compliant with GF(3) and MCP protocols." \
            f"{RESET}"
        )


def write_json_report(path: Path, violations: list[Violation], scanned_files: int) -> None:
    counts: dict[str, int] = {}
    for violation in violations:
        counts[violation.rule_id] = counts.get(violation.rule_id, 0) + 1

    payload = {
        "scanned_files": scanned_files,
        "violation_count": len(violations),
        "violations_by_rule": counts,
        "violations": [asdict(v) for v in violations],
    }

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Enforce q_mini_wasm_v2 architectural constitution rules")
    parser.add_argument("--json", dest="json_path", help="Optional JSON report output path")
    args = parser.parse_args()

    violations, scanned_files = collect_violations()
    print_report(violations, scanned_files)

    if args.json_path:
        write_json_report(Path(args.json_path), violations, scanned_files)

    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
