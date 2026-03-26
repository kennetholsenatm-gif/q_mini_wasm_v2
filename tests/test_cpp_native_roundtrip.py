"""Round-trip tests for optional qminiwasm_cpp_native (CMake pybind target)."""

import pytest


def test_pack_unpack_matches_python_trit_pack():
    cpp_native = pytest.importorskip("qminiwasm_cpp_native")
    from qminiwasm.wasm_host import trit_pack

    weights = [-1, 0, 1, -1, 0, 1, 1, -1, 0, 1, -1, 0, 1]
    py_packed = trit_pack.pack_ternary_list(weights)
    cpp_packed = cpp_native.pack_ternary_msb(weights)
    assert list(cpp_packed) == list(py_packed)
    out = cpp_native.unpack_ternary_msb(cpp_packed, len(weights))
    assert list(out) == weights


def test_matvec_best_runs():
    cpp_native = pytest.importorskip("qminiwasm_cpp_native")
    from qminiwasm.wasm_host import trit_pack

    in_f = 10
    rows = 2
    w = [1] * (in_f * rows)
    packed = bytearray()
    for r in range(rows):
        packed.extend(trit_pack.pack_ternary_list(w[r * in_f : (r + 1) * in_f]))
    a = [1] * in_f
    y = cpp_native.matvec_best(list(packed), rows, in_f, a)
    assert len(y) == rows
    assert all(isinstance(v, int) for v in y)
