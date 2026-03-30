"""Optional curriculum phases (e.g. CPT) for training."""

from __future__ import annotations

from typing import Any, List


def get_curriculum_phases() -> List[dict[str, Any]]:
    """Return high-level curriculum labels; see ``docs/ARCHITECTURE_WHITEPAPERS.md`` for TOML phases."""
    return [
        {
            "name": "latent_isa",
            "description": "Freeze semantic experts; train 2D heads and ternary WASM experts.",
        },
        {
            "name": "autoregressive_trace",
            "description": "Open-ended trace unrolling; hull queries for state retrieval.",
        },
        {"name": "cispo", "description": "CISPO reinforcement learning for alignment."},
    ]
