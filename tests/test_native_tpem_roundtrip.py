"""TPEM interchange v2: Python bundle I/O and optional C++ round-trip."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

import pytest
import torch

pytest.importorskip("safetensors")


def test_interchange_v2_python_roundtrip_io_and_multi_block(tmp_path: Path) -> None:
    """Save/load v2 with io_d_model != d_model and num_ternary_blocks > 1."""
    from qminiwasm.model import QMiniWASM
    from qminiwasm.tpem.trainable_tpem import (
        load_trainable_tpem_into_model,
        peek_trainable_tpem_geometry,
        save_trainable_tpem_interchange_v2,
    )

    torch.manual_seed(2027)
    m = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=64,
        io_d_model=32,
        num_ternary_blocks=2,
    )
    path = tmp_path / "bundle_io2.pt"
    save_trainable_tpem_interchange_v2(path, m, meta={"geom": "io2"})
    geom = peek_trainable_tpem_geometry(path)
    assert geom.get("d_model") == 64
    assert geom.get("io_d_model") == 32
    assert geom.get("num_ternary_blocks") == 2
    w_blocks = [b.weight.detach().clone() for b in m.ternary_blocks]
    stem_w = m.input_stem.weight.detach().clone()
    head_w = m.output_head.weight.detach().clone()

    m2 = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=64,
        io_d_model=32,
        num_ternary_blocks=2,
    )
    meta = load_trainable_tpem_into_model(m2, path)
    assert meta.get("geom") == "io2"
    for i, w0 in enumerate(w_blocks):
        assert torch.allclose(w0, m2.ternary_blocks[i].weight.detach(), atol=1e-6, rtol=1e-5)
    assert torch.allclose(stem_w, m2.input_stem.weight.detach(), atol=1e-6, rtol=1e-5)
    assert torch.allclose(head_w, m2.output_head.weight.detach(), atol=1e-6, rtol=1e-5)


def test_interchange_v2_python_roundtrip(tmp_path: Path) -> None:
    from qminiwasm.model import QMiniWASM
    from qminiwasm.tpem.trainable_tpem import (
        load_trainable_tpem_into_model,
        save_trainable_tpem_interchange_v2,
    )

    torch.manual_seed(2026)
    m = QMiniWASM(device=torch.device("cpu"), qaoa_execution_mode="pennylane")
    path = tmp_path / "bundle.pt"
    save_trainable_tpem_interchange_v2(path, m, meta={"test": True})
    w0 = m.ternary_expert.weight.detach().clone()

    m2 = QMiniWASM(device=torch.device("cpu"), qaoa_execution_mode="pennylane")
    meta = load_trainable_tpem_into_model(m2, path)
    assert meta.get("test") is True
    w1 = m2.ternary_expert.weight.detach()
    assert torch.allclose(w0, w1, atol=1e-6, rtol=1e-5)


def _roundtrip_exe() -> str:
    return os.environ.get("QMINIWASM_TPEM_ROUNDTRIP_EXE", "").strip()


@pytest.mark.skipif(
    not _roundtrip_exe(),
    reason="Set QMINIWASM_TPEM_ROUNDTRIP_EXE to the path of qminiwasm_tpem_roundtrip",
)
def test_python_cpp_python_tpem_io_multi_zero_steps(tmp_path: Path) -> None:
    """C++ load/save preserves weights for io != d_model and N > 1 (no train steps)."""
    from qminiwasm.model import QMiniWASM
    from qminiwasm.tpem.trainable_tpem import (
        load_trainable_tpem_into_model,
        save_trainable_tpem_interchange_v2,
    )

    torch.manual_seed(11)
    m = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=48,
        io_d_model=24,
        num_ternary_blocks=2,
    )
    inp = tmp_path / "in_io.pt"
    out = tmp_path / "out_io.pt"
    save_trainable_tpem_interchange_v2(inp, m)
    w0 = [b.weight.detach().clone() for b in m.ternary_blocks]
    stem0 = m.input_stem.weight.detach().clone()
    head0 = m.output_head.weight.detach().clone()

    exe = _roundtrip_exe()
    subprocess.run(
        [exe, str(inp), str(out), "99", "0", "0.001"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )

    m2 = QMiniWASM(
        device=torch.device("cpu"),
        qaoa_execution_mode="pennylane",
        d_model=48,
        io_d_model=24,
        num_ternary_blocks=2,
    )
    load_trainable_tpem_into_model(m2, out)
    for i, w in enumerate(w0):
        assert torch.allclose(w, m2.ternary_blocks[i].weight.detach(), atol=1e-5, rtol=1e-4)
    assert torch.allclose(stem0, m2.input_stem.weight.detach(), atol=1e-5, rtol=1e-4)
    assert torch.allclose(head0, m2.output_head.weight.detach(), atol=1e-5, rtol=1e-4)


@pytest.mark.skipif(
    not _roundtrip_exe(),
    reason="Set QMINIWASM_TPEM_ROUNDTRIP_EXE to the path of qminiwasm_tpem_roundtrip",
)
def test_python_cpp_python_tpem_zero_steps(tmp_path: Path) -> None:
    from qminiwasm.model import QMiniWASM
    from qminiwasm.tpem.trainable_tpem import (
        load_trainable_tpem_into_model,
        save_trainable_tpem_interchange_v2,
    )

    torch.manual_seed(42)
    m = QMiniWASM(device=torch.device("cpu"), qaoa_execution_mode="pennylane")
    inp = tmp_path / "in.pt"
    out = tmp_path / "out.pt"
    save_trainable_tpem_interchange_v2(inp, m)
    w0 = m.ternary_expert.weight.detach().clone()

    exe = _roundtrip_exe()
    subprocess.run(
        [exe, str(inp), str(out), "42", "0", "0.001"],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )

    m2 = QMiniWASM(device=torch.device("cpu"), qaoa_execution_mode="pennylane")
    load_trainable_tpem_into_model(m2, out)
    w1 = m2.ternary_expert.weight.detach()
    assert torch.allclose(w0, w1, atol=1e-5, rtol=1e-4)
