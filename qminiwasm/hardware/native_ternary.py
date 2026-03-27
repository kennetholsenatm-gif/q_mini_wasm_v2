"""Optional C++ extension for packed ternary-digit × int8 dot products (pybind11).

Build: ``pip install pybind11`` then ``set QMINIWASM_BUILD_NATIVE=1`` (Windows) or
``export QMINIWASM_BUILD_NATIVE=1`` (Unix) and ``pip install -e .``.
"""

from __future__ import annotations

from qminiwasm.runtime_modes import resolve_impl_mode


def _native_mode() -> str:
    return resolve_impl_mode("QMINIWASM_TERNARY_IMPL", "auto")


def dot_u8_i8(weights_u8: bytes, activations_i8: bytes, offset_per_lane: int = 1) -> int:
    """``sum_i (w[i] * int8(a[i]) - offset_per_lane)`` with ``w[i] in {0,1,2}``."""
    mode = _native_mode()
    if mode == "python":
        return _dot_u8_i8_python(weights_u8, activations_i8, offset_per_lane)
    try:
        from qminiwasm._native_ternary import dot_u8_i8 as _ext

        return int(_ext(weights_u8, activations_i8, int(offset_per_lane)))
    except ImportError:
        if mode == "native":
            raise RuntimeError("QMINIWASM_TERNARY_IMPL=native but native extension unavailable")
        return _dot_u8_i8_python(weights_u8, activations_i8, offset_per_lane)


def is_native_available() -> bool:
    if _native_mode() == "python":
        return False
    try:
        from qminiwasm._native_ternary import abi_version  # noqa: F401

        return True
    except ImportError:
        return False


def _dot_u8_i8_python(weights_u8: bytes, activations_i8: bytes, offset_per_lane: int = 1) -> int:
    n = min(len(weights_u8), len(activations_i8))
    acc = 0
    for i in range(n):
        wi = weights_u8[i]
        ai = int.from_bytes([activations_i8[i]], "little", signed=True)
        acc += int(wi) * ai - int(offset_per_lane)
    return int(acc)
