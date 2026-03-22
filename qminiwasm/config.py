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

    # Failure Taxonomy and Fallback Mechanisms
    max_convex_hull_size: int = 1000
    """Maximum size of convex hull cache for geometric state recovery."""
    qpu_fidelity_threshold: float = 0.90
    """Threshold for QPU fidelity before degradation mode activation."""

    # Mathematical Bridge (Tropical Geometry)
    d_model: int = 4096
    """Model dimension for tropical attention operations."""
    num_attention_heads: int = 8
    """Number of attention heads for tropical attention."""

    # Hardware Deployment (Intel ARC)
    qaoa_circuit_depth: int = 10
    """Circuit depth for QAOA optimization."""
    qaoa_parameter_count: int = 20
    """Number of parameters for QAOA optimization."""
    use_battlemage: bool = False
    """Whether to use Intel ARC Battlemage (Xe2-HPG) instead of Alchemist."""

    # Security and Privacy Enhancements
    dcpe_scale_factor: float = 1.0
    """Scale factor for DCPE encryption."""
    dcpe_perturbation_variance: float = 0.1
    """Perturbation variance for DCPE encryption."""
    vec2text_diffusion_steps: int = 50
    """Number of diffusion steps for Vec2Text inversion."""
    vec2text_masking_probability: float = 0.1
    """Masking probability for Vec2Text inversion."""
    max_memory_bank_size: int = 1000
    """Maximum size of memory bank for Vec2Text-RAG."""
    enable_quantum_routing: bool = True
    """Whether to enable quantum-accelerated routing."""

    # Tier 1.5 / Fog escalation (deterministic policy hooks)
    max_sequence_length_proxy: int = 1_000_000
    """Escalate when reported ``sequence_length_proxy`` in state exceeds this (disabled if <= 0)."""
    perplexity_spike_factor: float = 0.0
    """If > 0 and ``baseline_perplexity`` + ``last_perplexity`` present, escalate when
    ``last_perplexity >= factor * baseline_perplexity``."""
    contradiction_detected_key: str = "contradiction_detected"
    """If ``state[contradiction_detected_key]`` is truthy, escalate to Fog tier."""

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
            expert_capacity=(
                int(x) if (x := os.environ.get("EXPERT_CAPACITY", "")).isdigit() else None
            ),
        )


# Default instance for import
DEFAULT_HIERARCHICAL_CONFIG = HierarchicalConfig.from_env()
