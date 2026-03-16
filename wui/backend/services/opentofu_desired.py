"""Write OpenTofu desired state (tfvars) under infra/opentofu/desired/."""

import json
import os
import uuid
from pathlib import Path
from typing import Any

# Allowlist of provider ids safe for filenames (no user-controlled path segment).
# Must match deploy router TFVAR_PRESETS; add new providers here when adding presets.
_ALLOWED_PROVIDER_IDS = frozenset({"lambda", "runpod", "ibm_cloud"})

# Write path derived from source location only (no env/config) to satisfy path-expression taint.
_THIS_DIR = Path(__file__).resolve().parent
_DESIRED_DIR_FROM_SOURCE = (
    _THIS_DIR.parent.parent.parent / "infra" / "opentofu" / "desired"
).resolve()


def _path_under_base(resolved_path: Path, resolved_base: Path) -> bool:
    """Return True if resolved_path is under resolved_base (no path traversal)."""
    try:
        resolved_path.relative_to(resolved_base)
        return True
    except ValueError:
        return False


def _get_permitted_base() -> Path:
    """Return a single permitted base path (resolved) for desired dir. No user input."""
    base = os.environ.get("LLM_PRACT_ROOT", os.getcwd())
    return Path(base).resolve()


def get_desired_dir() -> Path:
    """Return the directory for desired tfvars (under permitted base only)."""
    from ..config import get_settings

    permitted = _get_permitted_base()
    default_desired = permitted / "infra" / "opentofu" / "desired"
    s = get_settings()
    if not s.opentofu_desired_dir:
        return default_desired
    candidate = Path(s.opentofu_desired_dir).resolve()
    if not _path_under_base(candidate, permitted):
        raise ValueError("opentofu_desired_dir must be under permitted base")
    return candidate


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
    # Use allowlist so path component is not user-controlled.
    pid = (provider_id or "").strip() if isinstance(provider_id, str) else ""
    if pid not in _ALLOWED_PROVIDER_IDS:
        raise ValueError("provider_id must be one of: " + ", ".join(sorted(_ALLOWED_PROVIDER_IDS)))
    # Path from source location + uuid only (no user input in path for CodeQL).
    safe_desired_dir = _DESIRED_DIR_FROM_SOURCE
    safe_desired_dir.mkdir(parents=True, exist_ok=True)
    short_id = uuid.uuid4().hex[:8]
    basename = f"{short_id}.tfvars.json"
    full_path = (safe_desired_dir / basename).resolve()
    if not _path_under_base(full_path, safe_desired_dir):
        raise ValueError("Invalid path")
    payload = {
        "provider_id": pid,
        "instance_type": instance_type,
        "region": region,
        **(metadata or {}),
    }
    full_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return basename, full_path


def list_desired_tfvars() -> list[str]:
    """List basenames of *.tfvars.json and *.tfvars in the desired dir.

    Empty if dir missing/unreadable.
    """
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
