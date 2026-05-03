#!/usr/bin/env python3
"""
Regression guard: no hidden fixed throughput / multislot caps in native training + MoE + SYCL.

Repo policy (.clinerules [THROUGHPUT_SIZING_NO_HIDDEN_CAPS]): sizing must be TOML / InitSession /
explicit MiB-derived budgets — not magic constants like the former 4M slot ceiling.

Run from repo root:
  python scripts/check_no_throughput_hidden_caps.py .

Exit 1 if forbidden patterns appear (CI + ctest).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# Paths relative to repository root (argument), under which .cpp/.hpp are scanned.
SCAN_SUBDIRS = (
    Path("q_mini_wasm_v2/core/training"),
    Path("q_mini_wasm_v2/core/moe"),
    Path("q_mini_wasm_v2/sycl"),
)

# Historical multislot / host-budget "sanity" cap and revived identifier.
LINE_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    (
        "magic 1<<22-style fixed slot/width cap (use TOML / host budget / numeric_limits)",
        re.compile(r"(?:size_t\s*\{\s*1\s*\}|1(?:ull|uL|u)?)\s*<<\s*22\b"),
    ),
    (
        "removed multislot sanity identifier must not return",
        re.compile(r"\bkFfMultislotSlotSanityCap\b"),
    ),
]


def iter_cpp_files(root: Path) -> list[Path]:
    out: list[Path] = []
    for sub in SCAN_SUBDIRS:
        d = root / sub
        if not d.is_dir():
            continue
        for p in d.rglob("*"):
            if not p.is_file():
                continue
            if p.suffix.lower() not in (".cpp", ".hpp", ".h", ".inl"):
                continue
            out.append(p)
    return sorted(out)


def line_allowed(line: str) -> bool:
    if "check_no_throughput_hidden_caps.py" in line:
        return True
    s = line.strip()
    if s.startswith("//") and ("FORBIDDEN" in line or "must not return" in line.lower()):
        return True
    return False


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    if not root.is_dir():
        print(f"Not a directory: {root}", file=sys.stderr)
        return 2

    violations: list[str] = []
    for path in iter_cpp_files(root):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"SKIP read error {path}: {e}", file=sys.stderr)
            continue
        rel = path.relative_to(root)
        for i, line in enumerate(text.splitlines(), start=1):
            if line_allowed(line):
                continue
            for label, rx in LINE_PATTERNS:
                if rx.search(line):
                    violations.append(f"{rel}:{i}: [{label}] {line.strip()[:200]}")

    if violations:
        print(
            "POLICY FAIL: hidden throughput / multislot cap regression.\n"
            "See .clinerules [THROUGHPUT_SIZING_NO_HIDDEN_CAPS] and "
            "scripts/check_no_throughput_hidden_caps.py — use TOML / InitSession, not repo magic.\n",
            file=sys.stderr,
        )
        for v in violations:
            print(v, file=sys.stderr)
        return 1

    print(f"OK: no forbidden throughput hidden-cap patterns under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
