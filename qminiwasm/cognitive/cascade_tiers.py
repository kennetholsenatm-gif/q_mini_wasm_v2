"""Semantic labels for cascade policy actions (training toy MDP maps to tier hints)."""

from __future__ import annotations

import enum
from typing import Final, List


class CascadeEscalationTier(enum.IntEnum):
    """Default four-way split when ``cascade_num_actions == 4`` (engine default)."""

    STAY_EDGE = 0
    """Prefer local Wasm / ternary path."""

    FOG_ILP = 1
    """Escalate to Fog or invoke classical ILP-style refinement."""

    CLOUD_LLM = 2
    """Full-precision remote LLM."""

    RESERVED = 3
    """Spare slot for future routing modes (e.g. specialized solver)."""


_DEFAULT_NAMES: Final[List[str]] = [
    "stay_edge",
    "fog_ilp",
    "cloud_llm",
    "reserved",
]


def cascade_action_tier_name(action_id: int, num_actions: int = 4) -> str:
    """Human-readable tier name for logging or payloads."""
    if num_actions == 4 and 0 <= action_id < 4:
        return _DEFAULT_NAMES[action_id]
    return f"action_{int(action_id)}"
