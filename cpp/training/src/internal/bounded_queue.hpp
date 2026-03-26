#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>

namespace qminiwasm::training::internal {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

  bool push(T item, std::stop_token token) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_not_full_.wait(lock, token, [&] { return closed_ || queue_.size() < capacity_; });
    if (closed_ || token.stop_requested()) {
      return false;
    }
    queue_.push(std::move(item));
    cv_not_empty_.notify_one();
    return true;
  }

  std::optional<T> pop(std::stop_token token) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_not_empty_.wait(lock, token, [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T out = std::move(queue_.front());
    queue_.pop();
    cv_not_full_.notify_one();
    return out;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mu_);
    closed_ = true;
    cv_not_full_.notify_all();
    cv_not_empty_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return queue_.size();
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mu_;
  std::condition_variable_any cv_not_full_;
  std::condition_variable_any cv_not_empty_;
  std::queue<T> queue_;
  bool closed_ = false;
};

}  // namespace qminiwasm::training::internal
