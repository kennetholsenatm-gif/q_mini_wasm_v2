"""Smoke tests for MOPD + Cascade GRPO scaffolding."""

from __future__ import annotations

import torch

from qminiwasm.rl.cascade_grpo import CascadeGRPO, CascadeGRPOConfig
from qminiwasm.training.cascade_rl import TinyCascadePolicy, ToyRoutingEnv, cascade_rl_train_step
from qminiwasm.training.distillation import MOPDLoss, MOPDLossConfig


def test_grpo_module_shapes() -> None:
    grpo = CascadeGRPO(CascadeGRPOConfig(normalize_advantage=True))
    logp = torch.tensor([0.0, -0.1, 0.2, -0.05], requires_grad=True)
    ret = torch.tensor([1.0, 0.5, 1.2, 0.8])
    loss, m = grpo(logp, ret)
    loss.backward()
    assert loss.ndim == 0
    assert "grpo_loss" in m


def test_mopd_feature_only() -> None:
    mopd = MOPDLoss(MOPDLossConfig(lambda_kl=0.0, lambda_feat=1.0))
    hs = {"h": torch.randn(2, 4, requires_grad=True)}
    ht = {"h": torch.randn(2, 4)}
    loss, parts = mopd(None, None, hs, ht)
    loss.backward()
    assert parts["loss_feat_h"].ndim == 0
    assert parts["loss_total"].ndim == 0


def test_cascade_rl_train_step_runs() -> None:
    device = torch.device("cpu")
    pol = TinyCascadePolicy(8, 4).to(device)
    opt = torch.optim.Adam(pol.parameters(), lr=1e-2)
    grpo = CascadeGRPO(CascadeGRPOConfig())
    metrics = cascade_rl_train_step(
        pol,
        opt,
        grpo,
        group_size=4,
        env_factory=lambda: ToyRoutingEnv(state_dim=8, num_actions=4, max_steps=8, device=device),
    )
    assert "loss" in metrics
    assert "return_mean" in metrics


def test_cascade_rl_with_mopd() -> None:
    device = torch.device("cpu")
    dim = 8
    pol = TinyCascadePolicy(dim, 4).to(device)
    opt = torch.optim.Adam(pol.parameters(), lr=1e-2)
    grpo = CascadeGRPO(CascadeGRPOConfig())
    mopd = MOPDLoss(MOPDLossConfig(lambda_kl=0.0, lambda_feat=0.5))

    def student_h(s: torch.Tensor) -> dict[str, torch.Tensor]:
        return {"emb": s.clone()}

    def teacher_h(s: torch.Tensor) -> dict[str, torch.Tensor]:
        return {"emb": s.detach() * 1.0 + 0.01 * torch.randn_like(s)}

    metrics = cascade_rl_train_step(
        pol,
        opt,
        grpo,
        group_size=4,
        env_factory=lambda: ToyRoutingEnv(state_dim=dim, num_actions=4, max_steps=6, device=device),
        mopd=mopd,
        student_hidden_fn=student_h,
        teacher_hidden_fn=teacher_h,
        lambda_mopd=0.1,
    )
    assert "loss" in metrics
    assert any(k.startswith("loss_feat") for k in metrics)
