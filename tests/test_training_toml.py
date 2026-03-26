"""Training TOML loader and EngineConfig integration."""

from __future__ import annotations

from pathlib import Path

import pytest
from pydantic import ValidationError

REPO_ROOT = Path(__file__).resolve().parents[1]


def test_load_training_toml_mesh_cpu():
    from qminiwasm.engine.training_schema import load_training_toml

    p = REPO_ROOT / "configs" / "training" / "mesh_cpu.toml"
    cfg = load_training_toml(p)
    assert cfg.data.source == "mesh"
    assert cfg.hardware.accelerator == "cpu"
    assert cfg.training.epochs == 25


def test_load_training_toml_missing_file():
    from qminiwasm.engine.training_schema import load_training_toml

    with pytest.raises(OSError):
        load_training_toml(REPO_ROOT / "configs" / "training" / "nonexistent_abc123.toml")


def test_load_training_toml_invalid_type(tmp_path):
    from qminiwasm.engine.training_schema import load_training_toml

    f = tmp_path / "bad.toml"
    f.write_text("[training]\nepochs = 'not_an_int'\n", encoding="utf-8")
    with pytest.raises(ValidationError):
        load_training_toml(f)


def test_runpod_serverless_section_parses(tmp_path):
    """Optional [runpod_serverless] is for WUI; engine load must still succeed."""
    f = tmp_path / "rp.toml"
    f.write_text(
        '[hardware]\naccelerator = "cpu"\n\n'
        '[data]\nsource = "mesh"\n\n'
        "[runpod_serverless]\n"
        'worker_image = "docker.io/example/worker:v1"\n',
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig
    from qminiwasm.engine.training_schema import load_training_toml

    cfg = load_training_toml(f)
    assert cfg.runpod_serverless is not None
    assert cfg.runpod_serverless.worker_image == "docker.io/example/worker:v1"

    EngineConfig.from_training_toml(f)


def test_engine_config_from_toml_merges_hf_token_from_env(monkeypatch, tmp_path):
    monkeypatch.delenv("HUGGING_FACE_HUB_TOKEN", raising=False)
    monkeypatch.delenv("HF_TOKEN", raising=False)
    monkeypatch.setenv("HF_TOKEN", "tok_from_env")

    f = tmp_path / "t.toml"
    f.write_text(
        '[data]\nsource = "mesh"\n[training]\nepochs = 3\n',
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig.from_training_toml(f)
    assert c.epochs == 3
    assert c.training_data_source == "mesh"
    assert c.hf_token == "tok_from_env"


def test_engine_config_hardware_only_sets_qiskit_ibm_when_default_pennylane():
    """WUI hardware_only + default MoE mode must use IBM QAOA path, not identity pennylane."""
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(quantum_execution_policy="hardware_only")
    assert c.qaoa_execution_mode == "qiskit_ibm"
    assert c.quantum_execution_policy == "hardware_only"


def test_engine_config_explicit_qaoa_mode_not_overridden_by_hardware_only_policy():
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(
        quantum_execution_policy="hardware_only",
        qaoa_execution_mode="qiskit_statevector",
    )
    assert c.qaoa_execution_mode == "qiskit_statevector"


def test_engine_config_toml_overrides_env_for_epochs(monkeypatch, tmp_path):
    monkeypatch.setenv("EPOCHS", "99")
    f = tmp_path / "t.toml"
    f.write_text("[training]\nepochs = 5\n", encoding="utf-8")
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig.from_training_toml(f)
    assert c.epochs == 5


def test_load_training_config_alias():
    from qminiwasm.engine.config import load_training_config

    p = REPO_ROOT / "configs" / "training" / "mesh_cpu.toml"
    root = load_training_config(p)
    assert root.training.batch_size == 32


def test_load_serve_toml_default_section(tmp_path):
    from qminiwasm.engine.training_schema import load_serve_toml

    f = tmp_path / "serve.toml"
    f.write_text(
        '[serve]\ncheckpoint = "ck.pt"\nhybrid_adapter = true\n',
        encoding="utf-8",
    )
    s = load_serve_toml(f)
    assert s.checkpoint == "ck.pt"
    assert s.tpem is None
    assert s.hybrid_adapter is True


def test_load_serve_toml_tpem_field(tmp_path):
    from qminiwasm.engine.training_schema import load_serve_toml

    f = tmp_path / "serve_tpem.toml"
    f.write_text(
        '[serve]\ntpem = "weights.pt"\nhybrid_adapter = false\n',
        encoding="utf-8",
    )
    s = load_serve_toml(f)
    assert s.tpem == "weights.pt"
    assert s.checkpoint is None


def test_tpem_table_overrides_checkpoint_in_engine_kwargs(tmp_path):
    from qminiwasm.engine.config import EngineConfig
    from qminiwasm.engine.training_schema import load_training_toml

    f = tmp_path / "merge.toml"
    f.write_text(
        '[data]\nsource = "mesh"\n[training]\nepochs = 1\n'
        '[checkpoint]\nload_path = "legacy.pt"\n'
        '[tpem]\nload_path = "preferred.pt"\n',
        encoding="utf-8",
    )
    doc = load_training_toml(f)
    assert doc.checkpoint.load_path == "legacy.pt"
    assert doc.tpem.load_path == "preferred.pt"
    c = EngineConfig.from_training_toml(f)
    assert c.checkpoint_load_path == "preferred.pt"


def test_load_serve_document_enclave_section(tmp_path):
    from qminiwasm.engine.training_schema import load_serve_document

    f = tmp_path / "serve2.toml"
    f.write_text(
        '[serve]\ncheckpoint = "c.pt"\n\n'
        "[enclave]\n"
        'enclave_tier = "macro"\n'
        "certainty_scalar_threshold = 0.9\n"
        "wasm_memory64_max_mb = 8192.0\n",
        encoding="utf-8",
    )
    doc = load_serve_document(f)
    assert doc.serve.checkpoint == "c.pt"
    assert doc.enclave.enclave_tier == "macro"
    assert doc.enclave.certainty_scalar_threshold == 0.9
    assert doc.enclave.wasm_memory64_max_mb == 8192.0


def test_enclave_fields_map_to_engine_kwargs(tmp_path):
    from qminiwasm.engine.training_schema import load_training_toml

    f = tmp_path / "enclave_train.toml"
    f.write_text(
        "[training]\n"
        "epochs = 1\n\n"
        "[enclave]\n"
        'enclave_tier = "meso"\n'
        "enclave_footprint_mb = 2048.0\n"
        "use_memory64 = false\n"
        "certainty_scalar_threshold = 0.77\n",
        encoding="utf-8",
    )
    cfg = load_training_toml(f)
    kwargs = cfg.to_engine_kwargs()
    assert kwargs["enclave_tier"] == "meso"
    assert kwargs["enclave_footprint_mb"] == 2048.0
    assert kwargs["use_memory64"] is False
    assert kwargs["certainty_scalar_threshold"] == 0.77


def test_hf_extra_specs_toml_and_engine_config(tmp_path):
    f = tmp_path / "multi_hf.toml"
    f.write_text(
        '[data]\nsource = "hf_tabular"\npath = "openai/gsm8k"\n\n'
        "[huggingface]\n"
        "extra_specs = [\n"
        '  { path = "imdb" },\n'
        '  { path = "wikitext", dataset_config = "wikitext-2-raw-v1" },\n'
        "]\n",
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig
    from qminiwasm.engine.training_schema import load_training_toml

    cfg = load_training_toml(f)
    assert cfg.data.path == "openai/gsm8k"
    assert cfg.huggingface.extra_specs is not None
    assert len(cfg.huggingface.extra_specs) == 2
    assert cfg.huggingface.extra_specs[1].dataset_config == "wikitext-2-raw-v1"

    c = EngineConfig.from_training_toml(f)
    assert len(c.hf_extra_specs) == 2
    assert c.hf_extra_specs[1].get("dataset_config") == "wikitext-2-raw-v1"


def test_loop_hf_merge_and_budget_helpers():
    from qminiwasm.training.loop import _merge_hf_dataset_specs, _split_int_budget

    assert _split_int_budget(100, 3) == [34, 33, 33]
    assert sum(_split_int_budget(100, 3)) == 100

    m = _merge_hf_dataset_specs(
        "openai/gsm8k",
        None,
        [
            {"path": "imdb"},
            {"path": "openai/gsm8k", "dataset_config": None},
        ],
    )
    assert m[0][0] == "openai/gsm8k"
    assert len(m) == 2

    extras_only = _merge_hf_dataset_specs(
        "qminiwasm/hf-multi",
        "python",
        [
            {"path": "openai/gsm8k"},
            {"path": "imdb"},
        ],
    )
    assert extras_only[0][0] == "openai/gsm8k"
    assert extras_only[1][0] == "imdb"
    assert len(extras_only) == 2


def test_hf_multi_without_extra_specs_raises(tmp_path):
    f = tmp_path / "bad_multi.toml"
    f.write_text(
        '[data]\nsource = "hf_tabular"\npath = "qminiwasm/hf-multi"\n\n'
        "[huggingface]\n"
        'split = "train"\n',
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig

    with pytest.raises(ValueError, match="extra_specs"):
        EngineConfig.from_training_toml(f)


def test_eval_early_stop_patience_from_toml(tmp_path):
    f = tmp_path / "eval_es.toml"
    f.write_text(
        '[data]\nsource = "mesh"\n\n'
        "[eval]\n"
        "holdout_fraction = 0.1\n"
        "every_epoch = true\n"
        "early_stop_patience = 4\n",
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig
    from qminiwasm.engine.training_schema import load_training_toml

    root = load_training_toml(f)
    assert root.eval.every_epoch is True
    assert root.eval.early_stop_patience == 4

    c = EngineConfig.from_training_toml(f)
    assert c.eval_early_stop_patience == 4
    assert c.eval_every_epoch is True


def test_edge_hf_streaming_knobs_from_toml(tmp_path):
    f = tmp_path / "edge.toml"
    f.write_text(
        '[data]\nsource = "hf_tabular"\npath = "openai/gsm8k"\n\n'
        "[huggingface]\n"
        "streaming = true\n"
        "max_scan_rows = 1000\n"
        "max_buffered_rows = 512\n"
        "text_truncate_bytes = 2048\n"
        "deterministic_keep_every_n = 3\n",
        encoding="utf-8",
    )
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig.from_training_toml(f)
    assert c.hf_streaming is True
    assert c.hf_max_scan_rows == 1000
    assert c.hf_max_buffered_rows == 512
    assert c.hf_text_truncate_bytes == 2048
    assert c.hf_deterministic_keep_every_n == 3


def test_enclave_tier_preset_macro_applies_memory64_defaults():
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(enclave_tier="macro")
    runtime = c.wasm_runtime_kwargs()["runtime"]
    # Macro preset: 8 GiB cap, Memory64 required, 8192 MB default ceiling.
    assert runtime.store_memory_limit_bytes >= 8 * 1024 * 1024 * 1024
    assert runtime.use_memory64 is True
    assert runtime.memory64_max_mb == 8192.0


def test_enclave_override_pages_win_over_tier_defaults():
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(enclave_tier="micro", max_linear_memory_pages=8192)
    runtime = c.wasm_runtime_kwargs()["runtime"]
    # Explicit override should win (8192 pages = 512 MiB).
    assert runtime.store_memory_limit_bytes == 8192 * 64 * 1024


def test_enclave_macro_rejects_memory64_false():
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(enclave_tier="macro", use_memory64=False)
    with pytest.raises(ValueError, match="requires Memory64"):
        c.wasm_runtime_kwargs()


def test_enclave_override_pages_too_low_for_tier_rejected():
    from qminiwasm.engine.config import EngineConfig

    c = EngineConfig(enclave_tier="macro", max_linear_memory_pages=1000)
    with pytest.raises(ValueError, match="too low"):
        c.wasm_runtime_kwargs()
