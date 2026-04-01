"""Clifford scrambling for Zero-Trust Execution Environment (ZTEE).

This module implements Clifford unitary conjugation for secure edge inference.
Weights are pre-scrambled on a secure server; the edge device operates entirely
in the encrypted domain. Only final output logits are unscrambled.

The scrambling uses the homomorphic properties of the Clifford group:
U_C · (X^a Z^b) · U_C† = X^a' Z^b' (another Pauli operator)

This preserves the routing logic while obscuring the ternary coordinates.
"""

from __future__ import annotations

from typing import List, Optional, Tuple
import numpy as np

from qminiwasm.quantum.clifford.qutrit_tableau import QutritStabilizerTableau
from qminiwasm.quantum.clifford.qutrit_gates import (
    apply_h3, apply_s3, apply_cz3, apply_x3, apply_z3
)


class CliffordScrambling:
    """Clifford unitary for weight/data encryption.

    The scrambling unitary U_C is a deterministically random sequence of
    H₃, S₃, and CZ₃ gates generated from a secure seed.
    """

    def __init__(self, n_qutrits: int, seed: int = 42, depth: int = 3):
        """Initialize scrambling unitary.

        Args:
            n_qutrits: Number of qutrits to scramble
            seed: Secure random seed (shared between server and edge)
            depth: Circuit depth for scrambling complexity
        """
        self.n = n_qutrits
        self.seed = seed
        self.depth = depth
        self.gates: List[Tuple[str, ...]] = []
        self._generate_gates()

    def _generate_gates(self) -> None:
        """Generate deterministic random gate sequence."""
        rng = np.random.default_rng(self.seed)

        for _ in range(self.depth):
            # Single-qutrit gates
            for q in range(self.n):
                if rng.random() < 0.4:
                    self.gates.append(("H", q))
                if rng.random() < 0.4:
                    self.gates.append(("S", q))

            # Two-qutrit gates
            for _ in range(self.n // 2):
                c = rng.integers(0, self.n)
                t = rng.integers(0, self.n)
                if c != t:
                    self.gates.append(("CZ", c, t))

    def encrypt_weights(
        self,
        weights: List[int]
    ) -> QutritStabilizerTableau:
        """Encrypt ternary weights via Clifford conjugation.

        Args:
            weights: Ternary weights in {-1, 0, 1}

        Returns:
            Scrambled QutritStabilizerTableau
        """
        if len(weights) != self.n:
            raise ValueError(f"Expected {self.n} weights, got {len(weights)}")

        # Initialize tableau
        tab = QutritStabilizerTableau(self.n)

        # Apply scrambling unitary U_C
        self._apply_gates(tab)

        # Encode weights into the scrambled basis
        for i, w in enumerate(weights):
            gf3_val = (w + 3) % 3
            if gf3_val == 1:
                apply_x3(tab, i)
            elif gf3_val == 2:
                apply_x3(tab, i, power=2)

        return tab

    def encrypt_data(
        self,
        data: List[int]
    ) -> QutritStabilizerTableau:
        """Encrypt input data via Clifford conjugation.

        Args:
            data: Ternary data in {-1, 0, 1}

        Returns:
            Encrypted QutritStabilizerTableau
        """
        if len(data) != self.n:
            raise ValueError(f"Expected {self.n} data values, got {len(data)}")

        # Initialize tableau
        tab = QutritStabilizerTableau(self.n)

        # Encode data first
        for i, d in enumerate(data):
            gf3_val = (d + 3) % 3
            if gf3_val == 1:
                apply_x3(tab, i)
            elif gf3_val == 2:
                apply_x3(tab, i, power=2)

        # Apply scrambling unitary U_C
        self._apply_gates(tab)

        return tab

    def decrypt_output(
        self,
        tab: QutritStabilizerTableau
    ) -> List[int]:
        """Decrypt output logits via inverse Clifford conjugation.

        Args:
            tab: Scrambled QutritStabilizerTableau

        Returns:
            Decrypted ternary values in {0, 1, 2}
        """
        # Apply inverse scrambling U_C†
        self._apply_inverse_gates(tab)

        # Extract decrypted values
        output = []
        for i in range(self.n):
            # Measure stabilizer phase
            r = int(tab.r[i]) % 3
            output.append(r)

        return output

    def _apply_gates(self, tab: QutritStabilizerTableau) -> None:
        """Apply scrambling gates to tableau."""
        for gate in self.gates:
            name = gate[0]
            if name == "H":
                apply_h3(tab, gate[1])
            elif name == "S":
                apply_s3(tab, gate[1])
            elif name == "CZ":
                apply_cz3(tab, gate[1], gate[2])

    def _apply_inverse_gates(self, tab: QutritStabilizerTableau) -> None:
        """Apply inverse scrambling gates to tableau.

        Inverse of H₃ is H₃† = H₃ (self-inverse)
        Inverse of S₃ is S₃† = S₃²
        Inverse of CZ₃ is CZ₃ (self-inverse)
        """
        # Apply gates in reverse order
        for gate in reversed(self.gates):
            name = gate[0]
            if name == "H":
                apply_h3(tab, gate[1])
            elif name == "S":
                # S₃† = S₃²
                apply_s3(tab, gate[1])
                apply_s3(tab, gate[1])
            elif name == "CZ":
                apply_cz3(tab, gate[1], gate[2])

    def get_gate_count(self) -> int:
        """Get the total number of gates in the scrambling circuit."""
        return len(self.gates)

    def __repr__(self) -> str:
        return (
            f"CliffordScrambling(n={self.n}, seed={self.seed}, "
            f"depth={self.depth}, gates={len(self.gates)})"
        )


def create_scrambling(
    n_qutrits: int,
    seed: int = 42,
    depth: int = 3
) -> CliffordScrambling:
    """Create a Clifford scrambling unitary.

    Args:
        n_qutrits: Number of qutrits
        seed: Secure random seed
        depth: Circuit depth

    Returns:
        CliffordScrambling instance
    """
    return CliffordScrambling(n_qutrits, seed, depth)


def scramble_weights(
    weights: List[int],
    seed: int = 42,
    depth: int = 3
) -> QutritStabilizerTableau:
    """Scramble ternary weights for secure deployment.

    Args:
        weights: Ternary weights in {-1, 0, 1}
        seed: Secure random seed
        depth: Scrambling depth

    Returns:
        Scrambled QutritStabilizerTableau
    """
    scrambler = create_scrambling(len(weights), seed, depth)
    return scrambler.encrypt_weights(weights)


def scramble_and_decrypt(
    data: List[int],
    seed: int = 42,
    depth: int = 3
) -> List[int]:
    """Scramble data and immediately decrypt (for testing).

    Args:
        data: Ternary data in {-1, 0, 1}
        seed: Secure random seed
        depth: Scrambling depth

    Returns:
        Decrypted data (should match input if no errors)
    """
    scrambler = create_scrambling(len(data), seed, depth)
    tab = scrambler.encrypt_data(data)
    return scrambler.decrypt_output(tab)