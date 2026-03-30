"""Unified Training Matrix phase helpers."""

from __future__ import annotations

from qminiwasm.training.phases import (
    flatten_training_phases,
    resolve_phase_at_epoch,
    total_epochs_from_phases,
)


def test_total_epochs_from_phases() -> None:
    p = [{"name": "a", "epochs": 2}, {"name": "b", "epochs": 3}]
    assert total_epochs_from_phases(p) == 5


def test_resolve_phase_at_epoch() -> None:
    p = [{"name": "a", "epochs": 2, "supervised": True}, {"name": "b", "epochs": 1, "supervised": False}]
    i0, s0 = resolve_phase_at_epoch(0, p)
    assert i0 == 0 and s0["name"] == "a"
    i1, s1 = resolve_phase_at_epoch(2, p)
    assert i1 == 1 and s1["name"] == "b"


def test_flatten_training_phases() -> None:
    p = [{"name": "x", "epochs": 2}]
    flat = flatten_training_phases(p)
    assert len(flat) == 2
    assert flat[0].phase_name == "x" and flat[1].global_epoch == 1
