"""Mesh-style routing as QUBO: edge selection with cardinality penalty.

For use with QAOA layers in :mod:`qminiwasm.quantum.qaoa_integration` or classical solvers.
"""

from __future__ import annotations

from typing import Tuple

import torch


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
