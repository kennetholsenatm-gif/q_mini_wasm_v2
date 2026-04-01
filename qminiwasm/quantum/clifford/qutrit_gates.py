"""Qutrit Clifford gate API on QutritStabilizerTableau.

This module provides a thin wrapper around QutritStabilizerTableau for applying
qutrit Clifford gates. The gates are:
- H₃: Qutrit Hadamard
- S₃: Qutrit Phase gate
- CZ₃: Qutrit Controlled-Z
- X₃: Qutrit shift (Pauli X)
- Z₃: Qutrit phase (Pauli Z)

These gates form the generating set for the qutrit Clifford group and can be
simulated efficiently via the Ternary Symplectic Pauli Frame (TSPF).
"""

from __future__ import annotations

from typing import List, Tuple
import numpy as np

from qminiwasm.quantum.clifford.qutrit_tableau import QutritStabilizerTableau


# Matrix representations for reference/validation
# ω = e^(2πi/3) = -1/2 + i√3/2
OMEGA = np.exp(2j * np.pi / 3)


def h3_matrix() -> np.ndarray:
    """Return the 3×3 unitary matrix for the qutrit Hadamard gate H₃.

    H₃ = (1/√3) * [[1, 1, 1],
                    [1, ω, ω²],
                    [1, ω², ω]]
    """
    return (1 / np.sqrt(3)) * np.array([
        [1, 1, 1],
        [1, OMEGA, OMEGA**2],
        [1, OMEGA**2, OMEGA]
    ], dtype=np.complex128)


def s3_matrix() -> np.ndarray:
    """Return the 3×3 unitary matrix for the qutrit Phase gate S₃.

    S₃ = diag(1, ω, ω²)
    """
    return np.diag([1, OMEGA, OMEGA**2]).astype(np.complex128)


def cz3_matrix() -> np.ndarray:
    """Return the 9×9 unitary matrix for the two-qutrit CZ₃ gate.

    CZ₃|a,b⟩ = ω^(a*b)|a,b⟩
    """
    mat = np.eye(9, dtype=np.complex128)
    for a in range(3):
        for b in range(3):
            idx = 3 * a + b
            mat[idx, idx] = OMEGA**(a * b)
    return mat


def x3_matrix() -> np.ndarray:
    """Return the 3×3 unitary matrix for the qutrit shift gate X₃.

    X₃|k⟩ = |k+1 mod 3⟩
    """
    return np.array([
        [0, 0, 1],
        [1, 0, 0],
        [0, 1, 0]
    ], dtype=np.complex128)


def z3_matrix() -> np.ndarray:
    """Return the 3×3 unitary matrix for the qutrit phase gate Z₃.

    Z₃|k⟩ = ω^k|k⟩
    """
    return np.diag([1, OMEGA, OMEGA**2]).astype(np.complex128)


# Gate application functions


def apply_h3(tab: QutritStabilizerTableau, q: int) -> None:
    """Apply qutrit Hadamard gate H₃ on qutrit q (in place).

    Args:
        tab: QutritStabilizerTableau to modify
        q: Qutrit index
    """
    tab.apply_h3(q)


def apply_s3(tab: QutritStabilizerTableau, q: int) -> None:
    """Apply qutrit Phase gate S₃ on qutrit q (in place).

    Args:
        tab: QutritStabilizerTableau to modify
        q: Qutrit index
    """
    tab.apply_s3(q)


def apply_cz3(tab: QutritStabilizerTableau, control: int, target: int) -> None:
    """Apply qutrit Controlled-Z gate CZ₃ (in place).

    Args:
        tab: QutritStabilizerTableau to modify
        control: Control qutrit index
        target: Target qutrit index
    """
    tab.apply_cz3(control, target)


def apply_x3(tab: QutritStabilizerTableau, q: int, power: int = 1) -> None:
    """Apply X₃^power on qutrit q (in place).

    Args:
        tab: QutritStabilizerTableau to modify
        q: Qutrit index
        power: Power of X₃ (default 1)
    """
    tab.apply_x3(q, power)


def apply_z3(tab: QutritStabilizerTableau, q: int, power: int = 1) -> None:
    """Apply Z₃^power on qutrit q (in place).

    Args:
        tab: QutritStabilizerTableau to modify
        q: Qutrit index
        power: Power of Z₃ (default 1)
    """
    tab.apply_z3(q, power)


def apply_gate_sequence(
    tab: QutritStabilizerTableau,
    gates: List[Tuple[str, ...]]
) -> None:
    """Apply a sequence of gates to the tableau.

    Args:
        tab: QutritStabilizerTableau to modify
        gates: List of gate specifications. Each tuple is:
            - ("H", q) for Hadamard
            - ("S", q) for Phase
            - ("CZ", c, t) for Controlled-Z
            - ("X", q) or ("X", q, power) for shift
            - ("Z", q) or ("Z", q, power) for phase
    """
    for gate in gates:
        name = gate[0].upper()
        if name == "H":
            apply_h3(tab, gate[1])
        elif name == "S":
            apply_s3(tab, gate[1])
        elif name == "CZ":
            apply_cz3(tab, gate[1], gate[2])
        elif name == "X":
            power = gate[2] if len(gate) > 2 else 1
            apply_x3(tab, gate[1], power)
        elif name == "Z":
            power = gate[2] if len(gate) > 2 else 1
            apply_z3(tab, gate[1], power)
        else:
            raise ValueError(f"Unknown gate: {name}")


def random_clifford_circuit(
    n_qutrits: int,
    depth: int,
    seed: int = 42
) -> QutritStabilizerTableau:
    """Generate a random qutrit Clifford circuit.

    Args:
        n_qutrits: Number of qutrits
        depth: Circuit depth (number of layers)
        seed: Random seed for reproducibility

    Returns:
        QutritStabilizerTableau after applying random Clifford gates
    """
    rng = np.random.default_rng(seed)
    tab = QutritStabilizerTableau(n_qutrits)

    for _ in range(depth):
        # Random single-qutrit gates
        for q in range(n_qutrits):
            if rng.random() < 0.3:
                apply_h3(tab, q)
            if rng.random() < 0.3:
                apply_s3(tab, q)

        # Random two-qutrit gates
        for _ in range(n_qutrits // 2):
            c = rng.integers(0, n_qutrits)
            t = rng.integers(0, n_qutrits)
            if c != t:
                apply_cz3(tab, c, t)

    return tab