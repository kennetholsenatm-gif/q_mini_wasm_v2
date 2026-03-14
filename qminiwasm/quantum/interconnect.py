"""Classical–quantum interconnect (The Bridge).

Runs the quantum circuit in the forward pass with classical inputs and exposes
backward via PennyLane's parameter-shift rule so gradients flow to the compressor
and QAOA parameters. Uses the quantum backend registry for backend selection.
"""

from __future__ import annotations

from typing import Any

import torch
import torch.nn as nn

from .backend_registry import QuantumBackend, get_backend


class ClassicalQuantumInterconnect(nn.Module):
    """Bridge between classical PyTorch and the QPU/simulator.

    Forward: compressed_state (from classical compressor), gammas, betas -> routing_weights.
    Backward: parameter-shift gradients flow from the loss into compressor and QAOA params.
    """

    def __init__(
        self,
        backend_id: str = "penny_lane",
        num_qubits: int = 8,
        qaoa_layers: int = 3,
        diff_method: str = "parameter-shift",
    ):
        super().__init__()
        self.backend_id = backend_id
        self._backend: QuantumBackend = get_backend(
            backend_id=backend_id,
            num_qubits=num_qubits,
            qaoa_layers=qaoa_layers,
            diff_method=diff_method,
        )

    def forward(
        self,
        compressed_state: torch.Tensor,
        gammas: torch.Tensor,
        betas: torch.Tensor,
    ) -> torch.Tensor:
        """Run the quantum circuit and return routing weights (differentiable)."""
        return self._backend.run_forward(compressed_state, gammas, betas)
