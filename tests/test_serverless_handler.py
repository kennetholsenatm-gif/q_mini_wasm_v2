from __future__ import annotations

from pathlib import Path

from serverless import handler as handler_mod


def test_run_train_uses_repo_root_and_returns_streams(monkeypatch, tmp_path):
    monkeypatch.setenv("QMW_REPO_ROOT", str(tmp_path))

    seen: dict[str, object] = {}

    def fake_run_train(config_abs: Path):
        seen["config_abs"] = config_abs
        return 0, "out", "err"

    config = tmp_path / "configs" / "training" / "ok.toml"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[training]\nepochs = 1\n", encoding="utf-8")

    result = handler_mod.handle_training_job(
        {"qmw_action": "train", "config_rel": "configs/training/ok.toml"},
        run_train=fake_run_train,
    )

    assert result["ok"] is True
    assert result["exit_code"] == 0
    assert result["stdout"] == "out"
    assert result["stderr"] == "err"
    assert seen["config_abs"] == config.resolve()


def test_handle_training_job_rejects_config_escape(monkeypatch, tmp_path):
    monkeypatch.setenv("QMW_REPO_ROOT", str(tmp_path))
    result = handler_mod.handle_training_job(
        {"qmw_action": "train", "config_rel": "../outside.toml"},
    )
    assert result["ok"] is False
    assert "escapes repo root" in result["error"]


def test_handle_training_job_missing_config(monkeypatch, tmp_path):
    monkeypatch.setenv("QMW_REPO_ROOT", str(tmp_path))
    result = handler_mod.handle_training_job(
        {"qmw_action": "train", "config_rel": "configs/training/missing.toml"},
    )
    assert result["ok"] is False
    assert "config not found" in result["error"]


def test_handle_training_job_returns_repo_root_for_observability(monkeypatch, tmp_path):
    monkeypatch.setenv("QMW_REPO_ROOT", str(tmp_path))
    config = tmp_path / "configs" / "training" / "ok.toml"
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[training]\nepochs = 1\n", encoding="utf-8")

    result = handler_mod.handle_training_job(
        {"qmw_action": "train", "config_rel": "configs/training/ok.toml"},
        run_train=lambda _cfg: (0, "", ""),
    )

    assert result["repo_root"] == str(tmp_path.resolve())


def test_handler_requires_input_object():
    result = handler_mod.handler({"input": "not-an-object"})
    assert result["ok"] is False
    assert "input must be an object" in result["error"]


def test_handler_requires_supported_action():
    result = handler_mod.handler({"input": {"qmw_action": "infer"}})
    assert result["ok"] is False
    assert "unsupported qmw_action" in result["error"]
