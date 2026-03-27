"""WASM linear-memory encoding and corpus manifest ingestion."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
import torch
import wasmtime

from qminiwasm.data.pipeline import DataPipeline, ESIStateRecoveryHull
from qminiwasm.wasm_host.engine import (
    DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES,
    WasmEngine,
    WasmRuntimeConfig,
)
from qminiwasm.wasm_host.memory_encode import D_MODEL, encode_linear_memory


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


def test_wasm_engine_execute_reports_execution_origin(tmp_path: Path):
    wat = r"""
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (return (i32.add (local.get 0) (local.get 1)))
      )
      (export "memory" (memory 0))
      (export "run" (func $run))
    )
    """
    wasm_path = tmp_path / "origin.wasm"
    wasm_path.write_bytes(wasmtime.wat2wasm(wat))

    eng = WasmEngine(use_mock=False)
    if eng.use_mock:
        pytest.skip("WASM runtime unavailable in this environment")
    mod = eng.compile_wasm(wasm_path.read_bytes())
    assert mod is not None

    out, state = eng.execute(mod, "run", [2, 5])
    assert out == 7
    assert state["wasm_execution_origin"] == "real"


def test_wasm_engine_execute_reports_mock_origin():
    eng = WasmEngine(use_mock=True)
    out, state = eng.execute(None, "hash", [2, 3])
    assert isinstance(out, int)
    assert state["wasm_execution_origin"] == "mock"


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


def test_wasm_engine_store_memory_limit_from_config():
    eng = WasmEngine(
        use_mock=True,
        runtime=WasmRuntimeConfig(store_memory_limit_bytes=128 * 1024 * 1024),
    )
    assert eng._store_memory_limit_bytes == 128 * 1024 * 1024


def test_default_wasm_store_memory_limit_matches_million_rows_times_d_model():
    assert DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES == 1_000_000 * D_MODEL
    eng = WasmEngine(use_mock=True)
    assert eng._store_memory_limit_bytes == DEFAULT_WASM_STORE_MEMORY_LIMIT_BYTES


def test_execute_wasm_reuses_instance_no_10k_cap_per_sample():
    """Mesh-style loops must not create a new wasmtime.Instance per call (Wasmtime default cap ~10k)."""
    import wasmtime

    pytest.importorskip("wasmtime")
    wat = """
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (return (i32.add (local.get 0) (local.get 1))))
      (export "memory" (memory 0))
      (export "run" (func $run))
    )
    """
    eng = WasmEngine(
        use_mock=False, runtime=WasmRuntimeConfig(store_memory_limit_bytes=32 * 1024 * 1024)
    )
    if eng.use_mock:
        pytest.skip("WASM runtime unavailable")
    mod = eng.compile_wasm(wasmtime.wat2wasm(wat))
    assert mod is not None
    for i in range(12_000):
        out, h, t, _pre, _post = eng.execute_wasm(mod, "run", [i, 1])
        assert out == i + 1
        assert h is not None and t is not None
    assert len(eng._instance_cache) == 1


def test_wasm_engine_memory_error_strict_policy_raises(monkeypatch):
    wat = r"""
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (return (i32.add (local.get 0) (local.get 1)))
      )
      (export "run" (func $run))
    )
    """
    eng = WasmEngine(use_mock=False, runtime=WasmRuntimeConfig(fallback_policy="error"))
    if eng.use_mock:
        pytest.skip("WASM runtime unavailable in this environment")
    mod = eng.compile_wasm(wasmtime.wat2wasm(wat))
    assert mod is not None

    def _boom(_store, _module):
        raise RuntimeError("mmap failed to reserve 0x104000000 bytes")

    monkeypatch.setattr("qminiwasm.wasm_host.engine.instantiate_wasmtime_module", _boom)
    with pytest.raises(RuntimeError, match="reservation failed"):
        eng.execute_wasm(mod, "run", [1, 2])


def test_wasm_engine_memory_error_mock_policy_falls_back(monkeypatch):
    wat = r"""
    (module
      (memory 1)
      (func $run (param i32 i32) (result i32)
        (return (i32.add (local.get 0) (local.get 1)))
      )
      (export "run" (func $run))
    )
    """
    eng = WasmEngine(use_mock=False, runtime=WasmRuntimeConfig(fallback_policy="mock"))
    if eng.use_mock:
        pytest.skip("WASM runtime unavailable in this environment")
    mod = eng.compile_wasm(wasmtime.wat2wasm(wat))
    assert mod is not None

    def _boom(_store, _module):
        raise RuntimeError("Cannot allocate memory (os error 12)")

    monkeypatch.setattr("qminiwasm.wasm_host.engine.instantiate_wasmtime_module", _boom)
    out, hidden, target, _pre, _post = eng.execute_wasm(mod, "hash", [7, 11])
    assert eng.use_mock is True
    assert isinstance(out, int)
    assert hidden is not None and target is not None


def test_esi_state_recovery_hull_hashed_key_roundtrip():
    hull = ESIStateRecoveryHull()
    hidden = torch.randn(16, dtype=torch.float32)
    target = torch.randn(16, dtype=torch.float32)
    hull.store_valid_state(hidden, target)
    restored = hull.retrieve_target_state(hidden.clone())
    assert restored is not None
    assert torch.allclose(restored, target)


def test_esi_state_recovery_hull_batch_store_lookup():
    hull = ESIStateRecoveryHull()
    hs = [torch.randn(16, dtype=torch.float32) for _ in range(3)]
    ts = [torch.randn(16, dtype=torch.float32) for _ in range(3)]
    hull.store_valid_states(hs, ts)
    out = hull.retrieve_target_states([h.clone() for h in hs])
    assert len(out) == 3
    assert all(v is not None for v in out)
