#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace qminiwasm::training {

struct EpochMetrics {
  std::size_t epoch = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double learning_rate = 0.0;
  double samples_per_second = 0.0;
  std::uint64_t elapsed_ms = 0;
};

struct RollingSummary {
  std::optional<double> current;
  std::optional<double> last3;
  std::optional<double> last5;
  std::optional<double> last10;
};

enum class ScheduleActionType {
  kNone = 0,
  kReduceLearningRate = 1,
  kCosineWarmRestart = 2,
};

struct ScheduleAction {
  ScheduleActionType type = ScheduleActionType::kNone;
  double new_learning_rate = 0.0;
  std::string reason;
};

struct TrackerSnapshot {
  std::size_t epoch = 0;
  RollingSummary train_loss;
  RollingSummary val_loss;
  bool overfitting_detected = false;
  bool plateau_detected = false;
  double divergence_rate = 0.0;
};

class TrainingTracker {
 public:
  explicit TrainingTracker(std::size_t max_history = 64);

  void record_epoch(const EpochMetrics& metrics);
  TrackerSnapshot snapshot() const;
  ScheduleAction evaluate_and_suggest(double current_learning_rate) const;

 private:
  std::optional<double> rolling_average(std::size_t window, bool validation) const;
  double divergence_slope(std::size_t window) const;
  bool detect_plateau(std::size_t window) const;

  std::size_t max_history_;
  std::deque<EpochMetrics> history_;
};

}  // namespace qminiwasm::training
