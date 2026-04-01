#pragma once

#include "qutrit_export.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qminiwasm::quantum {

/**
 * Qutrit stabilizer tableau over GF(3) for Clifford simulation.
 * 
 * This implements the Ternary Symplectic Pauli Frame (TSPF) described in the
 * Qutrit Clifford AI Edge Applications research. Operations are tracked via
 * symplectic matrices over GF(3) with phase tracking mod 3.
 * 
 * Key differences from qubit (F2) tableau:
 * - Arithmetic is mod 3 instead of mod 2
 * - Phase is tracked mod 3 (values 0, 1, 2 corresponding to ω^0, ω^1, ω^2)
 * - X and Z operators act on qutrits: X|a⟩ = |a+1 mod 3⟩, Z|a⟩ = ω^a|a⟩
 */
class QUTRIT_API QutritTableau {
 public:
  /** Initialize n-qutrit stabilizer state in |0⟩^n. */
  explicit QutritTableau(int n_qutrits);

  /** Number of qutrits. */
  int num_qutrits() const { return n_; }

  /** Create a deep copy of this tableau. */
  QutritTableau copy() const;

  /** Apply qutrit Hadamard gate H₃ on qutrit q (in place). */
  void apply_h3(int q);

  /** Apply qutrit Phase gate S₃ on qutrit q (in place). */
  void apply_s3(int q);

  /** Apply qutrit Controlled-Z gate CZ₃ (in place). */
  void apply_cz3(int control, int target);

  /** Apply X₃^power on qutrit q (in place). */
  void apply_x3(int q, int power = 1);

  /** Apply Z₃^power on qutrit q (in place). */
  void apply_z3(int q, int power = 1);

  /** Measure qutrit q in computational basis. Returns outcome in {0,1,2} or -1 if random. */
  int measure(int q);

  /** Get stabilizer string for generator row. */
  std::string stabilizer_string(int row) const;

  /** Access X component (for testing/binding). */
  const std::vector<int>& x() const { return x_; }

  /** Access Z component (for testing/binding). */
  const std::vector<int>& z() const { return z_; }

  /** Access phase component (for testing/binding). */
  const std::vector<int>& r() const { return r_; }

 private:
  void check_qutrit(int q) const;

  int n_;
  std::vector<int> x_;  // X components: n_ * n_ matrix stored row-major
  std::vector<int> z_;  // Z components: n_ * n_ matrix stored row-major
  std::vector<int> r_;  // Phase components: n_ values
};

}  // namespace qminiwasm::quantum