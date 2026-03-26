#include "qminiwasm/training/training_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "internal/adapter_interfaces.hpp"
#include "internal/balanced_sampler.hpp"
#include "internal/bounded_queue.hpp"
#include "internal/telemetry_bus.hpp"

namespace qminiwasm::training {

namespace {
using Clock = std::chrono::steady_clock;

std::uint64_t now_unix_ms() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
          .count());
}

struct PipelineBatch {
  std::size_t epoch = 0;
  std::size_t step = 0;
  std::vector<internal::SampleRef> samples;
};

struct ComputePacket {
  std::size_t epoch = 0;
  std::size_t step = 0;
  double train_loss = 0.0;
  double val_loss = 0.0;
  double samples_per_second = 0.0;
};
}  // namespace

class TrainingEngine::Impl {
 public:
  Impl() : tracker_(64), state_(EngineState::kIdle) {}

  ~Impl() { request_stop(); }

  bool start(const TrainingConfig& config, std::string* error_message) {
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ == EngineState::kStarting || state_ == EngineState::kRunning) {
      if (error_message != nullptr) {
        *error_message = "engine is already running";
      }
      return false;
    }

    config_ = config;
    if (config_.run_id.empty()) {
      config_.run_id = "run-cpp-foundation";
    }
    policy_ = make_taxonomy_policy(config_.taxonomy_tier, config_.worker_threads);
    if (config_.prefetch_depth == 0) {
      config_.prefetch_depth = policy_.prefetch_depth;
    }
    if (config_.compute_slots == 0) {
      config_.compute_slots = policy_.compute_slots;
    }

    sampler_to_prefetch_ =
        std::make_unique<internal::BoundedQueue<PipelineBatch>>(std::max<std::size_t>(2, config_.prefetch_depth * 2));
    prefetch_to_compute_ =
        std::make_unique<internal::BoundedQueue<PipelineBatch>>(std::max<std::size_t>(2, config_.prefetch_depth * 2));
    compute_to_update_ =
        std::make_unique<internal::BoundedQueue<ComputePacket>>(std::max<std::size_t>(2, config_.compute_slots * 2));

    stop_source_ = std::stop_source{};
    state_ = EngineState::kStarting;
    status_ = EngineStatus{
        .state = EngineState::kStarting,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = "starting workers",
    };
    workers_.clear();
    launch_workers();
    return true;
  }

  void request_stop() {
    stop_source_.request_stop();
    if (sampler_to_prefetch_) sampler_to_prefetch_->close();
    if (prefetch_to_compute_) prefetch_to_compute_->close();
    if (compute_to_update_) compute_to_update_->close();
    for (std::jthread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
    telemetry_bus_.close();
    std::lock_guard<std::mutex> lock(mu_);
    if (state_ != EngineState::kFailed) {
      state_ = EngineState::kStopped;
      status_.state = EngineState::kStopped;
      status_.message = "stopped";
    }
  }

  EngineStatus status() const {
    std::lock_guard<std::mutex> lock(mu_);
    return status_;
  }

  bool pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms) {
    if (out_event == nullptr) {
      return false;
    }
    auto event = telemetry_bus_.pop_for(timeout_ms);
    if (!event.has_value()) {
      return false;
    }
    *out_event = std::move(*event);
    return true;
  }

  void set_telemetry_callback(TelemetryCallback callback) {
    std::lock_guard<std::mutex> lock(mu_);
    callback_ = std::move(callback);
  }

 private:
  void emit(TelemetryEvent event) {
    telemetry_bus_.push(event);
    std::lock_guard<std::mutex> lock(mu_);
    if (callback_) {
      callback_(event);
    }
  }

  void set_status(EngineStatus next) {
    std::lock_guard<std::mutex> lock(mu_);
    status_ = std::move(next);
    state_ = status_.state;
  }

  void launch_workers() {
    workers_.emplace_back([this](std::stop_token token) { sampler_stage(token); });
    workers_.emplace_back([this](std::stop_token token) { prefetch_stage(token); });
    const std::size_t compute_workers = std::max<std::size_t>(1, config_.compute_slots);
    for (std::size_t i = 0; i < compute_workers; ++i) {
      workers_.emplace_back([this](std::stop_token token) { compute_stage(token); });
    }
    workers_.emplace_back([this](std::stop_token token) { update_stage(token); });
  }

  void sampler_stage(std::stop_token token) {
    internal::BalancedSampler sampler(config_.classes, config_.samples_per_class, config_.seed);
    const std::size_t total_samples = std::max<std::size_t>(1, config_.classes * config_.samples_per_class);
    const std::size_t steps_per_epoch = std::max<std::size_t>(1, total_samples / std::max<std::size_t>(1, config_.batch_size));
    set_status(EngineStatus{
        .state = EngineState::kRunning,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = "running",
    });
    for (std::size_t epoch = 0; epoch < config_.epochs && !token.stop_requested(); ++epoch) {
      sampler.reseed_for_epoch(epoch);
      for (std::size_t step = 0; step < steps_per_epoch && !token.stop_requested(); ++step) {
        PipelineBatch batch{.epoch = epoch, .step = step, .samples = sampler.next_micro_batch(config_.micro_batch_size)};
        if (!sampler_to_prefetch_->push(std::move(batch), token)) {
          break;
        }
      }
    }
    sampler_to_prefetch_->close();
  }

  void prefetch_stage(std::stop_token token) {
    while (!token.stop_requested()) {
      auto maybe = sampler_to_prefetch_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (!prefetch_to_compute_->push(std::move(*maybe), token)) {
        break;
      }
    }
    prefetch_to_compute_->close();
  }

  void compute_stage(std::stop_token token) {
    std::mt19937_64 rng(config_.seed + 17);
    std::uniform_real_distribution<double> noise(0.0, 0.005);
    while (!token.stop_requested()) {
      auto maybe = prefetch_to_compute_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
      const auto t0 = Clock::now();
      PipelineBatch batch = std::move(*maybe);
      double loss = 0.0;
      if (policy_.tier == TaxonomyTier::kXpuCluster) {
        loss = adapters::run_sycl_compute(batch.samples.size(), policy_.precision);
      } else if (policy_.tier == TaxonomyTier::kFogNode) {
        loss = adapters::run_avx512_compute(batch.samples.size(), policy_.precision);
      } else {
        loss = adapters::run_wasmedge_step(batch.samples.size(), policy_.precision);
      }
      const double epoch_decay = 1.0 / (1.0 + static_cast<double>(batch.epoch));
      const double train_loss = std::max(0.0001, loss * epoch_decay + noise(rng));
      const double val_loss = std::max(0.0001, train_loss + 0.005 * (1.0 + static_cast<double>(batch.epoch) * 0.1));
      const auto dt = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
      const double sps = static_cast<double>(batch.samples.size()) / std::max(1.0, static_cast<double>(dt) / 1e6);
      ComputePacket packet{
          .epoch = batch.epoch,
          .step = batch.step,
          .train_loss = train_loss,
          .val_loss = val_loss,
          .samples_per_second = sps,
      };
      if (!compute_to_update_->push(std::move(packet), token)) {
        break;
      }
    }
  }

  void update_stage(std::stop_token token) {
    const std::size_t total_samples = std::max<std::size_t>(1, config_.classes * config_.samples_per_class);
    const std::size_t steps_per_epoch = std::max<std::size_t>(1, total_samples / std::max<std::size_t>(1, config_.batch_size));
    double current_lr = config_.learning_rate;
    double epoch_train_sum = 0.0;
    double epoch_val_sum = 0.0;
    double epoch_sps_sum = 0.0;
    std::size_t epoch_count = 0;
    std::size_t current_epoch = 0;
    while (!token.stop_requested()) {
      auto maybe = compute_to_update_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
      const ComputePacket packet = std::move(*maybe);
      current_epoch = packet.epoch;
      epoch_train_sum += packet.train_loss;
      epoch_val_sum += packet.val_loss;
      epoch_sps_sum += packet.samples_per_second;
      ++epoch_count;

      TelemetryEvent event{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = packet.epoch,
          .step = packet.step,
          .train_loss = packet.train_loss,
          .val_loss = packet.val_loss,
          .learning_rate = current_lr,
          .samples_per_second = packet.samples_per_second,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "update",
          .event_type = "step",
          .message = "step_completed",
      };
      emit(std::move(event));
      set_status(EngineStatus{
          .state = EngineState::kRunning,
          .run_id = config_.run_id,
          .epoch = packet.epoch,
          .step = packet.step,
          .train_loss = packet.train_loss,
          .val_loss = packet.val_loss,
          .learning_rate = current_lr,
          .message = "running",
      });

      if ((packet.step + 1) >= steps_per_epoch) {
        const EpochMetrics metrics{
            .epoch = packet.epoch,
            .train_loss = epoch_train_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .val_loss = epoch_val_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .learning_rate = current_lr,
            .samples_per_second = epoch_sps_sum / static_cast<double>(std::max<std::size_t>(1, epoch_count)),
            .elapsed_ms = 0,
        };
        tracker_.record_epoch(metrics);
        const ScheduleAction action = tracker_.evaluate_and_suggest(current_lr);
        if (action.type != ScheduleActionType::kNone) {
          current_lr = action.new_learning_rate;
          emit(TelemetryEvent{
              .run_id = config_.run_id,
              .unix_ms = now_unix_ms(),
              .epoch = packet.epoch,
              .step = packet.step,
              .train_loss = metrics.train_loss,
              .val_loss = metrics.val_loss,
              .learning_rate = current_lr,
              .samples_per_second = metrics.samples_per_second,
              .sampler_queue_depth = sampler_to_prefetch_->size(),
              .prefetch_queue_depth = prefetch_to_compute_->size(),
              .compute_queue_depth = compute_to_update_->size(),
              .taxonomy_tier = policy_.tier,
              .precision_mode = policy_.precision,
              .stage = "scheduler",
              .event_type = "schedule_adjustment",
              .message = action.reason,
          });
        }
        epoch_train_sum = 0.0;
        epoch_val_sum = 0.0;
        epoch_sps_sum = 0.0;
        epoch_count = 0;
      }
    }
    set_status(EngineStatus{
        .state = EngineState::kStopped,
        .run_id = config_.run_id,
        .epoch = current_epoch,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = current_lr,
        .message = "completed",
    });
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = current_epoch,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = current_lr,
        .samples_per_second = 0.0,
        .sampler_queue_depth = sampler_to_prefetch_->size(),
        .prefetch_queue_depth = prefetch_to_compute_->size(),
        .compute_queue_depth = compute_to_update_->size(),
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = "lifecycle",
        .event_type = "completed",
        .message = "training_completed",
    });
    telemetry_bus_.close();
  }

  mutable std::mutex mu_;
  TrainingConfig config_{};
  TaxonomyPolicy policy_{};
  EngineStatus status_{};
  TelemetryCallback callback_;
  std::stop_source stop_source_{};
  std::vector<std::jthread> workers_;
  std::unique_ptr<internal::BoundedQueue<PipelineBatch>> sampler_to_prefetch_;
  std::unique_ptr<internal::BoundedQueue<PipelineBatch>> prefetch_to_compute_;
  std::unique_ptr<internal::BoundedQueue<ComputePacket>> compute_to_update_;
  internal::TelemetryBus telemetry_bus_;
  TrainingTracker tracker_;
  EngineState state_;
};

TrainingEngine::TrainingEngine() : impl_(std::make_unique<Impl>()) {}
TrainingEngine::~TrainingEngine() = default;

bool TrainingEngine::start(const TrainingConfig& config, std::string* error_message) {
  return impl_->start(config, error_message);
}

void TrainingEngine::request_stop() { impl_->request_stop(); }
EngineStatus TrainingEngine::status() const { return impl_->status(); }
bool TrainingEngine::pop_telemetry(TelemetryEvent* out_event, std::uint64_t timeout_ms) {
  return impl_->pop_telemetry(out_event, timeout_ms);
}
void TrainingEngine::set_telemetry_callback(TelemetryCallback callback) {
  impl_->set_telemetry_callback(std::move(callback));
}

}  // namespace qminiwasm::training
