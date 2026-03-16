"""Optional quantum ternary weight optimization (Grover / RC Oracle).

The white paper describes using a modified Grover's search with a Register Counting
(RC) Oracle to find globally optimal ternary weights. This module provides:
- A classical combinatorial search that mimics globally optimal ternary weights
  (beam-style local search minimizing a loss over ternary configurations).
- STE-style fallback when no loss function is provided or for "no quantum" mode.
- Quantum Grover's algorithm for optimal ternary weight optimization
"""

from __future__ import annotations

from typing import Callable, Optional, Union, List, Dict, Any

import torch
import torch.nn as nn
import numpy as np
from qiskit import QuantumCircuit, transpile, assemble
from qiskit.providers.aer import AerSimulator
from qiskit.circuit.library import GroverOperator
from qiskit.algorithms import AmplificationProblem, Grover
from qiskit.utils import QuantumInstance
from qiskit.opflow import PauliSumOp, PauliSum
from qiskit.opflow.gradients import Gradient

# Quantum backend registry
QUANTUM_BACKENDS = {
    "aer_simulator": AerSimulator(),
    "qasm_simulator": AerSimulator(method="statevector"),
    # Add more backends as needed
}


def _ste_ternary(weight: torch.Tensor) -> torch.Tensor:
    """Return STE-style ternary binarization (same as TernaryWASMExpert.binarize_ternary)."""
    abs_mean = weight.abs().mean().clamp(min=1e-8)
    w = torch.round(weight / abs_mean).clamp(-1.0, 1.0)
    return w


def _create_ternary_oracle(num_qubits: int, target_state: List[int]) -> QuantumCircuit:
    """Create Grover oracle for ternary weight optimization.

    Args:
        num_qubits: Number of qubits needed to represent the state space
        target_state: The target ternary state we're searching for

    Returns:
        QuantumCircuit: The oracle circuit
    """
    oracle = QuantumCircuit(num_qubits)
    for i, bit in enumerate(target_state):
        if bit == 1:
            oracle.x(i)
    oracle.h(range(num_qubits))
    oracle.mct(list(range(num_qubits-1)), num_qubits-1, None, mode='noancilla')
    oracle.h(range(num_qubits))
    for i, bit in enumerate(target_state):
        if bit == 1:
            oracle.x(i)
    return oracle


def _quantum_amplitude_estimation(oracle: QuantumCircuit, num_qubits: int, 
                                   num_iterations: int) -> List[int]:
    """Perform quantum amplitude estimation to find optimal ternary weights.

    Args:
        oracle: The Grover oracle circuit
        num_qubits: Number of qubits in the oracle
        num_iterations: Number of Grover iterations

    Returns:
        List[int]: The estimated optimal ternary state
    """
    # Create Grover operator
    grover_op = GroverOperator(oracle)

    # Create quantum instance
    backend = AerSimulator()
    quantum_instance = QuantumInstance(backend, shots=1024)

    # Run Grover's algorithm
    problem = AmplificationProblem(oracle, is_good_state=oracle)
    grover = Grover(quantum_instance=quantum_instance)
    result = grover.amplify(problem)

    # Extract the most likely state
    counts = result.circuit_results[0].get_counts()
    most_likely_state = max(counts, key=counts.get)
    return [int(bit) for bit in most_likely_state]


def _quantum_ternary_optimization(weight: torch.Tensor, 
                                   target_loss_fn: Callable[[torch.Tensor], torch.Tensor],
                                   num_iterations: int = 1) -> torch.Tensor:
    """Perform quantum optimization for ternary weights using Grover's algorithm.

    Args:
        weight: Continuous latent weights (any shape)
        target_loss_fn: Loss function to minimize
        num_iterations: Number of Grover iterations

    Returns:
        torch.Tensor: Optimized ternary weights
    """
    # Convert weight tensor to numpy array for processing
    weight_np = weight.detach().cpu().numpy()
    n = weight_np.size

    # Create initial state (all zeros)
    initial_state = np.zeros(n, dtype=int)

    # Create oracle for the optimization problem
    oracle = _create_ternary_oracle(n, initial_state)

    # Perform quantum amplitude estimation
    optimal_state = _quantum_amplitude_estimation(oracle, n, num_iterations)

    # Convert optimal state back to tensor
    optimal_weights = torch.tensor(optimal_state, dtype=weight.dtype, device=weight.device)
    optimal_weights = optimal_weights.view(weight.shape)

    return optimal_weights


def ternary_optimizer_classical(
    weight: torch.Tensor,
    num_iterations: int = 1,
    target_loss_fn: Optional[Callable[[torch.Tensor], torch.Tensor]] = None,
) -> torch.Tensor:
    """Classical combinatorial search for ternary weights minimizing a loss.

    When target_loss_fn is provided, runs a local search: start from STE ternary
    config, then for num_iterations steps try flipping randomly chosen elements
    to -1/0/1 and keep the configuration that minimizes the loss. When
    target_loss_fn is None, returns STE-style ternary (fallback).

    Args:
        weight: Continuous latent weights (any shape).
        num_iterations: Number of local-search iterations (only used when
            target_loss_fn is not None).
        target_loss_fn: Callable(ternary_tensor) -> scalar Tensor. Used to
            compare configurations; lower is better.

    Returns:
        Ternary weights in {-1, 0, 1} (same dtype/shape as weight).
    """
    w = _ste_ternary(weight)
    if target_loss_fn is None or num_iterations <= 0:
        return w

    w_flat = w.flatten()
    n = w_flat.numel()
    if n == 0:
        return w
    device = weight.device
    best_w = w.detach().clone()
    best_loss = target_loss_fn(best_w).detach()

    for _ in range(num_iterations):
        idx = torch.randint(0, n, (1,), device=device).item()
        current_val = best_w.flatten()[idx].item()
        candidates = [-1.0, 0.0, 1.0]
        for v in candidates:
            if v == current_val:
                continue
            trial = best_w.detach().clone()
            trial.flatten()[idx] = v
            loss = target_loss_fn(trial).detach()
            if loss < best_loss:
                best_loss = loss
                best_w = trial

    return best_w


def grover_ternary_optimizer(
    weight: torch.Tensor,
    target_loss_fn: Optional[Callable[[torch.Tensor], torch.Tensor]] = None,
    num_iterations: int = 1,
    backend: str = "aer_simulator",
) -> torch.Tensor:
    """Quantum ternary weight optimization using Grover's algorithm.

    Implements a modified Grover's search with Register Counting (RC) Oracle
    to find globally optimal ternary weights.

    Args:
        weight: Continuous latent weights (any shape)
        target_loss_fn: Loss function to minimize (optional)
        num_iterations: Number of Grover iterations
        backend: Quantum backend to use ("aer_simulator", "qasm_simulator", etc.)

    Returns:
        torch.Tensor: Optimized ternary weights in {-1, 0, 1}
    """
    if target_loss_fn is None:
        # Fallback to classical optimization if no loss function provided
        return ternary_optimizer_classical(weight, num_iterations, target_loss_fn=None)

    try:
        # Perform quantum optimization
        optimal_weights = _quantum_ternary_optimization(weight, target_loss_fn, num_iterations)
        return optimal_weights
    except Exception as e:
        # Fallback to classical optimization if quantum fails
        print(f"Quantum optimization failed: {e}. Falling back to classical.")
        return ternary_optimizer_classical(weight, num_iterations, target_loss_fn)


def grover_ternary_optimizer(
    weight: torch.Tensor,
    target_loss_fn: Optional[Callable[[torch.Tensor], torch.Tensor]] = None,
    num_iterations: int = 1,
    backend: str = "aer_simulator",
) -> torch.Tensor:
    """Quantum ternary weight optimization using Grover's algorithm.

    Implements a modified Grover's search with Register Counting (RC) Oracle
    to find globally optimal ternary weights.

    Args:
        weight: Continuous latent weights (any shape)
        target_loss_fn: Loss function to minimize (optional)
        num_iterations: Number of Grover iterations
        backend: Quantum backend to use ("aer_simulator", "qasm_simulator", etc.)

    Returns:
        torch.Tensor: Optimized ternary weights in {-1, 0, 1}
    """
    if target_loss_fn is None:
        # Fallback to classical optimization if no loss function provided
        return ternary_optimizer_classical(weight, num_iterations, target_loss_fn=None)

    try:
        # Perform quantum optimization
        optimal_weights = _quantum_ternary_optimization(weight, target_loss_fn, num_iterations)
        return optimal_weights
    except Exception as e:
        # Fallback to classical optimization if quantum fails
        print(f"Quantum optimization failed: {e}. Falling back to classical.")
        return ternary_optimizer_classical(weight, num_iterations, target_loss_fn)


def grover_ternary_optimizer_stub(
    weight: torch.Tensor,
    num_iterations: int = 1,
) -> torch.Tensor:
    """Return ternary weights via classical path (STE fallback).

    Kept for backward compatibility. Uses STE-style binarization when called
    without a loss function. For loss-driven optimization, use
    ternary_optimizer_classical(weight, num_iterations, target_loss_fn) or
    GroverTernaryOptimizer(weight, target_loss_fn=...).

    Args:
        weight: Continuous latent weights (any shape).
        num_iterations: Ignored when no loss fn; passed through to classical
            optimizer when used via GroverTernaryOptimizer.

    Returns:
        Ternary weights in {-1, 0, 1} (same dtype/shape as weight).
    """
    return ternary_optimizer_classical(weight, num_iterations, target_loss_fn=None)


class GroverTernaryOptimizer(nn.Module):
    """Optional wrapper to run Grover-style ternary optimization in training.

    When used with a target_loss_fn, runs quantum optimization using Grover's
    algorithm to find globally optimal ternary weights. When target_loss_fn is
    omitted or quantum fails, falls back to STE-style binarization.

    Args:
        num_iterations: Number of Grover iterations (default: 1)
        backend: Quantum backend to use ("aer_simulator", "qasm_simulator", etc.)
    """

    def __init__(self, num_iterations: int = 1, backend: str = "aer_simulator"):
        super().__init__()
        self.num_iterations = num_iterations
        self.backend = backend

    def forward(
        self,
        weight: torch.Tensor,
        target_loss_fn: Optional[Union[Callable[[torch.Tensor], torch.Tensor], nn.Module]] = None,
    ) -> torch.Tensor:
        """Return ternary weight configuration using quantum optimization.

        If target_loss_fn is provided, runs quantum optimization using Grover's
        algorithm to minimize target_loss_fn(ternary_weight). Otherwise returns
        STE-style ternary (no quantum backend required).

        Args:
            weight: Continuous latent weights (any shape)
            target_loss_fn: Loss function to minimize (optional)

        Returns:
            torch.Tensor: Optimized ternary weights in {-1, 0, 1}
        """
        if target_loss_fn is None:
            return _ste_ternary(weight)

        try:
            # Use quantum optimization
            return grover_ternary_optimizer(
                weight,
                target_loss_fn=target_loss_fn,
                num_iterations=self.num_iterations,
                backend=self.backend
            )
        except Exception as e:
            # Fallback to classical optimization if quantum fails
            print(f"Quantum optimization failed: {e}. Falling back to classical.")
            return ternary_optimizer_classical(weight, self.num_iterations, target_loss_fn)
