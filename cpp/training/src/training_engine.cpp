#include "qminiwasm/training/training_engine.hpp"

#include "qminiwasm/training/native_checkpoint.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "internal/adapter_interfaces.hpp"
#include "internal/balanced_sampler.hpp"
#include "internal/bounded_queue.hpp"
#include "internal/telemetry_bus.hpp"

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
#include "qminiwasm/training/libtorch_ternary_trainer.hpp"
#endif

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
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (state_ == EngineState::kStarting || state_ == EngineState::kRunning) {
      if (error_message != nullptr) {
        *error_message = "engine is already running";
      }
      return false;
    }

    telemetry_bus_.reopen();

    config_ = config;
    if (config_.run_id.empty()) {
      config_.run_id = "run-cpp-foundation";
    }
    best_val_loss_for_checkpoint_ = std::numeric_limits<double>::infinity();
    last_completed_train_loss_ = 0.0;
    last_completed_val_loss_ = 0.0;
    last_completed_epoch_ = 0;
    pipeline_failed_.store(false, std::memory_order_release);
    policy_ = make_taxonomy_policy(config_.taxonomy_tier, config_.worker_threads);
    if (config_.prefetch_depth == 0) {
      config_.prefetch_depth = policy_.prefetch_depth;
    }
    if (config_.compute_slots == 0) {
      config_.compute_slots = policy_.compute_slots;
    }

#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
    config_.compute_slots = 1;
    {
      std::string lib_err;
      libtorch_trainer_ = LibTorchTpemTrainer::create(config_.learning_rate, config_.seed, &lib_err);
      std::lock_guard<std::mutex> tlock(libtorch_mu_);
      if (!config_.model_uri.empty()) {
        if (!libtorch_trainer_->load_interchange(config_.model_uri, &lib_err)) {
          if (error_message != nullptr) {
            *error_message = lib_err;
          }
          return false;
        }
      } else if (config_.native_d_model > 0) {
        const auto d = static_cast<std::int64_t>(config_.native_d_model);
        const auto io = config_.native_io_d_model > 0 ? static_cast<std::int64_t>(config_.native_io_d_model) : d;
        const int nb = config_.native_num_ternary_blocks > 0 ? static_cast<int>(config_.native_num_ternary_blocks) : 1;
        if (!libtorch_trainer_->init_geometry(d, io, nb, &lib_err)) {
          if (error_message != nullptr) {
            *error_message = lib_err;
          }
          return false;
        }
      }
    }
#endif

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
    {
      double est_mib = 0.0;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      if (config_.native_d_model > 0) {
        const double d = static_cast<double>(config_.native_d_model);
        const int nb = config_.native_num_ternary_blocks > 0 ? static_cast<int>(config_.native_num_ternary_blocks) : 1;
        // Order-of-magnitude FP32 footprint hint for operators (not a tight bound).
        est_mib = (3.0 * d * d * static_cast<double>(nb) * 4.0) / (1024.0 * 1024.0);
      }
#endif
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = 0,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = config_.learning_rate,
          .samples_per_second = 0.0,
          .sampler_queue_depth = 0,
          .prefetch_queue_depth = 0,
          .compute_queue_depth = 0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "training_setup",
          .event_type = "enclave_summary",
          .message = "native_engine_synthetic_sampler_not_hf_tabular",
          .enclave_state = "native",
          .attestation_state = "n_a",
          .estimated_tpem_mib = est_mib,
          .tier_cap_mib = -1.0,
      });
    }
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
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (state_ != EngineState::kFailed) {
      state_ = EngineState::kStopped;
      status_.state = EngineState::kStopped;
      status_.message = "stopped";
    }
  }

  EngineStatus status() const {
    std::lock_guard<std::recursive_mutex> lock(mu_);
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
    std::lock_guard<std::recursive_mutex> lock(mu_);
    callback_ = std::move(callback);
  }

 private:
  void emit(TelemetryEvent event) {
    telemetry_bus_.push(event);
    std::lock_guard<std::recursive_mutex> lock(mu_);
    if (callback_) {
      callback_(event);
    }
  }

  void set_status(EngineStatus next) {
    std::lock_guard<std::recursive_mutex> lock(mu_);
    status_ = std::move(next);
    state_ = status_.state;
  }

  /// First LibTorch/compute failure wins; closes queues so sibling workers exit without joining self.
  void fail_pipeline_from_compute(const char* context, const std::string& reason) {
    const bool first = !pipeline_failed_.exchange(true, std::memory_order_acq_rel);
    if (!first) {
      return;
    }
    std::string msg = reason;
    constexpr std::size_t kMax = 1024;
    if (msg.size() > kMax) {
      msg.resize(kMax);
      msg += "...";
    }
    if (context != nullptr && context[0] != '\0') {
      msg = std::string(context) + ": " + msg;
    }
    set_status(EngineStatus{
        .state = EngineState::kFailed,
        .run_id = config_.run_id,
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .message = msg,
    });
    emit(TelemetryEvent{
        .run_id = config_.run_id,
        .unix_ms = now_unix_ms(),
        .epoch = 0,
        .step = 0,
        .train_loss = 0.0,
        .val_loss = 0.0,
        .learning_rate = config_.learning_rate,
        .samples_per_second = 0.0,
        .sampler_queue_depth = sampler_to_prefetch_ ? sampler_to_prefetch_->size() : 0,
        .prefetch_queue_depth = prefetch_to_compute_ ? prefetch_to_compute_->size() : 0,
        .compute_queue_depth = compute_to_update_ ? compute_to_update_->size() : 0,
        .taxonomy_tier = policy_.tier,
        .precision_mode = policy_.precision,
        .stage = "compute",
        .event_type = "libtorch_exception",
        .message = msg,
    });
    if (sampler_to_prefetch_) {
      sampler_to_prefetch_->close();
    }
    if (prefetch_to_compute_) {
      prefetch_to_compute_->close();
    }
    if (compute_to_update_) {
      compute_to_update_->close();
    }
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
    if (config_.enable_enclave_adapter) {
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = 0,
          .step = 0,
          .train_loss = 0.0,
          .val_loss = 0.0,
          .learning_rate = config_.learning_rate,
          .samples_per_second = 0.0,
          .sampler_queue_depth = 0,
          .prefetch_queue_depth = 0,
          .compute_queue_depth = 0,
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "adapter",
          .event_type = "enclave_adapter_fallback",
          .message = "enclave adapter requested; using default adapter dispatch (foundation no-op)",
      });
    }
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
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
    std::mt19937_64 rng(config_.seed + 17);
    std::uniform_real_distribution<double> noise(0.0, 0.005);
#endif
    while (!token.stop_requested()) {
      auto maybe = prefetch_to_compute_->pop(token);
      if (!maybe.has_value()) {
        break;
      }
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
      const auto t0 = Clock::now();
#endif
      PipelineBatch batch = std::move(*maybe);
      double train_loss = 0.0;
      double val_loss = 0.0;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      try {
        const auto t_inner = Clock::now();
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        const std::uint64_t mix =
            (static_cast<std::uint64_t>(batch.epoch) << 32) ^ static_cast<std::uint64_t>(batch.step);
        train_loss = libtorch_trainer_->train_step(config_.micro_batch_size, mix);
        val_loss = libtorch_trainer_->eval_step(config_.micro_batch_size, mix);
        const auto dt_inner = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t_inner).count();
        const double sps =
            static_cast<double>(batch.samples.size()) / std::max(1.0, static_cast<double>(dt_inner) / 1e6);
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
        continue;
      } catch (const std::exception& ex) {
        fail_pipeline_from_compute("libtorch", ex.what());
        break;
      } catch (...) {
        fail_pipeline_from_compute("libtorch", "non-std exception");
        break;
      }
#else
      double loss = 0.0;
      if (policy_.tier == TaxonomyTier::kXpuCluster) {
        loss = adapters::run_sycl_compute(batch.samples.size(), policy_.precision);
      } else if (policy_.tier == TaxonomyTier::kFogNode) {
        loss = adapters::run_avx512_compute(batch.samples.size(), policy_.precision);
      } else {
        loss = adapters::run_wasmedge_step(batch.samples.size(), policy_.precision);
      }
      const double epoch_decay = 1.0 / (1.0 + static_cast<double>(batch.epoch));
      train_loss = std::max(0.0001, loss * epoch_decay + noise(rng));
      val_loss = std::max(0.0001, train_loss + 0.005 * (1.0 + static_cast<double>(batch.epoch) * 0.1));
#endif
#ifndef QMINIWASM_HAS_LIBTORCH_TRAINING
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
#endif
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
    auto epoch_wall_start = Clock::now();
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
        const auto epoch_end_time = Clock::now();
        const double wall_s =
            std::chrono::duration<double>(epoch_end_time - epoch_wall_start).count();
        epoch_wall_start = epoch_end_time;
        const std::uint64_t epoch_samples =
            static_cast<std::uint64_t>(epoch_count) * static_cast<std::uint64_t>(config_.batch_size);
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
            .stage = "throughput",
            .event_type = "epoch_throughput",
            .message = "epoch_throughput",
            .epoch_wall_s = wall_s,
            .epoch_batch_count = static_cast<std::uint32_t>(epoch_count),
            .epoch_sample_count = epoch_samples,
            .epoch_mean_samples_per_s = metrics.samples_per_second,
        });
        tracker_.record_epoch(metrics);
        const ScheduleAction action = tracker_.evaluate_and_suggest(current_lr);
        if (action.type != ScheduleActionType::kNone) {
          current_lr = action.new_learning_rate;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
          {
            std::lock_guard<std::mutex> lk(libtorch_mu_);
            libtorch_trainer_->set_learning_rate(current_lr);
          }
#endif
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
        last_completed_train_loss_ = metrics.train_loss;
        last_completed_val_loss_ = metrics.val_loss;
        last_completed_epoch_ = packet.epoch;
        {
          const std::size_t epoch_1based = packet.epoch + 1;
          auto try_write = [&](const std::string& path, const char* label) {
            if (path.empty()) {
              return;
            }
            std::string err;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
            const bool ok = [&]() {
              std::lock_guard<std::mutex> lk(libtorch_mu_);
              return libtorch_trainer_->save_interchange(path, config_.run_id, epoch_1based, metrics.train_loss,
                                                         metrics.val_loss, current_lr, &err);
            }();
#else
            const bool ok = write_native_training_checkpoint(path, config_.run_id, epoch_1based, metrics.train_loss,
                                                             metrics.val_loss, current_lr, &err);
#endif
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
                .stage = "checkpoint",
                .event_type = ok ? "native_saved" : "native_save_failed",
                .message = ok ? (std::string(label) + ":" + path) : err,
            });
          };
          try_write(config_.checkpoint_latest_path, "latest");
          if (metrics.val_loss < best_val_loss_for_checkpoint_) {
            best_val_loss_for_checkpoint_ = metrics.val_loss;
            try_write(config_.checkpoint_best_path, "best");
          }
        }
        epoch_train_sum = 0.0;
        epoch_val_sum = 0.0;
        epoch_sps_sum = 0.0;
        epoch_count = 0;
      }
    }
    const bool pipeline_failed = pipeline_failed_.load(std::memory_order_acquire);
    std::string failed_message;
    if (pipeline_failed) {
      std::lock_guard<std::recursive_mutex> lk(mu_);
      failed_message = status_.message;
    }
    if (!pipeline_failed && !config_.checkpoint_save_path.empty()) {
      const std::size_t epoch_1based = last_completed_epoch_ + 1;
      std::string err;
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
      const bool ok = [&]() {
        std::lock_guard<std::mutex> lk(libtorch_mu_);
        return libtorch_trainer_->save_interchange(config_.checkpoint_save_path, config_.run_id, epoch_1based,
                                                   last_completed_train_loss_, last_completed_val_loss_, current_lr,
                                                   &err);
      }();
#else
      const bool ok =
          write_native_training_checkpoint(config_.checkpoint_save_path, config_.run_id, epoch_1based,
                                           last_completed_train_loss_, last_completed_val_loss_, current_lr, &err);
#endif
      emit(TelemetryEvent{
          .run_id = config_.run_id,
          .unix_ms = now_unix_ms(),
          .epoch = current_epoch,
          .step = 0,
          .train_loss = last_completed_train_loss_,
          .val_loss = last_completed_val_loss_,
          .learning_rate = current_lr,
          .samples_per_second = 0.0,
          .sampler_queue_depth = sampler_to_prefetch_->size(),
          .prefetch_queue_depth = prefetch_to_compute_->size(),
          .compute_queue_depth = compute_to_update_->size(),
          .taxonomy_tier = policy_.tier,
          .precision_mode = policy_.precision,
          .stage = "checkpoint",
          .event_type = ok ? "native_saved" : "native_save_failed",
          .message = ok ? (std::string("final:") + config_.checkpoint_save_path) : err,
      });
    }
    if (!pipeline_failed) {
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
    } else {
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
          .event_type = "failed",
          .message = failed_message.empty() ? "pipeline_failed" : failed_message,
      });
    }
    telemetry_bus_.close();
  }

  mutable std::recursive_mutex mu_;
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
  double best_val_loss_for_checkpoint_{std::numeric_limits<double>::infinity()};
  double last_completed_train_loss_{0.0};
  double last_completed_val_loss_{0.0};
  std::size_t last_completed_epoch_{0};
#ifdef QMINIWASM_HAS_LIBTORCH_TRAINING
  std::mutex libtorch_mu_;
  std::unique_ptr<LibTorchTpemTrainer> libtorch_trainer_;
#endif
  std::atomic<bool> pipeline_failed_{false};
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
