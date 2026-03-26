"""LoTA-QAF: t-SignSGD optimizer (LoRA lives in ``qminiwasm.layers.lota``)."""

from __future__ import annotations

import ctypes
import os
import torch
from qminiwasm.native_bridge import load_native_lib


class TSignSGD(torch.optim.Optimizer):
    """Signed gradient descent for discrete-friendly latent weights."""

    def __init__(self, params, lr: float = 1e-3):
        defaults = dict(lr=float(lr))
        super().__init__(params, defaults)

    @torch.no_grad()
    def step(self, closure=None):  # noqa: ARG002
        use_native = (
            os.getenv("QMINIWASM_NATIVE_LOTA_QAF", "0").strip().lower()
            not in {"", "0", "false", "off", "no"}
        )
        lib = load_native_lib() if use_native else None
        fn = None
        if lib is not None and hasattr(lib, "qmw_tsign_update_f32"):
            fn = lib.qmw_tsign_update_f32
            fn.argtypes = [
                ctypes.POINTER(ctypes.c_float),
                ctypes.POINTER(ctypes.c_float),
                ctypes.c_size_t,
                ctypes.c_float,
            ]
            fn.restype = None
        for group in self.param_groups:
            lr = group["lr"]
            for p in group["params"]:
                if p.grad is None:
                    continue
                if (
                    fn is not None
                    and p.device.type == "cpu"
                    and p.dtype == torch.float32
                    and p.grad.dtype == torch.float32
                    and p.is_contiguous()
                    and p.grad.is_contiguous()
                ):
                    fn(
                        ctypes.cast(p.numpy().ctypes.data, ctypes.POINTER(ctypes.c_float)),
                        ctypes.cast(p.grad.numpy().ctypes.data, ctypes.POINTER(ctypes.c_float)),
                        ctypes.c_size_t(int(p.numel())),
                        ctypes.c_float(float(lr)),
                    )
                    continue
                p.add_(torch.sign(p.grad), alpha=-lr)
        return None
