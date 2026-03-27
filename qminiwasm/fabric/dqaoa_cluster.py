"""Distributed QAOA orchestration: spectral clustering then per-cluster QUBO."""

from __future__ import annotations

from dataclasses import dataclass
import ctypes
import os
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np
import torch
from qminiwasm.native_bridge import load_native_lib


@dataclass
class ClusterQAOAJob:
    cluster_id: int
    node_indices: List[int]
    q_linear: torch.Tensor
    q_quad: torch.Tensor
    overlap_nodes: List[int]


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
    use_native = os.getenv("QMINIWASM_NATIVE_DQAOA_ROUTING", "0").strip().lower() not in {
        "",
        "0",
        "false",
        "off",
        "no",
    }
    if use_native:
        lib = load_native_lib()
        if lib is not None and hasattr(lib, "qmw_route_assign_clusters"):
            fn = lib.qmw_route_assign_clusters
            fn.argtypes = [
                ctypes.POINTER(ctypes.c_double),
                ctypes.c_size_t,
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_size_t),
            ]
            fn.restype = None
            labels = (ctypes.c_size_t * n)()
            adj = adjacency.astype(np.float64, copy=False)
            fn(
                ctypes.cast(adj.ctypes.data, ctypes.POINTER(ctypes.c_double)),
                ctypes.c_size_t(n),
                ctypes.c_size_t(k),
                ctypes.cast(labels, ctypes.POINTER(ctypes.c_size_t)),
            )
            return np.array([int(labels[i]) for i in range(n)], dtype=np.int64)
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
        jobs.append(
            ClusterQAOAJob(
                cluster_id=c,
                node_indices=nodes,
                q_linear=q_lin,
                q_quad=q_quad,
                overlap_nodes=[],
            )
        )
    return jobs


def build_overlapping_cluster_mesh_jobs(
    adjacency: np.ndarray,
    labels: np.ndarray,
    edge_costs_builder: Callable[[List[int]], Tuple[torch.Tensor, torch.Tensor]],
    overlap_hops: int = 1,
) -> List[ClusterQAOAJob]:
    """Build cluster jobs with lightweight overlap neighborhoods on boundaries."""
    base_jobs = build_cluster_mesh_jobs(adjacency, labels, edge_costs_builder)
    if overlap_hops <= 0:
        return base_jobs
    n = int(adjacency.shape[0])
    out: List[ClusterQAOAJob] = []
    for job in base_jobs:
        in_cluster = set(job.node_indices)
        overlap: set[int] = set()
        frontier = set(job.node_indices)
        for _ in range(int(overlap_hops)):
            nxt: set[int] = set()
            for u in frontier:
                for v in range(n):
                    if adjacency[u, v] > 0 and v not in in_cluster:
                        overlap.add(v)
                        nxt.add(v)
            frontier = nxt
            if not frontier:
                break
        merged_nodes = sorted(in_cluster | overlap)
        q_lin, q_quad = edge_costs_builder(merged_nodes)
        out.append(
            ClusterQAOAJob(
                cluster_id=job.cluster_id,
                node_indices=merged_nodes,
                q_linear=q_lin,
                q_quad=q_quad,
                overlap_nodes=sorted(overlap),
            )
        )
    return out


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
