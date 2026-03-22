"""Optional C++ extension for packed ternary-digit × int8 dot products (pybind11).

Build: ``pip install pybind11`` then ``set QMINIWASM_BUILD_NATIVE=1`` (Windows) or
``export QMINIWASM_BUILD_NATIVE=1`` (Unix) and ``pip install -e .``.
"""

from __future__ import annotations


def dot_u8_i8(weights_u8: bytes, activations_i8: bytes, offset_per_lane: int = 1) -> int:
    """``sum_i (w[i] * int8(a[i]) - offset_per_lane)`` with ``w[i] in {0,1,2}``."""
    try:
        from qminiwasm._native_ternary import dot_u8_i8 as _ext

        return int(_ext(weights_u8, activations_i8, int(offset_per_lane)))
    except ImportError:
        return _dot_u8_i8_python(weights_u8, activations_i8, offset_per_lane)


def is_native_available() -> bool:
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
