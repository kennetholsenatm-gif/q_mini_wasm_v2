"""LoRA side branch for LoTA-QAF (kept in ``layers`` to avoid import cycles with ``training``)."""

from __future__ import annotations

import torch
import torch.nn as nn


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
        return self.lora_b(self.lora_a(x))


def merge_lora_into_linear_weight(
    base_weight: torch.Tensor,
    lora: LoRALinearSide,
) -> None:
    """Add ``B @ A`` to ``base_weight`` (in-place) and reset ``lora_b`` to zero."""
    with torch.no_grad():
        delta = lora.lora_b.weight @ lora.lora_a.weight
        base_weight.add_(delta)
        lora.lora_b.weight.zero_()
