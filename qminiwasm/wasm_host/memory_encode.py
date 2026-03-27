"""Fixed-size encoding of WebAssembly linear memory for QMiniWASM training (d_model=4096).

**WASM Linear Execution Snapshots (WLES)** use the raw ``mem: bytes`` slice as the authoritative
TPEM / linear-memory image; this module provides a condensed 4096-d **encoding** for neural
pipelines. Full WLES envelopes add versioned headers in :func:`build_wles_envelope`.

Layout (stable; bump version in slot 0 if this changes):
  [0]: encoding version scalar (1.0)
  [1]: normalized first i32 argument (abs(arg0) % 65536) / 65535
  [2]: normalized result i32 (abs(result) % 65536) / 65535; use 0.0 before call
  [3]: normalized memory byte length min(len, 16_777_215) / 16_777_215
  [4:8]: reserved (0.0)
  [8:4096]: up to 4088 bytes from linear memory as floats in [0, 1] (byte/255); zero-padded
"""

from __future__ import annotations

from typing import Any, Dict, Optional

import torch
from qminiwasm.runtime_modes import resolve_impl_mode

try:
    from qminiwasm._native_ternary import encode_linear_memory_u8 as _native_encode_linear_memory_u8
except ImportError:
    _native_encode_linear_memory_u8 = None

D_MODEL = 4096
WLES_PAYLOAD_VERSION = 1
META_SLOTS = 8
BODY_SLOTS = D_MODEL - META_SLOTS

ENCODING_VERSION = 1.0


def _memory_encode_impl() -> str:
    return resolve_impl_mode("QMINIWASM_MEMORY_ENCODE_IMPL", "auto")


def encode_linear_memory(
    mem: bytes,
    *,
    result_i32: int = 0,
    first_arg: int = 0,
) -> torch.Tensor:
    """Encode a linear-memory snapshot and scalars into a single 4096-dim float vector."""
    impl = _memory_encode_impl()
    if impl != "python":
        try:
            if _native_encode_linear_memory_u8 is not None:
                out = _native_encode_linear_memory_u8(
                    bytes(mem),
                    int(result_i32),
                    int(first_arg),
                    int(D_MODEL),
                    int(META_SLOTS),
                )
                if isinstance(out, torch.Tensor) and out.shape == (D_MODEL,):
                    return out.to(dtype=torch.float32)
                out_t = torch.as_tensor(out, dtype=torch.float32)
                if out_t.shape == (D_MODEL,):
                    return out_t
            else:
                if impl == "native":
                    raise RuntimeError(
                        "QMINIWASM_MEMORY_ENCODE_IMPL=native but encode_linear_memory_u8 is unavailable"
                    )
        except RuntimeError:
            if impl == "native":
                raise
        except Exception:
            if impl == "native":
                raise
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


# Alias: WLES-oriented 4096-d training features from linear memory (TPEM image slice)
encode_wles_training_features = encode_linear_memory


def build_wles_envelope(
    linear_memory: bytes,
    *,
    stack_snapshot: Optional[bytes] = None,
    instruction_pointer: Optional[int] = None,
    format_version: int = 1,
) -> Dict[str, Any]:
    """Assemble a versioned WLES dict for NVMe/native snapshot hooks.

    Trainable TPEM / tensor bundles may embed tensors; this envelope is **linear memory** only.
    """
    env: Dict[str, Any] = {
        "wles_payload_version": WLES_PAYLOAD_VERSION,
        "format_version": format_version,
        "linear_memory": linear_memory,
        "linear_memory_byte_len": len(linear_memory),
        "stack_snapshot": stack_snapshot,
        "instruction_pointer": instruction_pointer,
    }
    return env
