"""Post-Training Quantization to Trit-Planes (PTQTP).

Decomposes a full-precision weight matrix into a small number of ternary
``planes'' plus per-row scales. Inference can use add/subtract accumulates
over {-1, 0, 1} weights; the PyTorch reference path uses :func:`torch.nn.functional.linear`
for correctness tests.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F


def mulfree_ternary_matvec_reference(x: torch.Tensor, T: torch.Tensor) -> torch.Tensor:
    """Reference ``y = x @ T.T`` with ``T`` in ``{-1, 0, 1}`` (float matmul).

    Deployed runtimes may replace this with packed add/subtract kernels.
    """
    return F.linear(x, T.to(dtype=x.dtype))


@dataclass
class PTQTPDecomposition:
    """Ternary planes ``P_i`` and row scales ``s_i`` (each ``s_i`` shape ``(out,)``)."""

    planes: List[torch.Tensor]
    scales: List[torch.Tensor]

    def to(self, device: torch.device, dtype: torch.dtype | None = None) -> PTQTPDecomposition:
        planes = [p.to(device=device, dtype=dtype or p.dtype) for p in self.planes]
        scales = [s.to(device=device, dtype=dtype or s.dtype) for s in self.scales]
        return PTQTPDecomposition(planes=planes, scales=scales)


def decompose_ptqtp(
    W: torch.Tensor,
    num_planes: int = 2,
    eps: float = 1e-8,
) -> PTQTPDecomposition:
    """Greedy residual ternary decomposition (post-training).

    Each plane: scale ``s_r = mean(|R_r|)`` per row, ``T = round(R / s)`` clamped to
    ``{-1,0,1}``, residual ``R <- R - s * T``.

    Args:
        W: Weight matrix ``(out_features, in_features)``.
        num_planes: Number of trit-planes (>= 1).

    Returns:
        :class:`PTQTPDecomposition` with ``len(planes) == num_planes``.
    """
    if W.dim() != 2:
        raise ValueError("W must be 2-D (out, in)")
    if num_planes < 1:
        raise ValueError("num_planes must be >= 1")
    R = W.detach().clone()
    planes: List[torch.Tensor] = []
    scales: List[torch.Tensor] = []
    for _ in range(num_planes):
        s = R.abs().mean(dim=1, keepdim=True).clamp_min(eps)
        T = torch.round(R / s).clamp(-1.0, 1.0)
        planes.append(T.clone())
        scales.append(s.squeeze(1).clone())
        R = R - s * T
    return PTQTPDecomposition(planes=planes, scales=scales)


def ptqtp_linear_forward(
    x: torch.Tensor,
    decomp: PTQTPDecomposition,
) -> torch.Tensor:
    """Apply PTQTP layers: ``y = sum_i D(s_i) @ P_i @ x`` with diagonal row scaling.

    Here ``D(s_i)`` is row-wise multiplication implemented as ``(P_i @ x.T) * s_i`` then transpose.
    """
    y = torch.zeros(x.shape[0], decomp.planes[0].shape[0], device=x.device, dtype=x.dtype)
    for T, s in zip(decomp.planes, decomp.scales):
        z = mulfree_ternary_matvec_reference(x, T)
        y = y + z * s.unsqueeze(0).to(dtype=x.dtype)
    return y


class PTQTPLinear(nn.Module):
    """Frozen PTQTP substitute for a linear layer (buffers only)."""

    def __init__(self, W: torch.Tensor, num_planes: int = 2):
        super().__init__()
        d = decompose_ptqtp(W, num_planes=num_planes)
        for i, (p, s) in enumerate(zip(d.planes, d.scales)):
            self.register_buffer(f"plane_{i}", p)
            self.register_buffer(f"scale_{i}", s)
        self.num_planes = num_planes

    def _decomp(self) -> PTQTPDecomposition:
        planes = [getattr(self, f"plane_{i}") for i in range(self.num_planes)]
        scales = [getattr(self, f"scale_{i}") for i in range(self.num_planes)]
        return PTQTPDecomposition(planes=planes, scales=scales)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return ptqtp_linear_forward(x, self._decomp())


def ptqtp_reconstruction_mse(
    W: torch.Tensor, num_planes: int = 2
) -> Tuple[float, PTQTPDecomposition]:
    """Mean squared error ``||W - W_hat||^2`` for the PTQTP reconstruction."""
    d = decompose_ptqtp(W, num_planes=num_planes)
    with torch.no_grad():
        W_hat = torch.zeros_like(W)
        for T, s in zip(d.planes, d.scales):
            W_hat = W_hat + s.unsqueeze(1) * T
        mse = (W - W_hat).pow(2).mean().item()
    return mse, d
