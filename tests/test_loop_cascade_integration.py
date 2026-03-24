"""Integration smoke: cascade RL phase runs inside run_training_loop (default on)."""

from __future__ import annotations

import math

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
    assert m.get("cascade_policy_mode") == "tiny_mlp"
    assert "epoch_cascade_loss" in m
    assert len(m["epoch_cascade_loss"]) == 1


def test_run_training_loop_cascade_learned_projector_mode() -> None:
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=True,
        cascade_learned_projector=True,
        cascade_steps_per_epoch=1,
        cascade_group_size=2,
        cascade_policy_lr=1e-2,
        cascade_state_dim=6,
        cascade_num_actions=3,
    )
    assert out["metrics"].get("cascade_policy_mode") == "loop_router"


def test_run_training_loop_cascade_model_router_mode() -> None:
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=True,
        use_cascade_router=True,
        cascade_steps_per_epoch=1,
        cascade_group_size=2,
        cascade_policy_lr=1e-2,
        cascade_state_dim=6,
        cascade_num_actions=3,
    )
    assert out["metrics"].get("cascade_policy_mode") == "model_router"


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


def test_run_training_loop_missing_checkpoint_load_path_trains_from_scratch() -> None:
    """load_path in TOML may point at a not-yet-created latest.pt; do not fail startup."""
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
        checkpoint_load_path="artifacts/models/__no_such_run__/latest.pt",
    )
    assert out["epochs_run"] >= 1


def test_run_training_loop_cascade_with_mopd_lambda() -> None:
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
        cascade_mopd_lambda=0.1,
        cascade_mopd_feat_loss="cosine",
    )
    m = out["metrics"]
    assert m.get("use_cascade_rl") is True
    assert "epoch_cascade_loss" in m
    assert len(m["epoch_cascade_loss"]) == 1
    assert m.get("cascade_mopd_feat_loss") == "cosine"
    assert math.isfinite(m["epoch_cascade_loss"][0])


def test_run_training_loop_cascade_mopd_invalid_feat_loss_defaults_to_mse() -> None:
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
        cascade_mopd_lambda=0.05,
        cascade_mopd_feat_loss="not_a_mode",
    )
    assert out["metrics"].get("cascade_mopd_feat_loss") == "mse"


def test_run_training_loop_reuses_model_pipeline(monkeypatch) -> None:
    def _should_not_construct_pipeline():
        raise AssertionError(
            "loop.DataPipeline should not be constructed when model has data_pipeline"
        )

    monkeypatch.setattr("qminiwasm.training.loop.DataPipeline", _should_not_construct_pipeline)
    out = run_training_loop(
        epochs=0,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
    )
    assert out["epochs_run"] == 0
