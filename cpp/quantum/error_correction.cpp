#include "error_correction.hpp"

#include <stdexcept>

namespace qminiwasm::quantum {

// ============================================================================
// [[5,1,3]] Code Implementation
// ============================================================================

QutritTableau Code513::encode(int state) const {
  QutritTableau tab(N_PHYSICAL);

  // Encoding circuit for [[5,1,3]] code
  // Step 1: Create entanglement
  tab.apply_h3(0);
  tab.apply_cz3(0, 1);
  tab.apply_cz3(0, 2);
  tab.apply_cz3(0, 3);
  tab.apply_cz3(0, 4);

  // Step 2: Apply stabilizer structure
  tab.apply_h3(1);
  tab.apply_cz3(1, 2);
  tab.apply_h3(2);
  tab.apply_cz3(2, 3);
  tab.apply_h3(3);
  tab.apply_cz3(3, 4);

  // Encode the logical state
  if (state == 1) {
    tab.apply_x3(0);
  } else if (state == 2) {
    tab.apply_x3(0, 2);
  }

  return tab;
}

std::array<int, 4> Code513::extract_syndrome(const QutritTableau& tab) const {
  std::array<int, 4> syndrome{};
  const auto& x = tab.x();
  const auto& z = tab.z();

  for (int i = 0; i < 4; ++i) {
    int s = 0;
    for (int j = 0; j < N_PHYSICAL; ++j) {
      // Symplectic inner product: H_x[i,j] * Z[j] + H_z[i,j] * X[j]
      s += X_CHECK[i][j] * z[j * N_PHYSICAL + j];
      s += Z_CHECK[i][j] * x[j * N_PHYSICAL + j];
    }
    syndrome[i] = ((s % 3) + 3) % 3;
  }

  return syndrome;
}

std::optional<std::pair<int, int>> Code513::identify_error(
    const std::array<int, 4>& syndrome) const {
  // Check for trivial syndrome
  if (syndrome[0] == 0 && syndrome[1] == 0 && syndrome[2] == 0 && syndrome[3] == 0) {
    return std::nullopt;
  }

  // Syndrome lookup table for [[5,1,3]] code
  // Maps syndrome to (qutrit, error_type)
  // error_type: 0=X error, 1=Z error, 2=Y error
  static const std::array<std::pair<int, int>, 81> syndrome_map = []() {
    std::array<std::pair<int, int>, 81> map{};
    // Initialize all to invalid
    for (auto& p : map) {
      p = {-1, -1};
    }

    // X errors on each qutrit
    map[1 * 27 + 0 * 9 + 0 * 3 + 2] = {0, 0};  // (1,0,0,2) -> X on qubit 0
    map[2 * 27 + 0 * 9 + 0 * 3 + 1] = {0, 0};  // (2,0,0,1) -> X² on qubit 0
    map[0 * 27 + 1 * 9 + 0 * 3 + 2] = {1, 0};  // (0,1,0,2) -> X on qubit 1
    map[0 * 27 + 2 * 9 + 0 * 3 + 1] = {1, 0};  // (0,2,0,1) -> X² on qubit 1
    map[0 * 27 + 0 * 9 + 1 * 3 + 2] = {2, 0};  // (0,0,1,2) -> X on qubit 2
    map[0 * 27 + 0 * 9 + 2 * 3 + 1] = {2, 0};  // (0,0,2,1) -> X² on qubit 2
    map[2 * 27 + 0 * 9 + 0 * 3 + 0] = {3, 0};  // (2,0,0,0) -> X on qubit 3
    map[1 * 27 + 0 * 9 + 0 * 3 + 0] = {3, 0};  // (1,0,0,0) -> X² on qubit 3
    map[0 * 27 + 2 * 9 + 0 * 3 + 0] = {4, 0};  // (0,2,0,0) -> X on qubit 4
    map[0 * 27 + 1 * 9 + 0 * 3 + 0] = {4, 0};  // (0,1,0,0) -> X² on qubit 4

    // Z errors on each qutrit
    map[0 * 27 + 2 * 9 + 2 * 3 + 1] = {0, 1};  // (0,2,2,1) -> Z on qubit 0
    map[0 * 27 + 1 * 9 + 1 * 3 + 2] = {0, 1};  // (0,1,1,2) -> Z² on qubit 0
    map[1 * 27 + 0 * 9 + 2 * 3 + 2] = {1, 1};  // (1,0,2,2) -> Z on qubit 1
    map[2 * 27 + 0 * 9 + 1 * 3 + 1] = {1, 1};  // (2,0,1,1) -> Z² on qubit 1
    map[2 * 27 + 1 * 9 + 0 * 3 + 2] = {2, 1};  // (2,1,0,2) -> Z on qubit 2
    map[1 * 27 + 2 * 9 + 0 * 3 + 1] = {2, 1};  // (1,2,0,1) -> Z² on qubit 2
    map[2 * 27 + 2 * 9 + 1 * 3 + 0] = {3, 1};  // (2,2,1,0) -> Z on qubit 3
    map[1 * 27 + 1 * 9 + 2 * 3 + 0] = {3, 1};  // (1,1,2,0) -> Z² on qubit 3
    map[1 * 27 + 2 * 9 + 2 * 3 + 2] = {4, 1};  // (1,2,2,2) -> Z on qubit 4
    map[2 * 27 + 1 * 9 + 1 * 3 + 1] = {4, 1};  // (2,1,1,1) -> Z² on qubit 4

    return map;
  }();

  int idx = syndrome[0] * 27 + syndrome[1] * 9 + syndrome[2] * 3 + syndrome[3];
  if (idx >= 0 && idx < 81) {
    auto result = syndrome_map[idx];
    if (result.first >= 0) {
      return result;
    }
  }

  // Unknown syndrome (possibly Y error or multiple errors)
  return std::nullopt;
}

void Code513::correct_error(QutritTableau& tab, int qutrit, int error_type) const {
  if (error_type == 0) {
    // Apply X⁻¹ = X² to correct X error
    tab.apply_x3(qutrit, 2);
  } else if (error_type == 1) {
    // Apply Z⁻¹ = Z² to correct Z error
    tab.apply_z3(qutrit, 2);
  } else if (error_type == 2) {
    // Y = XZ, so apply Y⁻¹ = Z⁻¹X⁻¹ = Z²X²
    tab.apply_z3(qutrit, 2);
    tab.apply_x3(qutrit, 2);
  }
}

// ============================================================================
// [[3,1,2]] Code Implementation
// ============================================================================

QutritTableau Code312::encode(int state) const {
  QutritTableau tab(N_PHYSICAL);

  // Encoding circuit for [[3,1,2]] code
  tab.apply_h3(0);
  tab.apply_cz3(0, 1);
  tab.apply_cz3(0, 2);

  // Encode the logical state
  if (state == 1) {
    tab.apply_x3(0);
  } else if (state == 2) {
    tab.apply_x3(0, 2);
  }

  return tab;
}

std::pair<int, int> Code312::extract_syndrome(const QutritTableau& tab) const {
  const auto& x = tab.x();
  const auto& z = tab.z();

  int x_syn = 0;
  int z_syn = 0;

  for (int j = 0; j < N_PHYSICAL; ++j) {
    x_syn += x[j * N_PHYSICAL + j];
    z_syn += z[j * N_PHYSICAL + j];
  }

  return {((x_syn % 3) + 3) % 3, ((z_syn % 3) + 3) % 3};
}

bool Code312::is_valid(const std::pair<int, int>& syndrome) const {
  return syndrome.first == 0 && syndrome.second == 0;
}

// ============================================================================
// Convenience Functions
// ============================================================================

QutritTableau encode_513(int state) {
  return Code513().encode(state);
}

QutritTableau encode_312(int state) {
  return Code312().encode(state);
}

bool correct_single_error_513(QutritTableau& tab) {
  Code513 code;
  auto syndrome = code.extract_syndrome(tab);
  auto error = code.identify_error(syndrome);

  if (error.has_value()) {
    auto [qutrit, error_type] = error.value();
    code.correct_error(tab, qutrit, error_type);
    return true;
  }

  return false;
}

bool detect_error_312(const QutritTableau& tab) {
  Code312 code;
  auto syndrome = code.extract_syndrome(tab);
  return !code.is_valid(syndrome);
}

}  // namespace qminiwasm::quantum