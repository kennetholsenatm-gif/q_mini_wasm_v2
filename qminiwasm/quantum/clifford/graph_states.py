"""Qutrit graph states for deterministic feature projection.

This module implements qutrit graph states as quantum-inspired random projection
matrices. Graph states provide high-dimensional, entangled feature hashing without
requiring learned weights or floating-point matrix multiplications.

A qutrit graph state |G⟩ is defined by a graph G = (V, E) where:
- Vertices V correspond to qutrits representing ternary features
- Edges E correspond to CZ₃ entanglement operations

The stabilizer generators of the graph state define the projection complexity
and can be computed efficiently via the Ternary Symplectic Pauli Frame (TSPF).
"""

from __future__ import annotations

from typing import List, Optional, Tuple
import numpy as np

from qminiwasm.quantum.clifford.qutrit_tableau import QutritStabilizerTableau
from qminiwasm.quantum.clifford.qutrit_gates import (
    apply_h3, apply_s3, apply_cz3, apply_x3, apply_z3, OMEGA
)


class QutritGraphState:
    """Qutrit graph state for deterministic feature projection.

    The graph state is initialized in the maximal superposition |+⟩^n, then
    entangled according to the adjacency matrix. Input features modulate
    the state via phase rotations, and output features are extracted via
    stabilizer generator expectation values.
    """

    def __init__(self, n_qutrits: int, adjacency: Optional[np.ndarray] = None):
        """Initialize qutrit graph state.

        Args:
            n_qutrits: Number of qutrits (vertices)
            adjacency: Adjacency matrix over GF(3). If None, uses identity (no edges).
        """
        self.n = n_qutrits
        if adjacency is None:
            self.adjacency = np.zeros((n_qutrits, n_qutrits), dtype=np.int32)
        else:
            if adjacency.shape != (n_qutrits, n_qutrits):
                raise ValueError(f"Adjacency must be ({n_qutrits}, {n_qutrits})")
            self.adjacency = adjacency.astype(np.int32) % 3

        self.tab: Optional[QutritStabilizerTableau] = None

    def initialize(self) -> None:
        """Initialize the graph state in maximal superposition.

        Applies H₃ to all qutrits, then CZ₃ according to adjacency matrix.
        """
        self.tab = QutritStabilizerTableau(self.n)

        # Apply transversal Hadamard to create maximal superposition
        for i in range(self.n):
            apply_h3(self.tab, i)

        # Apply CZ₃ entanglement according to adjacency matrix
        # Only upper triangle to avoid double-application
        for i in range(self.n):
            for j in range(i + 1, self.n):
                weight = int(self.adjacency[i, j])
                if weight != 0:
                    # Apply CZ₃^weight
                    for _ in range(weight):
                        apply_cz3(self.tab, i, j)

    def encode_features(self, features: List[int]) -> None:
        """Encode ternary input features via phase rotations.

        Args:
            features: List of ternary values in {-1, 0, 1} mapped to {2, 0, 1}
        """
        if self.tab is None:
            raise RuntimeError("Call initialize() first")

        if len(features) != self.n:
            raise ValueError(f"Expected {self.n} features, got {len(features)}")

        for i, val in enumerate(features):
            # Map {-1, 0, 1} to {2, 0, 1} for GF(3)
            gf3_val = (val + 3) % 3
            if gf3_val == 1:
                apply_s3(self.tab, i)
            elif gf3_val == 2:
                apply_s3(self.tab, i)
                apply_s3(self.tab, i)  # S₃²

    def extract_features(self) -> List[int]:
        """Extract output features via stabilizer generator measurements.

        Returns:
            List of ternary feature values in {0, 1, 2}
        """
        if self.tab is None:
            raise RuntimeError("Call initialize() first")

        features = []
        for i in range(self.n):
            # Measure stabilizer generator K_i
            # Expectation value is in {1, ω, ω²} → mapped to {0, 1, 2}
            r = int(self.tab.r[i]) % 3
            features.append(r)

        return features

    def compute_stabilizer_generators(self) -> List[str]:
        """Compute the stabilizer generators K_i for the graph state.

        K_i = X_i · ∏_{j∈N(i)} Z_j^{A_{ij}}

        where N(i) is the neighborhood of vertex i.

        Returns:
            List of stabilizer generator strings
        """
        generators = []
        for i in range(self.n):
            parts = []
            # X on vertex i
            parts.append(f"X{i}")
            # Z on neighbors
            for j in range(self.n):
                if i != j:
                    weight = int(self.adjacency[i, j])
                    if weight == 1:
                        parts.append(f"Z{j}")
                    elif weight == 2:
                        parts.append(f"Z²{j}")
            generators.append(" ".join(parts))
        return generators

    def get_adjacency(self) -> np.ndarray:
        """Get the adjacency matrix."""
        return self.adjacency.copy()

    def __repr__(self) -> str:
        return f"QutritGraphState(n={self.n}, edges={np.count_nonzero(self.adjacency)})"


def cyclic_graph(n: int) -> np.ndarray:
    """Create a cyclic graph adjacency matrix over GF(3).

    Args:
        n: Number of vertices

    Returns:
        Adjacency matrix with edges (i, i+1 mod n)
    """
    adj = np.zeros((n, n), dtype=np.int32)
    for i in range(n):
        j = (i + 1) % n
        adj[i, j] = 1
        adj[j, i] = 1
    return adj


def complete_graph(n: int) -> np.ndarray:
    """Create a complete graph adjacency matrix over GF(3).

    Args:
        n: Number of vertices

    Returns:
        Adjacency matrix with all edges
    """
    adj = np.ones((n, n), dtype=np.int32) - np.eye(n, dtype=np.int32)
    return adj % 3


def star_graph(n: int) -> np.ndarray:
    """Create a star graph adjacency matrix over GF(3).

    Args:
        n: Number of vertices (center is vertex 0)

    Returns:
        Adjacency matrix with edges from center to all others
    """
    adj = np.zeros((n, n), dtype=np.int32)
    for i in range(1, n):
        adj[0, i] = 1
        adj[i, 0] = 1
    return adj


def random_graph(n: int, edge_prob: float = 0.5, seed: int = 42) -> np.ndarray:
    """Create a random graph adjacency matrix over GF(3).

    Args:
        n: Number of vertices
        edge_prob: Probability of each edge existing
        seed: Random seed

    Returns:
        Random adjacency matrix
    """
    rng = np.random.default_rng(seed)
    adj = np.zeros((n, n), dtype=np.int32)
    for i in range(n):
        for j in range(i + 1, n):
            if rng.random() < edge_prob:
                weight = rng.choice([1, 2])
                adj[i, j] = weight
                adj[j, i] = weight
    return adj


def graph_state_projection(
    input_features: List[int],
    graph_type: str = "cyclic",
    n_output: Optional[int] = None
) -> List[int]:
    """Project ternary features using a qutrit graph state.

    This provides a deterministic, weight-free alternative to dense matrix
    multiplication for feature hashing and dimensionality reduction.

    Args:
        input_features: Input ternary values in {-1, 0, 1}
        graph_type: Type of graph ("cyclic", "complete", "star", "random")
        n_output: Number of output features (default: same as input)

    Returns:
        Projected ternary features in {0, 1, 2}
    """
    n_input = len(input_features)
    n_out = n_output or n_input

    # Create graph
    if graph_type == "cyclic":
        adj = cyclic_graph(n_input)
    elif graph_type == "complete":
        adj = complete_graph(n_input)
    elif graph_type == "star":
        adj = star_graph(n_input)
    elif graph_type == "random":
        adj = random_graph(n_input)
    else:
        raise ValueError(f"Unknown graph type: {graph_type}")

    # Create and initialize graph state
    gs = QutritGraphState(n_input, adj)
    gs.initialize()

    # Encode input features
    gs.encode_features(input_features)

    # Extract output features
    output = gs.extract_features()

    # Truncate or pad to desired output size
    if n_out < n_input:
        output = output[:n_out]
    elif n_out > n_input:
        output = output + [0] * (n_out - n_input)

    return output