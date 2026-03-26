#include "internal/telemetry_bus.hpp"

namespace qminiwasm::training::internal {

void TelemetryBus::push(TelemetryEvent event) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_) {
      return;
    }
    queue_.push_back(std::move(event));
  }
  cv_.notify_one();
}

std::optional<TelemetryEvent> TelemetryBus::pop_for(std::uint64_t timeout_ms) {
  std::unique_lock<std::mutex> lock(mu_);
  const auto timeout = std::chrono::milliseconds(timeout_ms);
  cv_.wait_for(lock, timeout, [&] { return closed_ || !queue_.empty(); });
  if (queue_.empty()) {
    return std::nullopt;
  }
  TelemetryEvent event = std::move(queue_.front());
  queue_.pop_front();
  return event;
}

void TelemetryBus::close() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
  }
  cv_.notify_all();
}

}  // namespace qminiwasm::training::internal
