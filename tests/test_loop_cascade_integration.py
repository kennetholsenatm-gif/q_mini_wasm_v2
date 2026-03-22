"""Integration smoke: cascade RL phase runs inside run_training_loop (default on)."""

from __future__ import annotations

from qminiwasm.training.loop import run_training_loop


def test_run_training_loop_includes_cascade_phase_by_default() -> None:
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=True,
        cascade_steps_per_epoch=1,
        cascade_group_size=2,
        cascade_policy_lr=1e-2,
        cascade_state_dim=6,
        cascade_num_actions=3,
    )
    assert out["epochs_run"] >= 1
    m = out["metrics"]
    assert m.get("use_cascade_rl") is True
    assert "epoch_cascade_loss" in m
    assert len(m["epoch_cascade_loss"]) == 1


def test_run_training_loop_cascade_can_be_disabled() -> None:
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
    )
    m = out["metrics"]
    assert m.get("use_cascade_rl") is not True
    assert "epoch_cascade_loss" not in m
