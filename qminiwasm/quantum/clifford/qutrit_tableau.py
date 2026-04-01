"""Qutrit stabilizer tableau over GF(3) for Clifford simulation.

This module implements the Ternary Symplectic Pauli Frame (TSPF) described in the
Qutrit Clifford AI Edge Applications research. Operations are tracked via symplectic
matrices over GF(3) with phase tracking mod 3 (roots of unity ω = e^(2πi/3)).

Key differences from qubit (F2) tableau:
- Arithmetic is mod 3 instead of mod 2
- Phase is tracked mod 3 (values 0, 1, 2 corresponding to ω^0, ω^1, ω^2)
- X and Z operators act on qutrits: X|a⟩ = |a+1 mod 3⟩, Z|a⟩ = ω^a|a⟩
"""

from __future__ import annotations

from typing import List, Tuple
import numpy as np


class QutritStabilizerTableau:
    """Stabilizer generators for a pure Clifford state on n qutrits.

    Uses symplectic representation over GF(3): each stabilizer generator is
    represented by X and Z components (integers 0, 1, 2) and a phase r (0, 1, 2).

    The Pauli group over GF(3) has operators:
    - X^a Z^b ω^c where a, b ∈ {0, 1, 2}, c ∈ {0, 1, 2}
    - X|k⟩ = |k+1 mod 3⟩ (shift operator)
    - Z|k⟩ = ω^k|k⟩ (phase operator, ω = e^(2πi/3))
    """

    __slots__ = ("n", "x", "z", "r")

    def __init__(self, n_qutrits: int) -> None:
        """Initialize n-qutrit stabilizer state in |0⟩^n.

        Args:
            n_qutrits: Number of qutrits (must be >= 1)
        """
        if n_qutrits < 1:
            raise ValueError("n_qutrits must be >= 1")
        self.n = n_qutrits
        # X and Z components for each generator (rows) and each qutrit (columns)
        # Values are in {0, 1, 2} representing powers of X and Z
        self.x = np.zeros((n_qutrits, n_qutrits), dtype=np.int32)
        self.z = np.zeros((n_qutrits, n_qutrits), dtype=np.int32)
        # Phase r: represents ω^r where ω = e^(2πi/3)
        self.r = np.zeros(n_qutrits, dtype=np.int32)
        # Initialize to |0⟩^n: stabilizers are Z_i for each qutrit i
        for i in range(n_qutrits):
            self.z[i, i] = 1

    def copy(self) -> QutritStabilizerTableau:
        """Create a deep copy of this tableau."""
        t = QutritStabilizerTableau.__new__(QutritStabilizerTableau)
        t.n = self.n
        t.x = self.x.copy()
        t.z = self.z.copy()
        t.r = self.r.copy()
        return t

    def apply_h3(self, q: int) -> None:
        """Apply qutrit Hadamard gate H₃ on qutrit q (in place).

        H₃ maps:
        - |0⟩ → (1/√3)(|0⟩ + |1⟩ + |2⟩)
        - |1⟩ → (1/√3)(|0⟩ + ω|1⟩ + ω²|2⟩)
        - |2⟩ → (1/√3)(|0⟩ + ω²|1⟩ + ω|2⟩)

        In symplectic representation over GF(3):
        - X → Z (with phase adjustment)
        - Z → -X (which is 2X mod 3)
        """
        self._check_qutrit(q)
        x_col = self.x[:, q].copy()
        z_col = self.z[:, q].copy()
        # Phase update: r += 2*x*z (mod 3) because H₃: XZ → ZX (commutation)
        self.r = (self.r + 2 * x_col * z_col) % 3
        # Swap X and Z with negation: X → 2Z, Z → 2X (mod 3)
        self.x[:, q] = (2 * z_col) % 3
        self.z[:, q] = (2 * x_col) % 3

    def apply_s3(self, q: int) -> None:
        """Apply qutrit Phase gate S₃ on qutrit q (in place).

        S₃ = diag(1, ω, ω²)
        - |0⟩ → |0⟩
        - |1⟩ → ω|1⟩
        - |2⟩ → ω²|2⟩

        In symplectic representation:
        - Z → Z (unchanged)
        - X → X + Z (because S₃† X S₃ = ω X Z)
        """
        self._check_qutrit(q)
        x_col = self.x[:, q].copy()
        z_col = self.z[:, q].copy()
        # Phase update: r += x*z (mod 3)
        self.r = (self.r + x_col * z_col) % 3
        # X → X + Z (mod 3)
        self.x[:, q] = (x_col + z_col) % 3

    def apply_cz3(self, control: int, target: int) -> None:
        """Apply qutrit Controlled-Z gate CZ₃ (in place).

        CZ₃|a,b⟩ = ω^(a*b)|a,b⟩

        In symplectic representation:
        - X_c → X_c + Z_t
        - X_t → X_t + Z_c
        - Z_c → Z_c
        - Z_t → Z_t
        """
        if control == target:
            raise ValueError("control and target must differ")
        self._check_qutrit(control)
        self._check_qutrit(target)

        xc = self.x[:, control].copy()
        zc = self.z[:, control].copy()
        xt = self.x[:, target].copy()
        zt = self.z[:, target].copy()

        # Phase update: r += 2 * x_c * z_t (mod 3)
        self.r = (self.r + 2 * xc * zt) % 3

        # X_c → X_c + Z_t (mod 3)
        self.x[:, control] = (xc + zt) % 3
        # X_t → X_t + Z_c (mod 3)
        self.x[:, target] = (xt + zc) % 3

    def apply_x3(self, q: int, power: int = 1) -> None:
        """Apply X₃^power on qutrit q (in place).

        X₃|k⟩ = |k+1 mod 3⟩ (shift operator)
        """
        self._check_qutrit(q)
        power = power % 3
        if power == 0:
            return
        # X^power: adds power * Z to phase when applied to stabilizer
        # For tracking: just update phase based on Z component
        z_col = self.z[:, q].copy()
        self.r = (self.r + power * z_col) % 3

    def apply_z3(self, q: int, power: int = 1) -> None:
        """Apply Z₃^power on qutrit q (in place).

        Z₃|k⟩ = ω^k|k⟩ (phase operator)
        """
        self._check_qutrit(q)
        power = power % 3
        if power == 0:
            return
        # Z^power: adds power * X to phase when applied to stabilizer
        x_col = self.x[:, q].copy()
        self.r = (self.r + power * x_col) % 3

    def measure(self, q: int) -> int:
        """Measure qutrit q in computational basis.

        Returns:
            Measurement outcome in {0, 1, 2}, or -1 if random (stabilizer state).
        """
        self._check_qutrit(q)
        # Check if any stabilizer has X component on qutrit q
        # If so, measurement is deterministic; otherwise random
        x_col = self.x[:, q]
        if np.all(x_col == 0):
            # No X component: measurement is random
            return -1
        # Find first stabilizer with non-zero X on this qutrit
        idx = np.nonzero(x_col)[0][0]
        # Deterministic outcome based on phase
        return int(self.r[idx]) % 3

    def _check_qutrit(self, q: int) -> None:
        """Validate qutrit index."""
        if q < 0 or q >= self.n:
            raise IndexError(f"qutrit index {q} out of range for n={self.n}")

    def stabilizer_string(self, row: int) -> str:
        """Human-readable Pauli string for stabilizer generator 'row'.

        Uses notation: I (identity), X (shift), Z (phase), Y (XZ).
        Superscripts indicate power (omitted if 1).
        """
        if row < 0 or row >= self.n:
            raise IndexError(f"row {row} out of range")
        parts: list[str] = []
        for j in range(self.n):
            xb = int(self.x[row, j]) % 3
            zb = int(self.z[row, j]) % 3
            if xb == 0 and zb == 0:
                parts.append("I")
            elif xb == 1 and zb == 0:
                parts.append("X")
            elif xb == 2 and zb == 0:
                parts.append("X²")
            elif xb == 0 and zb == 1:
                parts.append("Z")
            elif xb == 0 and zb == 2:
                parts.append("Z²")
            elif xb == 1 and zb == 1:
                parts.append("Y")
            elif xb == 1 and zb == 2:
                parts.append("XZ²")
            elif xb == 2 and zb == 1:
                parts.append("X²Z")
            else:
                parts.append("X²Z²")
        phase_str = ""
        r = int(self.r[row]) % 3
        if r == 1:
            phase_str = "ω·"
        elif r == 2:
            phase_str = "ω²·"
        return phase_str + " ".join(parts)

    def __repr__(self) -> str:
        lines = []
        for i in range(self.n):
            lines.append(self.stabilizer_string(i))
        return f"QutritStabilizerTableau(n={self.n})\n" + "\n".join(lines)