"""Unified Training Matrix: resolve which phase is active for a global epoch index."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, List, Tuple


@dataclass(frozen=True)
class PhaseEpochPlan:
    """Flattened schedule: one entry per global epoch."""

    global_epoch: int
    phase_index: int
    phase_name: str
    spec: dict[str, Any]


def flatten_training_phases(phases: List[dict[str, Any]]) -> List[PhaseEpochPlan]:
    """Expand ``[[training_phases]]`` rows into per-epoch plans (0-based global epochs)."""
    out: list[PhaseEpochPlan] = []
    g = 0
    for i, row in enumerate(phases):
        name = str(row.get("name") or f"phase_{i}")
        n_ep = max(1, int(row.get("epochs", 1)))
        for _ in range(n_ep):
            out.append(
                PhaseEpochPlan(global_epoch=g, phase_index=i, phase_name=name, spec=dict(row))
            )
            g += 1
    return out


def total_epochs_from_phases(phases: List[dict[str, Any]]) -> int:
    return sum(max(1, int(p.get("epochs", 1))) for p in phases)


def resolve_phase_at_epoch(
    global_epoch: int, phases: List[dict[str, Any]]
) -> Tuple[int, dict[str, Any]]:
    """Return ``(phase_index, phase_spec)`` for a completed global epoch counter (0-based)."""
    if not phases:
        return -1, {}
    flat = flatten_training_phases(phases)
    if global_epoch < 0:
        return 0, dict(phases[0])
    if global_epoch >= len(flat):
        return len(phases) - 1, dict(phases[-1])
    entry = flat[global_epoch]
    return entry.phase_index, entry.spec


def merge_phase_cascade_overrides(
    phase: dict[str, Any],
    *,
    default_mopd_lambda: float,
    default_cispo_epsilon: float,
) -> tuple[str, float, float]:
    """``(optimizer, mopd_lambda, cispo_epsilon)`` with phase overrides."""
    opt = str(phase.get("cascade_policy_optimizer") or "grpo").strip().lower()
    if opt not in ("grpo", "cispo"):
        opt = "grpo"
    mopd = phase.get("cascade_mopd_lambda")
    ml = float(default_mopd_lambda if mopd is None else mopd)
    ce = phase.get("cispo_clip_epsilon")
    eps = float(default_cispo_epsilon if ce is None else ce)
    return opt, ml, eps
