"""LoTA-QAF: t-SignSGD optimizer (LoRA lives in ``qminiwasm.layers.lota``)."""

from __future__ import annotations

import torch


class TSignSGD(torch.optim.Optimizer):
    """Signed gradient descent for discrete-friendly latent weights."""

    def __init__(self, params, lr: float = 1e-3):
        defaults = dict(lr=float(lr))
        super().__init__(params, defaults)

    @torch.no_grad()
    def step(self, closure=None):  # noqa: ARG002
        for group in self.param_groups:
            lr = group["lr"]
            for p in group["params"]:
                if p.grad is None:
                    continue
                p.add_(torch.sign(p.grad), alpha=-lr)
        return None
