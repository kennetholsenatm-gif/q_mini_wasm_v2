"""Tests for CISPO module and cascade_rl_train_step CISPO path."""

from __future__ import annotations

import math

import torch

from qminiwasm.rl.cascade_cispo import CascadeCISPO, CascadeCISPOConfig
from qminiwasm.rl.cascade_grpo import CascadeGRPO, CascadeGRPOConfig
from qminiwasm.training.cascade_rl import TinyCascadePolicy, cascade_rl_train_step


def test_cispo_backward_keeps_logprob_gradient_when_ratio_clipped() -> None:
    logp = torch.tensor([2.0, 0.0], requires_grad=True)
    logp_old = torch.tensor([0.0, 0.0], requires_grad=False)
    returns = torch.ones(2)
    cispo = CascadeCISPO(CascadeCISPOConfig(normalize_advantage=False, epsilon=0.1))
    loss, _ = cispo(logp, logp_old, returns)
    loss.backward()
    assert logp.grad is not None
    assert torch.isfinite(logp.grad).all()
    assert logp.grad.abs().sum() > 0


def test_cascade_rl_train_step_cispo_runs() -> None:
    pol = TinyCascadePolicy(state_dim=8, num_actions=4)
    opt = torch.optim.Adam(pol.parameters(), lr=1e-2)
    grpo = CascadeGRPO(CascadeGRPOConfig(normalize_advantage=True))
    cispo = CascadeCISPO(CascadeCISPOConfig(normalize_advantage=True, epsilon=0.2))
    m1 = cascade_rl_train_step(pol, opt, grpo, group_size=4)
    m2 = cascade_rl_train_step(pol, opt, grpo=None, cispo=cispo, group_size=4)
    assert "loss" in m1 and "loss" in m2
    assert math.isfinite(m1["loss"]) and math.isfinite(m2["loss"])


def test_cascade_rl_train_step_rejects_both_trainers() -> None:
    pol = TinyCascadePolicy(state_dim=8, num_actions=4)
    opt = torch.optim.Adam(pol.parameters(), lr=1e-2)
    grpo = CascadeGRPO(CascadeGRPOConfig(normalize_advantage=True))
    cispo = CascadeCISPO(CascadeCISPOConfig(normalize_advantage=True))
    try:
        cascade_rl_train_step(pol, opt, grpo, cispo=cispo, group_size=4)
    except ValueError as e:
        assert "exactly one" in str(e).lower()
    else:
        raise AssertionError("expected ValueError")
