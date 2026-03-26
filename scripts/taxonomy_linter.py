#!/usr/bin/env python3
"""Edge AI taxonomy linter — fail CI when legacy ML vocabulary appears in gated paths.

Designed for GitHub Actions (diff of added lines) or external orchestrators (N8N, Gitea hooks):

    # Pull request / branch delta (recommended for CI)
    python scripts/taxonomy_linter.py --diff-base origin/main

    # Full tree scan (local / release gate); skips fenced code blocks in Markdown
    python scripts/taxonomy_linter.py --full

Exit code 0 = clean, 1 = violations reported on stderr.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, Iterator, List, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = REPO_ROOT / "configs" / "ci" / "taxonomy_linter.json"


def load_config(path: Path) -> Dict[str, Any]:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def should_scan_path(rel: str, cfg: Dict[str, Any]) -> bool:
    rel_norm = rel.replace("\\", "/")
    for ign in cfg.get("ignore_path_substrings", []):
        if ign in rel_norm:
            return False
    prefixes = cfg.get("path_prefixes", [])
    if prefixes and not any(rel_norm.startswith(p) for p in prefixes):
        return False
    ext = Path(rel_norm).suffix.lower()
    return ext in set(cfg.get("scan_extensions", []))


def strip_markdown_fences(text: str) -> str:
    """Remove fenced code blocks (``` ... ```) for prose-only scanning."""
    out: List[str] = []
    i = 0
    lines = text.splitlines(keepends=True)
    in_fence = False
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if stripped.startswith("```"):
            in_fence = not in_fence
            i += 1
            continue
        if not in_fence:
            out.append(line)
        i += 1
    return "".join(out)


def iter_full_file_lines(rel_path: Path, cfg: Dict[str, Any]) -> Iterator[Tuple[str, int, str]]:
    try:
        raw = rel_path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"taxonomy_linter: skip unreadable {rel_path}: {e}", file=sys.stderr)
        return
    if rel_path.suffix.lower() == ".md":
        raw = strip_markdown_fences(raw)
    for lineno, line in enumerate(raw.splitlines(), start=1):
        yield (str(rel_path.relative_to(REPO_ROOT)).replace("\\", "/"), lineno, line)


def git_diff_added_lines(
    base: str, cfg: Dict[str, Any], *, triple_dot: bool = True
) -> List[Tuple[str, int, str]]:
    """Return (relpath, approx_lineno, line) for added lines only."""
    if triple_dot:
        diff_arg = f"{base}...HEAD"
        argv = ["git", "-C", str(REPO_ROOT), "diff", "-U0", diff_arg, "--"]
    else:
        argv = ["git", "-C", str(REPO_ROOT), "diff", "-U0", base, "--"]
    try:
        proc = subprocess.run(
            argv,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError:
        print("taxonomy_linter: git not found; use --full or install git.", file=sys.stderr)
        return []

    if proc.returncode != 0 and proc.stderr:
        print(proc.stderr, file=sys.stderr)

    diff_text = proc.stdout or ""

    results: List[Tuple[str, int, str]] = []
    current_file: str | None = None
    new_start = 0
    hunk_line = 0
    for line in diff_text.splitlines():
        if line.startswith("+++ b/"):
            current_file = line[6:].strip()
            new_start = 0
            hunk_line = 0
            continue
        if current_file is None or not should_scan_path(current_file, cfg):
            continue
        if line.startswith("@@"):
            m = re.search(r"\+(\d+)", line)
            if m:
                new_start = int(m.group(1))
                hunk_line = 0
            continue
        if line.startswith("+") and not line.startswith("+++"):
            content = line[1:]
            lineno = new_start + hunk_line
            hunk_line += 1
            if current_file.lower().endswith(".md") and content.strip().startswith("```"):
                continue
            results.append((current_file, lineno, content))
    return results


def _normalize_prefix_list(raw: Any) -> tuple[str, ...] | None:
    """Return tuple of posix prefixes ending in /, or None if unset."""
    if raw is None:
        return None
    if isinstance(raw, str):
        seq = [raw]
    else:
        seq = list(raw)
    out: list[str] = []
    for p in seq:
        s = str(p).replace("\\", "/")
        if not s.endswith("/"):
            s += "/"
        out.append(s)
    return tuple(out)


def _normalize_extension_list(raw: Any) -> tuple[str, ...] | None:
    """Lowercase suffixes including leading dot, e.g. '.md'. None = apply to all scanned extensions."""
    if raw is None:
        return None
    seq = [raw] if isinstance(raw, str) else list(raw)
    out: list[str] = []
    for x in seq:
        s = str(x).strip().lower()
        if not s.startswith("."):
            s = "." + s
        out.append(s)
    return tuple(out)


def compile_rules(
    cfg: Dict[str, Any],
) -> List[Tuple[re.Pattern[str], str, tuple[str, ...] | None, tuple[str, ...] | None]]:
    """Return (pattern, hint, path_prefixes_or_none, apply_extensions_or_none).

    If ``apply_extensions`` is set on a pattern, the rule runs only for files whose suffix
    is in that list (e.g. prose-only gates for float dtype jargon in ``.md`` / ``.toml``).
    """
    out: List[Tuple[re.Pattern[str], str, tuple[str, ...] | None, tuple[str, ...] | None]] = []
    for item in cfg.get("patterns", []):
        rx = item.get("regex", "")
        hint = item.get("hint", "")
        prefixes = _normalize_prefix_list(item.get("path_prefixes"))
        if prefixes is None and item.get("path_prefix") is not None:
            prefixes = _normalize_prefix_list([item["path_prefix"]])
        apply_ext = _normalize_extension_list(item.get("apply_extensions"))
        try:
            out.append((re.compile(rx), hint, prefixes, apply_ext))
        except re.error as e:
            print(f"taxonomy_linter: bad regex {rx!r}: {e}", file=sys.stderr)
    return out


def _is_skipped_authority_doc(rel_path: str, cfg: Dict[str, Any]) -> bool:
    rel_norm = rel_path.replace("\\", "/")
    for p in cfg.get("skip_all_patterns_for_paths", []):
        if rel_norm == p.replace("\\", "/"):
            return True
    return False


def scan_entries(
    entries: List[Tuple[str, int, str]],
    rules: List[
        Tuple[re.Pattern[str], str, tuple[str, ...] | None, tuple[str, ...] | None]
    ],
    cfg: Dict[str, Any],
) -> List[str]:
    violations: List[str] = []
    for path, lineno, text in entries:
        if _is_skipped_authority_doc(path, cfg):
            continue
        path_norm = path.replace("\\", "/")
        suffix = Path(path_norm).suffix.lower()
        for pat, hint, only_prefixes, apply_ext in rules:
            if only_prefixes is not None and not any(
                path_norm.startswith(pfx) for pfx in only_prefixes
            ):
                continue
            if apply_ext is not None and suffix not in apply_ext:
                continue
            if pat.search(text):
                violations.append(f"{path}:{lineno}: {text.strip()!r}  [{pat.pattern}]  {hint}")
    return violations


def collect_full_scan_files(cfg: Dict[str, Any]) -> List[Path]:
    files: List[Path] = []
    for prefix in cfg.get("path_prefixes", []):
        root = REPO_ROOT / prefix.rstrip("/")
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if not p.is_file():
                continue
            rel = p.relative_to(REPO_ROOT).as_posix()
            if not should_scan_path(rel, cfg):
                continue
            files.append(p)
    return sorted(set(files))


def main() -> int:
    ap = argparse.ArgumentParser(description="Edge AI taxonomy linter")
    ap.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG,
        help="Path to taxonomy_linter.json",
    )
    ap.add_argument(
        "--diff-base",
        metavar="REF",
        help="Git ref: scan only lines added vs REF...HEAD (recommended for CI)",
    )
    ap.add_argument(
        "--full",
        action="store_true",
        help="Scan all matching files under path_prefixes (Markdown fences stripped)",
    )
    ap.add_argument(
        "--diff-working",
        action="store_true",
        help="Like --diff-base but compares working tree to HEAD (includes uncommitted edits)",
    )
    args = ap.parse_args()

    if not args.config.is_file():
        print(f"taxonomy_linter: config missing: {args.config}", file=sys.stderr)
        return 1

    cfg = load_config(args.config)
    rules = compile_rules(cfg)
    if not rules:
        print("taxonomy_linter: no patterns configured", file=sys.stderr)
        return 1

    entries: List[Tuple[str, int, str]] = []
    if args.diff_working:
        entries.extend(git_diff_added_lines("HEAD", cfg, triple_dot=False))
    elif args.diff_base:
        entries.extend(git_diff_added_lines(args.diff_base, cfg))
    elif args.full:
        for p in collect_full_scan_files(cfg):
            rel = p.relative_to(REPO_ROOT).as_posix()
            for path, lineno, line in iter_full_file_lines(p, cfg):
                entries.append((path, lineno, line))
    else:
        ap.print_help()
        print(
            "\nProvide --diff-base REF (CI), --diff-working (uncommitted vs HEAD), or --full.",
            file=sys.stderr,
        )
        return 2

    violations = scan_entries(entries, rules, cfg)
    if violations:
        print("Edge AI taxonomy violations:", file=sys.stderr)
        for v in violations:
            print(v, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
