"""Hierarchical inference configuration.

Centralizes N_max_loops, T_conf, delta format version, and routing/capacity
constants for Tier 1 (edge) and Tier 2/3 (cloud). Loaded from environment or
defaults so operators can tune edge vs cloud behavior.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Optional


@dataclass
class HierarchicalConfig:
    """Configuration for the three-tier hierarchical inference architecture."""

    # Tier 1 (Edge): Cognitive Looping
    N_max_loops: int = 10
    """Maximum cognitive loops before escalation (N-loop halting)."""
    T_conf: float = 0.85
    """Confidence threshold; halt when certainty scalar > T_conf."""

    # Tier 2: State migration / delta payload
    delta_format_version: int = 1
    """Delta payload format version for compatibility."""

    # Tier 3: MoE routing (optional overrides)
    top_k_experts: Optional[int] = None
    expert_capacity: Optional[int] = None

    @classmethod
    def from_env(cls) -> "HierarchicalConfig":
        """Build config from environment variables with defaults."""
        n = os.environ.get("N_MAX_LOOPS", "")
        t = os.environ.get("T_CONF", "")
        return cls(
            N_max_loops=int(n) if n.isdigit() else 10,
            T_conf=float(t) if t else 0.85,
            delta_format_version=int(os.environ.get("DELTA_FORMAT_VERSION", "1") or "1") or 1,
            top_k_experts=int(x) if (x := os.environ.get("TOP_K_EXPERTS", "")).isdigit() else None,
            expert_capacity=int(x) if (x := os.environ.get("EXPERT_CAPACITY", "")).isdigit() else None,
        )


# Default instance for import
DEFAULT_HIERARCHICAL_CONFIG = HierarchicalConfig.from_env()
