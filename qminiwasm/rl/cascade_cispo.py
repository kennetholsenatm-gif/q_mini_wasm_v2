"""Clipped Importance Sampling Policy Optimization for discrete cascade routing.

Clips the importance-sampling ratio and **detaches** the clipped weight so the policy
gradient flows through ``log pi`` (trajectory log-prob sum) but variance is bounded
by the clipped coefficient. See whitepapers in ``docs/`` and CISPO references therein.
"""

from __future__ import annotations

from dataclasses import dataclass

import torch
import torch.nn as nn


@dataclass
class CascadeCISPOConfig:
    """Configuration for :class:`CascadeCISPO`."""

    normalize_advantage: bool = True
    epsilon: float = 0.2
    eps: float = 1e-8


class CascadeCISPO(nn.Module):
    """CISPO-style loss on trajectory-level log-prob sums.

    Expects ``logprob_sum`` (current policy, differentiable), ``logprob_sum_old``
    (behavior policy, typically detached), and per-trajectory returns.

    ``ratio = exp(logprob_sum - logprob_sum_old)`` is clipped to
    ``[1 - epsilon, 1 + epsilon]``; the loss is
    ``-(clip(ratio).detach() * advantage * logprob_sum).mean()``.
    """

    def __init__(self, cfg: CascadeCISPOConfig | None = None) -> None:
        super().__init__()
        self.cfg = cfg or CascadeCISPOConfig()

    def forward(
        self,
        logprob_sum: torch.Tensor,
        logprob_sum_old: torch.Tensor,
        returns: torch.Tensor,
    ) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
        if logprob_sum.shape != returns.shape or logprob_sum_old.shape != logprob_sum.shape:
            raise ValueError("logprob_sum, logprob_sum_old, returns must have the same shape")
        if logprob_sum.dim() != 1:
            raise ValueError("expected 1D tensors [G]")

        ratio = torch.exp(torch.clamp(logprob_sum - logprob_sum_old, min=-20.0, max=20.0))
        low = 1.0 - float(self.cfg.epsilon)
        high = 1.0 + float(self.cfg.epsilon)
        clipped = torch.clamp(ratio, low, high)

        adv = returns.detach()
        if self.cfg.normalize_advantage and returns.numel() > 1:
            adv = (adv - adv.mean()) / (adv.std(unbiased=False) + self.cfg.eps)

        coeff = clipped.detach()
        loss = -(coeff * adv * logprob_sum).mean()
        metrics = {
            "cispo_loss": loss.detach(),
            "return_mean": returns.mean().detach(),
            "advantage_mean": adv.mean().detach(),
            "ratio_mean": ratio.mean().detach(),
            "clipped_fraction": (ratio != clipped).float().mean().detach(),
        }
        return loss, metrics
