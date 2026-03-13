"""Write OpenTofu desired state (tfvars) under infra/opentofu/desired/."""

import json
import os
import uuid
from pathlib import Path
from typing import Any


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
    desired_dir = get_desired_dir()
    desired_dir.mkdir(parents=True, exist_ok=True)
    short_id = uuid.uuid4().hex[:8]
    basename = f"{provider_id}-{short_id}.tfvars.json"
    path = desired_dir / basename
    payload = {
        "provider_id": provider_id,
        "instance_type": instance_type,
        "region": region,
        **(metadata or {}),
    }
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    return basename, path
