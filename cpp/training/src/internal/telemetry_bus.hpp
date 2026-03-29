#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "qminiwasm/training/training_engine.hpp"

namespace qminiwasm::training::internal {

class TelemetryBus {
 public:
  void push(TelemetryEvent event);
  std::optional<TelemetryEvent> pop_for(std::uint64_t timeout_ms);
  void close();
  /// After ``close()`` (e.g. end of a run), reopen so a new training session can emit telemetry.
  void reopen();

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<TelemetryEvent> queue_;
  bool closed_ = false;
};

}  // namespace qminiwasm::training::internal
