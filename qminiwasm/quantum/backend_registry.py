"""Quantum backend factory for the classical–quantum interconnect.

Returns a backend implementation for the chosen provider (PennyLane, IBM, Intel QS).
The interconnect uses run_forward(compressed_state, gammas, betas) to execute the
circuit and obtain routing weights; gradients flow via PennyLane's parameter-shift.
"""

from __future__ import annotations

from typing import Any, Callable, Protocol

import torch


class QuantumBackend(Protocol):
    """Protocol for a quantum backend used by the interconnect."""

    def run_forward(
        self,
        compressed_state: torch.Tensor,
        gammas: torch.Tensor,
        betas: torch.Tensor,
    ) -> torch.Tensor:
        """Run the QAOA circuit and return routing weights (e.g. Pauli-Z expectations mapped to [0,1])."""
        ...


class PennyLaneBackend:
    """PennyLane default.qubit backend; differentiable via parameter-shift."""

    def __init__(
        self, num_qubits: int = 8, qaoa_layers: int = 3, diff_method: str = "parameter-shift"
    ):
        self.num_qubits = num_qubits
        self.qaoa_layers = qaoa_layers
        self._diff_method = diff_method
        self._qnode: Callable[..., Any] | None = None

    def _get_qnode(self) -> Callable[..., Any]:
        if self._qnode is not None:
            return self._qnode
        import pennylane as qml

        nq = self.num_qubits
        layers = self.qaoa_layers
        dev = qml.device("default.qubit", wires=nq)

        def qaoa_layer(gamma, beta, compressed_affinities, penalty_factor):
            for i in range(nq):
                qml.RZ(gamma * compressed_affinities[i], wires=i)
            for i in range(nq):
                for j in range(i + 1, nq):
                    qml.IsingZZ(gamma * penalty_factor, wires=[i, j])
            for i in range(nq):
                qml.RX(beta, wires=i)

        @qml.qnode(dev, interface="torch", diff_method=self._diff_method)
        def circuit(gammas, betas, compressed_affinities):
            for i in range(nq):
                qml.Hadamard(wires=i)
            for i in range(nq):
                qml.RY(compressed_affinities[i], wires=i)
            for p in range(layers):
                qaoa_layer(gammas[p], betas[p], compressed_affinities, penalty_factor=2.5)
            return [qml.expval(qml.PauliZ(i)) for i in range(nq)]

        self._qnode = circuit
        return self._qnode

    def run_forward(
        self,
        compressed_state: torch.Tensor,
        gammas: torch.Tensor,
        betas: torch.Tensor,
    ) -> torch.Tensor:
        qnode = self._get_qnode()
        exp_vals = qnode(gammas, betas, compressed_state)
        routing_weights = torch.stack([(v + 1.0) / 2.0 for v in exp_vals])
        return routing_weights


class IBMQuantumBackend:
    """Stub for IBM Quantum API; to be filled with real API calls."""

    def run_forward(
        self,
        compressed_state: torch.Tensor,
        gammas: torch.Tensor,
        betas: torch.Tensor,
    ) -> torch.Tensor:
        raise NotImplementedError(
            "IBM Quantum backend not implemented; use penny_lane for training."
        )


class IntelQSBackend:
    """Stub for Intel Quantum SDK / Intel QS; to be filled with real simulator calls."""

    def run_forward(
        self,
        compressed_state: torch.Tensor,
        gammas: torch.Tensor,
        betas: torch.Tensor,
    ) -> torch.Tensor:
        raise NotImplementedError("Intel QS backend not implemented; use penny_lane for training.")


_BACKEND_FACTORIES = {
    "penny_lane": lambda **kw: PennyLaneBackend(**kw),
    "ibm_quantum": lambda **kw: IBMQuantumBackend(),
    "intel_qs": lambda **kw: IntelQSBackend(),
}


def get_backend(
    backend_id: str,
    num_qubits: int = 8,
    qaoa_layers: int = 3,
    diff_method: str = "parameter-shift",
) -> QuantumBackend:
    """Return a quantum backend for the given id and circuit config.

    Args:
        backend_id: One of "penny_lane", "ibm_quantum", "intel_qs".
        num_qubits: Number of qubits (used by PennyLane).
        qaoa_layers: QAOA layers (used by PennyLane).
        diff_method: "parameter-shift" or "finite-diff" (used by PennyLane).

    Returns:
        Backend instance with run_forward(...).
    """
    if backend_id not in _BACKEND_FACTORIES:
        raise ValueError(
            f"Unknown quantum backend: {backend_id}. Choose from {list(_BACKEND_FACTORIES)}."
        )
    return _BACKEND_FACTORIES[backend_id](
        num_qubits=num_qubits,
        qaoa_layers=qaoa_layers,
        diff_method=diff_method,
    )
