"""Cooperative stop file (--wui-stop-file) for Training WUI / Windows."""

from __future__ import annotations

from pathlib import Path

from qminiwasm.training.loop import run_training_loop


def test_wui_stop_file_before_first_epoch_stops_immediately(tmp_path: Path) -> None:
    stop_path = tmp_path / "wui_stop"
    stop_path.write_text("1", encoding="utf-8")
    out = run_training_loop(
        epochs=5,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
        wui_stop_file=str(stop_path),
    )
    assert not stop_path.exists()
    assert out["epochs_run"] == 0
    assert out["metrics"]["stopped_early"] is True
    assert out["metrics"]["graceful_stop_requested"] is True


def test_wui_stop_file_absent_runs_at_least_one_epoch(tmp_path: Path) -> None:
    missing = tmp_path / "never_created"
    out = run_training_loop(
        epochs=1,
        batch_size=2,
        learning_rate=1e-3,
        accelerator="cpu",
        use_cascade_rl=False,
        wui_stop_file=str(missing),
    )
    assert out["epochs_run"] >= 1
