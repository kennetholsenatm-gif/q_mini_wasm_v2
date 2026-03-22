"""Hugging Face tabular loader: row encoding and optional Hub smoke."""

from __future__ import annotations

from unittest.mock import patch

import pytest
import torch

from qminiwasm.training.hf_loader import (
    encoded_blob_references_wasi,
    row_to_encoded_blob,
)
from qminiwasm.wasm.memory_encode import BODY_SLOTS, encode_linear_memory


def test_row_to_encoded_blob_code_fields_change_tensor():
    row_a = {"func_code_string": "def f():\n    return 1\n", "language": "python"}
    row_b = {"func_code_string": "def f():\n    return 2\n", "language": "python"}
    h_a = encode_linear_memory(row_to_encoded_blob(row_a)[:BODY_SLOTS], result_i32=0, first_arg=0)
    h_b = encode_linear_memory(row_to_encoded_blob(row_b)[:BODY_SLOTS], result_i32=0, first_arg=0)
    assert h_a.shape == h_b.shape
    assert (h_a - h_b).abs().sum().item() > 0
    assert not torch.allclose(h_a, h_b)


def test_auto_mode_prepends_context_when_keys_present():
    row = {
        "repo": "org/repo",
        "path": "pkg/mod.py",
        "language": "python",
        "func_name": "foo",
        "whole_func_string": "def foo():\n    return 1\n",
    }
    blob = row_to_encoded_blob(row, text_fields=None, context_fields=None)
    assert blob.startswith(b"[context]")
    assert b"repo: org/repo" in blob
    assert b"[code]" in blob


def test_context_fields_empty_skips_prefix():
    row = {
        "repo": "org/repo",
        "whole_func_string": "def x():\n  pass\n",
    }
    blob = row_to_encoded_blob(row, text_fields=None, context_fields=[])
    assert not blob.startswith(b"[context]")


def test_explicit_text_fields_no_auto_context_unless_listed():
    row = {"repo": "r", "func_code_string": "def f(): pass"}
    blob = row_to_encoded_blob(row, text_fields=["func_code_string"], context_fields=None)
    assert not blob.startswith(b"[context]")


def test_row_to_encoded_blob_explicit_text_fields():
    row = {"func_code_string": "x", "doc": "y"}
    blob = row_to_encoded_blob(row, text_fields=["doc", "func_code_string"])
    assert b"y" in blob
    assert b"x" in blob
    assert blob.index(b"y") < blob.index(b"x")


def test_row_to_encoded_blob_legacy_fallback():
    row = {"a": 1, "z": 2}
    blob = row_to_encoded_blob(row, text_fields=None)
    assert b"a" in blob or b"z" in blob


def test_engine_config_hf_context_fields_env(monkeypatch):
    monkeypatch.delenv("HF_CONTEXT_FIELDS", raising=False)
    from engine.config import EngineConfig

    assert EngineConfig().hf_context_fields is None

    monkeypatch.setenv("HF_CONTEXT_FIELDS", "0")
    assert EngineConfig().hf_context_fields == []

    monkeypatch.setenv("HF_CONTEXT_FIELDS", "repo,path")
    assert EngineConfig().hf_context_fields == ["repo", "path"]


def test_engine_config_hybrid_adapter_from_env(monkeypatch):
    monkeypatch.delenv("HYBRID_ADAPTER", raising=False)
    monkeypatch.delenv("HYBRID_ADAPTER_HIDDEN", raising=False)
    from engine.config import EngineConfig

    c = EngineConfig()
    assert c.hybrid_adapter is False
    assert c.hybrid_adapter_hidden == 1024

    monkeypatch.setenv("HYBRID_ADAPTER", "1")
    monkeypatch.setenv("HYBRID_ADAPTER_HIDDEN", "512")
    c2 = EngineConfig()
    assert c2.hybrid_adapter is True
    assert c2.hybrid_adapter_hidden == 512


def test_engine_config_target_mean_mse_from_env(monkeypatch):
    monkeypatch.delenv("TARGET_MEAN_MSE", raising=False)
    monkeypatch.delenv("STOP_ON_TARGET_MSE", raising=False)
    from engine.config import EngineConfig

    assert EngineConfig().target_mean_mse is None
    assert EngineConfig().stop_on_target_mse is False

    monkeypatch.setenv("TARGET_MEAN_MSE", "1e-4")
    monkeypatch.setenv("STOP_ON_TARGET_MSE", "1")
    c = EngineConfig()
    assert c.target_mean_mse == 1e-4
    assert c.stop_on_target_mse is True


def test_engine_config_hf_wasi_from_env(monkeypatch):
    monkeypatch.delenv("HF_WASI_SLICE_ONLY", raising=False)
    monkeypatch.delenv("HF_WASI_MAX_SCAN", raising=False)
    from engine.config import EngineConfig

    assert EngineConfig().hf_wasi_slice_only is False
    assert EngineConfig().hf_wasi_max_scan is None

    monkeypatch.setenv("HF_WASI_SLICE_ONLY", "1")
    monkeypatch.setenv("HF_WASI_MAX_SCAN", "5000000")
    assert EngineConfig().hf_wasi_slice_only is True
    assert EngineConfig().hf_wasi_max_scan == 5_000_000


def test_engine_config_hf_token_from_env(monkeypatch):
    monkeypatch.delenv("HUGGING_FACE_HUB_TOKEN", raising=False)
    monkeypatch.delenv("HF_TOKEN", raising=False)
    monkeypatch.setenv("HF_TOKEN", "hf_from_env")
    from engine.config import EngineConfig

    assert EngineConfig().hf_token == "hf_from_env"

    monkeypatch.setenv("HUGGING_FACE_HUB_TOKEN", "hub_wins")
    assert EngineConfig().hf_token == "hub_wins"


def test_encoded_blob_references_wasi():
    assert not encoded_blob_references_wasi(b"def foo():\n  return 1\n")
    assert encoded_blob_references_wasi(b'extern "wasi" fn random_get')
    assert encoded_blob_references_wasi(b"wasm32-wasip1-unknown-unknown")


@patch("datasets.load_dataset", autospec=True)
def test_load_hf_tabular_samples_passes_token_to_load_dataset(mock_load):
    pytest.importorskip("datasets")
    mock_load.return_value = [{"func_code_string": "def f():\n    pass\n"}]

    from qminiwasm.training.hf_loader import load_hf_tabular_samples

    out = load_hf_tabular_samples(
        "org/dataset",
        1,
        split="train",
        config_name="python",
        token="hf_test_token",
    )
    mock_load.assert_called_once()
    assert mock_load.call_args.kwargs.get("token") == "hf_test_token"
    assert mock_load.call_args.kwargs.get("revision") == "main"
    assert len(out) == 1
    assert out[0]["hidden"].shape == (4096,)


@patch("datasets.load_dataset", autospec=True)
def test_load_hf_wasi_slice_streams_and_filters(mock_load):
    pytest.importorskip("datasets")
    rows = [
        {"whole_func_string": "def f():\n    return 1\n"},
        {"whole_func_string": "fn x() { use wasi::snapshots::preview_1; }\n"},
    ]
    mock_load.return_value = iter(rows)

    from qminiwasm.training.hf_loader import load_hf_tabular_samples

    out = load_hf_tabular_samples(
        "org/dataset",
        10,
        split="train",
        config_name="python",
        wasi_slice_only=True,
        max_scan_rows=1000,
    )
    mock_load.assert_called_once()
    assert mock_load.call_args.kwargs.get("streaming") is True
    assert mock_load.call_args.kwargs.get("revision") == "main"
    assert len(out) == 1
    assert out[0]["algorithm"] == "hf_tabular_wasi"
    assert out[0]["execution_state"].get("wasi_slice_only") is True


def test_whole_func_string_wins_over_func_code_string():
    row = {
        "whole_func_string": '"""doc"""\ndef f():\n    return 99\n',
        "func_code_string": "def f():\n    return 0\n",
    }
    blob = row_to_encoded_blob(row, text_fields=None)
    assert b"doc" in blob
    assert b"return 99" in blob
    assert b"return 0" not in blob


@pytest.mark.integration
def test_codesearchnet_streaming_row_smoke():
    pytest.importorskip("datasets")
    import datasets

    try:
        ds = datasets.load_dataset(
            "code-search-net/code_search_net",
            "python",
            split="train",
            streaming=True,
        )
        row = next(iter(ds))
    except Exception as exc:
        pytest.skip(str(exc))

    blob = row_to_encoded_blob(dict(row))
    assert len(blob) > 0
    hidden = encode_linear_memory(blob[:BODY_SLOTS], result_i32=0, first_arg=0)
    assert hidden.numel() == 4096
