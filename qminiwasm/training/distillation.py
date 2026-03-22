"""Multi-domain on-policy distillation (MOPD): KL on auxiliary heads + feature matching.

Ternary / STE backbones often make full-logit KL unstable; prefer hidden-state alignment
against a stop-gradient teacher while a separate policy head is trained with GRPO.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping

import torch
import torch.nn as nn
import torch.nn.functional as F


@dataclass
class MOPDLossConfig:
    lambda_kl: float = 0.05
    lambda_feat: float = 1.0
    kl_temperature: float = 1.0
    feat_loss: str = "mse"  # "mse" | "cosine"


class MOPDLoss(nn.Module):
    """Composite loss: optional KL on auxiliary logits + per-layer feature distillation."""

    def __init__(self, cfg: MOPDLossConfig | None = None) -> None:
        super().__init__()
        self.cfg = cfg or MOPDLossConfig()

    def forward(
        self,
        student_aux_logits: torch.Tensor | None,
        teacher_aux_logits: torch.Tensor | None,
        student_hiddens: Mapping[str, torch.Tensor],
        teacher_hiddens: Mapping[str, torch.Tensor],
        layer_weights: Mapping[str, float] | None = None,
    ) -> tuple[torch.Tensor, dict[str, torch.Tensor]]:
        if not student_hiddens:
            raise ValueError("student_hiddens must be non-empty")
        device = next(iter(student_hiddens.values())).device
        dtype = next(iter(student_hiddens.values())).dtype
        zero = torch.zeros((), device=device, dtype=dtype)
        parts: dict[str, torch.Tensor] = {}

        kl = zero
        if student_aux_logits is not None and teacher_aux_logits is not None:
            t = float(self.cfg.kl_temperature)
            p = F.log_softmax(student_aux_logits / t, dim=-1)
            q = F.softmax(teacher_aux_logits.detach() / t, dim=-1)
            kl = F.kl_div(p, q, reduction="batchmean") * (t * t)
            parts["loss_kl"] = kl

        lw = layer_weights or {k: 1.0 for k in student_hiddens}
        feat = zero
        for name, hs in student_hiddens.items():
            if name not in teacher_hiddens:
                raise KeyError(f"teacher_hiddens missing key {name!r}")
            ht = teacher_hiddens[name].detach()
            if hs.shape != ht.shape:
                raise ValueError(f"hidden shape mismatch for {name}: {hs.shape} vs {ht.shape}")
            w = float(lw.get(name, 1.0))
            if self.cfg.feat_loss == "cosine":
                hs_n = F.normalize(hs, dim=-1)
                ht_n = F.normalize(ht, dim=-1)
                layer_loss = (1.0 - (hs_n * ht_n).sum(dim=-1)).mean()
            else:
                layer_loss = F.mse_loss(hs, ht)
            feat = feat + w * layer_loss
            parts[f"loss_feat_{name}"] = layer_loss

        total = self.cfg.lambda_kl * kl + self.cfg.lambda_feat * feat
        parts["loss_total"] = total
        return total, parts
