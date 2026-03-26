#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "qminiwasm/training/taxonomy_tier.hpp"
#include "qminiwasm/training/training_tracker.hpp"

namespace qminiwasm::training {

struct TrainingConfig {
  std::string run_id;
  std::size_t epochs = 1;
  std::size_t batch_size = 64;
  std::size_t micro_batch_size = 16;
  std::size_t worker_threads = 4;
  std::size_t prefetch_depth = 4;
  std::size_t compute_slots = 2;
  std::size_t classes = 2;
  std::size_t samples_per_class = 128;
  std::uint64_t seed = 42;
  double learning_rate = 1e-3;
  TaxonomyTier taxonomy_tier = TaxonomyTier::kEdgeConstrained;
  bool enable_enclave_adapter = false;
};

enum class EngineState {
  kIdle = 0,
  kStarting = 1,
  kRunning = 2,
  kStopping = 3,
  kStopped = 4,
  kFailed = 5,
};

struct TelemetryEvent {
  std::string run_id;
  std::uint64_t unix_ms = 0;
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double learning_rate = 0.0;
  double samples_per_second = 0.0;
  std::size_t sampler_queue_depth = 0;
  std::size_t prefetch_queue_depth = 0;
  std::size_t compute_queue_depth = 0;
  TaxonomyTier taxonomy_tier = TaxonomyTier::kEdgeConstrained;
  PrecisionMode precision_mode = PrecisionMode::kTernary;
  std::string stage;
  std::string event_type;
  std::string message;
};

struct EngineStatus {
  EngineState state = EngineState::kIdle;
  std::string run_id;
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double learning_rate = 0.0;
  std::string message;
};

using TelemetryCallback = std::function<void(const TelemetryEvent&)>;

class TrainingEngine {
 public:
  TrainingEngine();
  ~TrainingEngine();

  TrainingEngine(const TrainingEngine&) = delete;
  TrainingEngine& operator=(const TrainingEngine&) = delete;

  bool start(const TrainingConfig& config, std::string* error_message);
  void request_stop();

  EngineStatus status() const;
  bool pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms);
  void set_telemetry_callback(TelemetryCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qminiwasm::training
