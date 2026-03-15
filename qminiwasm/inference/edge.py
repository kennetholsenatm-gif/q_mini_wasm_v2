"""Tier 1 edge inference: Cognitive Looping and N-loop halting.

Runs local WASM execution in a loop; after each logical block computes a
certainty scalar; stops when certainty > T_conf or loop count > N (escalation).
"""

from __future__ import annotations

import enum
import logging
from typing import Any, Callable, Dict, List, Optional, Tuple

from ..config import HierarchicalConfig

logger = logging.getLogger(__name__)


class EdgeOutcome(enum.Enum):
    """Result of edge cognitive loop."""

    RESOLVED_LOCAL = "resolved_local"
    """Task resolved at edge; certainty breached T_conf."""
    ESCALATE_TO_CLOUD = "escalate_to_cloud"
    """N-loop limit exceeded or certainty never breached; escalate to Tier 3."""


def run_edge_cognitive_loop(
    execute_one_block: Callable[[int], Tuple[Any, Dict]],
    compute_certainty: Callable[[Dict], float],
    config: Optional[HierarchicalConfig] = None,
) -> Tuple[Any, EdgeOutcome, int, Dict]:
    """Run the edge cognitive loop with N-loop halting and certainty threshold.

    Args:
        execute_one_block: Called with loop index (0-based). Returns (block_result, state_dict).
        compute_certainty: Given current state dict, returns a scalar in [0, 1].
        config: Hierarchical config; uses defaults if None.

    Returns:
        (final_result, outcome, num_loops, last_state) where outcome is
        RESOLVED_LOCAL or ESCALATE_TO_CLOUD.
    """
    from ..config import DEFAULT_HIERARCHICAL_CONFIG

    cfg = config or DEFAULT_HIERARCHICAL_CONFIG
    last_result: Any = None
    last_state: Dict = {}
    num_loops = 0

    for loop_idx in range(cfg.N_max_loops):
        last_result, last_state = execute_one_block(loop_idx)
        num_loops = loop_idx + 1
        certainty = compute_certainty(last_state)
        if certainty >= cfg.T_conf:
            logger.info("Edge resolved at loop %d (certainty=%.4f)", num_loops, certainty)
            return last_result, EdgeOutcome.RESOLVED_LOCAL, num_loops, last_state

    logger.info("Edge escalation after N=%d loops (certainty never > T_conf)", cfg.N_max_loops)
    return last_result, EdgeOutcome.ESCALATE_TO_CLOUD, num_loops, last_state


def default_certainty_heuristic(state: Dict) -> float:
    """Heuristic certainty from state: higher if state has 'execution_state' with result."""
    if not state:
        return 0.0
    es = state.get("execution_state") or state.get("execution_state_dict")
    if es is None:
        return 0.0
    # Simple heuristic: if we have a return value and memory/stack keys, assume progress
    if isinstance(es, dict) and ("memory" in es or "stack" in es or "return_value" in str(es)):
        return 0.5
    return 0.3
