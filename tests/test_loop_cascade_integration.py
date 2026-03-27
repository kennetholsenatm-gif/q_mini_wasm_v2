"""Integration smoke: cascade RL phase runs inside run_training_loop (default on)."""

from __future__ import annotations

import math
import pytest
import torch

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


def test_run_training_loop_reports_wasm_execution_origin_metrics(monkeypatch) -> None:
    def _fake_hf_loader(*_args, **_kwargs):
        hidden = torch.zeros(4096, dtype=torch.float32)
        target = torch.ones(4096, dtype=torch.float32)
        return [
            {
                "hidden": hidden,
                "target": target,
                "execution_state": {"wasm_execution_origin": "real"},
            },
            {
                "hidden": hidden + 1.0,
                "target": target + 1.0,
                "execution_state": {"wasm_execution_origin": "mock"},
            },
        ]

    monkeypatch.setattr("qminiwasm.training.hf_loader.load_hf_tabular_samples", _fake_hf_loader)
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
        training_data_source="hf_tabular",
        data_path="dummy/dataset",
        hf_num_samples=2,
        eval_holdout_fraction=0.0,
    )
    m = out["metrics"]
    assert m["wasm_execution_origin_counts"]["real"] == 1
    assert m["wasm_execution_origin_counts"]["mock"] == 1
    assert m["wasm_mock_sample_ratio"] == pytest.approx(0.5)


def test_run_training_loop_strict_wasm_mock_ratio_gate(monkeypatch) -> None:
    def _fake_hf_loader(*_args, **_kwargs):
        hidden = torch.zeros(4096, dtype=torch.float32)
        target = torch.ones(4096, dtype=torch.float32)
        return [
            {
                "hidden": hidden,
                "target": target,
                "execution_state": {"wasm_execution_origin": "mock"},
            }
        ]

    monkeypatch.setattr("qminiwasm.training.hf_loader.load_hf_tabular_samples", _fake_hf_loader)
    monkeypatch.setenv("QMINIWASM_ASSERT_ZERO_MOCK_RATIO", "1")
    with pytest.raises(RuntimeError, match="mock sample ratio"):
        run_training_loop(
            epochs=1,
            batch_size=1,
            learning_rate=1e-3,
            accelerator="cpu",
            use_cascade_rl=False,
            training_data_source="hf_tabular",
            data_path="dummy/dataset",
            hf_num_samples=1,
        )


def test_run_training_loop_strict_config_rejects_unknown_source(monkeypatch) -> None:
    monkeypatch.setenv("QMINIWASM_STRICT_CONFIG_VALIDATION", "1")
    with pytest.raises(ValueError, match="Unsupported training_data_source"):
        run_training_loop(
            epochs=1,
            batch_size=1,
            learning_rate=1e-3,
            accelerator="cpu",
            use_cascade_rl=False,
            training_data_source="invalid_source",
        )
