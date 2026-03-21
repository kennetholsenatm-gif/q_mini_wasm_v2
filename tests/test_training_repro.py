"""Training reproducibility and loop robustness helpers."""

from __future__ import annotations

import torch

from qminiwasm.training.repro import set_training_seed


def test_set_training_seed_runs_twice():
    set_training_seed(12345)
    a = torch.randn(3)
    set_training_seed(12345)
    b = torch.randn(3)
    assert torch.allclose(a, b)


def test_set_training_seed_changes_with_seed():
    set_training_seed(1)
    a = torch.randn(3)
    set_training_seed(2)
    b = torch.randn(3)
    assert not torch.allclose(a, b)
