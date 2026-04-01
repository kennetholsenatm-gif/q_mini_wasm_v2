#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace qminiwasm::quantum {

/**
 * Clifford unitary for weight/data encryption.
 * 
 * The scrambling unitary U_C is a deterministically random sequence of
 * H₃, S₃, and CZ₃ gates generated from a secure seed.
 * 
 * Uses homomorphic properties of the Clifford group:
 * U_C · (X^a Z^b) · U_C† = X^a' Z^b' (another Pauli operator)
 * 
 * This preserves the routing logic while obscuring the ternary coordinates.
 */
class QUTRIT_API CliffordScrambling {
 public:
  /** Initialize scrambling unitary. */
  CliffordScrambling(int n_qutrits, uint64_t seed = 42, int depth = 3);

  /** Number of qutrits. */
  int num_qutrits() const { return n_; }

  /** Get the seed used for gate generation. */
  uint64_t seed() const { return seed_; }

  /** Get the circuit depth. */
  int depth() const { return depth_; }

  /** Get the total number of gates in the scrambling circuit. */
  int gate_count() const { return static_cast<int>(gates_.size()); }

  /** Encrypt ternary weights via Clifford conjugation. */
  QutritTableau encrypt_weights(const std::vector<int>& weights) const;

  /** Encrypt input data via Clifford conjugation. */
  QutritTableau encrypt_data(const std::vector<int>& data) const;

  /** Decrypt output logits via inverse Clifford conjugation. */
  std::vector<int> decrypt_output(QutritTableau& tab) const;

 private:
  void generate_gates();
  void apply_gates(QutritTableau& tab) const;
  void apply_inverse_gates(QutritTableau& tab) const;

  int n_;
  uint64_t seed_;
  int depth_;
  std::vector<std::tuple<char, int, int>> gates_;  // Gate sequence: (type, q1, q2)
};

// Convenience functions

/** Create a Clifford scrambling unitary. */
CliffordScrambling create_scrambling(int n_qutrits, uint64_t seed = 42, int depth = 3);

/** Scramble ternary weights for secure deployment. */
QutritTableau scramble_weights(const std::vector<int>& weights, uint64_t seed = 42, int depth = 3);

/** Scramble data and immediately decrypt (for testing). */
std::vector<int> scramble_and_decrypt(const std::vector<int>& data, uint64_t seed = 42, int depth = 3);

}  // namespace qminiwasm::quantum