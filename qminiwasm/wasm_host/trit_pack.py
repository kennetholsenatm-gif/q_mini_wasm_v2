"""Ternary weight packing: five signed trits per byte (1.58-bit / base-3).

Encoding (version 2, MSB-first within each byte) matches
``P(D) = sum_{j=0}^{k-1} d'_j * 3^{k-1-j}`` for ``k <= 5``, with
``d' = d + 1`` and ``d in {-1, 0, 1}`` so ``d' in {0, 1, 2}``.

Earlier repository revisions used LSB-first placement (``3**j``); that layout is
deprecated. :data:`PACK_ENCODING_VERSION` identifies the on-wire layout.
"""

from __future__ import annotations

from typing import List, Sequence

import torch
from qminiwasm.runtime_modes import resolve_impl_mode

try:
    from qminiwasm._native_ternary import (
        pack_ternary_list as _native_pack_ternary_list,
        unpack_ternary_list as _native_unpack_ternary_list,
    )
except ImportError:
    _native_pack_ternary_list = None
    _native_unpack_ternary_list = None

TRITS_PER_BYTE = 5
PACK_ENCODING_VERSION = 2
_POW3_BY_K = {
    1: (1,),
    2: (3, 1),
    3: (9, 3, 1),
    4: (27, 9, 3, 1),
    5: (81, 27, 9, 3, 1),
}


def _build_decode_lut() -> dict[int, tuple[tuple[int, ...] | None, ...]]:
    out: dict[int, tuple[tuple[int, ...] | None, ...]] = {}
    for k in range(1, TRITS_PER_BYTE + 1):
        max_valid = 3**k - 1
        rows: List[tuple[int, ...] | None] = []
        for b in range(256):
            if b > max_valid:
                rows.append(None)
                continue
            rem = b
            trits = []
            for power in _POW3_BY_K[k]:
                trit = rem // power
                rem %= power
                trits.append(trit - 1)
            rows.append(tuple(trits))
        out[k] = tuple(rows)
    return out


_DECODE_LUT_BY_K = _build_decode_lut()


def _trit_pack_impl() -> str:
    return resolve_impl_mode("QMINIWASM_TRIT_PACK_IMPL", "auto")


def signed_to_digit(w: int) -> int:
    """Map {-1, 0, 1} -> {0, 1, 2}."""
    if w == -1:
        return 0
    if w == 0:
        return 1
    if w == 1:
        return 2
    raise ValueError(f"expected ternary weight -1, 0, or 1, got {w!r}")


def digit_to_signed(trit: int) -> int:
    """Map {0, 1, 2} -> {-1, 0, 1}."""
    if trit == 0:
        return -1
    if trit == 1:
        return 0
    if trit == 2:
        return 1
    raise ValueError(f"expected base-3 digit 0..2, got {trit!r}")


def pack_ternary_list(weights: Sequence[int]) -> bytes:
    """Pack a sequence of {-1,0,1} into bytes (MSB-first per group, variable k on last group)."""
    impl = _trit_pack_impl()
    if impl != "python" and _native_pack_ternary_list is not None:
        out = _native_pack_ternary_list(list(int(x) for x in weights))
        if isinstance(out, (bytes, bytearray)):
            return bytes(out)
    if impl == "native" and _native_pack_ternary_list is None:
        raise RuntimeError("QMINIWASM_TRIT_PACK_IMPL=native but native packer is unavailable")
    n = len(weights)
    out: List[int] = []
    i = 0
    while i < n:
        k = min(TRITS_PER_BYTE, n - i)
        byte_val = 0
        powers = _POW3_BY_K[k]
        for j in range(k):
            trit = signed_to_digit(int(weights[i + j]))
            byte_val += trit * powers[j]
        out.append(byte_val & 0xFF)
        i += k
    return bytes(out)


def unpack_ternary_list(packed: bytes, total_trits: int) -> List[int]:
    """Unpack bytes to ``total_trits`` signed ternary values."""
    impl = _trit_pack_impl()
    if impl != "python" and _native_unpack_ternary_list is not None:
        out = _native_unpack_ternary_list(bytes(packed), int(total_trits))
        return [int(x) for x in out]
    if impl == "native" and _native_unpack_ternary_list is None:
        raise RuntimeError("QMINIWASM_TRIT_PACK_IMPL=native but native unpacker is unavailable")
    if total_trits < 0:
        raise ValueError("total_trits must be non-negative")
    weights: List[int] = []
    for byte_val in packed:
        if len(weights) >= total_trits:
            break
        remaining = total_trits - len(weights)
        k = min(TRITS_PER_BYTE, remaining)
        lut_row = _DECODE_LUT_BY_K[k][int(byte_val) & 0xFF]
        if lut_row is None:
            # Preserve strict behavior for invalid base-3 encoded byte values.
            rem = int(byte_val) & 0xFF
            for j in range(k):
                power = _POW3_BY_K[k][j]
                trit = rem // power
                rem %= power
                weights.append(digit_to_signed(trit))
            continue
        weights.extend(lut_row[:k])
    return weights[:total_trits]


def pack_ternary_tensor(weights: torch.Tensor) -> torch.Tensor:
    """Pack a ternary tensor to uint8 1-D (same layout as :func:`pack_ternary_list`)."""
    flat = weights.detach().reshape(-1).to(dtype=torch.int16)
    lst: List[int] = []
    for v in flat.tolist():
        v = int(v)
        if v not in (-1, 0, 1):
            v = max(-1, min(1, v))
        lst.append(v)
    b = pack_ternary_list(lst)
    return torch.tensor(list(b), dtype=torch.uint8, device=weights.device)


def unpack_ternary_tensor(packed: torch.Tensor, original_shape: torch.Size) -> torch.Tensor:
    """Unpack uint8 tensor to float ternary tensor with ``original_shape``."""
    n = int(original_shape.numel())
    data = packed.detach().to(dtype=torch.uint8, device="cpu").flatten().numpy().tobytes()
    lst = unpack_ternary_list(data, n)
    return torch.tensor(lst, dtype=torch.float32, device=packed.device).view(original_shape)
