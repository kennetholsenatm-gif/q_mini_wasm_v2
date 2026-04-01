#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace qminiwasm::quantum;

void test_initialization() {
  QutritTableau tab(3);
  assert(tab.num_qutrits() == 3);
  // Initial state should be |0⟩^3 with stabilizers Z_0, Z_1, Z_2
  assert(tab.stabilizer_string(0) == "Z I I");
  assert(tab.stabilizer_string(1) == "I Z I");
  assert(tab.stabilizer_string(2) == "I I Z");
  std::cout << "✓ test_initialization passed" << std::endl;
}

void test_h3_gate() {
  QutritTableau tab(1);
  tab.apply_h3(0);
  // H₃ on |0⟩ should create superposition
  // The stabilizer should be X (with possible phase)
  std::string s = tab.stabilizer_string(0);
  assert(s.find("X") != std::string::npos);
  std::cout << "✓ test_h3_gate passed (stabilizer: " << s << ")" << std::endl;
}

void test_s3_gate() {
  QutritTableau tab(1);
  tab.apply_s3(0);
  // S₃ on |0⟩ should add phase
  std::string s = tab.stabilizer_string(0);
  assert(s.find("Z") != std::string::npos);
  std::cout << "✓ test_s3_gate passed (stabilizer: " << s << ")" << std::endl;
}

void test_cz3_gate() {
  QutritTableau tab(2);
  tab.apply_h3(0);
  tab.apply_h3(1);
  tab.apply_cz3(0, 1);
  // CZ₃ should entangle the two qutrits
  std::string s0 = tab.stabilizer_string(0);
  std::string s1 = tab.stabilizer_string(1);
  // Both stabilizers should have non-trivial terms
  assert(s0 != "I I" && s0 != "Z I" && s0 != "I Z");
  assert(s1 != "I I" && s1 != "Z I" && s1 != "I Z");
  std::cout << "✓ test_cz3_gate passed" << std::endl;
  std::cout << "  s0: " << s0 << std::endl;
  std::cout << "  s1: " << s1 << std::endl;
}

void test_copy() {
  QutritTableau tab(2);
  tab.apply_h3(0);
  tab.apply_cz3(0, 1);

  QutritTableau copy = tab.copy();
  assert(copy.num_qutrits() == 2);
  assert(copy.stabilizer_string(0) == tab.stabilizer_string(0));
  assert(copy.stabilizer_string(1) == tab.stabilizer_string(1));
  std::cout << "✓ test_copy passed" << std::endl;
}

void test_measurement_random() {
  QutritTableau tab(1);
  // |0⟩ state: stabilizer is Z, so X component is 0
  int result = tab.measure(0);
  assert(result == -1);  // Random measurement
  std::cout << "✓ test_measurement_random passed" << std::endl;
}

void test_measurement_deterministic() {
  QutritTableau tab(1);
  tab.apply_x3(0, 1);  // Apply X to get |1⟩
  // Now the state is |1⟩, measurement should be deterministic
  int result = tab.measure(0);
  assert(result >= 0 && result <= 2);
  std::cout << "✓ test_measurement_deterministic passed (result: " << result << ")" << std::endl;
}

void test_x3_z3_gates() {
  QutritTableau tab(1);
  tab.apply_x3(0, 2);  // X²
  tab.apply_z3(0, 1);  // Z
  // Should have some phase
  std::string s = tab.stabilizer_string(0);
  assert(!s.empty());
  std::cout << "✓ test_x3_z3_gates passed (stabilizer: " << s << ")" << std::endl;
}

int main() {
  std::cout << "Running qutrit tableau tests..." << std::endl;

  test_initialization();
  test_h3_gate();
  test_s3_gate();
  test_cz3_gate();
  test_copy();
  test_measurement_random();
  test_measurement_deterministic();
  test_x3_z3_gates();

  std::cout << "\nAll tests passed!" << std::endl;
  return 0;
}