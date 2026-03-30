"""Bloch-sphere fidelity attention (classical Mode A).

Maps per-head queries and keys to unit Stokes vectors on the Bloch sphere and uses
quantum fidelity ``F = (1 + s_q · s_k) / 2`` as nonnegative attention weights.
Replaces softmax with a normalized fidelity matrix over the sequence dimension.

QPU compute-uncompute execution (Mode B) is not wired here; see architecture docs.
"""

from __future__ import annotations

import math

import torch
import torch.nn as nn
import torch.nn.functional as F


def _stokes_from_logits(logits: torch.Tensor) -> torch.Tensor:
    """Map ``[..., 3]`` unconstrained logits to unit 3-vectors (Stokes / Bloch direction)."""
    return F.normalize(torch.tanh(logits), dim=-1, eps=1e-6)


class BlochSphereAttention(nn.Module):
    """Multi-head fidelity attention for sequences ``[batch, seq, d_model]``."""

    def __init__(
        self,
        d_model: int,
        num_heads: int,
        d_value: int | None = None,
        bias: bool = True,
        device: torch.device | None = None,
        dtype: torch.dtype | None = None,
    ) -> None:
        super().__init__()
        if num_heads <= 0:
            raise ValueError("num_heads must be positive")
        factory = {"device": device, "dtype": dtype}
        self.d_model = d_model
        self.num_heads = num_heads
        if d_value is None:
            if d_model % num_heads != 0:
                raise ValueError("d_model must be divisible by num_heads when d_value is None")
            d_value = d_model // num_heads
        self.d_value = d_value
        self.q_proj = nn.Linear(d_model, num_heads * 3, bias=bias, **factory)
        self.k_proj = nn.Linear(d_model, num_heads * 3, bias=bias, **factory)
        self.v_proj = nn.Linear(d_model, num_heads * d_value, bias=bias, **factory)
        self.out_proj = nn.Linear(num_heads * d_value, d_model, bias=bias, **factory)
        self.scale = 1.0 / math.sqrt(2.0)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """Args:
            x: ``[batch, seq_len, d_model]``
        Returns:
            ``[batch, seq_len, d_model]``
        """
        if x.dim() != 3:
            raise ValueError(f"expected [B,T,D], got {tuple(x.shape)}")
        b, t, _d = x.shape
        h = self.num_heads
        q = _stokes_from_logits(self.q_proj(x).view(b, t, h, 3))
        k = _stokes_from_logits(self.k_proj(x).view(b, t, h, 3))
        v = self.v_proj(x).view(b, t, h, self.d_value)
        # fidelity [B, H, Tq, Tk]
        fid = (1.0 + torch.einsum("bthd,bshd->bhts", q, k)) * 0.5
        fid = torch.clamp(fid, min=1e-6)
        w = fid / fid.sum(dim=-1, keepdim=True)
        ctx = torch.einsum("bhts,bshd->bthd", w, v)
        ctx = ctx.reshape(b, t, h * self.d_value)
        return self.out_proj(ctx)
