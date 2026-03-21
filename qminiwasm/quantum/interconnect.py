"""Classical–quantum interconnect (The Bridge) and Tier 2 state migration.

- ClassicalQuantumInterconnect: routing bridge (compressor -> QAOA -> weights).
- StateMigrationInterconnect: accepts delta payload from Tier 1, feeds deltas
  into HullKVCache on the cloud side (in-process only for now).
"""

from __future__ import annotations

from typing import Any, Callable, Dict, List, Optional, Tuple

import torch
import torch.nn as nn

from .backend_registry import QuantumBackend, get_backend


class StateMigrationInterconnect:
    """Tier 2 state migration: accept escalation payload, yield (addr, value) for HullKV.

    Accepts delta payload from prepare_escalation_payload; optionally runs
    delta compression when baseline is available. Produces a list of (address, value)
    pairs for HullKVCache ingestion. Encrypt/attest and streaming are left for later.
    """

    def __init__(
        self,
        hull_kv_callback: Optional[Callable[[List[Tuple[int, Optional[bytes]]]], None]] = None,
    ):
        self.hull_kv_callback = hull_kv_callback

    def accept(self, payload: Dict[str, Any]) -> List[Tuple[int, Optional[bytes]]]:
        """Accept Tier 1 escalation payload and return (addr, value) deltas for HullKV.

        Args:
            payload: From prepare_escalation_payload (format_version, linear_memory,
                stack_snapshot, etc.).

        Returns:
            List of (address, value) pairs; value is bytes or None. Caller or
            hull_kv_callback can feed these into HullKVCache.ingest_deltas.
        """
        linear_memory = payload.get("linear_memory")
        if linear_memory is None:
            return []
        if isinstance(linear_memory, (bytes, bytearray)):
            # Emit as single chunk or word-sized (addr, value) pairs
            out: List[Tuple[int, Optional[bytes]]] = []
            chunk = 8
            for addr in range(0, len(linear_memory), chunk):
                end = min(addr + chunk, len(linear_memory))
                out.append((addr, bytes(linear_memory[addr:end])))
            if self.hull_kv_callback is not None:
                self.hull_kv_callback(out)
            return out
        return []


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
