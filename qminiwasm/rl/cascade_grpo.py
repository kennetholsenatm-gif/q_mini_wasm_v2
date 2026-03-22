"""Group-relative policy optimization for discrete cascade (escalation / routing) actions."""

from __future__ import annotations

from dataclasses import dataclass

import torch
import torch.nn as nn


@dataclass
class CascadeGRPOConfig:
    """Configuration for :class:`CascadeGRPO`."""

    normalize_advantage: bool = True
    eps: float = 1e-8


class CascadeGRPO(nn.Module):
    """Advantage-weighted policy gradient with per-group baseline (GRPO-style).

    Expects **one scalar log-probability sum per trajectory** (sum over time of
    ``log pi(a_t|s_t)``), and a **total return** per trajectory. Suitable for
    short cascade episodes where actions are discrete.
    """

    def __init__(self, cfg: CascadeGRPOConfig | None = None) -> None:
        super().__init__()
        self.cfg = cfg or CascadeGRPOConfig()

    def forward(
        self,
        logprob_sum: torch.Tensor,
        returns: torch.Tensor,
    ) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
        """
        Args:
            logprob_sum: shape ``[G]``, sum of log probs along each trajectory.
            returns: shape ``[G]``, total discounted (or sparse terminal) return.

        Returns:
            Scalar loss to **minimize** (negative advantage-weighted log prob).
        """
        if logprob_sum.shape != returns.shape:
            raise ValueError(f"logprob_sum {logprob_sum.shape} vs returns {returns.shape}")
        if logprob_sum.dim() != 1:
            raise ValueError("expected 1D tensors [G]")

        adv = returns.detach()
        if self.cfg.normalize_advantage and returns.numel() > 1:
            adv = (adv - adv.mean()) / (adv.std(unbiased=False) + self.cfg.eps)

        loss = -(adv * logprob_sum).mean()
        metrics = {
            "grpo_loss": loss.detach(),
            "return_mean": returns.mean().detach(),
            "advantage_mean": adv.mean().detach(),
        }
        return loss, metrics
