#pragma once

#include "qutrit_export.hpp"
#include "qutrit_tableau.hpp"
#include "error_correction.hpp"

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace qminiwasm::quantum {

/**
 * Training metrics for monitoring optimization progress.
 */
struct TrainingMetrics {
  int64_t total_epochs = 0;
  int64_t successful_epochs = 0;
  float current_loss = 0.0f;
  float best_loss = 1e10f;
  std::vector<float> loss_history;
};

/**
 * gRPC service implementation for Qutrit Clifford training.
 * 
 * Implements the three-phase training paradigm:
 * 1. Quantum Superposition over Simulated Annealing
 * 2. Entanglement-Based Loss Optimization
 * 3. Hybrid Execution via Stabilizer Tableaus
 */
class QUTRIT_API QutritTrainingServiceImpl final {
 public:
  QutritTrainingServiceImpl();
  ~QutritTrainingServiceImpl();

  // ========================================================================
  // Phase 1: Quantum Superposition
  // ========================================================================

  /**
   * Initialize parameters in quantum superposition.
   * Creates a new tableau with n_params qutrits in |0⟩ state.
   */
  int64_t InitializeSuperposition(int32_t n_params, uint64_t seed);

  /**
   * Apply qutrit Hadamard gate to create superposition.
   * If qubits is empty, applies to all qubits.
   */
  bool ApplyHadamardLayer(int64_t tableau_id, const std::vector<int32_t>& qubits);

  /**
   * Perform lattice collapse via projective measurement.
   * Returns final ternary weights {-1, 0, 1} and measurement probabilities.
   */
  std::pair<std::vector<int32_t>, std::vector<float>> LatticeCollapse(
      int64_t tableau_id, int32_t n_measurements);

  // ========================================================================
  // Phase 2: Entanglement-Based Optimization
  // ========================================================================

  /**
   * Apply Controlled-Z gate to create parameter correlations.
   */
  bool ApplyControlledZ(int64_t tableau_id, int32_t control, int32_t target, int32_t power);

  /**
   * Push noise phase for error tracking.
   */
  bool PushNoisePhase(int64_t tableau_id, int32_t phase);

  /**
   * Extract error syndrome for noise handling.
   */
  std::pair<std::vector<int32_t>, bool> ExtractErrorSyndrome(int64_t tableau_id);

  // ========================================================================
  // Phase 3: Stabilizer Tableau Engine
  // ========================================================================

  /**
   * Update phase tableau with discrete algebraic phase updates.
   */
  bool UpdatePhaseTableau(int64_t tableau_id, const std::vector<int32_t>& phase_updates);

  /**
   * Get training metrics for monitoring.
   */
  TrainingMetrics GetTrainingMetrics(int64_t tableau_id);

  /**
   * Get tableau state for inspection.
   */
  std::tuple<std::vector<int32_t>, std::vector<int32_t>, std::vector<int32_t>> GetTableauState(
      int64_t tableau_id);

 private:
  // Tableau storage (ID -> unique_ptr)
  std::mutex tableaus_mutex_;
  std::unordered_map<int64_t, std::unique_ptr<QutritTableau>> tableaus_;
  int64_t next_tableau_id_ = 1;

  // Training metrics storage
  std::mutex metrics_mutex_;
  std::unordered_map<int64_t, TrainingMetrics> metrics_;

  // Helper to get tableau by ID
  QutritTableau* GetTableau(int64_t id);
  TrainingMetrics* GetMetrics(int64_t id);
};

/**
 * Create and run the gRPC server for qutrit training.
 * @param address Server address (e.g., "0.0.0.0:50053")
 * @param service Shared pointer to the service implementation
 */
QUTRIT_API void RunQutritTrainingServer(const std::string& address,
                                        std::shared_ptr<QutritTrainingServiceImpl> service);

}  // namespace qminiwasm::quantum