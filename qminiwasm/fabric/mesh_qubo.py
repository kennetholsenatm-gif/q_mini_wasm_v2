"""Mesh-style routing as QUBO: edge selection with cardinality penalty.

For use with QAOA layers in :mod:`qminiwasm.fabric.qaoa_integration` or classical solvers.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple

import torch


@dataclass
class PrunedTopology:
    """Pruned edge topology with reversible index mappings."""

    kept_edge_indices: List[int]
    dropped_edge_indices: List[int]
    old_to_new_edge: List[int]
    new_to_old_edge: List[int]


def mesh_edge_selection_qubo(
    edge_costs: torch.Tensor,
    num_select: int,
    lambda_cardinality: float = 1e6,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """QUBO: choose exactly ``k`` edges minimizing ``sum c_e x_e``.

    Penalty ``lambda * (sum_e x_e - k)^2`` with ``x_e in {0,1}``.
    Expansion uses ``(sum x)^2 = sum x_i + 2 sum_{i<j} x_i x_j``.

    Returns:
        ``(q_linear, q_quad)`` with ``q_quad`` symmetric (off-diagonal pairs).
    """
    m = int(edge_costs.numel())
    if m <= 0:
        raise ValueError("edge_costs must be non-empty")
    k = int(num_select)
    if k < 0 or k > m:
        raise ValueError("num_select must be in [0, m]")
    lam = float(lambda_cardinality)
    c = edge_costs.reshape(-1).to(dtype=torch.float64)
    q_linear = c + lam * (1.0 - 2.0 * k)
    q_quad = torch.zeros((m, m), dtype=torch.float64)
    for i in range(m):
        for j in range(i + 1, m):
            q_quad[i, j] = 2.0 * lam
            q_quad[j, i] = 2.0 * lam
    return q_linear, q_quad


def prune_topology_from_edge_scores(
    edge_scores: torch.Tensor,
    *,
    threshold: float = 0.0,
    min_edges: int = 2,
) -> PrunedTopology:
    """Prune low-confidence edges and produce reversible index mapping.

    Args:
        edge_scores: Per-edge confidence/utility scores.
        threshold: Keep edges with score >= threshold.
        min_edges: Keep at least this many highest-scoring edges.
    """
    scores = edge_scores.reshape(-1).to(dtype=torch.float64)
    n = int(scores.numel())
    if n <= 0:
        raise ValueError("edge_scores must be non-empty")
    keep_mask = (scores >= float(threshold)).tolist()
    kept = [i for i, k in enumerate(keep_mask) if k]
    if len(kept) < int(min_edges):
        topk = min(n, max(1, int(min_edges)))
        order = torch.argsort(scores, descending=True).tolist()
        kept = sorted(order[:topk])
    dropped = [i for i in range(n) if i not in set(kept)]

    old_to_new = [-1] * n
    for new_idx, old_idx in enumerate(kept):
        old_to_new[old_idx] = new_idx

    return PrunedTopology(
        kept_edge_indices=kept,
        dropped_edge_indices=dropped,
        old_to_new_edge=old_to_new,
        new_to_old_edge=list(kept),
    )
