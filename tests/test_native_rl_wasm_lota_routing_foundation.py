from __future__ import annotations

import os

import numpy as np
import torch

from qminiwasm.layers.lota import LoRALinearSide, merge_lora_into_linear_weight
from qminiwasm.training.lota_qaf import TSignSGD
from qminiwasm.training.cascade_rl import TinyCascadePolicy, cascade_rl_train_step
from qminiwasm.rl.cascade_grpo import CascadeGRPO, CascadeGRPOConfig
from qminiwasm.wasm_host.tpem_bundle import read_tpem_bundle, write_tpem_bundle
from qminiwasm.fabric.dqaoa_cluster import spectral_partition
from qminiwasm.fabric.router import QuantumRouter


def test_lota_merge_shape_and_update() -> None:
    base = torch.zeros(4, 8, dtype=torch.float32)
    lora = LoRALinearSide(8, 4, rank=2)
    x = torch.randn(3, 8)
    y = lora(x)
    assert y.shape == (3, 4)
    merge_lora_into_linear_weight(base, lora)
    assert base.shape == (4, 8)


def test_tsign_optimizer_step_runs() -> None:
    p = torch.nn.Parameter(torch.ones(8, dtype=torch.float32))
    p.grad = torch.ones_like(p)
    opt = TSignSGD([p], lr=1e-2)
    before = p.detach().clone()
    opt.step()
    assert not torch.equal(before, p.detach())


def test_native_rl_gate_path_runs() -> None:
    os.environ["QMINIWASM_NATIVE_RL_RUNTIME"] = "1"
    pol = TinyCascadePolicy(state_dim=8, num_actions=4)
    opt = torch.optim.Adam(pol.parameters(), lr=1e-3)
    grpo = CascadeGRPO(CascadeGRPOConfig())
    m = cascade_rl_train_step(pol, opt, grpo, group_size=4, env_factory=None)
    assert "loss" in m
    os.environ.pop("QMINIWASM_NATIVE_RL_RUNTIME", None)


def test_tpem_bundle_roundtrip() -> None:
    payload = b"abc123"
    path = "artifacts/models/_tmp_native_foundation.tpem"
    write_tpem_bundle(path, payload)
    b = read_tpem_bundle(path)
    assert b.payload == payload


def test_routing_helpers_fallback_outputs() -> None:
    a = np.eye(6, dtype=np.float64)
    labels = spectral_partition(a, num_clusters=3)
    assert labels.shape == (6,)
    q = np.array([0.0, 0.0], dtype=np.float64)
    db = [np.array([1.0, 1.0], dtype=np.float64), np.array([0.1, 0.1], dtype=np.float64)]
    out = QuantumRouter().find_k_nearest_neighbors(q, db, k=1)
    assert out and out[0] in (0, 1)
