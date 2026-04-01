#include "scrambling.hpp"

#include <random>
#include <stdexcept>

namespace qminiwasm::quantum {

CliffordScrambling::CliffordScrambling(int n_qutrits, uint64_t seed, int depth)
    : n_(n_qutrits), seed_(seed), depth_(depth) {
  if (n_qutrits < 1) {
    throw std::invalid_argument("n_qutrits must be >= 1");
  }
  if (depth < 1) {
    throw std::invalid_argument("depth must be >= 1");
  }
  generate_gates();
}

void CliffordScrambling::generate_gates() {
  std::mt19937_64 rng(seed_);
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  std::uniform_int_distribution<int> qubit_dist(0, n_ - 1);

  gates_.clear();

  for (int d = 0; d < depth_; ++d) {
    // Single-qutrit gates
    for (int q = 0; q < n_; ++q) {
      if (dist(rng) < 0.4) {
        gates_.emplace_back('H', q, -1);
      }
      if (dist(rng) < 0.4) {
        gates_.emplace_back('S', q, -1);
      }
    }

    // Two-qutrit gates
    for (int i = 0; i < n_ / 2; ++i) {
      int c = qubit_dist(rng);
      int t = qubit_dist(rng);
      if (c != t) {
        gates_.emplace_back('C', c, t);
      }
    }
  }
}

QutritTableau CliffordScrambling::encrypt_weights(const std::vector<int>& weights) const {
  if (static_cast<int>(weights.size()) != n_) {
    throw std::invalid_argument("weights size must match n_qutrits");
  }

  QutritTableau tab(n_);

  // Apply scrambling unitary U_C
  apply_gates(const_cast<QutritTableau&>(tab));

  // Encode weights into the scrambled basis
  for (int i = 0; i < n_; ++i) {
    int gf3_val = ((weights[i] + 3) % 3);
    if (gf3_val == 1) {
      tab.apply_x3(i);
    } else if (gf3_val == 2) {
      tab.apply_x3(i, 2);
    }
  }

  return tab;
}

QutritTableau CliffordScrambling::encrypt_data(const std::vector<int>& data) const {
  if (static_cast<int>(data.size()) != n_) {
    throw std::invalid_argument("data size must match n_qutrits");
  }

  QutritTableau tab(n_);

  // Encode data first
  for (int i = 0; i < n_; ++i) {
    int gf3_val = ((data[i] + 3) % 3);
    if (gf3_val == 1) {
      tab.apply_x3(i);
    } else if (gf3_val == 2) {
      tab.apply_x3(i, 2);
    }
  }

  // Apply scrambling unitary U_C
  apply_gates(tab);

  return tab;
}

std::vector<int> CliffordScrambling::decrypt_output(QutritTableau& tab) const {
  // Apply inverse scrambling U_C†
  apply_inverse_gates(tab);

  // Extract decrypted values
  std::vector<int> output;
  output.reserve(n_);

  const auto& r = tab.r();
  for (int i = 0; i < n_; ++i) {
    output.push_back(r[i] % 3);
  }

  return output;
}

void CliffordScrambling::apply_gates(QutritTableau& tab) const {
  for (const auto& gate : gates_) {
    char type = std::get<0>(gate);
    int q1 = std::get<1>(gate);
    int q2 = std::get<2>(gate);

    if (type == 'H') {
      tab.apply_h3(q1);
    } else if (type == 'S') {
      tab.apply_s3(q1);
    } else if (type == 'C') {
      tab.apply_cz3(q1, q2);
    }
  }
}

void CliffordScrambling::apply_inverse_gates(QutritTableau& tab) const {
  // Apply gates in reverse order
  for (auto it = gates_.rbegin(); it != gates_.rend(); ++it) {
    char type = std::get<0>(*it);
    int q1 = std::get<1>(*it);
    int q2 = std::get<2>(*it);

    if (type == 'H') {
      tab.apply_h3(q1);  // H₃ is self-inverse
    } else if (type == 'S') {
      // S₃† = S₃²
      tab.apply_s3(q1);
      tab.apply_s3(q1);
    } else if (type == 'C') {
      tab.apply_cz3(q1, q2);  // CZ₃ is self-inverse
    }
  }
}

// ============================================================================
// Convenience Functions
// ============================================================================

CliffordScrambling create_scrambling(int n_qutrits, uint64_t seed, int depth) {
  return CliffordScrambling(n_qutrits, seed, depth);
}

QutritTableau scramble_weights(const std::vector<int>& weights, uint64_t seed, int depth) {
  CliffordScrambling scrambler(static_cast<int>(weights.size()), seed, depth);
  return scrambler.encrypt_weights(weights);
}

std::vector<int> scramble_and_decrypt(const std::vector<int>& data, uint64_t seed, int depth) {
  CliffordScrambling scrambler(static_cast<int>(data.size()), seed, depth);
  auto tab = scrambler.encrypt_data(data);
  return scrambler.decrypt_output(tab);
}

}  // namespace qminiwasm::quantum