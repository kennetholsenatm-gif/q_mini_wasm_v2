"""Qutrit Clifford simulation for ternary neural networks.

This module provides quantum-inspired Clifford operations for 1.58-bit ternary
neural networks on extreme-edge devices. All operations stay within the
Gottesman-Knill simulability bounds (polynomial time via TSPF).

Modules:
- tableau: Qubit stabilizer tableau over F2
- gates: Qubit Clifford gates (H, S, CNOT)
- qutrit_tableau: Qutrit stabilizer tableau over GF(3)
- qutrit_gates: Qutrit Clifford gates (H₃, S₃, CZ₃)
- error_correction: [[5,1,3]] and [[3,1,2]] qutrit stabilizer codes
- graph_states: Qutrit graph states for feature projection
- scrambling: Clifford scrambling for ZTEE
- attention: Bell difference sampling for attention approximation
"""

from qminiwasm.quantum.clifford.gates import apply_cnot, apply_h, apply_s
from qminiwasm.quantum.clifford.tableau import StabilizerTableau
from qminiwasm.quantum.clifford.qutrit_tableau import QutritStabilizerTableau
from qminiwasm.quantum.clifford.qutrit_gates import (
    apply_h3,
    apply_s3,
    apply_cz3,
    apply_x3,
    apply_z3,
    h3_matrix,
    s3_matrix,
    cz3_matrix,
    x3_matrix,
    z3_matrix,
)
from qminiwasm.quantum.clifford.error_correction import (
    Code513,
    Code312,
    encode_513,
    encode_312,
    correct_single_error_513,
    detect_error_312,
)
from qminiwasm.quantum.clifford.graph_states import (
    QutritGraphState,
    cyclic_graph,
    complete_graph,
    star_graph,
    random_graph,
    graph_state_projection,
)
from qminiwasm.quantum.clifford.scrambling import (
    CliffordScrambling,
    create_scrambling,
    scramble_weights,
    scramble_and_decrypt,
)
from qminiwasm.quantum.clifford.attention import (
    QutritBellAttention,
    qutrit_attention,
    softmax_approximation,
    top_k_attention,
)

__all__ = [
    # Qubit modules
    "StabilizerTableau",
    "apply_cnot",
    "apply_h",
    "apply_s",
    # Qutrit tableau
    "QutritStabilizerTableau",
    # Qutrit gates
    "apply_h3",
    "apply_s3",
    "apply_cz3",
    "apply_x3",
    "apply_z3",
    "h3_matrix",
    "s3_matrix",
    "cz3_matrix",
    "x3_matrix",
    "z3_matrix",
    # Error correction
    "Code513",
    "Code312",
    "encode_513",
    "encode_312",
    "correct_single_error_513",
    "detect_error_312",
    # Graph states
    "QutritGraphState",
    "cyclic_graph",
    "complete_graph",
    "star_graph",
    "random_graph",
    "graph_state_projection",
    # Scrambling
    "CliffordScrambling",
    "create_scrambling",
    "scramble_weights",
    "scramble_and_decrypt",
    # Attention
    "QutritBellAttention",
    "qutrit_attention",
    "softmax_approximation",
    "top_k_attention",
]
