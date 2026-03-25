"""Delta compression: diff linear memory (and optionally stack) at loop N vs baseline.

Prefer **Ephemeral State Inversion (ESI)** for unbounded conversational context; this module
targets **Tier 2** migration of **bounded** linear-memory slices. Produces compact
(address, value) or (address_range, bytes) deltas. Includes optional payload struct
(checksum, length, format version) for later encryption/attestation.
"""

from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple, Union

BytesLike = Union[bytes, bytearray, memoryview]


@dataclass
class delta_payload_struct:
    """Delta payload with metadata for attestation and streaming."""

    format_version: int
    length: int
    checksum: str
    deltas: List[Tuple[int, Optional[bytes]]]
    """List of (address, value) or (address, None) for zero-fill."""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "format_version": self.format_version,
            "length": self.length,
            "checksum": self.checksum,
            "deltas": self.deltas,
        }


def _to_bytes(mem: Any) -> Optional[bytes]:
    if mem is None:
        return None
    if isinstance(mem, (bytes, bytearray)):
        return bytes(mem)
    if isinstance(mem, memoryview):
        return bytes(mem)
    if hasattr(mem, "tobytes"):
        return mem.tobytes()
    return None


def compress_deltas(
    current_memory: Any,
    baseline_memory: Any,
    format_version: int = 1,
    chunk_size: int = 8,
) -> delta_payload_struct:
    """Compute binary diff of current linear memory vs baseline (loop 0).

    Outputs (address, value) pairs for modified regions; optionally coalesces
    adjacent changes into (address_range, bytes). chunk_size is the alignment
    for address boundaries (e.g. 4 or 8 for word-aligned deltas).

    Args:
        current_memory: Linear memory at loop N (bytes-like or None).
        baseline_memory: Linear memory at loop 0 (bytes-like or None).
        format_version: Payload format version.
        chunk_size: Minimum delta granularity in bytes.

    Returns:
        delta_payload_struct with checksum and list of (addr, value) deltas.
    """
    cur = _to_bytes(current_memory)
    base = _to_bytes(baseline_memory)
    deltas: List[Tuple[int, Optional[bytes]]] = []

    if cur is None and base is None:
        pass
    elif cur is None:
        # Current is empty; treat full baseline as "to remove" (zero) or skip
        pass
    elif base is None or len(base) == 0:
        # No baseline: emit all current as deltas in chunks
        for addr in range(0, len(cur), chunk_size):
            end = min(addr + chunk_size, len(cur))
            chunk = cur[addr:end]
            if chunk:
                deltas.append((addr, chunk))
    else:
        # Both present: emit only changed chunks
        max_len = max(len(cur), len(base))
        for addr in range(0, max_len, chunk_size):
            end_cur = min(addr + chunk_size, len(cur))
            end_base = min(addr + chunk_size, len(base))
            c = cur[addr:end_cur] if end_cur > addr else b""
            b = base[addr:end_base] if end_base > addr else b""
            if c != b:
                deltas.append((addr, c if c else None))

    # Serialize for checksum
    buf = struct.pack("<I", format_version)
    for addr, val in deltas:
        buf += struct.pack("<I", addr)
        if val is not None:
            buf += struct.pack("<I", len(val)) + val
        else:
            buf += struct.pack("<I", 0)
    checksum = hashlib.sha256(buf).hexdigest()

    return delta_payload_struct(
        format_version=format_version,
        length=len(buf),
        checksum=checksum,
        deltas=deltas,
    )
