"""LoRA side branch for LoTA-QAF (kept in ``layers`` to avoid import cycles with ``training``)."""

from __future__ import annotations

import ctypes
import os
import torch
import torch.nn as nn
from qminiwasm.native_bridge import load_native_lib


class LoRALinearSide(nn.Module):
    """Low-rank adaptation parallel to a linear map: ``y += x @ A.T @ B.T``."""

    def __init__(self, in_features: int, out_features: int, rank: int):
        super().__init__()
        r = max(1, int(rank))
        self.lora_a = nn.Linear(in_features, r, bias=False)
        self.lora_b = nn.Linear(r, out_features, bias=False)
        nn.init.kaiming_uniform_(self.lora_a.weight, a=5**0.5)
        nn.init.zeros_(self.lora_b.weight)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        use_native = (
            os.getenv("QMINIWASM_NATIVE_LOTA_QAF", "0").strip().lower()
            not in {"", "0", "false", "off", "no"}
        )
        if (
            use_native
            and x.device.type == "cpu"
            and x.dtype == torch.float32
            and x.dim() == 2
            and self.lora_a.weight.dtype == torch.float32
            and self.lora_b.weight.dtype == torch.float32
        ):
            lib = load_native_lib()
            if lib is not None and hasattr(lib, "qmw_lota_forward_f32"):
                bsz, in_features = int(x.shape[0]), int(x.shape[1])
                rank = int(self.lora_a.weight.shape[0])
                out_features = int(self.lora_b.weight.shape[0])
                out = torch.empty((bsz, out_features), dtype=torch.float32)
                fn = lib.qmw_lota_forward_f32
                fn.argtypes = [
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.POINTER(ctypes.c_float),
                ]
                fn.restype = None
                fn(
                    ctypes.cast(x.contiguous().numpy().ctypes.data, ctypes.POINTER(ctypes.c_float)),
                    ctypes.cast(
                        self.lora_a.weight.contiguous().detach().numpy().ctypes.data,
                        ctypes.POINTER(ctypes.c_float),
                    ),
                    ctypes.cast(
                        self.lora_b.weight.contiguous().detach().numpy().ctypes.data,
                        ctypes.POINTER(ctypes.c_float),
                    ),
                    ctypes.c_size_t(bsz),
                    ctypes.c_size_t(in_features),
                    ctypes.c_size_t(rank),
                    ctypes.c_size_t(out_features),
                    ctypes.cast(out.numpy().ctypes.data, ctypes.POINTER(ctypes.c_float)),
                )
                return out
        return self.lora_b(self.lora_a(x))


def merge_lora_into_linear_weight(
    base_weight: torch.Tensor,
    lora: LoRALinearSide,
) -> None:
    """Add ``B @ A`` to ``base_weight`` (in-place) and reset ``lora_b`` to zero."""
    with torch.no_grad():
        use_native = (
            os.getenv("QMINIWASM_NATIVE_LOTA_QAF", "0").strip().lower()
            not in {"", "0", "false", "off", "no"}
        )
        if (
            use_native
            and base_weight.device.type == "cpu"
            and base_weight.dtype == torch.float32
            and lora.lora_a.weight.dtype == torch.float32
            and lora.lora_b.weight.dtype == torch.float32
        ):
            lib = load_native_lib()
            if lib is not None and hasattr(lib, "qmw_lota_merge_f32"):
                fn = lib.qmw_lota_merge_f32
                fn.argtypes = [
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.POINTER(ctypes.c_float),
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                    ctypes.c_size_t,
                ]
                fn.restype = None
                fn(
                    ctypes.cast(base_weight.contiguous().numpy().ctypes.data, ctypes.POINTER(ctypes.c_float)),
                    ctypes.cast(
                        lora.lora_a.weight.contiguous().detach().numpy().ctypes.data,
                        ctypes.POINTER(ctypes.c_float),
                    ),
                    ctypes.cast(
                        lora.lora_b.weight.contiguous().detach().numpy().ctypes.data,
                        ctypes.POINTER(ctypes.c_float),
                    ),
                    ctypes.c_size_t(int(lora.lora_a.weight.shape[1])),
                    ctypes.c_size_t(int(lora.lora_a.weight.shape[0])),
                    ctypes.c_size_t(int(lora.lora_b.weight.shape[0])),
                )
            else:
                delta = lora.lora_b.weight @ lora.lora_a.weight
                base_weight.add_(delta)
        else:
            delta = lora.lora_b.weight @ lora.lora_a.weight
            base_weight.add_(delta)
        lora.lora_b.weight.zero_()
