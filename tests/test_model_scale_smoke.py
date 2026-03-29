"""Smoke tests for configurable QMiniWASM geometry (production scaling path)."""

from __future__ import annotations

import torch

from qminiwasm.model import QMiniWASM


def test_qminiwasm_stem_two_blocks_forward():
    m = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=512,
        num_ternary_blocks=2,
        io_d_model=128,
        use_hybrid_adapter=False,
        use_cascade_router=False,
    )
    x = torch.randn(4, 128)
    y = m.hybrid_inference(x)
    assert y.shape == (4, 128)


def test_trainable_tpem_payload_multi_block_roundtrip(tmp_path):
    from qminiwasm.tpem.trainable_tpem import (
        build_trainable_tpem_payload,
        load_trainable_tpem_into_model,
    )

    m = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=64,
        num_ternary_blocks=2,
        io_d_model=64,
    )
    p = tmp_path / "t.pt"
    payload = build_trainable_tpem_payload(m, meta={"test": True})
    torch.save(payload, p)
    m2 = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=64,
        num_ternary_blocks=2,
        io_d_model=64,
    )
    meta = load_trainable_tpem_into_model(m2, p, map_location="cpu")
    assert meta.get("test") is True
    x = torch.randn(2, 64)
    assert torch.allclose(m.hybrid_inference(x), m2.hybrid_inference(x), atol=1e-5, rtol=1e-4)


def test_peek_trainable_tpem_geometry_pickle(tmp_path):
    from qminiwasm.tpem.trainable_tpem import (
        build_trainable_tpem_payload,
        peek_trainable_tpem_geometry,
    )

    m = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=256,
        num_ternary_blocks=3,
        io_d_model=128,
    )
    p = tmp_path / "g.pt"
    torch.save(build_trainable_tpem_payload(m), p)
    g = peek_trainable_tpem_geometry(p)
    assert g["d_model"] == 256
    assert g["num_ternary_blocks"] == 3
    assert g["io_d_model"] == 128
