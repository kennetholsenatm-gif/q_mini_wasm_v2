"""Hugging Face tabular loader: row encoding and optional Hub smoke."""

from __future__ import annotations

import pytest
import torch

from qminiwasm.training.hf_loader import row_to_encoded_blob
from qminiwasm.wasm.memory_encode import encode_linear_memory


def test_row_to_encoded_blob_code_fields_change_tensor():
    row_a = {"func_code_string": "def f():\n    return 1\n", "language": "python"}
    row_b = {"func_code_string": "def f():\n    return 2\n", "language": "python"}
    h_a = encode_linear_memory(row_to_encoded_blob(row_a)[:16384], result_i32=0, first_arg=0)
    h_b = encode_linear_memory(row_to_encoded_blob(row_b)[:16384], result_i32=0, first_arg=0)
    assert h_a.shape == h_b.shape
    assert (h_a - h_b).abs().sum().item() > 0
    assert not torch.allclose(h_a, h_b)


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
    hidden = encode_linear_memory(blob[:16384], result_i32=0, first_arg=0)
    assert hidden.numel() == 4096
