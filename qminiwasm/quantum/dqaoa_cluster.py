"""Distributed QAOA orchestration: spectral clustering then per-cluster QUBO."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np
import torch


@dataclass
class ClusterQAOAJob:
    cluster_id: int
    node_indices: List[int]
    q_linear: torch.Tensor
    q_quad: torch.Tensor


def adjacency_from_edge_list(
    num_nodes: int, edges: Sequence[Tuple[int, int]], weights: Optional[Sequence[float]] = None
) -> np.ndarray:
    """Build symmetric weighted adjacency (numpy) from undirected edges."""
    a = np.zeros((num_nodes, num_nodes), dtype=np.float64)
    for i, (u, v) in enumerate(edges):
        u, v = int(u), int(v)
        w = float(weights[i]) if weights is not None else 1.0
        a[u, v] = w
        a[v, u] = w
    return a


def spectral_partition(
    adjacency: np.ndarray,
    num_clusters: int,
    seed: int = 0,
) -> np.ndarray:
    """Partition nodes via spectral clustering (``sklearn`` if available, else degree fallback)."""
    n = adjacency.shape[0]
    k = max(2, min(int(num_clusters), n))
    try:
        from sklearn.cluster import SpectralClustering  # type: ignore

        sc = SpectralClustering(
            n_clusters=k,
            affinity="precomputed",
            random_state=int(seed),
            assign_labels="kmeans",
        )
        return sc.fit_predict(adjacency).astype(np.int64)
    except Exception:
        rng = np.random.default_rng(int(seed))
        labels = np.zeros(n, dtype=np.int64)
        chunk = max(1, n // k)
        for c in range(k):
            lo = c * chunk
            hi = n if c == k - 1 else min(n, (c + 1) * chunk)
            labels[lo:hi] = c
        rng.shuffle(labels)
        return labels


def build_cluster_mesh_jobs(
    adjacency: np.ndarray,
    labels: np.ndarray,
    edge_costs_builder: Callable[[List[int]], Tuple[torch.Tensor, torch.Tensor]],
) -> List[ClusterQAOAJob]:
    """For each cluster, build a local QUBO via ``edge_costs_builder(subgraph_nodes)``."""
    jobs: List[ClusterQAOAJob] = []
    k = int(labels.max()) + 1 if labels.size else 0
    for c in range(k):
        nodes = [i for i in range(len(labels)) if int(labels[i]) == c]
        if len(nodes) < 2:
            continue
        q_lin, q_quad = edge_costs_builder(nodes)
        jobs.append(ClusterQAOAJob(cluster_id=c, node_indices=nodes, q_linear=q_lin, q_quad=q_quad))
    return jobs


def merge_cluster_bitstrings(
    num_nodes: int,
    jobs: Sequence[ClusterQAOAJob],
    local_solutions: Sequence[Dict[str, Any]],
) -> np.ndarray:
    """Scatter local binary solutions into a global length-``num_nodes`` vector (best-effort)."""
    out = np.zeros(num_nodes, dtype=np.int8)
    for job, sol in zip(jobs, local_solutions):
        bits = sol.get("x")
        if bits is None:
            continue
        for j, node in enumerate(job.node_indices):
            if j < len(bits):
                out[node] = int(bits[j])
    return out
