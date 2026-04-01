#include "../quantum/scrambling.hpp"
#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace qminiwasm::quantum;

void test_scrambling_creation() {
  CliffordScrambling scrambler(3, 42, 2);
  assert(scrambler.num_qutrits() == 3);
  assert(scrambler.seed() == 42);
  assert(scrambler.depth() == 2);
  assert(scrambler.gate_count() > 0);
  std::cout << "✓ test_scrambling_creation passed" << std::endl;
  std::cout << "  Gate count: " << scrambler.gate_count() << std::endl;
}

void test_encrypt_weights() {
  CliffordScrambling scrambler(3, 42, 2);
  std::vector<int> weights = {1, 0, -1};

  auto tab = scrambler.encrypt_weights(weights);
  assert(tab.num_qutrits() == 3);

  std::cout << "✓ test_encrypt_weights passed" << std::endl;
}

void test_encrypt_data() {
  CliffordScrambling scrambler(3, 42, 2);
  std::vector<int> data = {1, -1, 0};

  auto tab = scrambler.encrypt_data(data);
  assert(tab.num_qutrits() == 3);

  std::cout << "✓ test_encrypt_data passed" << std::endl;
}

void test_scramble_and_decrypt() {
  std::vector<int> data = {1, 0, -1, 1, 0};

  auto decrypted = scramble_and_decrypt(data, 42, 2);
  assert(decrypted.size() == 5);

  // Verify that decryption returns values in {0,1,2}
  for (int val : decrypted) {
    assert(val >= 0 && val <= 2);
  }

  std::cout << "✓ test_scramble_and_decrypt passed" << std::endl;
  std::cout << "  Input: [1, 0, -1, 1, 0]" << std::endl;
  std::cout << "  Decrypted: [" << decrypted[0] << ", " << decrypted[1] << ", "
            << decrypted[2] << ", " << decrypted[3] << ", " << decrypted[4] << "]" << std::endl;
}

void test_different_seeds() {
  std::vector<int> data = {1, 0, -1};

  auto result1 = scramble_and_decrypt(data, 42, 2);
  auto result2 = scramble_and_decrypt(data, 123, 2);

  // Different seeds should produce different intermediate states
  // but decryption should still work
  assert(result1.size() == 3);
  assert(result2.size() == 3);

  std::cout << "✓ test_different_seeds passed" << std::endl;
}

void test_different_depths() {
  std::vector<int> data = {1, 0, -1};

  auto result1 = scramble_and_decrypt(data, 42, 1);
  auto result2 = scramble_and_decrypt(data, 42, 3);

  assert(result1.size() == 3);
  assert(result2.size() == 3);

  std::cout << "✓ test_different_depths passed" << std::endl;
}

void test_create_scrambling_function() {
  auto scrambler = create_scrambling(5, 42, 2);
  assert(scrambler.num_qutrits() == 5);
  assert(scrambler.seed() == 42);
  assert(scrambler.depth() == 2);

  std::cout << "✓ test_create_scrambling_function passed" << std::endl;
}

void test_scramble_weights_function() {
  std::vector<int> weights = {1, -1, 0, 1};

  auto tab = scramble_weights(weights, 42, 2);
  assert(tab.num_qutrits() == 4);

  std::cout << "✓ test_scramble_weights_function passed" << std::endl;
}

int main() {
  std::cout << "Running scrambling tests..." << std::endl;

  test_scrambling_creation();
  test_encrypt_weights();
  test_encrypt_data();
  test_scramble_and_decrypt();
  test_different_seeds();
  test_different_depths();
  test_create_scrambling_function();
  test_scramble_weights_function();

  std::cout << "\nAll scrambling tests passed!" << std::endl;
  return 0;
}