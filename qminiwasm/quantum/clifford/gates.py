"""Thin Clifford gate API on :class:`~qminiwasm.quantum.clifford.tableau.StabilizerTableau`."""

from __future__ import annotations

from qminiwasm.quantum.clifford.tableau import StabilizerTableau


def apply_h(tab: StabilizerTableau, q: int) -> None:
    """Apply Hadamard on qubit ``q`` (in place)."""
    tab.apply_h(q)


def apply_s(tab: StabilizerTableau, q: int) -> None:
    """Apply S phase gate on qubit ``q`` (in place)."""
    tab.apply_s(q)


def apply_cnot(tab: StabilizerTableau, control: int, target: int) -> None:
    """Apply CNOT with given control and target (in place)."""
    tab.apply_cnot(control, target)
