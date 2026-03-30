"""Tests for CHP-style stabilizer tableau (H, S, CNOT)."""

from __future__ import annotations

import numpy as np
import pytest

from qminiwasm.quantum.clifford import (
    StabilizerTableau,
    apply_cnot,
    apply_h,
    apply_s,
)


def test_h_squared_restores_computational_basis() -> None:
    tab = StabilizerTableau(1)
    assert tab.stabilizer_string(0) == "Z"
    apply_h(tab, 0)
    assert tab.stabilizer_string(0) == "X"
    apply_h(tab, 0)
    assert tab.stabilizer_string(0) == "Z"
    np.testing.assert_array_equal(tab.r % 4, 0)


def test_s_fourth_power_on_pauli_x() -> None:
    tab = StabilizerTableau(1)
    apply_h(tab, 0)
    assert tab.stabilizer_string(0) == "X"
    for _ in range(4):
        apply_s(tab, 0)
    assert tab.stabilizer_string(0) == "X"


def test_bell_state_stabilizers() -> None:
    tab = StabilizerTableau(2)
    apply_h(tab, 0)
    apply_cnot(tab, 0, 1)
    gens = {tab.stabilizer_string(0), tab.stabilizer_string(1)}
    assert gens == {"XX", "ZZ"}


def test_ghz_three_qubits() -> None:
    tab = StabilizerTableau(3)
    apply_h(tab, 0)
    apply_cnot(tab, 0, 1)
    apply_cnot(tab, 0, 2)
    gens = {tab.stabilizer_string(i) for i in range(3)}
    assert gens == {"XXX", "ZZI", "ZIZ"}


def test_cnot_invalid_same_qubit() -> None:
    tab = StabilizerTableau(2)
    with pytest.raises(ValueError, match="must differ"):
        tab.apply_cnot(0, 0)


def test_gate_functions_delegate() -> None:
    tab = StabilizerTableau(2)
    apply_h(tab, 0)
    apply_cnot(tab, 0, 1)
    assert tab.stabilizer_string(0) == "XX"
