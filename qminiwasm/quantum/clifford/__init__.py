"""Stabilizer / Clifford simulation stub (Pauli frame over F2; ternary packing is WASM-side)."""

from qminiwasm.quantum.clifford.gates import apply_cnot, apply_h, apply_s
from qminiwasm.quantum.clifford.tableau import StabilizerTableau

__all__ = [
    "StabilizerTableau",
    "apply_cnot",
    "apply_h",
    "apply_s",
]
