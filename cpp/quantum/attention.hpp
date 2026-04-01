#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"

#include <string>
#include <vector>

namespace qminiwasm::quantum {

/**
 * Qutrit Bell basis attention mechanism.
 * 
 * Approximates Softmax attention using Clifford stabilizer measurements.
 * No floating-point exponentiation or normalization required.
 * 
 * Key insight: For stabilizer states, measurement probabilities are constrained
 * to {0, 1/3^k}, creating a discrete step-function that mimics low-temperature
 * Softmax behavior.
 */
class QUTRIT_API QutritBellAttention {
 public:
  /** Initialize Bell attention. */
  QutritBellAttention(int n_heads, int n_features);

  /** Number of attention heads. */
  int num_heads() const { return n_heads_; }

  /** Feature dimension per head. */
  int num_features() const { return n_features_; }

  /** Encode key and query vectors into qutrit state. */
  QutritTableau encode_key_query(const std::vector<int>& keys, const std::vector<int>& queries) const;

  /** Perform transversal Bell measurement to compute attention scores. */
  std::vector<float> bell_measurement(QutritTableau& tab) const;

  /** Compute attention scores for key-query pairs. */
  std::vector<float> compute_attention(const std::vector<int>& keys, const std::vector<int>& queries) const;

 private:
  int n_heads_;
  int n_features_;
  int n_qutrits_;  // 2 * n_heads (Key + Query qutrits per head)
};

// Convenience functions

/** Compute qutrit Bell attention scores. */
std::vector<float> qutrit_attention(
    const std::vector<int>& keys,
    const std::vector<int>& queries,
    int n_heads = -1);

/** Approximate Softmax using discrete stabilizer probabilities. */
std::vector<float> softmax_approximation(const std::vector<float>& scores, float temperature = 1.0f);

/** Select top-k attention indices. */
std::vector<int> top_k_attention(const std::vector<float>& scores, int k = 3);

}  // namespace qminiwasm::quantum