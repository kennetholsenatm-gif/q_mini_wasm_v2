"""Bloch-sphere fidelity attention shapes."""

from __future__ import annotations

import torch

from qminiwasm.layers.bloch_attention import BlochSphereAttention


def test_bloch_attention_forward_shape() -> None:
    m = BlochSphereAttention(d_model=32, num_heads=4)
    x = torch.randn(2, 5, 32)
    y = m(x)
    assert y.shape == x.shape
