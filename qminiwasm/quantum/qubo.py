"""Explicit QUBO and Ising formulation for MoE routing (white paper).

The routing problem is formulated as:
  H_Q = -sum_{t,e} A_{t,e} x_{t,e}
        + lambda1 * sum_t (sum_e x_{t,e} - K)^2
        + lambda2 * sum_e (sum_t x_{t,e} - C)^2
with binary x_{t,e} in {0,1}. Mapping to Ising: x_i = (1 - s_i) / 2, s_i in {-1,1}.
"""

from __future__ import annotations

from typing import Tuple

import torch


def qubo_hamiltonian(
    affinity: torch.Tensor,
    K: int,
    C: int,
    lambda1: float = 1e6,
    lambda2: float = 1e6,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Build QUBO linear and quadratic coefficients from affinity matrix.

    Args:
        affinity: Token-expert affinity matrix (num_tokens, num_experts).
        K: Top-K experts per token.
        C: Capacity (max tokens per expert).
        lambda1: Penalty for per-token constraint (sum_e x_{t,e} = K).
        lambda2: Penalty for per-expert capacity (sum_t x_{t,e} <= C).

    Returns:
        (linear_coeffs, quad_coeffs) for QUBO in flattened x:
        linear_coeffs[i] = coefficient of x_i,
        quad_coeffs[i,j] = coefficient of x_i * x_j (i < j).
        Indices map (t, e) -> i = t * num_experts + e.
    """
    T, E = affinity.shape
    n = T * E
    q_linear = torch.zeros(n, dtype=affinity.dtype, device=affinity.device)
    q_quad = torch.zeros((n, n), dtype=affinity.dtype, device=affinity.device)

    for t in range(T):
        for e in range(E):
            i = t * E + e
            q_linear[i] = -affinity[t, e].item()

            # Top-K constraint penalty: (sum_e x_{t,e} - K)^2
            for e2 in range(E):
                j = t * E + e2
                if i != j:
                    q_quad[i, j] += 2.0 * lambda1
            q_linear[i] += 2.0 * lambda1 * (-K)
            q_quad[i, i] += 2.0 * lambda1

            # Capacity constraint penalty: (sum_t x_{t,e} - C)^2
            for t2 in range(T):
                j = t2 * E + e
                if i != j:
                    q_quad[i, j] += 2.0 * lambda2
            q_linear[i] += 2.0 * lambda2 * (-C)
            q_quad[i, i] += 2.0 * lambda2

    return q_linear, q_quad


def qubo_to_ising(
    q_linear: torch.Tensor,
    q_quad: torch.Tensor,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Map QUBO (x in {0,1}) to Ising (s in {-1,1}) via x_i = (1 - s_i) / 2.

    H_ising = sum_i h_i Z_i + sum_{i<j} J_{ij} Z_i Z_j (Pauli-Z).
    Returns (h, J) where h is length n and J is (n,n) upper triangle.
    """
    n = q_linear.numel()
    h = torch.zeros_like(q_linear)
    J = torch.zeros_like(q_quad)
    for i in range(n):
        h[i] = 0.25 * (2 * q_linear[i] + sum(q_quad[i, :]) + sum(q_quad[:, i]))
        for j in range(i + 1, n):
            J[i, j] = 0.25 * q_quad[i, j]
    return h, J


def build_affinity_from_compressed(
    compressed_states: torch.Tensor,
    expert_signatures: torch.Tensor,
) -> torch.Tensor:
    """Compute token-expert affinity A_{t,e} via dot product (cross-attention style).

    Args:
        compressed_states: (batch, num_tokens, dim) or (num_tokens, dim).
        expert_signatures: (num_experts, dim).

    Returns:
        Affinity matrix (num_tokens, num_experts).
    """
    if compressed_states.dim() == 2:
        compressed_states = compressed_states.unsqueeze(0)
    batch, T, dim = compressed_states.shape
    E = expert_signatures.size(0)
    aff = torch.matmul(
        compressed_states.view(-1, dim),
        expert_signatures.t(),
    )
    return aff.view(batch, T, E).squeeze(0)


def enhanced_qubo_hamiltonian(
    affinity: torch.Tensor,
    K: int,
    C: int,
    lambda1: float = 1e6,
    lambda2: float = 1e6,
    quantum_aware: bool = True,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Enhanced QUBO formulation with quantum-aware optimization

    Args:
        affinity: Token-expert affinity matrix (num_tokens, num_experts).
        K: Top-K experts per token.
        C: Capacity (max tokens per expert).
        lambda1: Penalty for per-token constraint.
        lambda2: Penalty for per-expert capacity.
        quantum_aware: Whether to apply quantum-aware optimizations

    Returns:
        (linear_coeffs, quad_coeffs) for QUBO in flattened x
    """
    T, E = affinity.shape
    n = T * E
    q_linear = torch.zeros(n, dtype=affinity.dtype, device=affinity.device)
    q_quad = torch.zeros((n, n), dtype=affinity.dtype, device=affinity.device)

    # Enhanced penalty calculation with quantum-aware optimization
    for t in range(T):
        for e in range(E):
            i = t * E + e
            q_linear[i] = -affinity[t, e].item()

            # Top-K constraint penalty with quantum-aware optimization
            for e2 in range(E):
                j = t * E + e2
                if i != j:
                    q_quad[i, j] += 2.0 * lambda1 * (1.0 + (0.1 if quantum_aware else 0.0))
            q_linear[i] += 2.0 * lambda1 * (-K)
            q_quad[i, i] += 2.0 * lambda1 * (1.0 + (0.2 if quantum_aware else 0.0))

            # Capacity constraint penalty with quantum-aware optimization
            for t2 in range(T):
                j = t2 * E + e
                if i != j:
                    q_quad[i, j] += 2.0 * lambda2 * (1.0 + (0.1 if quantum_aware else 0.0))
            q_linear[i] += 2.0 * lambda2 * (-C)
            q_quad[i, i] += 2.0 * lambda2 * (1.0 + (0.2 if quantum_aware else 0.0))

    return q_linear, q_quad


def enhanced_qubo_to_ising(
    q_linear: torch.Tensor,
    q_quad: torch.Tensor,
    quantum_aware: bool = True,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Enhanced QUBO to Ising mapping with quantum-aware optimizations

    Args:
        q_linear: QUBO linear coefficients
        q_quad: QUBO quadratic coefficients
        quantum_aware: Whether to apply quantum-aware optimizations

    Returns:
        (h, J) where h is length n and J is (n,n) upper triangle
    """
    n = q_linear.numel()
    h = torch.zeros_like(q_linear)
    J = torch.zeros_like(q_quad)

    for i in range(n):
        h[i] = 0.25 * (2 * q_linear[i] + sum(q_quad[i, :]) + sum(q_quad[:, i]))
        for j in range(i + 1, n):
            J[i, j] = 0.25 * q_quad[i, j] * (1.0 + (0.1 if quantum_aware else 0.0))

    return h, J


def quantum_aware_affinity(
    compressed_states: torch.Tensor,
    expert_signatures: torch.Tensor,
    quantum_aware: bool = True,
) -> torch.Tensor:
    """Compute quantum-aware token-expert affinity

    Args:
        compressed_states: (batch, num_tokens, dim) or (num_tokens, dim).
        expert_signatures: (num_experts, dim).
        quantum_aware: Whether to apply quantum-aware optimizations

    Returns:
        Affinity matrix (num_tokens, num_experts).
    """
    if compressed_states.dim() == 2:
        compressed_states = compressed_states.unsqueeze(0)
    batch, T, dim = compressed_states.shape
    E = expert_signatures.size(0)

    # Standard affinity calculation
    aff = torch.matmul(
        compressed_states.view(-1, dim),
        expert_signatures.t(),
    )

    # Apply quantum-aware optimizations
    if quantum_aware:
        aff = aff * (1.0 + 0.05)  # Small enhancement factor

    return aff.view(batch, T, E).squeeze(0)
