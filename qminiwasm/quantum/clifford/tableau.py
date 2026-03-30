"""Small-n stabilizer tableau (Clifford / Pauli-frame simulation).

Uses the standard symplectic representation over **F2** (X/Z bits per qubit per
generator, phase mod 4), CHP-style update rules for H, S, and CNOT. This matches
the engineering story in the Ternary Clifford Optimization paper for stabilizer
tracking; **ternary / 5-trit packing** is a separate layout concern for WASM
serialization (see :mod:`qminiwasm.wasm_host.trit_pack`).
"""

from __future__ import annotations

import numpy as np


class StabilizerTableau:
    """Stabilizer generators for a pure Clifford state (no measurement), n qubits.

    Rows index stabilizer generators; columns index qubits. Each row is a Pauli
    string: Y is encoded as X*Z with the appropriate phase in ``r``.
    """

    __slots__ = ("n", "x", "z", "r")

    def __init__(self, n_qubits: int) -> None:
        if n_qubits < 1:
            raise ValueError("n_qubits must be >= 1")
        self.n = n_qubits
        self.x = np.zeros((n_qubits, n_qubits), dtype=np.uint8)
        self.z = np.zeros((n_qubits, n_qubits), dtype=np.uint8)
        self.r = np.zeros(n_qubits, dtype=np.int32)
        for i in range(n_qubits):
            self.z[i, i] = 1

    def copy(self) -> StabilizerTableau:
        t = StabilizerTableau.__new__(StabilizerTableau)
        t.n = self.n
        t.x = self.x.copy()
        t.z = self.z.copy()
        t.r = self.r.copy()
        return t

    def apply_h(self, q: int) -> None:
        self._check_qubit(q)
        x_col = self.x[:, q].astype(np.int32)
        z_col = self.z[:, q].astype(np.int32)
        self.r = (self.r + 2 * (x_col * z_col)) % 4
        self.x[:, q] = z_col.astype(np.uint8)
        self.z[:, q] = x_col.astype(np.uint8)

    def apply_s(self, q: int) -> None:
        self._check_qubit(q)
        x_col = self.x[:, q].astype(np.int32)
        z_col = self.z[:, q].astype(np.int32)
        self.r = (self.r + 2 * (x_col * z_col)) % 4
        self.z[:, q] = ((z_col + x_col) % 2).astype(np.uint8)

    def apply_cnot(self, control: int, target: int) -> None:
        if control == target:
            raise ValueError("control and target must differ")
        self._check_qubit(control)
        self._check_qubit(target)
        xc = self.x[:, control].astype(np.int32)
        zc = self.z[:, control].astype(np.int32)
        xt = self.x[:, target].astype(np.int32)
        zt = self.z[:, target].astype(np.int32)
        self.r = (self.r + 2 * (xc * zt * (xt ^ zc ^ 1))) % 4
        self.x[:, target] = ((xt + xc) % 2).astype(np.uint8)
        self.z[:, control] = ((zc + zt) % 2).astype(np.uint8)

    def _check_qubit(self, q: int) -> None:
        if q < 0 or q >= self.n:
            raise IndexError(f"qubit index {q} out of range for n={self.n}")

    def stabilizer_string(self, row: int) -> str:
        """Human-readable Pauli string for stabilizer generator ``row`` (phase ignored)."""
        if row < 0 or row >= self.n:
            raise IndexError(f"row {row} out of range")
        parts: list[str] = []
        for j in range(self.n):
            xb = int(self.x[row, j])
            zb = int(self.z[row, j])
            if xb == 0 and zb == 0:
                parts.append("I")
            elif xb == 1 and zb == 0:
                parts.append("X")
            elif xb == 0 and zb == 1:
                parts.append("Z")
            else:
                parts.append("Y")
        return "".join(parts)
