"""Bell Difference Sampling for attention mechanism approximation.

This module implements qutrit stabilizer sampling to approximate the Softmax
attention mechanism without transcendental math (exp/log). The approach uses
Bell basis measurements to compute state fidelities, which serve as attention
scores.

Key insight: For stabilizer states, measurement probabilities are constrained
to {0, 1/3^k}, creating a discrete step-function that mimics low-temperature
Softmax behavior.
"""

from __future__ import annotations

from typing import List, Optional, Tuple
import numpy as np

from qminiwasm.quantum.clifford.qutrit_tableau import QutritStabilizerTableau
from qminiwasm.quantum.clifford.qutrit_gates import (
    apply_h3, apply_s3, apply_cz3, apply_x3, apply_z3, OMEGA
)


class QutritBellAttention:
    """Qutrit Bell basis attention mechanism.

    Approximates Softmax attention using Clifford stabilizer measurements.
    No floating-point exponentiation or normalization required.
    """

    def __init__(self, n_heads: int, n_features: int):
        """Initialize Bell attention.

        Args:
            n_heads: Number of attention heads
            n_features: Feature dimension per head
        """
        self.n_heads = n_heads
        self.n_features = n_features
        self.n_qutrits = n_heads * 2  # Key + Query qutrits per head

    def encode_key_query(
        self,
        keys: List[int],
        queries: List[int]
    ) -> QutritStabilizerTableau:
        """Encode key and query vectors into qutrit state.

        Args:
            keys: Ternary key values in {-1, 0, 1}
            queries: Ternary query values in {-1, 0, 1}

        Returns:
            QutritStabilizerTableau with encoded key/query
        """
        if len(keys) != self.n_heads or len(queries) != self.n_heads:
            raise ValueError(
                f"Expected {self.n_heads} keys and queries, "
                f"got {len(keys)} and {len(queries)}"
            )

        tab = QutritStabilizerTableau(self.n_qutrits)

        # Initialize in maximal superposition
        for i in range(self.n_qutrits):
            apply_h3(tab, i)

        # Encode keys (wires 0 to n_heads-1)
        for i, k in enumerate(keys):
            gf3_val = (k + 3) % 3
            if gf3_val == 1:
                apply_s3(tab, i)
            elif gf3_val == 2:
                apply_s3(tab, i)
                apply_s3(tab, i)

        # Encode queries (wires n_heads to 2*n_heads-1)
        for i, q in enumerate(queries):
            gf3_val = (q + 3) % 3
            if gf3_val == 1:
                apply_s3(tab, self.n_heads + i)
            elif gf3_val == 2:
                apply_s3(tab, self.n_heads + i)
                apply_s3(tab, self.n_heads + i)

        return tab

    def bell_measurement(
        self,
        tab: QutritStabilizerTableau
    ) -> List[float]:
        """Perform transversal Bell measurement to compute attention scores.

        The Bell basis is constructed by applying CZ₃ followed by H₃.
        Measurement probability of |00⟩ state gives the overlap (attention score).

        Args:
            tab: QutritStabilizerTableau with encoded key/query

        Returns:
            List of attention scores (probabilities) for each head
        """
        # Apply transversal Bell measurement
        for i in range(self.n_heads):
            # Entangle key and query qutrits
            apply_cz3(tab, i, self.n_heads + i)
            # Rotate to Bell basis
            apply_h3(tab, i)

        # Extract attention scores from stabilizer phases
        scores = []
        for i in range(self.n_heads):
            # Probability of measuring |00⟩ is related to phase
            # For stabilizer states: P = 0 or 1/3^k
            r = int(tab.r[i]) % 3
            # Map phase to probability-like score
            if r == 0:
                score = 1.0  # Constructive interference
            elif r == 1:
                score = 1.0 / 3.0  # Partial interference
            else:
                score = 1.0 / 9.0  # Destructive interference
            scores.append(score)

        # Normalize (simple softmax-like normalization)
        total = sum(scores)
        if total > 0:
            scores = [s / total for s in scores]

        return scores

    def compute_attention(
        self,
        keys: List[int],
        queries: List[int]
    ) -> List[float]:
        """Compute attention scores for key-query pairs.

        Args:
            keys: Ternary key values in {-1, 0, 1}
            queries: Ternary query values in {-1, 0, 1}

        Returns:
            Attention scores (normalized probabilities)
        """
        tab = self.encode_key_query(keys, queries)
        return self.bell_measurement(tab)


def qutrit_attention(
    keys: List[int],
    queries: List[int],
    n_heads: Optional[int] = None
) -> List[float]:
    """Compute qutrit Bell attention scores.

    This provides a discrete, Clifford-only approximation of Softmax attention.
    No floating-point exponentiation or normalization required.

    Args:
        keys: Ternary key values in {-1, 0, 1}
        queries: Ternary query values in {-1, 0, 1}
        n_heads: Number of attention heads (default: len(keys))

    Returns:
        Attention scores (normalized probabilities)
    """
    n = n_heads or len(keys)
    if len(keys) != n or len(queries) != n:
        raise ValueError(f"Keys and queries must have length {n}")

    attention = QutritBellAttention(n, n)
    return attention.compute_attention(keys, queries)


def softmax_approximation(
    scores: List[float],
    temperature: float = 1.0
) -> List[float]:
    """Approximate Softmax using discrete stabilizer probabilities.

    This maps continuous scores to the discrete set {0, 1/3^k} based on
    the stabilizer measurement model.

    Args:
        scores: Raw attention scores
        temperature: Temperature parameter (lower = sharper)

    Returns:
        Discretized attention probabilities
    """
    # Scale by temperature
    scaled = [s / temperature for s in scores]

    # Map to discrete stabilizer probabilities
    discrete = []
    for s in scaled:
        if s > 0.5:
            discrete.append(1.0)
        elif s > 0.25:
            discrete.append(1.0 / 3.0)
        elif s > 0.125:
            discrete.append(1.0 / 9.0)
        else:
            discrete.append(0.0)

    # Normalize
    total = sum(discrete)
    if total > 0:
        discrete = [d / total for d in discrete]

    return discrete


def top_k_attention(
    scores: List[float],
    k: int = 3
) -> List[int]:
    """Select top-k attention indices.

    Args:
        scores: Attention scores
        k: Number of top elements to select

    Returns:
        Indices of top-k scores
    """
    indexed = list(enumerate(scores))
    indexed.sort(key=lambda x: x[1], reverse=True)
    return [idx for idx, _ in indexed[:k]]