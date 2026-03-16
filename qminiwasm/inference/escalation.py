"""Escalation trigger: prepare Tier 2 state payload from captured deltas.

When the edge N-loop limit is exceeded or certainty never breaches T_conf,
the escalation trigger builds a payload dict for the StateMigrationInterconnect.
No network in this module—in-process only.
"""

from __future__ import annotations

from typing import Any, Dict

from ..config import DEFAULT_HIERARCHICAL_CONFIG, HierarchicalConfig


def prepare_escalation_payload(
    captured_deltas: Dict[str, Any],
    config: HierarchicalConfig | None = None,
) -> Dict[str, Any]:
    """Build Tier 2 escalation payload from captured edge state.

    Args:
        captured_deltas: State from the edge at halt (e.g. last_state from
            run_edge_cognitive_loop, containing execution_state, linear_memory,
            stack_snapshot, etc.).
        config: Optional config for delta_format_version; uses default if None.

    Returns:
        Payload dict with keys: format_version, loop_index, linear_memory,
        stack_snapshot, instruction_pointer, execution_state. Ready for
        StateMigrationInterconnect (Phase 2).
    """
    cfg = config or DEFAULT_HIERARCHICAL_CONFIG
    execution_state = captured_deltas.get("execution_state") or {}
    if isinstance(execution_state, dict):
        linear_memory = execution_state.get("linear_memory")
        stack_snapshot = execution_state.get("stack_snapshot")
        instruction_pointer = execution_state.get("instruction_pointer")
    else:
        linear_memory = stack_snapshot = instruction_pointer = None

    # Fallback: allow top-level keys when execution_state does not carry them
    if linear_memory is None:
        linear_memory = captured_deltas.get("linear_memory")
    if stack_snapshot is None:
        stack_snapshot = captured_deltas.get("stack_snapshot")
    if instruction_pointer is None:
        instruction_pointer = captured_deltas.get("instruction_pointer")

    payload = {
        "format_version": cfg.delta_format_version,
        "loop_index": captured_deltas.get("loop_idx", -1),
        "linear_memory": linear_memory,
        "stack_snapshot": stack_snapshot,
        "instruction_pointer": instruction_pointer,
        "execution_state": execution_state,
    }
    return payload
