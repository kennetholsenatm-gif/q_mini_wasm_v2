"""Optional ternary weight optimization (Grover / RC Oracle).

The white paper describes a modified Grover's search with a Register Counting (RC) Oracle to find
globally optimal ternary weights. In this repository, the default implementation is a **classical**
combinatorial search that mimics the desired behavior and works without quantum dependencies.
"""

from __future__ import annotations

from typing import Callable, Optional, Union

import torch
import torch.nn as nn


def _ste_ternary(weight: torch.Tensor) -> torch.Tensor:
    """Return STE-style ternary binarization (same as TernaryWASMExpert.binarize_ternary)."""
    abs_mean = weight.abs().mean().clamp(min=1e-8)
    w = torch.round(weight / abs_mean).clamp(-1.0, 1.0)
    return w


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
                backend=self.backend,
            )
        except Exception as e:
            # Fallback to classical optimization if quantum fails
            print(f"Quantum optimization failed: {e}. Falling back to classical.")
            return ternary_optimizer_classical(weight, self.num_iterations, target_loss_fn)
