#!/usr/bin/env python3
"""
Regression guard: SyclRouteMode::Off / training.sycl_route_mode="off" must never return.

Run from repo root:
  python scripts/check_no_sycl_route_off.py .

Exit 1 if forbidden patterns appear (CI + ctest).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

SKIP_DIRS = {
    ".git",
    "node_modules",
    "__pycache__",
    ".venv",
    "venv",
    "build",
    "dist",
    "target",
    "flash_cim_243expert",
}
SKIP_DIR_PREFIXES = ("build_", ".vs")


def iter_files(root: Path) -> list[Path]:
    out: list[Path] = []
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        if p.name == "check_no_sycl_route_off.py":
            continue
        parts = set(p.parts)
        if parts & SKIP_DIRS:
            continue
        if any(part.startswith(SKIP_DIR_PREFIXES) for part in p.parts):
            continue
        suf = p.suffix.lower()
        if suf in (
            ".cpp",
            ".hpp",
            ".h",
            ".go",
            ".toml",
            ".md",
            ".py",
            ".mdc",
            ".yml",
            ".yaml",
        ):
            out.append(p)
        elif p.name in ("CMakeLists.txt",):
            out.append(p)
    return sorted(out)


# Line-oriented forbidden regex (matched anywhere on the line).
LINE_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("SyclRouteMode::Off enum/value must stay removed", re.compile(r"SyclRouteMode::Off")),
    ("DLL/API numeric mode 2 for former Off must stay removed", re.compile(r"syclRouteModeCode\s*=\s*2\b")),
    ("InitSession validator must not accept route mode > 1", re.compile(r"training_sycl_route_mode\s*>\s*2")),
    (
        "gen_pipeline_default_config must not map off -> Off",
        re.compile(r'"off"\s*:\s*".*SyclRouteMode::Off'),
    ),
    (
        'TOML/docs must not set sycl_route_mode = "off"',
        re.compile(r'sycl_route_mode\s*=\s*["\']off["\']', re.I),
    ),
]

# Docs may mention "no off" in explanatory text — allow only these safe phrases.
ALLOW_LINE_SUBSTR = (
    'There is no separate **`"off"`**',
    'no `"off"`',
    'no separate **`"off"`**',
    "former Off",
    "legacy Off",
    "check_no_sycl_route_off",
)


def line_allowed(line: str) -> bool:
    s = line.strip()
    if any(a in line for a in ALLOW_LINE_SUBSTR):
        return True
    if "check_no_sycl_route_off.py" in line:
        return True
    return False


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    if not root.is_dir():
        print(f"Not a directory: {root}", file=sys.stderr)
        return 2

    violations: list[str] = []
    for path in iter_files(root):
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
            "POLICY FAIL: re-introduced sycl_route_mode / SyclRouteMode::Off regression.\n"
            "See scripts/check_no_sycl_route_off.py — remove these matches or update policy explicitly.\n",
            file=sys.stderr,
        )
        for v in violations:
            print(v, file=sys.stderr)
        return 1

    print(f"OK: no forbidden sycl_route Off regression patterns under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
