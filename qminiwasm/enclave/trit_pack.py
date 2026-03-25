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

TRITS_PER_BYTE = 5
PACK_ENCODING_VERSION = 2


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
    n = len(weights)
    out: List[int] = []
    i = 0
    while i < n:
        k = min(TRITS_PER_BYTE, n - i)
        byte_val = 0
        for j in range(k):
            trit = signed_to_digit(int(weights[i + j]))
            byte_val += trit * (3 ** (k - 1 - j))
        out.append(byte_val & 0xFF)
        i += k
    return bytes(out)


def unpack_ternary_list(packed: bytes, total_trits: int) -> List[int]:
    """Unpack bytes to ``total_trits`` signed ternary values."""
    if total_trits < 0:
        raise ValueError("total_trits must be non-negative")
    weights: List[int] = []
    for byte_val in packed:
        if len(weights) >= total_trits:
            break
        remaining = total_trits - len(weights)
        k = min(TRITS_PER_BYTE, remaining)
        rem = int(byte_val) & 0xFF
        for j in range(k):
            power = 3 ** (k - 1 - j)
            trit = rem // power
            rem %= power
            weights.append(digit_to_signed(trit))
    return weights[:total_trits]


def pack_ternary_tensor(weights: torch.Tensor) -> torch.Tensor:
    """Pack a ternary tensor to uint8 1-D (same layout as :func:`pack_ternary_list`)."""
    flat = weights.detach().reshape(-1)
    lst: List[int] = []
    for x in flat.tolist():
        v = int(round(float(x)))
        if v not in (-1, 0, 1):
            v = max(-1, min(1, v))
        lst.append(v)
    b = pack_ternary_list(lst)
    return torch.tensor(list(b), dtype=torch.uint8, device=weights.device)


def unpack_ternary_tensor(packed: torch.Tensor, original_shape: torch.Size) -> torch.Tensor:
    """Unpack uint8 tensor to float ternary tensor with ``original_shape``."""
    n = int(original_shape.numel())
    data = bytes(int(x) for x in packed.flatten().tolist())
    lst = unpack_ternary_list(data, n)
    return torch.tensor(lst, dtype=torch.float32, device=packed.device).view(original_shape)
