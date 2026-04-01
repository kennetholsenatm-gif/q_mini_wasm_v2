#include "../quantum/error_correction.hpp"
#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace qminiwasm::quantum;

void test_encode_513() {
  Code513 code;
  auto tab = code.encode(0);
  assert(tab.num_qutrits() == 5);

  // Check that initial state has no errors
  auto syndrome = code.extract_syndrome(tab);
  assert(syndrome[0] == 0 && syndrome[1] == 0 && syndrome[2] == 0 && syndrome[3] == 0);

  std::cout << "✓ test_encode_513 passed" << std::endl;
}

void test_encode_312() {
  Code312 code;
  auto tab = code.encode(0);
  assert(tab.num_qutrits() == 3);

  // Check that initial state has no errors
  auto syndrome = code.extract_syndrome(tab);
  assert(syndrome.first == 0 && syndrome.second == 0);

  std::cout << "✓ test_encode_312 passed" << std::endl;
}

void test_syndrome_extraction_513() {
  Code513 code;
  auto tab = code.encode(0);

  // Apply an X error on qubit 0
  tab.apply_x3(0);

  auto syndrome = code.extract_syndrome(tab);
  // Syndrome should be non-trivial
  assert(!(syndrome[0] == 0 && syndrome[1] == 0 && syndrome[2] == 0 && syndrome[3] == 0));

  std::cout << "✓ test_syndrome_extraction_513 passed" << std::endl;
  std::cout << "  Syndrome: (" << syndrome[0] << ", " << syndrome[1] << ", "
            << syndrome[2] << ", " << syndrome[3] << ")" << std::endl;
}

void test_error_identification_513() {
  Code513 code;
  auto tab = code.encode(0);

  // Apply an X error on qubit 2
  tab.apply_x3(2);

  auto syndrome = code.extract_syndrome(tab);
  auto error = code.identify_error(syndrome);

  assert(error.has_value());
  assert(error->first == 2);  // Error on qubit 2
  assert(error->second == 0); // X error

  std::cout << "✓ test_error_identification_513 passed" << std::endl;
  std::cout << "  Identified error: qubit " << error->first << ", type " << error->second << std::endl;
}

void test_error_correction_513() {
  Code513 code;
  auto tab = code.encode(0);

  // Apply an X error on qubit 1
  tab.apply_x3(1);

  // Correct the error
  bool corrected = correct_single_error_513(tab);
  assert(corrected);

  // Check that syndrome is now trivial
  auto syndrome = code.extract_syndrome(tab);
  assert(syndrome[0] == 0 && syndrome[1] == 0 && syndrome[2] == 0 && syndrome[3] == 0);

  std::cout << "✓ test_error_correction_513 passed" << std::endl;
}

void test_error_detection_312() {
  Code312 code;
  auto tab = code.encode(0);

  // Apply an X error on qubit 0
  tab.apply_x3(0);

  // Detect the error
  bool detected = detect_error_312(tab);
  assert(detected);

  std::cout << "✓ test_error_detection_312 passed" << std::endl;
}

void test_no_error_513() {
  Code513 code;
  auto tab = code.encode(0);

  // No error applied
  bool corrected = correct_single_error_513(tab);
  assert(!corrected);  // Should return false when no error

  std::cout << "✓ test_no_error_513 passed" << std::endl;
}

void test_no_error_312() {
  Code312 code;
  auto tab = code.encode(0);

  // No error applied
  bool detected = detect_error_312(tab);
  assert(!detected);  // Should return false when no error

  std::cout << "✓ test_no_error_312 passed" << std::endl;
}

int main() {
  std::cout << "Running error correction tests..." << std::endl;

  test_encode_513();
  test_encode_312();
  test_syndrome_extraction_513();
  test_error_identification_513();
  test_error_correction_513();
  test_error_detection_312();
  test_no_error_513();
  test_no_error_312();

  std::cout << "\nAll error correction tests passed!" << std::endl;
  return 0;
}