#include "attention.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace qminiwasm::quantum {

QutritBellAttention::QutritBellAttention(int n_heads, int n_features)
    : n_heads_(n_heads), n_features_(n_features), n_qutrits_(n_heads * 2) {
  if (n_heads < 1) {
    throw std::invalid_argument("n_heads must be >= 1");
  }
  if (n_features < 1) {
    throw std::invalid_argument("n_features must be >= 1");
  }
}

QutritTableau QutritBellAttention::encode_key_query(
    const std::vector<int>& keys, const std::vector<int>& queries) const {
  if (static_cast<int>(keys.size()) != n_heads_) {
    throw std::invalid_argument("keys size must match n_heads");
  }
  if (static_cast<int>(queries.size()) != n_heads_) {
    throw std::invalid_argument("queries size must match n_heads");
  }

  QutritTableau tab(n_qutrits_);

  // Initialize in maximal superposition
  for (int i = 0; i < n_qutrits_; ++i) {
    tab.apply_h3(i);
  }

  // Encode keys (wires 0 to n_heads-1)
  for (int i = 0; i < n_heads_; ++i) {
    int gf3_val = ((keys[i] + 3) % 3);
    if (gf3_val == 1) {
      tab.apply_s3(i);
    } else if (gf3_val == 2) {
      tab.apply_s3(i);
      tab.apply_s3(i);
    }
  }

  // Encode queries (wires n_heads to 2*n_heads-1)
  for (int i = 0; i < n_heads_; ++i) {
    int gf3_val = ((queries[i] + 3) % 3);
    if (gf3_val == 1) {
      tab.apply_s3(n_heads_ + i);
    } else if (gf3_val == 2) {
      tab.apply_s3(n_heads_ + i);
      tab.apply_s3(n_heads_ + i);
    }
  }

  return tab;
}

std::vector<float> QutritBellAttention::bell_measurement(QutritTableau& tab) const {
  // Apply transversal Bell measurement
  for (int i = 0; i < n_heads_; ++i) {
    // Entangle key and query qutrits
    tab.apply_cz3(i, n_heads_ + i);
    // Rotate to Bell basis
    tab.apply_h3(i);
  }

  // Extract attention scores from stabilizer phases
  std::vector<float> scores;
  scores.reserve(n_heads_);

  const auto& r = tab.r();
  for (int i = 0; i < n_heads_; ++i) {
    int phase = r[i] % 3;
    // Map phase to probability-like score
    float score;
    if (phase == 0) {
      score = 1.0f;  // Constructive interference
    } else if (phase == 1) {
      score = 1.0f / 3.0f;  // Partial interference
    } else {
      score = 1.0f / 9.0f;  // Destructive interference
    }
    scores.push_back(score);
  }

  // Normalize (simple softmax-like normalization)
  float total = 0.0f;
  for (float s : scores) {
    total += s;
  }
  if (total > 0.0f) {
    for (float& s : scores) {
      s /= total;
    }
  }

  return scores;
}

std::vector<float> QutritBellAttention::compute_attention(
    const std::vector<int>& keys, const std::vector<int>& queries) const {
  auto tab = encode_key_query(keys, queries);
  return bell_measurement(tab);
}

// ============================================================================
// Convenience Functions
// ============================================================================

std::vector<float> qutrit_attention(
    const std::vector<int>& keys,
    const std::vector<int>& queries,
    int n_heads) {
  int n = (n_heads < 0) ? static_cast<int>(keys.size()) : n_heads;
  if (static_cast<int>(keys.size()) != n || static_cast<int>(queries.size()) != n) {
    throw std::invalid_argument("Keys and queries must have the same size");
  }

  QutritBellAttention attention(n, n);
  return attention.compute_attention(keys, queries);
}

std::vector<float> softmax_approximation(const std::vector<float>& scores, float temperature) {
  // Scale by temperature
  std::vector<float> scaled;
  scaled.reserve(scores.size());
  for (float s : scores) {
    scaled.push_back(s / temperature);
  }

  // Map to discrete stabilizer probabilities
  std::vector<float> discrete;
  discrete.reserve(scaled.size());
  for (float s : scaled) {
    if (s > 0.5f) {
      discrete.push_back(1.0f);
    } else if (s > 0.25f) {
      discrete.push_back(1.0f / 3.0f);
    } else if (s > 0.125f) {
      discrete.push_back(1.0f / 9.0f);
    } else {
      discrete.push_back(0.0f);
    }
  }

  // Normalize
  float total = 0.0f;
  for (float d : discrete) {
    total += d;
  }
  if (total > 0.0f) {
    for (float& d : discrete) {
      d /= total;
    }
  }

  return discrete;
}

std::vector<int> top_k_attention(const std::vector<float>& scores, int k) {
  std::vector<std::pair<float, int>> indexed;
  indexed.reserve(scores.size());
  for (int i = 0; i < static_cast<int>(scores.size()); ++i) {
    indexed.emplace_back(scores[i], i);
  }

  std::sort(indexed.begin(), indexed.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<int> result;
  result.reserve(k);
  for (int i = 0; i < k && i < static_cast<int>(indexed.size()); ++i) {
    result.push_back(indexed[i].second);
  }

  return result;
}

}  // namespace qminiwasm::quantum