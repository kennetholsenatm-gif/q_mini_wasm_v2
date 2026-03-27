"""Classical–quantum interconnect (The Bridge) and Tier 2 state migration.

- ClassicalQuantumInterconnect: routing bridge (compressor -> QAOA -> weights).
- StateMigrationInterconnect: accepts delta payload from Tier 1, feeds deltas
  into ESIGeometricHull / tropical attention ingestion (in-process only for now).
"""

from __future__ import annotations

from typing import Any, Callable, Dict, List, Optional, Tuple

import torch
import torch.nn as nn

from .backend_registry import QuantumBackend, get_backend


class StateMigrationInterconnect:
    """Tier 2 state migration: accept CGE escalation payload, yield (addr, value) for hull ingest.

    Accepts delta payload from prepare_escalation_payload; optionally runs
    delta compression when baseline is available. Produces a list of (address, value)
    pairs for geometric hull / WLES-oriented ingestion. Encrypt/attest and
    streaming are left for later.
    """

    def __init__(
        self,
        hull_kv_callback: Optional[Callable[[List[Tuple[int, Optional[bytes]]]], None]] = None,
        *,
        default_chunk_size: int = 64,
        max_payload_bytes: int = 16 * 1024 * 1024,
    ):
        self.hull_kv_callback = hull_kv_callback
        self.default_chunk_size = max(1, int(default_chunk_size))
        self.max_payload_bytes = max(1, int(max_payload_bytes))

    def accept(self, payload: Dict[str, Any]) -> List[Tuple[int, Optional[bytes]]]:
        """Accept Tier 1 escalation payload and return (addr, value) deltas for hull ingest.

        Args:
            payload: From prepare_escalation_payload (format_version, linear_memory,
                stack_snapshot, etc.).

        Returns:
            List of (address, value) pairs; value is bytes or None. Caller or
            hull_kv_callback can feed these into tropical attention / ESIGeometricStateHull paths.
        """
        if not isinstance(payload, dict):
            raise TypeError("payload must be a dict")
        linear_memory = payload.get("linear_memory")
        if linear_memory is None:
            return []
        if not isinstance(linear_memory, (bytes, bytearray)):
            raise TypeError("payload.linear_memory must be bytes-like")

        current = bytes(linear_memory)
        if len(current) > self.max_payload_bytes:
            raise ValueError(
                "payload.linear_memory exceeds max_payload_bytes "
                f"({len(current)} > {self.max_payload_bytes})"
            )

        baseline_raw = payload.get("baseline_linear_memory")
        baseline: Optional[bytes]
        if baseline_raw is None:
            baseline = None
        elif isinstance(baseline_raw, (bytes, bytearray)):
            baseline = bytes(baseline_raw)
        else:
            raise TypeError("payload.baseline_linear_memory must be bytes-like when provided")

        chunk_size_raw = payload.get("chunk_size", self.default_chunk_size)
        try:
            chunk_size = int(chunk_size_raw)
        except Exception as exc:
            raise TypeError("payload.chunk_size must be an integer") from exc
        if chunk_size <= 0:
            raise ValueError("payload.chunk_size must be > 0")

        out: List[Tuple[int, Optional[bytes]]] = []
        if baseline is None:
            for addr in range(0, len(current), chunk_size):
                end = min(addr + chunk_size, len(current))
                out.append((addr, current[addr:end]))
        else:
            max_len = max(len(current), len(baseline))
            for addr in range(0, max_len, chunk_size):
                cur = current[addr : min(addr + chunk_size, len(current))]
                base = baseline[addr : min(addr + chunk_size, len(baseline))]
                if cur == base:
                    continue
                out.append((addr, cur if cur else None))

        if self.hull_kv_callback is not None:
            self.hull_kv_callback(out)
        return out


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
