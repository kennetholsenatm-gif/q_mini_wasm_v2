#include "../quantum/qutrit_training_service.hpp"
#include "../quantum/qutrit_tableau.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace qminiwasm::quantum;

void test_initialize_superposition() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(5, 42);
  assert(id > 0);
  std::cout << "✓ test_initialize_superposition passed" << std::endl;
}

void test_apply_hadamard_layer() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  // Apply Hadamard to all qubits
  bool success = service.ApplyHadamardLayer(id, {});
  assert(success);

  // Apply Hadamard to specific qubits
  success = service.ApplyHadamardLayer(id, {0, 2});
  assert(success);

  std::cout << "✓ test_apply_hadamard_layer passed" << std::endl;
}

void test_lattice_collapse() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);
  service.ApplyHadamardLayer(id, {});

  auto [weights, probs] = service.LatticeCollapse(id, 100);
  assert(weights.size() == 3);
  assert(probs.size() == 3);

  // Verify weights are in {-1, 0, 1}
  for (int w : weights) {
    assert(w >= -1 && w <= 1);
  }

  // Verify probabilities are in [0, 1]
  for (float p : probs) {
    assert(p >= 0.0f && p <= 1.0f);
  }

  std::cout << "✓ test_lattice_collapse passed" << std::endl;
}

void test_apply_controlled_z() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  bool success = service.ApplyControlledZ(id, 0, 1, 1);
  assert(success);

  success = service.ApplyControlledZ(id, 1, 2, 2);
  assert(success);

  std::cout << "✓ test_apply_controlled_z passed" << std::endl;
}

void test_push_noise_phase() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  bool success = service.PushNoisePhase(id, 0);
  assert(success);

  success = service.PushNoisePhase(id, 1);
  assert(success);

  success = service.PushNoisePhase(id, 2);
  assert(success);

  std::cout << "✓ test_push_noise_phase passed" << std::endl;
}

void test_extract_error_syndrome() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  auto [syndrome, has_error] = service.ExtractErrorSyndrome(id);
  assert(syndrome.size() == 3);
  assert(!has_error);  // No errors initially

  // Apply some phases and check
  service.PushNoisePhase(id, 1);
  auto [syndrome2, has_error2] = service.ExtractErrorSyndrome(id);
  assert(has_error2);  // Should have error now

  std::cout << "✓ test_extract_error_syndrome passed" << std::endl;
}

void test_update_phase_tableau() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  std::vector<int32_t> updates = {0, 1, 2};
  bool success = service.UpdatePhaseTableau(id, updates);
  assert(success);

  // Check metrics were updated
  auto metrics = service.GetTrainingMetrics(id);
  assert(metrics.total_epochs == 1);
  assert(metrics.successful_epochs == 1);

  std::cout << "✓ test_update_phase_tableau passed" << std::endl;
}

void test_get_training_metrics() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(5, 42);

  auto metrics = service.GetTrainingMetrics(id);
  assert(metrics.total_epochs == 0);
  assert(metrics.successful_epochs == 0);
  assert(metrics.current_loss == 0.0f);

  std::cout << "✓ test_get_training_metrics passed" << std::endl;
}

void test_get_tableau_state() {
  QutritTrainingServiceImpl service;
  int64_t id = service.InitializeSuperposition(3, 42);

  auto [x_comp, z_comp, phases] = service.GetTableauState(id);
  assert(x_comp.size() == 9);  // 3x3 matrix
  assert(z_comp.size() == 9);
  assert(phases.size() == 3);

  std::cout << "✓ test_get_tableau_state passed" << std::endl;
}

int main() {
  std::cout << "Running qutrit training service tests..." << std::endl;

  test_initialize_superposition();
  test_apply_hadamard_layer();
  test_lattice_collapse();
  test_apply_controlled_z();
  test_push_noise_phase();
  test_extract_error_syndrome();
  test_update_phase_tableau();
  test_get_training_metrics();
  test_get_tableau_state();

  std::cout << "\nAll qutrit training service tests passed!" << std::endl;
  return 0;
}