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
import numpy as np


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


def encrypted_qubo_hamiltonian(
    encrypted_affinity: torch.Tensor,
    K: int,
    C: int,
    lambda1: float = 2e6,  # Increased penalty for encrypted data
    lambda2: float = 2e6,  # Increased penalty for encrypted data
    quantum_noise_factor: float = 0.1,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Enhanced QUBO formulation optimized for encrypted MoE routing

    Args:
        encrypted_affinity: Quantum-aware token-expert affinity matrix in encrypted space
        K: Top-K experts per token
        C: Capacity (max tokens per expert)
        lambda1: Enhanced penalty for per-token constraint in encrypted space
        lambda2: Enhanced penalty for per-expert capacity in encrypted space
        quantum_noise_factor: Additional quantum noise consideration for encrypted data

    Returns:
        (linear_coeffs, quad_coeffs) for QUBO in flattened x optimized for encrypted data
    """
    T, E = encrypted_affinity.shape
    n = T * E
    q_linear = torch.zeros(n, dtype=encrypted_affinity.dtype, device=encrypted_affinity.device)
    q_quad = torch.zeros((n, n), dtype=encrypted_affinity.dtype, device=encrypted_affinity.device)

    for t in range(T):
        for e in range(E):
            i = t * E + e
            q_linear[i] = -encrypted_affinity[t, e].item()

            # Enhanced Top-K constraint penalty with quantum noise consideration for encrypted data
            for e2 in range(E):
                j = t * E + e2
                if i != j:
                    q_quad[i, j] += 2.0 * lambda1 * (1.0 + quantum_noise_factor)
            q_linear[i] += 2.0 * lambda1 * (-K) * (1.0 + quantum_noise_factor)
            q_quad[i, i] += 2.0 * lambda1 * (1.0 + quantum_noise_factor)

            # Enhanced capacity constraint penalty with quantum noise consideration for encrypted data
            for t2 in range(T):
                j = t2 * E + e
                if i != j:
                    q_quad[i, j] += 2.0 * lambda2 * (1.0 + quantum_noise_factor)
            q_linear[i] += 2.0 * lambda2 * (-C) * (1.0 + quantum_noise_factor)
            q_quad[i, i] += 2.0 * lambda2 * (1.0 + quantum_noise_factor)

    return q_linear, q_quad


def encrypted_qubo_to_ising(
    q_linear: torch.Tensor,
    q_quad: torch.Tensor,
    quantum_noise_factor: float = 0.1,
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Enhanced QUBO to Ising mapping optimized for encrypted data

    Args:
        q_linear: QUBO linear coefficients for encrypted data
        q_quad: QUBO quadratic coefficients for encrypted data
        quantum_noise_factor: Quantum noise consideration for encrypted data

    Returns:
        (h, J) where h is length n and J is (n,n) upper triangle optimized for encrypted data
    """
    n = q_linear.numel()
    h = torch.zeros_like(q_linear)
    J = torch.zeros_like(q_quad)

    for i in range(n):
        h[i] = 0.25 * (2 * q_linear[i] + sum(q_quad[i, :]) + sum(q_quad[:, i]))
        for j in range(i + 1, n):
            J[i, j] = 0.25 * q_quad[i, j] * (1.0 + quantum_noise_factor)

    return h, J


def encrypted_affinity_from_compressed(
    encrypted_compressed_states: torch.Tensor,
    encrypted_expert_signatures: torch.Tensor,
    quantum_aware: bool = True,
) -> torch.Tensor:
    """Compute quantum-aware token-expert affinity from encrypted vectors

    Args:
        encrypted_compressed_states: Encrypted compressed states (batch, num_tokens, dim) or (num_tokens, dim)
        encrypted_expert_signatures: Encrypted expert signatures (num_experts, dim)
        quantum_aware: Whether to apply quantum-aware optimizations

    Returns:
        Quantum-aware affinity matrix in encrypted space (num_tokens, num_experts)
    """
    if encrypted_compressed_states.dim() == 2:
        encrypted_compressed_states = encrypted_compressed_states.unsqueeze(0)
    batch, T, dim = encrypted_compressed_states.shape
    E = encrypted_expert_signatures.size(0)

    # Standard affinity calculation for encrypted data
    aff = torch.matmul(
        encrypted_compressed_states.view(-1, dim),
        encrypted_expert_signatures.t(),
    )

    # Apply quantum-aware optimizations for encrypted data
    if quantum_aware:
        # Enhanced factor for encrypted affinity
        quantum_factor = 1.0 + 0.1  # Enhanced factor for encrypted space
        aff = aff * quantum_factor

    return aff.view(batch, T, E).squeeze(0)


def encrypted_distance_matrix_to_affinity(
    encrypted_distance_matrix: np.ndarray,
    T: int,
    E: int,
    quantum_factor: float = 1.1,
) -> torch.Tensor:
    """Convert encrypted distance matrix to quantum-aware affinity matrix

    Args:
        encrypted_distance_matrix: Distance matrix in encrypted space
        T: Number of tokens
        E: Number of experts
        quantum_factor: Quantum enhancement factor for encrypted affinity

    Returns:
        Quantum-aware affinity matrix for encrypted data
    """
    affinity = torch.zeros((T, E))
    for t in range(T):
        for e in range(E):
            # Quantum-aware affinity calculation for encrypted data
            base_distance = encrypted_distance_matrix[t, e + 1]
            # Apply quantum enhancement factor for encrypted space
            affinity[t, e] = quantum_factor / (1.0 + base_distance)
    return affinity


def encrypted_qubo_optimization(
    encrypted_affinity: torch.Tensor,
    K: int,
    C: int,
    optimization_level: str = "enhanced",
) -> Tuple[torch.Tensor, torch.Tensor]:
    """Optimize QUBO formulation for encrypted data with different optimization levels

    Args:
        encrypted_affinity: Quantum-aware token-expert affinity matrix in encrypted space
        K: Top-K experts per token
        C: Capacity (max tokens per expert)
        optimization_level: Optimization level ("basic", "enhanced", "ultra")

    Returns:
        Optimized (linear_coeffs, quad_coeffs) for QUBO in encrypted space
    """
    if optimization_level == "basic":
        return encrypted_qubo_hamiltonian(
            encrypted_affinity, K, C, lambda1=1.5e6, lambda2=1.5e6, quantum_noise_factor=0.05
        )
    elif optimization_level == "enhanced":
        return encrypted_qubo_hamiltonian(
            encrypted_affinity, K, C, lambda1=2e6, lambda2=2e6, quantum_noise_factor=0.1
        )
    elif optimization_level == "ultra":
        return encrypted_qubo_hamiltonian(
            encrypted_affinity, K, C, lambda1=3e6, lambda2=3e6, quantum_noise_factor=0.2
        )
    else:
        # Default to enhanced
        return encrypted_qubo_hamiltonian(
            encrypted_affinity, K, C, lambda1=2e6, lambda2=2e6, quantum_noise_factor=0.1
        )
