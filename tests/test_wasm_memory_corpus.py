"""WASM linear-memory encoding and corpus manifest ingestion."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import torch
import wasmtime

from qminiwasm.data.pipeline import DataPipeline
from qminiwasm.wasm.engine import WasmEngine
from qminiwasm.wasm.memory_encode import D_MODEL, encode_linear_memory


def test_encode_linear_memory_shape_and_stable_meta():
    mem = bytes(range(256)) + b"\xff" * 5000
    t = encode_linear_memory(mem, result_i32=42, first_arg=7)
    assert t.shape == (D_MODEL,)
    assert t.dtype == torch.float32
    assert t[0].item() == pytest.approx(1.0)
    assert t[2].item() > 0


def test_wasm_engine_memory_snapshot_changes(tmp_path: Path):
    wat = r"""
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (i32.store (i32.const 32) (i32.add (local.get 0) (local.get 1)))
        (return (i32.add (local.get 0) (local.get 1)))
      )
      (export "memory" (memory 0))
      (export "run" (func $run))
    )
    """
    wasm_path = tmp_path / "t.wasm"
    wasm_path.write_bytes(wasmtime.wat2wasm(wat))

    eng = WasmEngine(use_mock=False)
    if eng.use_mock:
        pytest.skip("WASM runtime unavailable in this environment")
    mod = eng.compile_wasm(wasm_path.read_bytes())
    assert mod is not None

    _, h1, t1, _pre1, post1 = eng.execute_wasm(mod, "run", [3, 4])
    _, h2, t2, _pre2, post2 = eng.execute_wasm(mod, "run", [10, 1])
    assert h1 is not None and t1 is not None and h2 is not None and t2 is not None
    assert h1.numel() == 4096 and t1.numel() == 4096
    assert not torch.allclose(t1, t2)
    assert len(post1) >= 36


def test_generate_training_data_from_corpus(tmp_path: Path):
    wat = r"""
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (i32.store (i32.const 8) (i32.mul (local.get 0) (local.get 1)))
        (return (i32.add (local.get 0) (local.get 1)))
      )
      (export "memory" (memory 0))
      (export "run" (func $run))
    )
    """
    wasm_path = tmp_path / "c.wasm"
    wasm_path.write_bytes(wasmtime.wat2wasm(wat))
    manifest = {
        "version": 1,
        "base_dir": str(tmp_path),
        "entries": [
            {
                "id": "mul_store",
                "wasm_path": "c.wasm",
                "export_func": "run",
                "num_args": 2,
                "arg_max": 50,
            }
        ],
    }
    man_path = tmp_path / "manifest.json"
    man_path.write_text(json.dumps(manifest), encoding="utf-8")

    pipe = DataPipeline()
    if pipe.wasm_engine.use_mock:
        pytest.skip("WASM runtime unavailable")

    data = pipe.generate_training_data_from_corpus(str(man_path), num_samples=5, seed=0)
    assert len(data) == 5
    for s in data:
        assert s["hidden"].numel() == 4096
        assert s["target"].numel() == 4096
        assert isinstance(s["wasm_memory"], (bytes, bytearray))
