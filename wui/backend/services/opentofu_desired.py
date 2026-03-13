"""Write OpenTofu desired state (tfvars) under infra/opentofu/desired/."""

import json
import os
import re
import uuid
from pathlib import Path
from typing import Any


def _safe_provider_id_for_filename(provider_id: str) -> str | None:
    """Return a sanitized provider_id safe for use in filenames, or None if invalid."""
    if not provider_id or not isinstance(provider_id, str):
        return None
    if re.fullmatch(r"[A-Za-z0-9_.-]+", provider_id.strip()):
        return provider_id.strip()
    return None


def _path_under_base(resolved_path: Path, resolved_base: Path) -> bool:
    """Return True if resolved_path is under resolved_base (no path traversal)."""
    try:
        resolved_path.relative_to(resolved_base)
        return True
    except ValueError:
        return False


def get_desired_dir() -> Path:
    """Return the directory for desired tfvars (repo root relative)."""
    from ..config import get_settings
    s = get_settings()
    if s.opentofu_desired_dir:
        return Path(s.opentofu_desired_dir)
    # Default: infra/opentofu/desired relative to repo root (assume cwd or env)
    base = os.environ.get("LLM_PRACT_ROOT", os.getcwd())
    return Path(base) / "infra" / "opentofu" / "desired"


def write_desired_tfvars(
    provider_id: str,
    instance_type: str | None = None,
    region: str | None = None,
    metadata: dict[str, Any] | None = None,
) -> tuple[str, Path]:
    """Write a tfvars JSON file for the given provider. CI will run OpenTofu on push.

    Returns:
        (config_path_basename, full_path)
    """
    safe_id = _safe_provider_id_for_filename(provider_id)
    if safe_id is None:
        raise ValueError("provider_id must be alphanumeric with only dots, underscores, or hyphens")
    desired_dir = get_desired_dir().resolve()
    desired_dir.mkdir(parents=True, exist_ok=True)
    short_id = uuid.uuid4().hex[:8]
    basename = f"{safe_id}-{short_id}.tfvars.json"
    # Build path from resolved base + sanitized basename only; verify no escape.
    full_path = (desired_dir / basename).resolve()
    if not _path_under_base(full_path, desired_dir):
        raise ValueError("Invalid path")
    payload = {
        "provider_id": safe_id,
        "instance_type": instance_type,
        "region": region,
        **(metadata or {}),
    }
    full_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return basename, full_path


def list_desired_tfvars() -> list[str]:
    """List basenames of *.tfvars.json and *.tfvars in the desired dir. Empty if dir missing/unreadable."""
    desired_dir = get_desired_dir()
    if not desired_dir.exists() or not desired_dir.is_dir():
        return []
    try:
        files = []
        for p in desired_dir.iterdir():
            if p.is_file() and (p.name.endswith(".tfvars.json") or p.name.endswith(".tfvars")):
                files.append(p.name)
        return sorted(files)
    except OSError:
        return []
