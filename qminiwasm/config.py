"""Hierarchical inference configuration.

Centralizes N_max_loops, T_conf, delta format version, and routing/capacity
constants for Tier 1 (edge) and Tier 2/3 (cloud). Loaded from environment or
defaults so operators can tune edge vs cloud behavior.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any, Dict, Optional, Tuple

WASM_PAGE_SIZE_BYTES = 64 * 1024
_MIB = 1024 * 1024


@dataclass(frozen=True)
class EnclaveTierPreset:
    """Tier preset for EF and linear-memory policy."""

    tier: str
    tier_number: str
    enclave_class: str
    ef_target_mb: float
    default_linear_memory_pages: int
    memory64_required: bool
    default_memory64_max_mb: Optional[float]
    boundary_band_mb: Tuple[float, float]

    @property
    def default_linear_memory_mb(self) -> float:
        return (self.default_linear_memory_pages * WASM_PAGE_SIZE_BYTES) / _MIB


ENCLAVE_TIER_PRESETS: Dict[str, EnclaveTierPreset] = {
    "micro": EnclaveTierPreset(
        tier="micro",
        tier_number="1",
        enclave_class="Micro-Enclaves",
        ef_target_mb=250.0,
        default_linear_memory_pages=4_096,  # 256 MiB
        memory64_required=False,
        default_memory64_max_mb=None,
        boundary_band_mb=(128.0, 256.0),
    ),
    "meso": EnclaveTierPreset(
        tier="meso",
        tier_number="2",
        enclave_class="Meso-Enclaves",
        ef_target_mb=2_048.0,
        default_linear_memory_pages=32_768,  # 2 GiB
        memory64_required=False,
        default_memory64_max_mb=None,
        boundary_band_mb=(1_024.0, 2_048.0),
    ),
    "macro": EnclaveTierPreset(
        tier="macro",
        tier_number="3",
        enclave_class="Macro-Enclaves",
        ef_target_mb=8_192.0,
        default_linear_memory_pages=131_072,  # 8 GiB
        memory64_required=True,
        default_memory64_max_mb=8_192.0,
        boundary_band_mb=(4_096.0, 8_192.0),
    ),
    "workgroup": EnclaveTierPreset(
        tier="workgroup",
        tier_number="4",
        enclave_class="Workgroup Enclaves",
        ef_target_mb=16_384.0,
        default_linear_memory_pages=262_144,  # 16 GiB
        memory64_required=True,
        default_memory64_max_mb=16_384.0,
        boundary_band_mb=(8_192.0, 65_536.0),
    ),
    "enterprise_core": EnclaveTierPreset(
        tier="enterprise_core",
        tier_number="5",
        enclave_class="Enterprise Core Enclaves",
        ef_target_mb=262_144.0,  # 256 GiB
        default_linear_memory_pages=4_194_304,  # 256 GiB
        memory64_required=True,
        default_memory64_max_mb=262_144.0,
        boundary_band_mb=(65_536.0, 262_144.0),
    ),
}

_NUMERIC_ENCLAVE_TIERS: Dict[int, str] = {
    1: "micro",
    2: "meso",
    3: "macro",
    4: "workgroup",
    5: "enterprise_core",
}


def normalize_enclave_tier_value(v: Any) -> Optional[str]:
    """Map ints 1–5, digit strings, or tier names to canonical preset keys.

    Used by TOML (integer tiers), :class:`~qminiwasm.engine.config.EngineConfig`, and
    Pydantic ``[enclave].enclave_tier`` validation.
    """
    if v is None:
        return None
    if isinstance(v, bool):
        raise ValueError(f"Invalid enclave_tier {v!r}; expected 1–5 or a tier name.")
    if isinstance(v, (int, float)):
        n = int(v)
        if n in _NUMERIC_ENCLAVE_TIERS:
            return _NUMERIC_ENCLAVE_TIERS[n]
        raise ValueError(f"Invalid enclave_tier numeric value {v!r}; expected 1–5.")
    s = str(v).strip().lower()
    if not s:
        return None
    if s.isdigit():
        return normalize_enclave_tier_value(int(s))
    if s in ENCLAVE_TIER_PRESETS:
        return s
    raise ValueError(
        f"Invalid enclave_tier {v!r}; expected 1–5 or one of " f"{sorted(ENCLAVE_TIER_PRESETS)}."
    )


def get_enclave_tier_preset(enclave_tier: Optional[str]) -> Optional[EnclaveTierPreset]:
    """Return normalized tier preset or None."""
    if not enclave_tier:
        return None
    return ENCLAVE_TIER_PRESETS.get(str(enclave_tier).strip().lower())


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
    """Deployment tier: micro | meso | macro | workgroup | enterprise_core.

    Matches Tiers 1–5 EF taxonomy.
    """
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
