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
    """Certainty scalar gate ($T_{conf}$); ECL resolves locally when certainty >= T_conf."""

    # TPEM / enclave geometry (EF targets; optional runtime hints)
    enclave_footprint_mb: Optional[float] = None
    """Enclave Footprint (EF) in MB: contiguous TPEM + static heap budget."""
    enclave_tier: Optional[str] = None
    """Deployment tier label: micro | meso | macro | workgroup | enterprise_core (Tiers 1–5 EF taxonomy)."""
    max_linear_memory_pages: Optional[int] = None
    """Max WASM 64KiB pages when enforcing tier caps."""
    wasm_memory64_max_mb: Optional[float] = None
    """Memory64 linear memory ceiling (MB), e.g. 8192 for Macro enclave."""
    use_memory64: bool = False
    """Prefer Memory64 addressing when the runtime supports it."""

    # Tier 2: State migration / delta payload
    delta_format_version: int = 1
    """Delta payload format version for compatibility."""

    # Tier 3: QAHR topology hints (optional capacity overrides)
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

    @property
    def certainty_scalar_threshold(self) -> float:
        """Alias for ``T_conf`` (Certainty-Gated Escalation / CGE gate)."""
        return self.T_conf

    def with_enclave_overrides(
        self,
        *,
        enclave_footprint_mb: Optional[float] = None,
        enclave_tier: Optional[str] = None,
        certainty_scalar_threshold: Optional[float] = None,
        max_linear_memory_pages: Optional[int] = None,
        wasm_memory64_max_mb: Optional[float] = None,
        use_memory64: Optional[bool] = None,
    ) -> "HierarchicalConfig":
        """Return a copy with TOML ``[enclave]`` fields applied (non-None only)."""
        from dataclasses import replace

        t_conf = (
            float(certainty_scalar_threshold)
            if certainty_scalar_threshold is not None
            else self.T_conf
        )
        return replace(
            self,
            T_conf=t_conf,
            enclave_footprint_mb=(
                enclave_footprint_mb
                if enclave_footprint_mb is not None
                else self.enclave_footprint_mb
            ),
            enclave_tier=enclave_tier if enclave_tier is not None else self.enclave_tier,
            max_linear_memory_pages=(
                max_linear_memory_pages
                if max_linear_memory_pages is not None
                else self.max_linear_memory_pages
            ),
            wasm_memory64_max_mb=(
                wasm_memory64_max_mb
                if wasm_memory64_max_mb is not None
                else self.wasm_memory64_max_mb
            ),
            use_memory64=self.use_memory64 if use_memory64 is None else bool(use_memory64),
        )

    @classmethod
    def from_env(cls) -> "HierarchicalConfig":
        """Build config from environment variables with defaults."""
        n = os.environ.get("N_MAX_LOOPS", "")
        t_raw = (os.environ.get("T_CONF", "") or "").strip()
        cst_raw = (os.environ.get("CERTAINTY_SCALAR_THRESHOLD", "") or "").strip()
        if t_raw:
            t_conf = float(t_raw)
        elif cst_raw:
            t_conf = float(cst_raw)
        else:
            t_conf = 0.85

        def _opt_float(key: str) -> Optional[float]:
            s = (os.environ.get(key, "") or "").strip()
            if not s:
                return None
            try:
                return float(s)
            except ValueError:
                return None

        def _opt_int(key: str) -> Optional[int]:
            s = (os.environ.get(key, "") or "").strip()
            if not s or not s.isdigit():
                return None
            return int(s)

        tier = (os.environ.get("ENCLAVE_TIER", "") or "").strip().lower()
        _tiers = ("", "micro", "meso", "macro", "workgroup", "enterprise_core")
        if tier not in _tiers:
            tier = ""

        mem64 = (os.environ.get("WASM_USE_MEMORY64", "") or "").strip().lower()
        use_m64 = mem64 in ("1", "true", "yes", "on")

        return cls(
            N_max_loops=int(n) if n.isdigit() else 10,
            T_conf=t_conf,
            enclave_footprint_mb=_opt_float("ENCLAVE_FOOTPRINT_MB"),
            enclave_tier=tier or None,
            max_linear_memory_pages=_opt_int("WASM_MAX_LINEAR_MEMORY_PAGES"),
            wasm_memory64_max_mb=_opt_float("WASM_MEMORY64_MAX_MB"),
            use_memory64=use_m64,
            delta_format_version=int(os.environ.get("DELTA_FORMAT_VERSION", "1") or "1") or 1,
            top_k_experts=int(x) if (x := os.environ.get("TOP_K_EXPERTS", "")).isdigit() else None,
            expert_capacity=(
                int(x) if (x := os.environ.get("EXPERT_CAPACITY", "")).isdigit() else None
            ),
        )


# Default instance for import
DEFAULT_HIERARCHICAL_CONFIG = HierarchicalConfig.from_env()
