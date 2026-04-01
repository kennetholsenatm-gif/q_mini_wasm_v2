#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace qminiwasm::quantum {

/**
 * [[5,1,3]] qutrit stabilizer code.
 * 
 * Encodes 1 logical qutrit into 5 physical qutrits.
 * Code distance 3: corrects any single-qutrit error (X, Z, or Y type).
 * 
 * Stabilizer generators (cyclic structure):
 * g₁ = X₁ Z₂ Z₃ X₄ I₅
 * g₂ = I₁ X₂ Z₃ Z₄ X₅
 * g₃ = X₁ I₂ X₃ Z₄ Z₅
 * g₄ = Z₁ X₂ I₃ X₄ Z₅
 */
class QUTRIT_API Code513 {
 public:
  static constexpr int N_PHYSICAL = 5;
  static constexpr int K_LOGICAL = 1;
  static constexpr int DISTANCE = 3;

  /** Encode a logical qutrit into the [[5,1,3]] code. */
  QutritTableau encode(int state = 0) const;

  /** Extract error syndrome from the tableau. */
  std::array<int, 4> extract_syndrome(const QutritTableau& tab) const;

  /** Identify error location and type from syndrome. Returns (qutrit, error_type) or nullopt. */
  std::optional<std::pair<int, int>> identify_error(const std::array<int, 4>& syndrome) const;

  /** Apply correction for identified error. */
  void correct_error(QutritTableau& tab, int qutrit, int error_type) const;

 private:
  // Check matrix X components (4 rows x 5 columns)
  static constexpr std::array<std::array<int, 5>, 4> X_CHECK = {{
    {1, 0, 0, 1, 0},  // g₁: X₁ Z₂ Z₃ X₄ I₅
    {0, 1, 0, 0, 1},  // g₂: I₁ X₂ Z₃ Z₄ X₅
    {1, 0, 1, 0, 0},  // g₃: X₁ I₂ X₃ Z₄ Z₅
    {0, 1, 0, 1, 0},  // g₄: Z₁ X₂ I₃ X₄ Z₅
  }};

  // Check matrix Z components (4 rows x 5 columns)
  static constexpr std::array<std::array<int, 5>, 4> Z_CHECK = {{
    {0, 1, 1, 0, 0},  // g₁: X₁ Z₂ Z₃ X₄ I₅
    {0, 0, 1, 1, 0},  // g₂: I₁ X₂ Z₃ Z₄ X₅
    {0, 0, 0, 1, 1},  // g₃: X₁ I₂ X₃ Z₄ Z₅
    {1, 0, 0, 0, 1},  // g₄: Z₁ X₂ I₃ X₄ Z₅
  }};
};

/**
 * [[3,1,2]] qutrit stabilizer code.
 * 
 * Encodes 1 logical qutrit into 3 physical qutrits.
 * Code distance 2: detects single erasures (but cannot correct).
 * 
 * Stabilizer generators:
 * g₁ = X₁ X₂ X₃
 * g₂ = Z₁ Z₂ Z₃
 */
class QUTRIT_API Code312 {
 public:
  static constexpr int N_PHYSICAL = 3;
  static constexpr int K_LOGICAL = 1;
  static constexpr int DISTANCE = 2;

  /** Encode a logical qutrit into the [[3,1,2]] code. */
  QutritTableau encode(int state = 0) const;

  /** Extract error syndrome from the tableau. Returns (x_syndrome, z_syndrome). */
  std::pair<int, int> extract_syndrome(const QutritTableau& tab) const;

  /** Check if syndrome indicates a valid (error-free) state. */
  bool is_valid(const std::pair<int, int>& syndrome) const;
};

// Convenience functions

/** Encode a logical qutrit using the [[5,1,3]] code. */
QutritTableau encode_513(int state = 0);

/** Encode a logical qutrit using the [[3,1,2]] code. */
QutritTableau encode_312(int state = 0);

/** Detect and correct a single-qutrit error in [[5,1,3]] code. Returns true if error was corrected. */
bool correct_single_error_513(QutritTableau& tab);

/** Detect an error in [[3,1,2]] code. Returns true if error was detected. */
bool detect_error_312(const QutritTableau& tab);

}  // namespace qminiwasm::quantum