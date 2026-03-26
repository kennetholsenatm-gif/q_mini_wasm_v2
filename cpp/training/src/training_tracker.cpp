#include "qminiwasm/training/training_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace qminiwasm::training {

namespace {
double average_window(const std::deque<EpochMetrics>& history, std::size_t window, bool validation) {
  if (history.empty()) {
    return 0.0;
  }
  const std::size_t n = std::min(window, history.size());
  double sum = 0.0;
  for (std::size_t i = history.size() - n; i < history.size(); ++i) {
    sum += validation ? history[i].val_loss : history[i].train_loss;
  }
  return sum / static_cast<double>(n);
}
}  // namespace

TrainingTracker::TrainingTracker(std::size_t max_history) : max_history_(std::max<std::size_t>(10, max_history)) {}

void TrainingTracker::record_epoch(const EpochMetrics& metrics) {
  history_.push_back(metrics);
  while (history_.size() > max_history_) {
    history_.pop_front();
  }
}

TrackerSnapshot TrainingTracker::snapshot() const {
  TrackerSnapshot out;
  if (history_.empty()) {
    return out;
  }
  out.epoch = history_.back().epoch;
  out.train_loss.current = history_.back().train_loss;
  out.val_loss.current = history_.back().val_loss;
  out.train_loss.last3 = rolling_average(3, false);
  out.train_loss.last5 = rolling_average(5, false);
  out.train_loss.last10 = rolling_average(10, false);
  out.val_loss.last3 = rolling_average(3, true);
  out.val_loss.last5 = rolling_average(5, true);
  out.val_loss.last10 = rolling_average(10, true);
  out.divergence_rate = divergence_slope(5);
  out.overfitting_detected = out.divergence_rate > 0.015;
  out.plateau_detected = detect_plateau(5);
  return out;
}

ScheduleAction TrainingTracker::evaluate_and_suggest(double current_learning_rate) const {
  const TrackerSnapshot snap = snapshot();
  if (history_.size() < 3) {
    return ScheduleAction{};
  }
  if (snap.overfitting_detected) {
    return ScheduleAction{
        .type = ScheduleActionType::kReduceLearningRate,
        .new_learning_rate = std::max(current_learning_rate * 0.5, 1e-6),
        .reason = "overfitting divergence detected",
    };
  }
  if (snap.plateau_detected) {
    return ScheduleAction{
        .type = ScheduleActionType::kCosineWarmRestart,
        .new_learning_rate = std::max(current_learning_rate * 0.9, 1e-6),
        .reason = "plateau detected over rolling window",
    };
  }
  return ScheduleAction{};
}

std::optional<double> TrainingTracker::rolling_average(std::size_t window, bool validation) const {
  if (history_.empty()) {
    return std::nullopt;
  }
  return average_window(history_, window, validation);
}

double TrainingTracker::divergence_slope(std::size_t window) const {
  if (history_.size() < 2) {
    return 0.0;
  }
  const std::size_t n = std::min(window, history_.size());
  double first = 0.0;
  double last = 0.0;
  for (std::size_t i = history_.size() - n; i < history_.size(); ++i) {
    const double diff = history_[i].val_loss - history_[i].train_loss;
    if (i == history_.size() - n) {
      first = diff;
    }
    last = diff;
  }
  return (last - first) / static_cast<double>(n);
}

bool TrainingTracker::detect_plateau(std::size_t window) const {
  if (history_.size() < window + 1) {
    return false;
  }
  const double recent = average_window(history_, window, false);
  std::deque<EpochMetrics> without_recent = history_;
  for (std::size_t i = 0; i < window; ++i) {
    without_recent.pop_back();
  }
  if (without_recent.empty()) {
    return false;
  }
  const double prior = average_window(without_recent, window, false);
  return std::fabs(prior - recent) < 1e-3;
}

}  // namespace qminiwasm::training
