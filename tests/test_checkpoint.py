"""Checkpoint save/load and train/eval split helpers."""

from __future__ import annotations

import tempfile
from pathlib import Path

import torch

from qminiwasm.model import QMiniWASM
from qminiwasm.training.cascade_rl import TinyCascadePolicy
from qminiwasm.tpem.trainable_tpem import (
    load_cascade_policy_from_checkpoint,
    load_checkpoint_into_model,
    save_checkpoint,
)
from qminiwasm.training.loop import split_train_eval_data


def test_checkpoint_round_trip_changes_hybrid_output():
    device = torch.device("cpu")
    m1 = QMiniWASM(device=device)
    m2 = QMiniWASM(device=device)
    x = torch.randn(2, 4096, dtype=torch.float32)
    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        path = Path(f.name)
    try:
        save_checkpoint(path, m1, meta={"tag": "t1"})
        load_checkpoint_into_model(m2, path, map_location=device)
        o1 = m1.hybrid_inference(x)
        o2 = m2.hybrid_inference(x)
        assert torch.allclose(o1, o2)
    finally:
        path.unlink(missing_ok=True)


def test_checkpoint_loads_cascade_policy_into_model_cascade_router():
    device = torch.device("cpu")
    m1 = QMiniWASM(
        device=device,
        use_cascade_router=True,
        cascade_state_dim=8,
        cascade_num_actions=4,
    )
    m2 = QMiniWASM(
        device=device,
        use_cascade_router=True,
        cascade_state_dim=8,
        cascade_num_actions=4,
    )
    assert m1.cascade_router is not None and m2.cascade_router is not None
    with torch.no_grad():
        m1.cascade_router.projector.weight.fill_(0.33)
    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        path = Path(f.name)
    try:
        save_checkpoint(path, m1, meta={"tag": "cr"}, cascade_policy=m1.cascade_router)
        load_checkpoint_into_model(m2, path, map_location=device)
        assert torch.allclose(
            m1.cascade_router.projector.weight,
            m2.cascade_router.projector.weight,
        )
    finally:
        path.unlink(missing_ok=True)


def test_checkpoint_saves_and_loads_cascade_policy():
    device = torch.device("cpu")
    m1 = QMiniWASM(device=device)
    m2 = QMiniWASM(device=device)
    c1 = TinyCascadePolicy(8, 4).to(device)
    c2 = TinyCascadePolicy(8, 4).to(device)
    with torch.no_grad():
        c1.net[0].weight.fill_(0.42)
    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        path = Path(f.name)
    try:
        save_checkpoint(path, m1, meta={"tag": "cascade"}, cascade_policy=c1)
        load_checkpoint_into_model(m2, path, map_location=device)
        assert load_cascade_policy_from_checkpoint(c2, path, map_location=device)
        assert torch.allclose(c1.net[0].weight, c2.net[0].weight)
    finally:
        path.unlink(missing_ok=True)


def test_checkpoint_round_trip_with_hybrid_adapter():
    device = torch.device("cpu")
    m1 = QMiniWASM(device=device, use_hybrid_adapter=True, hybrid_adapter_hidden=128)
    m2 = QMiniWASM(device=device)
    x = torch.randn(2, 4096, dtype=torch.float32)
    with tempfile.NamedTemporaryFile(suffix=".pt", delete=False) as f:
        path = Path(f.name)
    try:
        save_checkpoint(path, m1, meta={"tag": "adapt"})
        load_checkpoint_into_model(m2, path, map_location=device)
        assert m2.hybrid_adapter is not None
        o1 = m1.hybrid_inference(x)
        o2 = m2.hybrid_inference(x)
        assert torch.allclose(o1, o2)
    finally:
        path.unlink(missing_ok=True)


def test_split_train_eval_deterministic_with_seed():
    data = [{"i": i, "hidden": torch.zeros(4096), "target": torch.zeros(4096)} for i in range(100)]
    t1, e1 = split_train_eval_data(data, 0.1, seed=42)
    t2, e2 = split_train_eval_data(data, 0.1, seed=42)
    assert len(e1) == 10
    assert len(t1) == 90
    assert [x["i"] for x in e1] == [x["i"] for x in e2]
    assert [x["i"] for x in t1] == [x["i"] for x in t2]


def test_split_train_eval_no_seed_last_fraction_eval():
    data = [{"i": i, "hidden": torch.zeros(4096), "target": torch.zeros(4096)} for i in range(20)]
    train, ev = split_train_eval_data(data, 0.2, seed=None)
    assert len(ev) == 4
    assert [x["i"] for x in ev] == [16, 17, 18, 19]
    assert len(train) == 16


def test_split_zero_fraction_no_eval():
    data = [{"i": i} for i in range(5)]
    train, ev = split_train_eval_data(data, 0.0, seed=1)
    assert ev == []
    assert train == data
