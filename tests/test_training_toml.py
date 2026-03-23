"""Training TOML loader and EngineConfig integration."""

from __future__ import annotations

from pathlib import Path

import pytest
from pydantic import ValidationError

REPO_ROOT = Path(__file__).resolve().parents[1]


def test_load_training_toml_mesh_cpu():
    from engine.training_schema import load_training_toml

    p = REPO_ROOT / "configs" / "training" / "mesh_cpu.toml"
    cfg = load_training_toml(p)
    assert cfg.data.source == "mesh"
    assert cfg.hardware.accelerator == "cpu"
    assert cfg.training.epochs == 25


def test_load_training_toml_missing_file():
    from engine.training_schema import load_training_toml

    with pytest.raises(OSError):
        load_training_toml(REPO_ROOT / "configs" / "training" / "nonexistent_abc123.toml")


def test_load_training_toml_invalid_type(tmp_path):
    from engine.training_schema import load_training_toml

    f = tmp_path / "bad.toml"
    f.write_text("[training]\nepochs = 'not_an_int'\n", encoding="utf-8")
    with pytest.raises(ValidationError):
        load_training_toml(f)


def test_engine_config_from_toml_merges_hf_token_from_env(monkeypatch, tmp_path):
    monkeypatch.delenv("HUGGING_FACE_HUB_TOKEN", raising=False)
    monkeypatch.delenv("HF_TOKEN", raising=False)
    monkeypatch.setenv("HF_TOKEN", "tok_from_env")

    f = tmp_path / "t.toml"
    f.write_text(
        "[data]\nsource = \"mesh\"\n[training]\nepochs = 3\n",
        encoding="utf-8",
    )
    from engine.config import EngineConfig

    c = EngineConfig.from_training_toml(f)
    assert c.epochs == 3
    assert c.training_data_source == "mesh"
    assert c.hf_token == "tok_from_env"


def test_engine_config_hardware_only_sets_qiskit_ibm_when_default_pennylane(monkeypatch):
    """WUI hardware_only + default MoE mode must use IBM QAOA path, not identity pennylane."""
    monkeypatch.delenv("QMINIWASM_QAOA_EXECUTION", raising=False)
    monkeypatch.setenv("QUANTUM_EXECUTION_POLICY", "hardware_only")
    from engine.config import EngineConfig

    c = EngineConfig()
    assert c.qaoa_execution_mode == "qiskit_ibm"
    assert c.quantum_execution_policy == "hardware_only"


def test_engine_config_explicit_qaoa_mode_not_overridden_by_hardware_only_policy(monkeypatch):
    monkeypatch.setenv("QUANTUM_EXECUTION_POLICY", "hardware_only")
    monkeypatch.setenv("QMINIWASM_QAOA_EXECUTION", "qiskit_statevector")
    from engine.config import EngineConfig

    c = EngineConfig()
    assert c.qaoa_execution_mode == "qiskit_statevector"


def test_engine_config_toml_overrides_env_for_epochs(monkeypatch, tmp_path):
    monkeypatch.setenv("EPOCHS", "99")
    f = tmp_path / "t.toml"
    f.write_text("[training]\nepochs = 5\n", encoding="utf-8")
    from engine.config import EngineConfig

    c = EngineConfig.from_training_toml(f)
    assert c.epochs == 5


def test_load_training_config_alias():
    from engine.config import load_training_config

    p = REPO_ROOT / "configs" / "training" / "mesh_cpu.toml"
    root = load_training_config(p)
    assert root.training.batch_size == 32


def test_load_serve_toml_default_section(tmp_path):
    from engine.training_schema import load_serve_toml

    f = tmp_path / "serve.toml"
    f.write_text(
        "[serve]\ncheckpoint = \"ck.pt\"\nhybrid_adapter = true\n",
        encoding="utf-8",
    )
    s = load_serve_toml(f)
    assert s.checkpoint == "ck.pt"
    assert s.hybrid_adapter is True
