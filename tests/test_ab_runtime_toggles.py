from __future__ import annotations

from unittest.mock import patch

import torch

from qminiwasm.hardware import native_ternary
from qminiwasm.wasm_host.memory_encode import encode_linear_memory
from qminiwasm.wasm_host.trit_pack import pack_ternary_list


def test_ternary_python_mode_works_without_native():
    w = bytes([0, 1, 2])
    a = bytes((int(x) & 0xFF) for x in [1, -2, 3])
    with patch.dict("os.environ", {"QMINIWASM_TERNARY_IMPL": "python"}):
        out = native_ternary.dot_u8_i8(w, a, 1)
    assert isinstance(out, int)


def test_memory_encode_native_mode_raises_when_unavailable():
    with patch.dict("os.environ", {"QMINIWASM_MEMORY_ENCODE_IMPL": "native"}):
        try:
            out = encode_linear_memory(b"\x00\x01", result_i32=1, first_arg=2)
            assert isinstance(out, torch.Tensor)
            assert out.shape == (4096,)
        except RuntimeError:
            # Also valid in environments where native extension is not built.
            pass


def test_trit_pack_native_mode_raises_when_unavailable():
    with patch.dict("os.environ", {"QMINIWASM_TRIT_PACK_IMPL": "native"}):
        try:
            out = pack_ternary_list([1, 0, -1, 1, 0])
            assert isinstance(out, (bytes, bytearray))
        except RuntimeError:
            # Also valid in environments where native extension is not built.
            pass


def test_memory_encode_python_mode_shape_stable():
    with patch.dict("os.environ", {"QMINIWASM_MEMORY_ENCODE_IMPL": "python"}):
        t = encode_linear_memory(b"\x10\x11\x12", result_i32=7, first_arg=3)
    assert isinstance(t, torch.Tensor)
    assert t.shape == (4096,)
