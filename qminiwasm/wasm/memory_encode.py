"""Fixed-size encoding of WebAssembly linear memory for QMiniWASM training (d_model=4096).

Layout (stable; bump version in slot 0 if this changes):
  [0]: encoding version scalar (1.0)
  [1]: normalized first i32 argument (abs(arg0) % 65536) / 65535
  [2]: normalized result i32 (abs(result) % 65536) / 65535; use 0.0 before call
  [3]: normalized memory byte length min(len, 16_777_215) / 16_777_215
  [4:8]: reserved (0.0)
  [8:4096]: up to 4088 bytes from linear memory as floats in [0, 1] (byte/255); zero-padded
"""

from __future__ import annotations

import torch

D_MODEL = 4096
META_SLOTS = 8
BODY_SLOTS = D_MODEL - META_SLOTS

ENCODING_VERSION = 1.0


def encode_linear_memory(
    mem: bytes,
    *,
    result_i32: int = 0,
    first_arg: int = 0,
) -> torch.Tensor:
    """Encode a linear-memory snapshot and scalars into a single 4096-dim float vector."""
    out = torch.zeros(D_MODEL, dtype=torch.float32)
    out[0] = ENCODING_VERSION
    out[1] = (abs(int(first_arg)) % 65536) / 65535.0
    out[2] = (abs(int(result_i32)) % 65536) / 65535.0
    out[3] = min(len(mem), 16777215) / 16777215.0
    n = min(BODY_SLOTS, len(mem))
    if n > 0:
        chunk = mem[:n]
        out[META_SLOTS : META_SLOTS + n] = torch.tensor(list(chunk), dtype=torch.float32) / 255.0
    return out
