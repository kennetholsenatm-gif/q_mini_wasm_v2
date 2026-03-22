#!/usr/bin/env python3
"""Synthetic mesh QUBO + optional QAOA (PennyLane) or classical random probe."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import torch

from qminiwasm.quantum.mesh_qubo import mesh_edge_selection_qubo


def classical_greedy_select(q_linear: torch.Tensor, q_quad: torch.Tensor, k: int) -> torch.Tensor:
    """Very small greedy solver (not optimal); for smoke without quantum deps."""
    m = q_linear.numel()
    x = torch.zeros(m, dtype=torch.float64)
    for _ in range(min(k, m)):
        best_i = -1
        best_delta = float("inf")
        for i in range(m):
            if x[i] > 0.5:
                continue
            trial = x.clone()
            trial[i] = 1.0
            energy = float((trial * q_linear).sum() + (trial.unsqueeze(0) @ q_quad @ trial.unsqueeze(1)).item())
            if energy < best_delta:
                best_delta = energy
                best_i = i
        if best_i >= 0:
            x[best_i] = 1.0
    return x


def main() -> None:
    p = argparse.ArgumentParser(description="Mesh edge-selection QUBO demo")
    p.add_argument("--m", type=int, default=6, help="number of candidate edges")
    p.add_argument("--k", type=int, default=2, help="edges to select")
    p.add_argument("--lambda-card", type=float, default=1e3, help="cardinality penalty")
    p.add_argument("--seed", type=int, default=0)
    args = p.parse_args()
    torch.manual_seed(int(args.seed))
    costs = torch.rand(int(args.m), dtype=torch.float64)
    ql, qq = mesh_edge_selection_qubo(costs, int(args.k), lambda_cardinality=float(args.lambda_card))
    x = classical_greedy_select(ql, qq, int(args.k))
    out = {
        "m": int(args.m),
        "k": int(args.k),
        "costs": costs.tolist(),
        "selected_mask": [int(v) for v in x.tolist()],
        "sum_selected": int(x.sum().item()),
    }
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
