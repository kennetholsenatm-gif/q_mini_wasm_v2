#include "qutrit_training_service.hpp"

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace qminiwasm::quantum {

QutritTrainingServiceImpl::QutritTrainingServiceImpl() = default;
QutritTrainingServiceImpl::~QutritTrainingServiceImpl() = default;

// ============================================================================
// Helper Methods
// ============================================================================

QutritTableau* QutritTrainingServiceImpl::GetTableau(int64_t id) {
  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  auto it = tableaus_.find(id);
  return (it != tableaus_.end()) ? it->second.get() : nullptr;
}

TrainingMetrics* QutritTrainingServiceImpl::GetMetrics(int64_t id) {
  std::lock_guard<std::mutex> lock(metrics_mutex_);
  auto it = metrics_.find(id);
  return (it != metrics_.end()) ? &it->second : nullptr;
}

// ============================================================================
// Phase 1: Quantum Superposition
// ============================================================================

int64_t QutritTrainingServiceImpl::InitializeSuperposition(int32_t n_params, uint64_t seed) {
  if (n_params < 1) return -1;

  std::lock_guard<std::mutex> lock(tableaus_mutex_);
  int64_t id = next_tableau_id_++;

  try {
    tableaus_[id] = std::make_unique<QutritTableau>(n_params);

    // Initialize metrics
    std::lock_guard<std::mutex> mlock(metrics_mutex_);
    metrics_[id] = TrainingMetrics{};

    return id;
  } catch (...) {
    return -1;
  }
}

bool QutritTrainingServiceImpl::ApplyHadamardLayer(int64_t tableau_id, const std::vector<int32_t>& qubits) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  try {
    if (qubits.empty()) {
      // Apply to all qubits
      for (int i = 0; i < tab->num_qutrits(); ++i) {
        tab->apply_h3(i);
      }
    } else {
      // Apply to specified qubits
      for (int32_t q : qubits) {
        if (q >= 0 && q < tab->num_qutrits()) {
          tab->apply_h3(q);
        }
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

std::pair<std::vector<int32_t>, std::vector<float>> QutritTrainingServiceImpl::LatticeCollapse(
    int64_t tableau_id, int32_t n_measurements) {
  auto* tab = GetTableau(tableau_id);
  if (!tab || n_measurements < 1) return {{}, {}};

  try {
    int n = tab->num_qutrits();
    std::vector<int32_t> collapsed_weights(n, 0);
    std::vector<float> probabilities(n, 0.0f);

    // Perform measurements
    for (int i = 0; i < n; ++i) {
      std::vector<int> counts(3, 0);  // Count for each outcome {-1, 0, 1}

      for (int shot = 0; shot < n_measurements; ++shot) {
        // Copy tableau for measurement
        auto tab_copy = tab->copy();
        int result = tab_copy.measure(i);

        if (result >= 0 && result <= 2) {
          counts[result]++;
        }
      }

      // Find most probable outcome
      int max_idx = 0;
      for (int j = 1; j < 3; ++j) {
        if (counts[j] > counts[max_idx]) {
          max_idx = j;
        }
      }

      // Map {0,1,2} to {-1,0,1}
      collapsed_weights[i] = max_idx - 1;
      probabilities[i] = static_cast<float>(counts[max_idx]) / n_measurements;
    }

    return {collapsed_weights, probabilities};
  } catch (...) {
    return {{}, {}};
  }
}

// ============================================================================
// Phase 2: Entanglement-Based Optimization
// ============================================================================

bool QutritTrainingServiceImpl::ApplyControlledZ(
    int64_t tableau_id, int32_t control, int32_t target, int32_t power) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  try {
    if (power < 1) power = 1;
    for (int i = 0; i < power; ++i) {
      tab->apply_cz3(control, target);
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool QutritTrainingServiceImpl::PushNoisePhase(int64_t tableau_id, int32_t phase) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  try {
    phase = ((phase % 3) + 3) % 3;
    // Apply phase to all qubits
    for (int i = 0; i < tab->num_qutrits(); ++i) {
      if (phase == 1) {
        tab->apply_s3(i);
      } else if (phase == 2) {
        tab->apply_s3(i);
        tab->apply_s3(i);
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

std::pair<std::vector<int32_t>, bool> QutritTrainingServiceImpl::ExtractErrorSyndrome(
    int64_t tableau_id) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return {{}, false};

  try {
    // Extract phase information as syndrome
    const auto& r = tab->r();
    std::vector<int32_t> syndrome(r.begin(), r.end());

    // Check for non-zero phases (errors)
    bool has_error = false;
    for (int32_t s : syndrome) {
      if (s % 3 != 0) {
        has_error = true;
        break;
      }
    }

    return {syndrome, has_error};
  } catch (...) {
    return {{}, false};
  }
}

// ============================================================================
// Phase 3: Stabilizer Tableau Engine
// ============================================================================

bool QutritTrainingServiceImpl::UpdatePhaseTableau(
    int64_t tableau_id, const std::vector<int32_t>& phase_updates) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) return false;

  try {
    int n = tab->num_qutrits();
    if (static_cast<int>(phase_updates.size()) != n) return false;

    // Apply phase updates
    for (int i = 0; i < n; ++i) {
      int phase = ((phase_updates[i] % 3) + 3) % 3;
      if (phase == 1) {
        tab->apply_s3(i);
      } else if (phase == 2) {
        tab->apply_s3(i);
        tab->apply_s3(i);
      }
    }

    // Update metrics
    auto* metrics = GetMetrics(tableau_id);
    if (metrics) {
      metrics->total_epochs++;
      metrics->successful_epochs++;
    }

    return true;
  } catch (...) {
    return false;
  }
}

TrainingMetrics QutritTrainingServiceImpl::GetTrainingMetrics(int64_t tableau_id) {
  auto* metrics = GetMetrics(tableau_id);
  if (metrics) {
    return *metrics;
  }
  return TrainingMetrics{};
}

std::tuple<std::vector<int32_t>, std::vector<int32_t>, std::vector<int32_t>>
QutritTrainingServiceImpl::GetTableauState(int64_t tableau_id) {
  auto* tab = GetTableau(tableau_id);
  if (!tab) {
    return {{}, {}, {}};
  }

  try {
    const auto& x = tab->x();
    const auto& z = tab->z();
    const auto& r = tab->r();

    std::vector<int32_t> x_components(x.begin(), x.end());
    std::vector<int32_t> z_components(z.begin(), z.end());
    std::vector<int32_t> phases(r.begin(), r.end());

    return {x_components, z_components, phases};
  } catch (...) {
    return {{}, {}, {}};
  }
}

// ============================================================================
// Server Startup
// ============================================================================

void RunQutritTrainingServer(const std::string& address,
                              std::shared_ptr<QutritTrainingServiceImpl> service) {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());

  // TODO: Register the generated gRPC service here
  // builder.RegisterService(service.get());

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "QutritTrainingService listening on " << address << std::endl;
  server->Wait();
}

}  // namespace qminiwasm::quantum