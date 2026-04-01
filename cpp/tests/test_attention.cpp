#include "../quantum/attention.hpp"
#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace qminiwasm::quantum;

void test_attention_creation() {
  QutritBellAttention attention(3, 4);
  assert(attention.num_heads() == 3);
  assert(attention.num_features() == 4);
  std::cout << "✓ test_attention_creation passed" << std::endl;
}

void test_encode_key_query() {
  QutritBellAttention attention(2, 3);
  std::vector<int> keys = {1, -1};
  std::vector<int> queries = {0, 1};

  auto tab = attention.encode_key_query(keys, queries);
  assert(tab.num_qutrits() == 4);  // 2 heads * 2 qutrits per head

  std::cout << "✓ test_encode_key_query passed" << std::endl;
}

void test_compute_attention() {
  QutritBellAttention attention(2, 3);
  std::vector<int> keys = {1, -1};
  std::vector<int> queries = {0, 1};

  auto scores = attention.compute_attention(keys, queries);
  assert(scores.size() == 2);

  // Verify scores are normalized (sum to ~1)
  float sum = 0.0f;
  for (float s : scores) {
    assert(s >= 0.0f);
    sum += s;
  }
  assert(std::abs(sum - 1.0f) < 0.01f);

  std::cout << "✓ test_compute_attention passed" << std::endl;
  std::cout << "  Scores: [" << scores[0] << ", " << scores[1] << "]" << std::endl;
}

void test_qutrit_attention_function() {
  std::vector<int> keys = {1, 0, -1};
  std::vector<int> queries = {-1, 1, 0};

  auto scores = qutrit_attention(keys, queries);
  assert(scores.size() == 3);

  float sum = 0.0f;
  for (float s : scores) {
    assert(s >= 0.0f);
    sum += s;
  }
  assert(std::abs(sum - 1.0f) < 0.01f);

  std::cout << "✓ test_qutrit_attention_function passed" << std::endl;
  std::cout << "  Scores: [" << scores[0] << ", " << scores[1] << ", " << scores[2] << "]" << std::endl;
}

void test_softmax_approximation() {
  std::vector<float> raw_scores = {0.8f, 0.3f, 0.1f};

  auto discrete = softmax_approximation(raw_scores, 1.0f);
  assert(discrete.size() == 3);

  // Verify discrete values are in {0, 1/3, 1}
  for (float d : discrete) {
    assert(d == 0.0f || std::abs(d - 1.0f / 3.0f) < 0.01f || std::abs(d - 1.0f) < 0.01f);
  }

  // Verify normalization
  float sum = 0.0f;
  for (float d : discrete) {
    sum += d;
  }
  assert(std::abs(sum - 1.0f) < 0.01f);

  std::cout << "✓ test_softmax_approximation passed" << std::endl;
  std::cout << "  Input: [0.8, 0.3, 0.1]" << std::endl;
  std::cout << "  Discrete: [" << discrete[0] << ", " << discrete[1] << ", " << discrete[2] << "]" << std::endl;
}

void test_softmax_temperature() {
  std::vector<float> raw_scores = {0.6f, 0.4f};

  auto discrete_t1 = softmax_approximation(raw_scores, 1.0f);
  auto discrete_t2 = softmax_approximation(raw_scores, 0.5f);

  assert(discrete_t1.size() == 2);
  assert(discrete_t2.size() == 2);

  std::cout << "✓ test_softmax_temperature passed" << std::endl;
}

void test_top_k_attention() {
  std::vector<float> scores = {0.1f, 0.5f, 0.3f, 0.1f};

  auto top2 = top_k_attention(scores, 2);
  assert(top2.size() == 2);
  assert(top2[0] == 1);  // Index of highest score (0.5)
  assert(top2[1] == 2);  // Index of second highest (0.3)

  std::cout << "✓ test_top_k_attention passed" << std::endl;
  std::cout << "  Top-2 indices: [" << top2[0] << ", " << top2[1] << "]" << std::endl;
}

void test_different_head_counts() {
  // Test with different numbers of heads
  QutritBellAttention attention1(1, 3);
  QutritBellAttention attention2(4, 3);

  std::vector<int> keys1 = {1};
  std::vector<int> queries1 = {-1};
  auto scores1 = attention1.compute_attention(keys1, queries1);
  assert(scores1.size() == 1);

  std::vector<int> keys2 = {1, -1, 0, 1};
  std::vector<int> queries2 = {-1, 1, 0, -1};
  auto scores2 = attention2.compute_attention(keys2, queries2);
  assert(scores2.size() == 4);

  std::cout << "✓ test_different_head_counts passed" << std::endl;
}

int main() {
  std::cout << "Running attention tests..." << std::endl;

  test_attention_creation();
  test_encode_key_query();
  test_compute_attention();
  test_qutrit_attention_function();
  test_softmax_approximation();
  test_softmax_temperature();
  test_top_k_attention();
  test_different_head_counts();

  std::cout << "\nAll attention tests passed!" << std::endl;
  return 0;
}